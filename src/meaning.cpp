#include "unllm/meaning.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <queue>
#include <sstream>

#include "unllm/reader.h"
#ifdef UNLLM_HAVE_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace unllm {

namespace {

struct List {
    int64_t size = 0;
    const int64_t* ids = nullptr;
    const uint8_t* codes = nullptr;
};

// The value of "key" in a small JSON file, strings and numbers only.
std::string json_value(const std::string& text, const std::string& key) {
    auto at = text.find("\"" + key + "\"");
    if (at == std::string::npos) return "";
    at = text.find(':', at);
    if (at == std::string::npos) return "";
    ++at;
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
    if (at < text.size() && text[at] == '"') {
        std::string out;
        for (++at; at < text.size() && text[at] != '"'; ++at) {
            if (text[at] == '\\' && at + 1 < text.size()) ++at;
            out += text[at];
        }
        return out;
    }
    std::string out;
    while (at < text.size() && (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == '.' || text[at] == '-'))
        out += text[at++];
    return out;
}

float dot(const float* a, const float* b, int n) {
    float s = 0;
    for (int i = 0; i < n; ++i) s += a[i] * b[i];
    return s;
}

}  // namespace

struct Meaning::Impl {
    void* map = nullptr;
    std::size_t map_size = 0;
    int d = 0, nlist = 0, M = 0, nbits = 0, metric = 0, by_residual = 1, ksub = 256, dsub = 0;
    int64_t ntotal = 0;
    const float* centres = nullptr;
    const float* books = nullptr;
    std::vector<List> lists;
    std::vector<ShardRef> shards;
    WordPiece wp;
    std::string prefix;
    int max_len = 64;
#ifdef UNLLM_HAVE_ONNX
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "unllm-meaning"};
    std::unique_ptr<Ort::Session> session;
#endif
    ~Impl() {
        if (map) munmap(map, map_size);
    }
};

Meaning::Meaning() : impl_(std::make_unique<Impl>()) {}
Meaning::~Meaning() = default;

