// unllm_embed: the vector index's maintenance, without faiss or Python.
//
//   unllm_embed audit  INDEX.unlpq PACKS_DIR
//       every shard the index names, with how many of its passages the index holds against how many the pack's
//       shard file holds; exit 1 when they differ anywhere (a build that resumed past a checkpoint left gaps)
//   unllm_embed encode INDEX.unlpq MODEL_DIR SHARD.sqlite SHARD_NO OUT.codes [--cpu] [--chars 900] [--batch 128]
//       every passage of the shard file embedded with the index's model (ONNX Runtime, the GPU when its CUDA
//       provider loads) as "title: text", assigned to its nearest list and encoded with the index's own codebooks,
//       written as records of (list, id, codes); progress lines on stdout ("progress N of TOTAL")
//   unllm_embed patch  INDEX.unlpq OUT.unlpq [--drop NO]... [--add FILE.codes NO PACK SHARD_FILE]...
//       the index rewritten with the dropped shards' entries gone and the records of each codes file added, the
//       shard table extended for a shard it did not name; the trained part (centres, codebooks) is kept, so a
//       patched index searches exactly as the old one did for everything untouched
//
// The index format is the library's .unlpq (bench/export_index.py and src/meaning.cpp): a header, the coarse
// centres, the product-quantiser codebooks, then per list its ids and codes. Encoding mirrors faiss's IndexIVFPQ
// with an inner-product coarse quantiser: the list is the centre with the largest inner product, the code is the
// nearest codebook entry (squared distance) of each sub-vector of the residual (the vector less its centre) when the
// index was built by residual, of the vector itself otherwise.
#include <sqlite3.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "unllm/reader.h"  // WordPiece
#ifdef UNLLM_HAVE_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace {

struct Index {
    void* map = nullptr;
    std::size_t map_size = 0;
    int d = 0, nlist = 0, M = 0, nbits = 0, metric = 0, by_residual = 1, ksub = 256, dsub = 0;
    int64_t ntotal = 0;
    struct Shard {
        int no = 0;
        std::string pack, file;
    };
    std::vector<Shard> shards;
    const float* centres = nullptr;
    const float* books = nullptr;
    struct List {
        int64_t size = 0;
        const int64_t* ids = nullptr;
        const uint8_t* codes = nullptr;
    };
    std::vector<List> lists;
    ~Index() {
        if (map) munmap(map, map_size);
    }
    bool open(const std::string& path, std::string* why) {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) return *why = "cannot open " + path, false;
        struct stat st {};
        if (fstat(fd, &st) != 0 || st.st_size < 64) return ::close(fd), *why = "not an index: " + path, false;
        map_size = std::size_t(st.st_size);
        map = mmap(nullptr, map_size, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (map == MAP_FAILED) return map = nullptr, *why = "cannot map " + path, false;
        const char* base = static_cast<const char*>(map);
        const char* p = base;
        const char* end = base + map_size;
        if (std::memcmp(p, "UNLPQ1\0\0", 8) != 0) return *why = "not a .unlpq file: " + path, false;
        p += 8;
        int32_t head[6];
        std::memcpy(head, p, sizeof head);
        p += sizeof head;
        d = head[0], nlist = head[1], M = head[2], nbits = head[3], metric = head[4], by_residual = head[5];
        std::memcpy(&ntotal, p, 8);
        p += 8;
        int32_t nshards = 0;
        std::memcpy(&nshards, p, 4);
        p += 4;
        if (metric != 0 || nbits != 8 || M <= 0 || d % M != 0 || nshards < 0) return *why = "an index of a kind this tool does not read", false;
        dsub = d / M;
        for (int i = 0; i < nshards; ++i) {
            if (p + 132 > end) return *why = "truncated shard table", false;
            Shard s;
            std::memcpy(&s.no, p, 4);
            s.pack = std::string(p + 4, strnlen(p + 4, 64));
            s.file = std::string(p + 68, strnlen(p + 68, 64));
            shards.push_back(s);
            p += 132;
        }
        p = base + ((std::size_t(p - base) + 7) & ~std::size_t(7));
        const std::size_t cb = std::size_t(nlist) * d * 4, bb = std::size_t(M) * ksub * dsub * 4;
        if (p + cb + bb > end) return *why = "truncated tables", false;
        centres = reinterpret_cast<const float*>(p);
        p += cb;
        books = reinterpret_cast<const float*>(p);
        p += bb;
        lists.resize(std::size_t(nlist));
        for (int l = 0; l < nlist; ++l) {
            if (p + 8 > end) return *why = "truncated lists", false;
            List& L = lists[std::size_t(l)];
            std::memcpy(&L.size, p, 8);
            p += 8;
            if (L.size < 0 || p + std::size_t(L.size) * (8 + std::size_t(M)) > end) return *why = "truncated list " + std::to_string(l), false;
            L.ids = reinterpret_cast<const int64_t*>(p);
            p += std::size_t(L.size) * 8;
            L.codes = p ? reinterpret_cast<const uint8_t*>(p) : nullptr;
            p += std::size_t(L.size) * std::size_t(M);
        }
        return true;
    }
};

