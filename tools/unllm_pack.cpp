// unllm_pack: look inside installed knowledge packs.
//
//   unllm_pack list   [PACKS]               what is installed
//   unllm_pack query  PACKS "question words"  the candidate passages, best first
//   unllm_pack bench  PACKS FILE            latency over one question per line
//   unllm_pack wordpiece MODEL              reader token ids, one input line each
//   unllm_pack read   MODEL "question"      the reader's best span in each stdin line
//
// PACKS defaults to ~/.local/share/unllm/packs.

#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "unllm/knowledge.h"
#include "unllm/reader.h"

using namespace unllm;

static std::string default_packs() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    return (xdg ? std::string(xdg) : std::string(home ? home : "") + "/.local/share") + "/unllm/packs";
}

static Query query_of(const std::string& text) {
    Query q;
    for (const auto& t : tokenize(text))
        if (t.lower.size() >= 3) q.words.push_back(t.lower);
    q.subject = text;
    return q;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("usage: unllm_pack list|query|search|bench [PACKS] ...");
        return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "wordpiece" && argc > 2) {
        WordPiece wp;
        if (!wp.load(std::string(argv[2]) + "/vocab.txt")) return 1;
        std::string line;
        while (std::getline(std::cin, line)) {
            std::string out;
            for (const auto& p : wp.tokenize(line)) out += std::to_string(p.id) + " ";
            std::puts(out.c_str());
        }
        return 0;
    }
    if (cmd == "read" && argc > 3) {
        Reader reader;
        if (!reader.load(argv[2])) {
            std::puts("no reader model (or built without ONNX Runtime)");
            return 1;
        }
        std::vector<std::string> ps;
        std::string line;
        while (std::getline(std::cin, line)) ps.push_back(line);
        const auto t0 = std::chrono::steady_clock::now();
        const auto spans = reader.read(argv[3], ps);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        for (const auto& r : spans)
            std::printf("%zu\t%.3f\t%.3f\t%.4f\t%s\n", r.passage, r.score, r.null_score, r.prob, r.text.c_str());
        std::printf("---\t%.0f ms\n", ms);
        return 0;
    }
    const std::string packs = argc > 2 ? argv[2] : default_packs();
    Library lib;
    if (!lib.open(packs)) {
        std::printf("no packs in %s\n", packs.c_str());
        return 1;
    }
    if (cmd == "list") {
        for (const auto& p : lib.packs())
            std::printf("%-22s %-13s %-16s %s\n", p.name.c_str(), p.kind.c_str(), p.version.c_str(), p.license.c_str());
        std::printf("%lld passages\n", (long long)lib.passage_count());
        return 0;
    }
    if (cmd == "query" && argc > 3) {
        auto t0 = std::chrono::steady_clock::now();
        auto ps = lib.candidates(query_of(argv[3]));
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        for (std::size_t i = 0; i < ps.size() && i < 8; ++i)
            std::printf("%5.1f %-18s %-28.28s %.110s\n", ps[i].bm25, ps[i].pack.c_str(), ps[i].title.c_str(),
                        ps[i].text.c_str());
        std::printf("%zu candidates in %.1f ms\n", ps.size(), ms);
        return 0;
    }
    if (cmd == "search" && argc > 3 && std::string(argv[3]) == "-") {  // one question per line, "---" after each
        const int k = argc > 4 ? std::atoi(argv[4]) : 20;
        std::string line;
        while (std::getline(std::cin, line)) {
            auto t0 = std::chrono::steady_clock::now();
            auto ps = lib.ranked(line, {}, k);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            for (const auto& p : ps) {
                std::string t = p.text;
                for (char& c : t)
                    if (c == '\n') c = ' ';
                std::printf("%s\t%s\t%s\n", p.pack.c_str(), p.title.c_str(), t.c_str());
            }
            std::printf("---\t%.1f\n", ms);
            std::fflush(stdout);
        }
        return 0;
    }
    if (cmd == "search" && argc > 3) {  // plain Tantivy ranking, no unllm rules
        const int k = argc > 4 ? std::atoi(argv[4]) : 20;
        auto t0 = std::chrono::steady_clock::now();
        auto ps = lib.ranked(argv[3], {}, k);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        for (const auto& p : ps) {
            std::string t = p.text;
            for (char& c : t)
                if (c == '\n') c = ' ';
            std::printf("[passage] %s: %s\n", p.title.c_str(), t.c_str());
        }
        std::printf("%zu passages in %.1f ms%s\n", ps.size(), ms, lib.has_ranked() ? "" : " (no Tantivy index)");
        return 0;
    }
    if (cmd == "answer" && argc > 3) {
        // The whole world path: read, route, search, rerank, compose.
        Vectors vx;
        const char* home = std::getenv("HOME");
        vx.load(std::string(home ? home : "") + "/.local/share/unllm/vectors/meaning.unl2");
        Analyzer an;
        World world(lib, &vx);
        for (int a = 3; a < argc; ++a) {
            auto t0 = std::chrono::steady_clock::now();
            Reading r = an.read(argv[a], 0);
            WorldAnswer w = world.answer(r, argv[a]);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("== %s   [route %s, frame %s, subject \"%s\", %.0f ms, confidence %.2f]\n", argv[a],
                        route_name(route(r)), frame_name(r.frame), r.subject.c_str(), ms, w.confidence);
            std::printf("%s\n", w.kind == WorldAnswer::None ? "(no answer)" : w.text.c_str());
            for (const auto& rc : w.receipts) std::printf("   %s\n", rc.c_str());
        }
        return 0;
    }
    if (cmd == "bench" && argc > 3) {
        std::ifstream f(argv[3]);
        std::vector<double> times;
        std::string line;
        while (std::getline(f, line)) {
            auto tab = line.rfind('\t');
            if (tab != std::string::npos) line = line.substr(tab + 1);
            if (line.empty() || line[0] == '#') continue;
            auto t0 = std::chrono::steady_clock::now();
            lib.candidates(query_of(line));
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        if (times.empty()) return 1;
        std::sort(times.begin(), times.end());
        auto at = [&](double q) { return times[std::size_t(q * double(times.size() - 1))]; };
        std::printf("%zu questions: p50 %.1f ms, p95 %.1f ms, p99 %.1f ms, max %.1f ms\n", times.size(), at(0.5),
                    at(0.95), at(0.99), times.back());
        return 0;
    }
    std::puts("usage: unllm_pack list|query|search|bench [PACKS] ...");
    return 1;
}
