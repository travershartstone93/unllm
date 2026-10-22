// unllm_rerank: checks the C++ reranker against the Python reference and
// times it.
//   unllm_rerank check REF.jsonl [DIR]   every pair's token ids and score against the reference
//   unllm_rerank time POOLS.jsonl [DIR]  seconds per question over saved pools (pool_read50.py save)
// DIR defaults to ~/.local/share/unllm/models/reranker.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "unllm/local_model.h"
#include "unllm/reranker.h"

using namespace unllm;

namespace {

std::string default_dir() {
    const char* h = getenv("HOME");
    return std::string(h ? h : "") + "/.local/share/unllm/models/reranker";
}

// The integers of a JSON array value of "key".
std::vector<int64_t> json_ints(const std::string& text, const std::string& key) {
    std::vector<int64_t> out;
    auto at = text.find("\"" + key + "\"");
    if (at == std::string::npos) return out;
    at = text.find('[', at);
    if (at == std::string::npos) return out;
    const auto end = text.find(']', at);
    std::string cur;
    for (std::size_t i = at + 1; i < end; ++i) {
        if (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '-') cur += text[i];
        else if (!cur.empty()) {
            out.push_back(std::atoll(cur.c_str()));
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(std::atoll(cur.c_str()));
    return out;
}

// The strings of a JSON array value of "key".
std::vector<std::string> json_strings(const std::string& text, const std::string& key) {
    std::vector<std::string> out;
    auto at = text.find("\"" + key + "\"");
    if (at == std::string::npos) return out;
    at = text.find('[', at);
    if (at == std::string::npos) return out;
    for (std::size_t i = at + 1; i < text.size() && text[i] != ']';) {
        if (text[i] != '"') {
            ++i;
            continue;
        }
        std::string raw;
        for (++i; i < text.size() && text[i] != '"'; ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) raw += text[i++];
            raw += text[i];
        }
        ++i;
        out.push_back(json_unescape(raw));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::puts("usage: unllm_rerank check REF.jsonl [DIR] | time POOLS.jsonl [DIR]");
        return 2;
    }
    const std::string mode = argv[1], file = argv[2], dir = argc > 3 ? argv[3] : default_dir();
    Reranker rr;
    if (!rr.load(dir)) {
        std::printf("could not load the reranker from %s\n", dir.c_str());
        return 1;
    }
    std::printf("reranker loaded from %s, %s\n", dir.c_str(), rr.on_gpu() ? "on the GPU" : "on the CPU");
    std::ifstream in(file);
    std::string line;
    if (mode == "check") {
        int n = 0, ids_ok = 0, score_ok = 0;
        double worst = 0;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            const std::string q = json_field(line, "question"), p = json_field(line, "passage");
            const auto ref_ids = json_ints(line, "ids");
            const double ref_score = std::atof(json_field(line, "score").c_str());
            const auto ids = rr.encode(q, p);
            const auto sc = rr.scores(q, {p});
            const bool same_ids = ids == ref_ids;
            const double gap = sc.empty() ? 99 : std::fabs(double(sc[0]) - ref_score);
            ++n;
            ids_ok += same_ids;
            score_ok += gap < 0.05;
            worst = std::max(worst, gap);
            if (!same_ids || gap >= 0.05) {
                std::printf("  pair %d: ids %s (%zu vs %zu), score %.3f vs %.3f\n", n, same_ids ? "same" : "DIFFER",
                            ids.size(), ref_ids.size(), sc.empty() ? 0.0 : double(sc[0]), ref_score);
                if (!same_ids)
                    for (std::size_t i = 0; i < std::min(ids.size(), ref_ids.size()); ++i)
                        if (ids[i] != ref_ids[i]) {
                            std::printf("    first difference at %zu: %lld vs %lld\n", i, (long long)ids[i], (long long)ref_ids[i]);
                            break;
                        }
            }
        }
        std::printf("check: %d pairs, token ids same on %d, scores within 0.05 on %d, largest score gap %.4f\n", n, ids_ok,
                    score_ok, worst);
        return (ids_ok == n && score_ok == n) ? 0 : 1;
    }
    if (mode == "time") {
        int n = 0;
        double secs = 0;
        std::size_t docs = 0;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            const std::string q = json_field(line, "question");
            const auto pool = json_strings(line, "pool");
            if (pool.empty()) continue;
            const auto t0 = std::chrono::steady_clock::now();
            const auto sc = rr.scores(q, pool);
            const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (n > 0) secs += dt;  // the first run warms the GPU up
            docs += pool.size();
            ++n;
            if (sc.size() != pool.size()) std::printf("  question %d: %zu scores for %zu passages\n", n, sc.size(), pool.size());
            if (n >= 20) break;
        }
        std::printf("time: %d questions, %.2f s per question after the first, %zu passages each\n", n,
                    n > 1 ? secs / (n - 1) : 0.0, n ? docs / std::size_t(n) : 0);
        return 0;
    }
    std::puts("unknown mode");
    return 2;
}