// One encoded passage: its list, its id (shard << 32 | pid) and its M code bytes.
struct Record {
    int32_t list;
    int64_t id;
};

const char CODES_MAGIC[8] = {'U', 'N', 'L', 'C', 'O', 'D', '1', '\0'};

int64_t count_passages(const std::string& sqlite_path) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(sqlite_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) return -1;
    sqlite3_stmt* st = nullptr;
    int64_t n = -1;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM passages", -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    sqlite3_close(db);
    return n;
}

int audit(const std::string& index_path, const std::string& packs) {
    Index ix;
    std::string why;
    if (!ix.open(index_path, &why)) return std::fprintf(stderr, "unllm_embed: %s\n", why.c_str()), 2;
    std::map<int, int64_t> held;
    for (const auto& L : ix.lists)
        for (int64_t i = 0; i < L.size; ++i) ++held[int(L.ids[i] >> 32)];
    std::printf("index %s: %lld passages, %d lists, %d code bytes a passage, %s\n", index_path.c_str(), (long long)ix.ntotal, ix.nlist, ix.M,
                ix.by_residual ? "encoded by residual" : "encoded whole");
    int bad = 0;
    for (const auto& s : ix.shards) {
        const int64_t in_index = held.count(s.no) ? held[s.no] : 0;
        const int64_t in_pack = count_passages(packs + "/" + s.pack + "/" + s.file);
        const char* verdict = in_pack < 0 ? "pack file missing" : in_pack == in_index ? "complete" : "SHORT";
        if (in_pack != in_index) ++bad;
        std::printf("shard %3d  %-18s %-18s  index %10lld  pack %10lld  %s%s\n", s.no, s.pack.c_str(), s.file.c_str(), (long long)in_index,
                    (long long)in_pack, verdict, in_pack > in_index && in_pack >= 0 ? (" (" + std::to_string(in_pack - in_index) + " missing)").c_str() : "");
    }
    for (const auto& [no, n] : held) {
        bool named = false;
        for (const auto& s : ix.shards) named |= s.no == no;
        if (!named) std::printf("shard %3d  (not in the shard table)  index %10lld\n", no, (long long)n), ++bad;
    }
    std::printf(bad ? "%d shard(s) differ from their packs\n" : "every shard complete\n", bad);
    return bad ? 1 : 0;
}

struct Passage {
    int64_t pid;
    std::string text;  // "title: text"
};