bool Meaning::load(const std::string& index_path, const std::string& model_dir, int threads) {
    Impl& im = *impl_;
    int fd = ::open(index_path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size < 64) {
        ::close(fd);
        return false;
    }
    im.map_size = std::size_t(st.st_size);
    im.map = mmap(nullptr, im.map_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (im.map == MAP_FAILED) {
        im.map = nullptr;
        return false;
    }
    const char* p = static_cast<const char*>(im.map);
    const char* end = p + im.map_size;
    if (std::memcmp(p, "UNLPQ1\0\0", 8) != 0) return false;
    p += 8;
    int32_t head[6];
    std::memcpy(head, p, sizeof head);
    p += sizeof head;
    im.d = head[0];
    im.nlist = head[1];
    im.M = head[2];
    im.nbits = head[3];
    im.metric = head[4];
    im.by_residual = head[5];
    std::memcpy(&im.ntotal, p, 8);
    p += 8;
    int32_t nshards = 0;
    std::memcpy(&nshards, p, 4);
    p += 4;
    if (im.metric != 0 || im.nbits != 8 || im.M <= 0 || im.d % im.M != 0 || nshards < 0) return false;
    im.ksub = 256;
    im.dsub = im.d / im.M;
    for (int i = 0; i < nshards; ++i) {
        if (p + 132 > end) return false;
        ShardRef s;
        std::memcpy(&s.no, p, 4);
        s.pack = std::string(p + 4, strnlen(p + 4, 64));
        s.file = std::string(p + 68, strnlen(p + 68, 64));
        s.file_no = s.file.size() > 6 ? std::atoi(s.file.c_str() + 6) : 0;
        im.shards.push_back(std::move(s));
        p += 132;
    }
    // The header is padded so that every array after it starts on 8 bytes.
    p = static_cast<const char*>(im.map) + ((std::size_t(p - static_cast<const char*>(im.map)) + 7) & ~std::size_t(7));
    const std::size_t centres_bytes = std::size_t(im.nlist) * im.d * 4;
    const std::size_t books_bytes = std::size_t(im.M) * im.ksub * im.dsub * 4;
    if (p + centres_bytes + books_bytes > end) return false;
    im.centres = reinterpret_cast<const float*>(p);
    p += centres_bytes;
    im.books = reinterpret_cast<const float*>(p);
    p += books_bytes;
    im.lists.resize(std::size_t(im.nlist));
    for (int l = 0; l < im.nlist; ++l) {
        if (p + 8 > end) return false;
        List& L = im.lists[std::size_t(l)];
        std::memcpy(&L.size, p, 8);
        p += 8;
        if (L.size < 0 || p + std::size_t(L.size) * (8 + std::size_t(im.M)) > end) return false;
        L.ids = reinterpret_cast<const int64_t*>(p);
        p += std::size_t(L.size) * 8;
        L.codes = reinterpret_cast<const uint8_t*>(p);
        p += std::size_t(L.size) * std::size_t(im.M);
    }
    return load_model(model_dir, threads);
}

bool Meaning::load_model(const std::string& model_dir, int threads) {
    Impl& im = *impl_;
    if (!im.wp.load(model_dir + "/vocab.txt")) return false;
    std::ifstream jf(model_dir + "/embed.json");
    std::stringstream ss;
    ss << jf.rdbuf();
    im.prefix = json_value(ss.str(), "query_prefix");
    const std::string ml = json_value(ss.str(), "max_len");
    if (!ml.empty()) im.max_len = std::atoi(ml.c_str());
    // Without an index the vector size comes from the model's own description.
    const std::string dim = json_value(ss.str(), "dim");
    if (im.d == 0 && !dim.empty()) im.d = std::atoi(dim.c_str());
#ifdef UNLLM_HAVE_ONNX
    try {
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(threads);
        so.SetInterOpNumThreads(1);
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        im.session = std::make_unique<Ort::Session>(im.env, (model_dir + "/model.onnx").c_str(), so);
    } catch (const Ort::Exception&) {
        im.session.reset();
        return false;
    }
    return im.d > 0;
#else
    (void)threads;
    return false;
#endif
}

bool Meaning::ready() const {
#ifdef UNLLM_HAVE_ONNX
    return impl_->session != nullptr && impl_->centres != nullptr;
#else
    return false;
#endif
}

bool Meaning::can_embed() const {
#ifdef UNLLM_HAVE_ONNX
    return impl_->session != nullptr && impl_->d > 0;
#else
    return false;
#endif
}

int Meaning::dim() const { return impl_->d; }
int64_t Meaning::size() const { return impl_->ntotal; }
const std::vector<Meaning::ShardRef>& Meaning::shards() const { return impl_->shards; }

std::vector<float> Meaning::embed(const std::string& text, bool query) const {
    std::vector<float> out;
#ifdef UNLLM_HAVE_ONNX
    const Impl& im = *impl_;
    if (!can_embed()) return out;
    // A question looking for passages gets the model's search prefix; two texts
    // compared with each other (a question against example phrasings) get none.
    auto pieces = im.wp.tokenize((query ? im.prefix : std::string()) + text);
    const std::size_t room = std::size_t(std::max(im.max_len, 3)) - 2;
    if (pieces.size() > room) pieces.resize(room);
    const std::size_t N = pieces.size() + 2;
    const int64_t n = int64_t(N);
    std::vector<int64_t> ids(N), mask(N, 1), types(N, 0), pos(N);
    ids[0] = im.wp.cls();
    for (std::size_t i = 0; i < pieces.size(); ++i) ids[i + 1] = pieces[i].id;
    ids[N - 1] = im.wp.sep();
    for (std::size_t i = 0; i < N; ++i) pos[i] = int64_t(i);
    try {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 2> shape{1, n};
        std::array<Ort::Value, 4> in{
            Ort::Value::CreateTensor<int64_t>(mem, ids.data(), ids.size(), shape.data(), 2),
            Ort::Value::CreateTensor<int64_t>(mem, mask.data(), mask.size(), shape.data(), 2),
            Ort::Value::CreateTensor<int64_t>(mem, types.data(), types.size(), shape.data(), 2),
            Ort::Value::CreateTensor<int64_t>(mem, pos.data(), pos.size(), shape.data(), 2)};
        const char* in_names[] = {"input_ids", "attention_mask", "token_type_ids", "position_ids"};
        const char* out_names[] = {"last_hidden_state"};
        auto res = im.session->Run(Ort::RunOptions{nullptr}, in_names, in.data(), 4, out_names, 1);
        const float* h = res[0].GetTensorData<float>();  // (1, n, d): the first row is the [CLS] vector
        out.assign(h, h + im.d);
    } catch (const Ort::Exception&) {
        return {};
    }
    float norm = std::sqrt(dot(out.data(), out.data(), im.d));
    if (norm > 0)
        for (float& x : out) x /= norm;
#else
    (void)question;
#endif
    return out;
}

std::vector<Meaning::Hit> Meaning::search(const std::vector<float>& q, int k, int nprobe) const {
    std::vector<Hit> out;
    const Impl& im = *impl_;
    if (!im.centres || int(q.size()) != im.d || k <= 0) return out;
    // The nearest lists.
    std::vector<float> coarse(std::size_t(im.nlist));
    for (int l = 0; l < im.nlist; ++l) coarse[std::size_t(l)] = dot(q.data(), im.centres + std::size_t(l) * im.d, im.d);
    std::vector<int> order(std::size_t(im.nlist));
    for (int l = 0; l < im.nlist; ++l) order[std::size_t(l)] = l;
    nprobe = std::min(std::max(nprobe, 1), im.nlist);
    std::partial_sort(order.begin(), order.begin() + nprobe, order.end(),
                      [&](int a, int b) { return coarse[std::size_t(a)] > coarse[std::size_t(b)]; });
    // One lookup table for the whole query: the score of each codebook entry
    // in each sub-space. With residual codes the list centre's share is added
    // per list.
    std::vector<float> table(std::size_t(im.M) * im.ksub);
    for (int m = 0; m < im.M; ++m)
        for (int j = 0; j < im.ksub; ++j)
            table[std::size_t(m) * im.ksub + j] =
                dot(q.data() + m * im.dsub, im.books + (std::size_t(m) * im.ksub + j) * im.dsub, im.dsub);
    using Item = std::pair<float, int64_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    for (int pi = 0; pi < nprobe; ++pi) {
        const int l = order[std::size_t(pi)];
        const List& L = im.lists[std::size_t(l)];
        const float base = im.by_residual ? coarse[std::size_t(l)] : 0.0f;
        for (int64_t i = 0; i < L.size; ++i) {
            const uint8_t* code = L.codes + std::size_t(i) * im.M;
            float s = base;
            for (int m = 0; m < im.M; ++m) s += table[std::size_t(m) * im.ksub + code[m]];
            if (int(heap.size()) < k) heap.emplace(s, L.ids[i]);
            else if (s > heap.top().first) {
                heap.pop();
                heap.emplace(s, L.ids[i]);
            }
        }
    }
    while (!heap.empty()) {
        const auto [s, id] = heap.top();
        heap.pop();
        out.push_back({int(id >> 32), int64_t(id & 0xffffffff), s});
    }
    std::reverse(out.begin(), out.end());
    return out;
}

}  // namespace unllm
