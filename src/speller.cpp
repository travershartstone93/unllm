#include <cctype>
#include <fstream>

#include "unllm/unllm.h"

namespace unllm {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool all_alpha(const std::string& w) {
    for (char c : w)
        if (!std::isalpha(static_cast<unsigned char>(c))) return false;
    return !w.empty();
}

}  // namespace

bool Speller::load_chatspeak(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto arrow = line.find("->");
        if (arrow == std::string::npos) continue;
        std::string from = trim(line.substr(0, arrow)), to = trim(line.substr(arrow + 2));
        if (!from.empty() && !to.empty()) chatspeak_[from] = to;
    }
    return !chatspeak_.empty();
}

void Speller::set_known(const std::vector<std::string>& words) {
    known_.clear();
    known_norm_.clear();
    for (const auto& w : words) {
        known_.insert(w);
        known_norm_.insert(normalize_word(w));
    }
}

std::string Speller::fix(const std::string& question,
                         std::vector<std::pair<std::string, std::string>>* changes) const {
    // Split on spaces, keeping each word's surrounding punctuation aside so
    // "2day?" still matches "2day".
    std::string out;
    std::size_t i = 0;
    while (i < question.size()) {
        while (i < question.size() && question[i] == ' ') ++i;
        std::size_t j = i;
        while (j < question.size() && question[j] != ' ') ++j;
        if (j == i) break;
        std::string raw = question.substr(i, j - i);
        i = j;
        std::size_t a = 0, b = raw.size();
        while (a < b && !std::isalnum(static_cast<unsigned char>(raw[a]))) ++a;
        while (b > a && !std::isalnum(static_cast<unsigned char>(raw[b - 1]))) --b;
        std::string pre = raw.substr(0, a), core = raw.substr(a, b - a), post = raw.substr(b);
        std::string low;
        for (char c : core) low += char(std::tolower(static_cast<unsigned char>(c)));
        std::string word = low;
        auto cs = chatspeak_.find(low);
        if (cs != chatspeak_.end()) {
            word = cs->second;
        } else if (low.size() >= 5 && all_alpha(low) && !known_.count(low) &&
                   !known_norm_.count(normalize_word(low)) && !known_.count(normalize_word(low)) &&
                   !(protected_ && protected_->count(low))) {
            // Unknown even after the stemmer took its ending off (so "evenings",
            // a known word's plural, is no typo). One keystroke away, same first
            // letter, five letters or more, and every candidate must belong to
            // ONE word family.
            std::string best;
            std::set<std::string> families;
            for (const auto& k : known_) {
                if (k.size() < 5 || k[0] != low[0]) continue;
                std::size_t d = k.size() > low.size() ? k.size() - low.size() : low.size() - k.size();
                if (d > 1) continue;
                if (edit_distance(low, k, 1) > 1) continue;
                families.insert(normalize_word(k));
                if (best.empty() || k < best) best = k;
            }
            if (families.size() == 1) {
                word = best;
                if (changes) changes->push_back({low, best});
            }
        }
        out += (out.empty() ? "" : " ") + pre + word + post;
    }
    return out;
}

}  // namespace unllm