// The shard's passages, in pid order, each as the text the index was built from.
std::vector<Passage> read_passages(const std::string& sqlite_path, std::size_t chars, std::string* why) {
    std::vector<Passage> out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(sqlite_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) return *why = "cannot open " + sqlite_path, out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT p.pid, d.title, p.text FROM passages p JOIN docs d ON d.doc_id = p.doc_id ORDER BY p.pid", -1, &st, nullptr) != SQLITE_OK) {
        *why = "cannot read passages from " + sqlite_path;
        sqlite3_close(db);
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        Passage p;
        p.pid = sqlite3_column_int64(st, 0);
        const char* title = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        std::string body = text ? text : "";
        if (body.size() > chars) {
            body.resize(chars);
            while (!body.empty() && (static_cast<unsigned char>(body.back()) & 0xC0) == 0x80) body.pop_back();  // not inside a UTF-8 character
        }
        p.text = std::string(title ? title : "") + ": " + body;
        out.push_back(std::move(p));
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

// Vectors assigned and encoded against the index's tables, in parallel.
void encode_block(const Index& ix, const float* vecs, std::size_t n, int64_t shard_no, const std::vector<int64_t>& pids,
                  std::vector<Record>& recs, std::vector<uint8_t>& codes, int threads) {
    const std::size_t base = recs.size();
    recs.resize(base + n);
    codes.resize((base + n) * std::size_t(ix.M));
    auto work = [&](std::size_t from, std::size_t to) {
        std::vector<float> resid(std::size_t(ix.d));
        // The nearest centre by inner product: eight running sums per dot product, so the compiler vectorises the loop
        // without being allowed to reorder floating-point sums (the result is the same to the last bit either way for
        // the argmax).
        const int d8 = ix.d - ix.d % 8;
        for (std::size_t i = from; i < to; ++i) {
            const float* x = vecs + i * std::size_t(ix.d);
            int best = 0;
            float best_ip = -1e30f;
            for (int l = 0; l < ix.nlist; ++l) {
                const float* c = ix.centres + std::size_t(l) * ix.d;
                float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                for (int k = 0; k < d8; k += 8)
                    for (int j = 0; j < 8; ++j) acc[j] += c[k + j] * x[k + j];
                float s = acc[0] + acc[1] + acc[2] + acc[3] + acc[4] + acc[5] + acc[6] + acc[7];
                for (int k = d8; k < ix.d; ++k) s += c[k] * x[k];
                if (s > best_ip) best_ip = s, best = l;
            }
            const float* c = ix.centres + std::size_t(best) * ix.d;
            for (int k = 0; k < ix.d; ++k) resid[std::size_t(k)] = ix.by_residual ? x[k] - c[k] : x[k];
            uint8_t* code = codes.data() + (base + i) * std::size_t(ix.M);
            for (int m = 0; m < ix.M; ++m) {
                const float* r = resid.data() + m * ix.dsub;
                const float* book = ix.books + (std::size_t(m) * ix.ksub) * ix.dsub;
                int bj = 0;
                float bd = 1e30f;
                for (int j = 0; j < ix.ksub; ++j) {
                    const float* e = book + std::size_t(j) * ix.dsub;
                    float dist = 0;
                    for (int k = 0; k < ix.dsub; ++k) {
                        const float t = r[k] - e[k];
                        dist += t * t;
                    }
                    if (dist < bd) bd = dist, bj = j;
                }
                code[m] = uint8_t(bj);
            }
            recs[base + i] = Record{int32_t(best), (shard_no << 32) | pids[i]};
        }
    };
    std::vector<std::thread> pool;
    const std::size_t step = (n + std::size_t(threads) - 1) / std::size_t(threads);
    for (int t = 0; t < threads; ++t) {
        const std::size_t from = std::size_t(t) * step, to = std::min(n, from + step);
        if (from < to) pool.emplace_back(work, from, to);
    }
    for (auto& th : pool) th.join();
}

int encode(const std::string& index_path, const std::string& model_dir, const std::string& shard_path, int shard_no, const std::string& out_path,
           bool cpu, std::size_t chars, int batch) {
#ifndef UNLLM_HAVE_ONNX
    std::fprintf(stderr, "unllm_embed: built without ONNX Runtime\n");
    return 2;
#else
    Index ix;
    std::string why;
    if (!ix.open(index_path, &why)) return std::fprintf(stderr, "unllm_embed: %s\n", why.c_str()), 2;
    unllm::WordPiece wp;
    if (!wp.load(model_dir + "/vocab.txt")) return std::fprintf(stderr, "unllm_embed: no vocab.txt in %s\n", model_dir.c_str()), 2;
    int max_len = 192;
    if (std::ifstream jf(model_dir + "/embed.json"); jf) {
        std::stringstream ss;
        ss << jf.rdbuf();
        const std::string j = ss.str();
        // The passage length the index was built with is the index's business, not the query length in embed.json:
        // UNLLM_PASSAGE_TOKENS overrides the 192 the full build used.
        (void)j;
    }
    if (const char* e = std::getenv("UNLLM_PASSAGE_TOKENS")) max_len = std::max(8, std::atoi(e));
    const int threads = std::max(2u, std::thread::hardware_concurrency());
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "unllm-embed"};
    std::unique_ptr<Ort::Session> session;
    bool gpu = false;
    auto open = [&](bool cuda) {
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(threads);
        so.SetInterOpNumThreads(1);
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (cuda) {
            OrtCUDAProviderOptions o{};
            so.AppendExecutionProvider_CUDA(o);
        }
        return std::make_unique<Ort::Session>(env, (model_dir + "/model.onnx").c_str(), so);
    };
    try {
        session = open(!cpu);
        gpu = !cpu;
    } catch (const Ort::Exception& e) {
        if (cpu) return std::fprintf(stderr, "unllm_embed: cannot open the model: %s\n", e.what()), 2;
        std::fprintf(stderr, "unllm_embed: the GPU provider did not load (%s); using the CPU, which is far slower\n", e.what());
        try {
            session = open(false);
        } catch (const Ort::Exception& e2) {
            return std::fprintf(stderr, "unllm_embed: cannot open the model: %s\n", e2.what()), 2;
        }
    }
    std::printf("reading %s\n", shard_path.c_str());
    std::fflush(stdout);
    std::vector<Passage> passages = read_passages(shard_path, chars, &why);
    if (!why.empty()) return std::fprintf(stderr, "unllm_embed: %s\n", why.c_str()), 2;
    const std::size_t total = passages.size();
    std::printf("embedding %zu passages of shard %d on the %s, %d tokens each at most\n", total, shard_no, gpu ? "GPU" : "CPU", max_len);
    std::fflush(stdout);
    // Tokenised once, then taken in runs of similar length so padding stays small.
    struct Tok {
        std::size_t at;
        std::vector<int64_t> ids;
    };
    std::vector<Tok> toks(total);
    {
        std::atomic<std::size_t> next{0};
        auto work = [&] {
            for (std::size_t i; (i = next.fetch_add(256)) < total;)
                for (std::size_t k = i; k < std::min(total, i + 256); ++k) {
                    auto pieces = wp.tokenize(passages[k].text);
                    const std::size_t room = std::size_t(std::max(max_len, 3)) - 2;
                    if (pieces.size() > room) pieces.resize(room);
                    Tok& t = toks[k];
                    t.at = k;
                    t.ids.reserve(pieces.size() + 2);
                    t.ids.push_back(wp.cls());
                    for (const auto& p : pieces) t.ids.push_back(p.id);
                    t.ids.push_back(wp.sep());
                }
        };
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; ++t) pool.emplace_back(work);
        for (auto& th : pool) th.join();
    }
    std::vector<std::size_t> order(total);
    std::iota(order.begin(), order.end(), 0);
    // Sorted by length within runs of 8,192, so the order of the file is kept roughly and each batch pads little.
    for (std::size_t s = 0; s < total; s += 8192)
        std::sort(order.begin() + std::ptrdiff_t(s), order.begin() + std::ptrdiff_t(std::min(total, s + 8192)),
                  [&](std::size_t a, std::size_t b) { return toks[a].ids.size() < toks[b].ids.size(); });
    std::vector<Record> recs;
    std::vector<uint8_t> codes;
    recs.reserve(total);
    codes.reserve(total * std::size_t(ix.M));
    std::vector<float> vecs;  // the vectors of several batches, encoded together so the threads have real work
    std::vector<int64_t> pids;
    const std::size_t block = 4096;
    const auto t0 = std::chrono::steady_clock::now();
    double model_s = 0, encode_s = 0;  // where the time goes, said with the progress
    std::size_t done = 0, last_said = 0;
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    
    const char* out_names[] = {"last_hidden_state"};
    // Each batch is padded to its own longest text (sorted runs keep that small); UNLLM_PAD_FIXED=1 pads every batch
    // to the full length instead (measured 2026-10-26: four times slower on this card, the work grows with the padding).
    const bool tight = std::getenv("UNLLM_PAD_FIXED") == nullptr;
    // The model's inputs, by name: an export without position_ids gets only the three it has.
    std::vector<std::string> input_names;
    {
        Ort::AllocatorWithDefaultOptions alloc;
        for (std::size_t i = 0; i < session->GetInputCount(); ++i) input_names.push_back(session->GetInputNameAllocated(i, alloc).get());
    }
    std::printf("runtime %s; model inputs:", Ort::GetVersionString().c_str());
    for (const auto& nm : input_names) std::printf(" %s", nm.c_str());
    std::printf("\n");
    std::fflush(stdout);
    for (std::size_t b = 0; b < total; b += std::size_t(batch)) {
        const std::size_t n = std::min(std::size_t(batch), total - b);
        std::size_t L = tight ? 0 : std::size_t(max_len);
        if (tight)
            for (std::size_t i = 0; i < n; ++i) L = std::max(L, toks[order[b + i]].ids.size());
        std::vector<int64_t> ids(n * L, wp.pad()), mask(n * L, 0), types(n * L, 0), pos(n * L, 0);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& t = toks[order[b + i]].ids;
            for (std::size_t k = 0; k < t.size(); ++k) ids[i * L + k] = t[k], mask[i * L + k] = 1;
            for (std::size_t k = 0; k < L; ++k) pos[i * L + k] = int64_t(k);
        }
        const std::array<int64_t, 2> shape{int64_t(n), int64_t(L)};
        std::vector<Ort::Value> in;
        std::vector<const char*> names;
        for (const auto& nm : input_names) {
            std::vector<int64_t>* src = nm == "input_ids" ? &ids : nm == "attention_mask" ? &mask : nm == "token_type_ids" ? &types : nm == "position_ids" ? &pos : nullptr;
            if (!src) return std::fprintf(stderr, "unllm_embed: the model wants an input this tool doesn't make: %s\n", nm.c_str()), 2;
            in.push_back(Ort::Value::CreateTensor<int64_t>(mem, src->data(), src->size(), shape.data(), 2));
            names.push_back(nm.c_str());
        }
        std::vector<Ort::Value> res;
        const auto tm = std::chrono::steady_clock::now();
        try {
            res = session->Run(Ort::RunOptions{nullptr}, names.data(), in.data(), in.size(), out_names, 1);
        } catch (const Ort::Exception& e) {
            return std::fprintf(stderr, "unllm_embed: the model failed on a batch: %s\n", e.what()), 2;
        }
        model_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - tm).count();
        const float* h = res[0].GetTensorData<float>();  // (n, L, d): row 0 of each is the [CLS] vector
        const std::size_t have = pids.size();
        vecs.resize((have + n) * std::size_t(ix.d));
        pids.resize(have + n);
        for (std::size_t i = 0; i < n; ++i) {
            const float* v = h + i * L * std::size_t(ix.d);
            float norm = 0;
            for (int k = 0; k < ix.d; ++k) norm += v[k] * v[k];
            norm = std::sqrt(norm);
            for (int k = 0; k < ix.d; ++k) vecs[(have + i) * std::size_t(ix.d) + std::size_t(k)] = norm > 0 ? v[k] / norm : 0.0f;
            pids[have + i] = passages[order[b + i]].pid;
        }
        if (pids.size() >= block || b + n >= total) {
            const auto te = std::chrono::steady_clock::now();
            encode_block(ix, vecs.data(), pids.size(), shard_no, pids, recs, codes, threads);
            encode_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - te).count();
            vecs.clear();
            pids.clear();
        }
        done += n;
        if (done - last_said >= 20000 || done == total) {
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("progress %zu of %zu (%.0f a second; model %.0f s, encoding %.0f s)\n", done, total, secs > 0 ? double(done) / secs : 0.0, model_s, encode_s);
            std::fflush(stdout);
            last_said = done;
        }
    }
    // The records in pid order, as the shard reads.
    std::vector<std::size_t> by_pid(recs.size());
    std::iota(by_pid.begin(), by_pid.end(), 0);
    std::sort(by_pid.begin(), by_pid.end(), [&](std::size_t a, std::size_t b) { return recs[a].id < recs[b].id; });
    std::ofstream out(out_path + ".tmp", std::ios::binary);
    if (!out) return std::fprintf(stderr, "unllm_embed: cannot write %s\n", out_path.c_str()), 2;
    out.write(CODES_MAGIC, 8);
    const int32_t m32 = ix.M;
    const int64_t n64 = int64_t(recs.size());
    out.write(reinterpret_cast<const char*>(&m32), 4);
    out.write(reinterpret_cast<const char*>(&n64), 8);
    for (std::size_t i : by_pid) {
        out.write(reinterpret_cast<const char*>(&recs[i].list), 4);
        out.write(reinterpret_cast<const char*>(&recs[i].id), 8);
        out.write(reinterpret_cast<const char*>(codes.data() + i * std::size_t(ix.M)), ix.M);
    }
    out.close();
    if (std::rename((out_path + ".tmp").c_str(), out_path.c_str()) != 0) return std::fprintf(stderr, "unllm_embed: cannot place %s\n", out_path.c_str()), 2;
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("wrote %s: %zu passages in %.0f s\n", out_path.c_str(), recs.size(), secs);
    return 0;
