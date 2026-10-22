#include <fstream>

#include "unllm/unllm.h"

// Banter: fixed replies to the classics people type at anything with a text
// box. The trigger families were mined from sixty years of chatbot logs
// (ELIZA's script, the ALICE AIML sets); the replies are this library's own.
// Matching is normalized-token comparison, exact or phrase-within, so word
// endings and punctuation do not matter. No scoring, no fallthrough chains:
// a question either is one of the classics or it is not.

namespace unllm {

namespace {

std::string norm_join(const std::vector<Token>& tokens) {
    std::string s;
    for (const auto& t : tokens) s += (s.empty() ? "" : " ") + t.norm;
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

}  // namespace

bool Banter::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) add(line);
    return !exact_.empty() || !within_.empty();
}

void Banter::add(const std::string& line) {
    if (line.empty() || line[0] == '#') return;
    auto sep = line.find("::");
    if (sep == std::string::npos) return;
    std::string reply = trim(line.substr(sep + 2));
    std::string left = line.substr(0, sep);
    if (reply.empty()) return;
    std::size_t i = 0;
    while (i <= left.size()) {
        auto semi = left.find(';', i);
        std::string trig = trim(left.substr(i, (semi == std::string::npos ? left.size() : semi) - i));
        if (!trig.empty()) {
            bool within = trig[0] == '~';
            if (within) trig = trim(trig.substr(1));
            std::string key = norm_join(tokenize(trig));
            if (!key.empty()) {
                if (within) within_.push_back({key, reply});
                else exact_[key] = reply;
            }
        }
        if (semi == std::string::npos) break;
        i = semi + 1;
    }
}

std::string Banter::match(const std::vector<Token>& tokens) const {
    std::string q = norm_join(tokens);
    if (q.empty()) return "";
    auto it = exact_.find(q);
    if (it != exact_.end()) return it->second;
    const std::string padded = " " + q + " ";
    for (const auto& [phrase, reply] : within_)
        if (padded.find(" " + phrase + " ") != std::string::npos) return reply;
    return "";
}

bool Categories::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line, section;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.size() > 2 && t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        if (section.empty() || t.empty() || t[0] == '#') continue;
        if (t.rfind("*", 0) == 0 && t.find("::") != std::string::npos) {
            defaults_[section] = trim(t.substr(t.find("::") + 2));
            continue;
        }
        tables_[section].add(t);
    }
    return !defaults_.empty();
}

std::string Categories::reply(const std::string& category, const std::vector<Token>& tokens) const {
    auto t = tables_.find(category);
    if (t != tables_.end()) {
        std::string r = t->second.match(tokens);
        if (!r.empty()) return r;
    }
    auto d = defaults_.find(category);
    return d == defaults_.end() ? "" : d->second;
}

}  // namespace unllm
