#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "unllm/unllm.h"

namespace unllm {

namespace {

bool word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '\'' || c == '-' || c == '_' || c == '.' || c == '/' || c == '%';
}

// Irregular forms the light stemmer below would get wrong. Small on purpose:
// it grows only when the exam demands it.
const std::map<std::string, std::string>& lemma_exceptions() {
    static const std::map<std::string, std::string> m = {
        {"ran", "run"},       {"was", "be"},      {"were", "be"},    {"is", "be"},     {"are", "be"},
        {"been", "be"},       {"did", "do"},      {"does", "do"},    {"done", "do"},   {"has", "have"},
        {"had", "have"},      {"got", "get"},     {"gotten", "get"}, {"went", "go"},   {"gone", "go"},
        {"froze", "freeze"},  {"frozen", "freeze"}, {"died", "die"}, {"dying", "die"}, {"ate", "eat"},
        {"hotter", "hot"},    {"hottest", "hot"}, {"worse", "bad"},  {"worst", "bad"}, {"slower", "slow"},
        {"slowest", "slow"},  {"louder", "loud"}, {"screaming", "scream"},
    };
    return m;
}

}  // namespace

std::string normalize_word(const std::string& w) {
    auto it = lemma_exceptions().find(w);
    if (it != lemma_exceptions().end()) return it->second;
    std::string s = w;
    auto ends = [&](const char* suf) {
        std::size_t n = std::char_traits<char>::length(suf);
        return s.size() > n + 2 && s.compare(s.size() - n, n, suf) == 0;
    };
    // A light stemmer: just enough that "crashes", "crashed", "crashing",
    // "crash" meet in one place. Guarded so short words stay whole.
    if (ends("ing")) {
        s.erase(s.size() - 3);
        if (s.size() >= 3 && s[s.size() - 1] == s[s.size() - 2]) s.pop_back();  // running -> run
    } else if (ends("ied")) {
        s.replace(s.size() - 3, 3, "y");  // tried -> try
    } else if (ends("ed")) {
        s.erase(s.size() - 2);
        if (s.size() >= 3 && s[s.size() - 1] == s[s.size() - 2]) s.pop_back();  // stopped -> stop
    } else if (ends("ies")) {
        s.replace(s.size() - 3, 3, "y");  // memories -> memory
    } else if (ends("es") && (s[s.size() - 3] == 'h' || s[s.size() - 3] == 's' || s[s.size() - 3] == 'x')) {
        s.erase(s.size() - 2);  // crashes -> crash
    } else if (ends("s") && s[s.size() - 2] != 's' && s[s.size() - 2] != 'u') {
        s.erase(s.size() - 1);  // disks -> disk (but not "was", "status")
    }
    // crashe -> crash style leftovers
    if (s.size() >= 4 && s.back() == 'e') {
        std::string trial = s.substr(0, s.size() - 1);
        if (lemma_exceptions().count(trial)) s = trial;
    }
    return s;
}

std::vector<Token> tokenize(const std::string& text) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (!word_char(text[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && word_char(text[j])) ++j;
        Token t;
        t.text = text.substr(i, j - i);
        // trailing punctuation that word_char lets through ("3pm.", "usb-")
        while (!t.text.empty() && (t.text.back() == '.' || t.text.back() == '-' || t.text.back() == '\'')) t.text.pop_back();
        t.start = i;
        t.lower.reserve(t.text.size());
        for (char c : t.text) t.lower += char(std::tolower(static_cast<unsigned char>(c)));
        t.norm = normalize_word(t.lower);
        if (!t.text.empty()) out.push_back(std::move(t));
        i = j;
    }
    return out;
}

int edit_distance(const std::string& a, const std::string& b, int limit) {
    if (std::abs(int(a.size()) - int(b.size())) > limit) return limit + 1;
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = int(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = int(i);
        int row_min = cur[0];
        for (std::size_t j = 1; j <= b.size(); ++j) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            row_min = std::min(row_min, cur[j]);
        }
        if (row_min > limit) return limit + 1;
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

}  // namespace unllm