#endif
}

struct Added {
    std::string path, pack, file;
    int no = 0;
};

int patch(const std::string& index_path, const std::string& out_path, const std::vector<int>& drop, const std::vector<Added>& adds) {
    Index ix;
    std::string why;
    if (!ix.open(index_path, &why)) return std::fprintf(stderr, "unllm_embed: %s\n", why.c_str()), 2;
    // The records to add, bucketed by list.
    std::vector<std::vector<std::pair<int64_t, std::vector<uint8_t>>>> add_by_list(std::size_t(ix.nlist));
    int64_t added = 0;
    for (const auto& a : adds) {
        std::ifstream in(a.path, std::ios::binary);
        char magic[8];
        int32_t m = 0;
        int64_t n = 0;
        if (!in.read(magic, 8) || std::memcmp(magic, CODES_MAGIC, 8) != 0 || !in.read(reinterpret_cast<char*>(&m), 4) ||
            !in.read(reinterpret_cast<char*>(&n), 8) || m != ix.M)
            return std::fprintf(stderr, "unllm_embed: %s is not a codes file for this index\n", a.path.c_str()), 2;
        for (int64_t i = 0; i < n; ++i) {
            int32_t list = 0;
            int64_t id = 0;
            std::vector<uint8_t> code(std::size_t(ix.M));
            if (!in.read(reinterpret_cast<char*>(&list), 4) || !in.read(reinterpret_cast<char*>(&id), 8) ||
                !in.read(reinterpret_cast<char*>(code.data()), ix.M))
                return std::fprintf(stderr, "unllm_embed: %s ends early\n", a.path.c_str()), 2;
            if (list < 0 || list >= ix.nlist) return std::fprintf(stderr, "unllm_embed: %s names list %d\n", a.path.c_str(), list), 2;
            if (int(id >> 32) != a.no) return std::fprintf(stderr, "unllm_embed: %s holds shard %d, not %d\n", a.path.c_str(), int(id >> 32), a.no), 2;
            add_by_list[std::size_t(list)].emplace_back(id, std::move(code));
            ++added;
        }
    }
    // Shards replaced are dropped as well as added; a shard new to the index joins the table.
    std::vector<int> dropped = drop;
    for (const auto& a : adds) dropped.push_back(a.no);
    std::vector<Index::Shard> shards = ix.shards;
    for (const auto& a : adds) {
        bool named = false;
        for (auto& s : shards)
            if (s.no == a.no) named = true, s.pack = a.pack, s.file = a.file;
        if (!named) shards.push_back(Index::Shard{a.no, a.pack, a.file});
    }
    auto is_dropped = [&](int64_t id) { return std::find(dropped.begin(), dropped.end(), int(id >> 32)) != dropped.end(); };
    int64_t kept = 0, removed = 0;
    for (const auto& L : ix.lists)
        for (int64_t i = 0; i < L.size; ++i) (is_dropped(L.ids[i]) ? removed : kept)++;
    const int64_t ntotal = kept + added;
    std::ofstream out(out_path + ".tmp", std::ios::binary);
    if (!out) return std::fprintf(stderr, "unllm_embed: cannot write %s\n", out_path.c_str()), 2;
    out.write("UNLPQ1\0\0", 8);
    const int32_t head[6] = {ix.d, ix.nlist, ix.M, ix.nbits, ix.metric, ix.by_residual};
    out.write(reinterpret_cast<const char*>(head), sizeof head);
    out.write(reinterpret_cast<const char*>(&ntotal), 8);
    const int32_t nshards = int32_t(shards.size());
    out.write(reinterpret_cast<const char*>(&nshards), 4);
    for (const auto& s : shards) {
        out.write(reinterpret_cast<const char*>(&s.no), 4);
        char name[64] = {}, file[64] = {};
        std::strncpy(name, s.pack.c_str(), 63);
        std::strncpy(file, s.file.c_str(), 63);
        out.write(name, 64);
        out.write(file, 64);
    }
    const std::size_t at = 8 + sizeof head + 8 + 4 + shards.size() * 132;
    for (std::size_t k = at; k % 8; ++k) out.put('\0');
    out.write(reinterpret_cast<const char*>(ix.centres), std::streamsize(std::size_t(ix.nlist) * ix.d * 4));
    out.write(reinterpret_cast<const char*>(ix.books), std::streamsize(std::size_t(ix.M) * ix.ksub * ix.dsub * 4));
    std::vector<int64_t> ids;
    std::vector<uint8_t> codes;
    for (int l = 0; l < ix.nlist; ++l) {
        const auto& L = ix.lists[std::size_t(l)];
        ids.clear();
        codes.clear();
        for (int64_t i = 0; i < L.size; ++i)
            if (!is_dropped(L.ids[i])) {
                ids.push_back(L.ids[i]);
                codes.insert(codes.end(), L.codes + i * ix.M, L.codes + (i + 1) * ix.M);
            }
        for (const auto& [id, code] : add_by_list[std::size_t(l)]) {
            ids.push_back(id);
            codes.insert(codes.end(), code.begin(), code.end());
        }
        const int64_t n = int64_t(ids.size());
        out.write(reinterpret_cast<const char*>(&n), 8);
        out.write(reinterpret_cast<const char*>(ids.data()), std::streamsize(ids.size() * 8));
        out.write(reinterpret_cast<const char*>(codes.data()), std::streamsize(codes.size()));
    }
    out.close();
    if (std::rename((out_path + ".tmp").c_str(), out_path.c_str()) != 0) return std::fprintf(stderr, "unllm_embed: cannot place %s\n", out_path.c_str()), 2;
    std::printf("wrote %s: %lld passages (%lld kept, %lld removed, %lld added), %zu shards\n", out_path.c_str(), (long long)ntotal, (long long)kept,
                (long long)removed, (long long)added, shards.size());
    return 0;
}

