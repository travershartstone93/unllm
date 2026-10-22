// Coverage: run the analyzer over corpora of real-world question phrasings
// and report how many are understood, refused honestly, or refused badly.
// The published corpus (data/corpus.txt) is mined from what people actually
// ask: top voted support-site questions, chatbot logs, autocomplete
// phrasings. One question per line, # for comments; lines with tabs keep
// only their last field, so ranked lists ("count<TAB>question") work as-is.
// This tool is a gauge, not a gate: the exam owns correctness, this number
// owns breadth, and it should only go up.
//
//   unllm_coverage [files...] [--banter PATH] [--report N] [-v]
//
// --report N lists the N most frequent words in refused questions that no
// table recognizes: the to-do list for the vocabulary.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "unllm/unllm.h"

using namespace unllm;

int main(int argc, char** argv) {
    std::vector<std::string> files;
    std::string banter_path = "data/banter.txt";
    int report = 0;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--banter" && i + 1 < argc) banter_path = argv[++i];
        else if (a == "--report" && i + 1 < argc) report = std::atoi(argv[++i]);
        else if (a == "-v") verbose = true;
        else files.push_back(a);
    }
    if (files.empty()) files.push_back("data/corpus.txt");

    Analyzer an;
    // A plausible gazetteer, so program-name questions can land.
    an.set_programs({"firefox", "chrome", "chromium", "blender", "steam", "spotify", "discord",
                     "vlc", "gimp", "libreoffice", "python", "node", "java", "docker"});
    auto syn = load_synonyms("data/synonyms.txt");
    if (!syn.empty()) an.set_synonyms(std::move(syn));
    Vectors vx;
    if (vx.load("data/vectors.bin")) an.set_vectors(&vx);
    Banter banter;
    banter.load(banter_path);
    Rewrites rewrites;
    rewrites.load("data/rewrites.txt");
    const auto known_list = an.known_words();
    const std::set<std::string> known(known_list.begin(), known_list.end());

    const int64_t now = int64_t(std::time(nullptr));
    int total = 0, understood = 0, bantered = 0, honest = 0, bad = 0;
    std::map<std::string, int> unknown;
    for (const auto& path : files) {
        std::ifstream f(path);
        if (!f) {
            std::printf("coverage: cannot open %s\n", path.c_str());
            return 1;
        }
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto tab = line.rfind('\t');
            if (tab != std::string::npos) line = line.substr(tab + 1);
            if (line.empty()) continue;
            ++total;
            // Banter sees the raw words (the classics keep their exact shape);
            // analysis sees the question with its chatty coat removed.
            if (!banter.match(tokenize(line)).empty()) {
                ++bantered;
                continue;
            }
            Reading r = an.read(rewrites.apply(line), now);
            // Understood means the ask gate would answer: a Story question with a
            // clean reading counts, it has the whole timeline to tell.
            if (r.domain >= 0.6) {
                ++understood;
                continue;
            }
            // A refusal is honest when nothing was understood and the question
            // did not name the machine; it is bad when the machine was named or
            // a topic was half-seen and still nothing could be answered.
            if (r.self_named || !r.intents.empty())
                ++bad;
            else
                ++honest;
            if (report)
                for (const auto& t : tokenize(line))
                    if (t.lower.size() >= 3 && !known.count(t.lower) && !known.count(t.norm)) ++unknown[t.lower];
            if (verbose) std::printf("  miss (%.2f): %s\n", r.domain, line.c_str());
        }
    }
    if (!total) {
        std::printf("coverage: no questions found\n");
        return 1;
    }
    std::printf("coverage over %d questions:\n", total);
    std::printf("  understood       %4d  (%.0f%%)\n", understood, 100.0 * understood / total);
    std::printf("  banter           %4d  (%.0f%%)\n", bantered, 100.0 * bantered / total);
    std::printf("  refused honestly %4d  (%.0f%%)\n", honest, 100.0 * honest / total);
    std::printf("  refused badly    %4d  (%.0f%%)\n", bad, 100.0 * bad / total);
    if (report) {
        std::vector<std::pair<int, std::string>> ranked;
        for (const auto& [w, n] : unknown) ranked.push_back({n, w});
        std::sort(ranked.rbegin(), ranked.rend());
        std::printf("most frequent unrecognized words in refused questions:\n");
        for (int i = 0; i < report && i < int(ranked.size()); ++i)
            std::printf("  %5d  %s\n", ranked[i].first, ranked[i].second.c_str());
    }
    return 0;
}
