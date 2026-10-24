// Measuring tool for the meaning search: for each question on stdin, prints one JSON line
// with the question's vector and the ids of its k best passages, for comparison with the
// Python reference (bench/meaning_check.py).
//   unllm_meaning INDEX.unlpq MODEL_DIR [k] [nprobe]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "unllm/meaning.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: unllm_meaning INDEX.unlpq MODEL_DIR [k] [nprobe]\n");
        return 2;
    }
    const int k = argc > 3 ? std::atoi(argv[3]) : 20;
    const int nprobe = argc > 4 ? std::atoi(argv[4]) : 64;
    unllm::Meaning m;
    if (std::string(argv[1]) == "--embed") {  // the model alone: one plain vector (no search prefix) per line
        if (!m.load_model(argv[2])) {
            std::fprintf(stderr, "unllm_meaning: cannot load the model\n");
            return 1;
        }
        std::string line;
        while (std::getline(std::cin, line)) {
            auto v = m.embed(line, false);
            std::printf("[");
            for (std::size_t i = 0; i < v.size(); ++i) std::printf("%s%.6f", i ? "," : "", double(v[i]));
            std::printf("]\n");
        }
        return 0;
    }
    if (!m.load(argv[1], argv[2])) {
        std::fprintf(stderr, "unllm_meaning: cannot load the index or the model\n");
        return 1;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        auto t0 = std::chrono::steady_clock::now();
        auto v = m.embed(line);
        auto t1 = std::chrono::steady_clock::now();
        auto hits = m.search(v, k, nprobe);
        auto t2 = std::chrono::steady_clock::now();
        std::printf("{\"vector\": [");
        for (std::size_t i = 0; i < v.size(); ++i) std::printf("%s%.6f", i ? "," : "", double(v[i]));
        std::printf("], \"ids\": [");
        for (std::size_t i = 0; i < hits.size(); ++i)
            std::printf("%s%lld", i ? "," : "", static_cast<long long>((int64_t(hits[i].shard_no) << 32) | hits[i].pid));
        std::printf("], \"embed_ms\": %.1f, \"search_ms\": %.1f}\n",
                    std::chrono::duration<double, std::milli>(t1 - t0).count(),
                    std::chrono::duration<double, std::milli>(t2 - t1).count());
    }
    return 0;
}