int usage() {
    std::fputs("unllm_embed: the vector index's maintenance.\n"
               "  unllm_embed audit  INDEX.unlpq PACKS_DIR\n"
               "  unllm_embed encode INDEX.unlpq MODEL_DIR SHARD.sqlite SHARD_NO OUT.codes [--cpu] [--chars 900] [--batch 128]\n"
               "  unllm_embed patch  INDEX.unlpq OUT.unlpq [--drop NO]... [--add FILE.codes NO PACK SHARD_FILE]...\n",
               stderr);
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    if (cmd == "audit" && argc == 4) return audit(argv[2], argv[3]);
    if (cmd == "encode" && argc >= 7) {
        bool cpu = false;
        std::size_t chars = 900;
        int batch = 128;
        for (int i = 7; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--cpu") cpu = true;
            else if (a == "--chars" && i + 1 < argc) chars = std::size_t(std::atol(argv[++i]));
            else if (a == "--batch" && i + 1 < argc) batch = std::max(1, std::atoi(argv[++i]));
            else return usage();
        }
        return encode(argv[2], argv[3], argv[4], std::atoi(argv[5]), argv[6], cpu, chars, batch);
    }
    if (cmd == "patch" && argc >= 4) {
        std::vector<int> drop;
        std::vector<Added> adds;
        for (int i = 4; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--drop" && i + 1 < argc) drop.push_back(std::atoi(argv[++i]));
            else if (a == "--add" && i + 4 < argc) {
                Added x;
                x.path = argv[++i];
                x.no = std::atoi(argv[++i]);
                x.pack = argv[++i];
                x.file = argv[++i];
                adds.push_back(x);
            } else return usage();
        }
        return patch(argv[2], argv[3], drop, adds);
    }
    return usage();
}
