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

// Split on spaces; each word normalizes through the tokenizer, except the
// wildcard, which the tokenizer would drop.
std::vector<std::string> words_of(const std::string& text, bool normalized) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        std::size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        if (j > i) {
            std::string w = text.substr(i, j - i);
            if (w == "*") {
                out.push_back("*");
            } else {
                auto t = tokenize(w);
                if (!t.empty()) out.push_back(normalized ? t[0].norm : t[0].lower);
            }
        }
        i = j;
    }
    return out;
}

}  // namespace

bool Rewrites::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto arrow = line.find("->");
        if (arrow == std::string::npos) continue;
        Rule r;
        r.pat = words_of(trim(line.substr(0, arrow)), true);
        r.rep = words_of(trim(line.substr(arrow + 2)), false);
        if (!r.pat.empty()) rules_.push_back(std::move(r));
    }
    return !rules_.empty();
}

std::string Rewrites::apply(const std::string& question) const {
    if (rules_.empty()) return question;
    std::string q = question;
    for (int pass = 0; pass < 3; ++pass) {
        auto tokens = tokenize(q);
        bool fired = false;
        for (const auto& rule : rules_) {
            std::size_t star = rule.pat.size();
            for (std::size_t k = 0; k < rule.pat.size(); ++k)
                if (rule.pat[k] == "*") star = k;
            const bool has_star = star < rule.pat.size();
            const std::size_t head = has_star ? star : rule.pat.size();
            const std::size_t tail = has_star ? rule.pat.size() - star - 1 : 0;
            for (std::size_t start = 0; !fired && start + head <= tokens.size(); ++start) {
                bool ok = true;
                for (std::size_t k = 0; ok && k < head; ++k)
                    ok = tokens[start + k].norm == rule.pat[k];
                if (!ok) continue;
                // The wildcard binds one or more tokens, the shortest run
                // that still leaves room for the pattern's tail.
                std::size_t bound_from = start + head, bound_to = bound_from, tail_at = bound_from;
                if (has_star) {
                    bool found = false;
                    if (tail == 0) {
                        if (tokens.size() > bound_from) {
                            bound_to = tail_at = tokens.size();
                            found = true;
                        }
                    } else {
                        for (std::size_t j = bound_from + 1; j + tail <= tokens.size(); ++j) {
                            bool m = true;
                            for (std::size_t k = 0; m && k < tail; ++k)
                                m = tokens[j + k].norm == rule.pat[star + 1 + k];
                            if (m) {
                                bound_to = j;
                                tail_at = j + tail;
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) continue;
                }
                std::string out;
                auto add = [&](const std::string& w) { out += (out.empty() ? "" : " ") + w; };
                for (std::size_t k = 0; k < start; ++k) add(tokens[k].lower);
                for (const auto& w : rule.rep) {
                    if (w == "*")
                        for (std::size_t k = bound_from; k < bound_to; ++k) add(tokens[k].lower);
                    else
                        add(w);
                }
                for (std::size_t k = tail_at; k < tokens.size(); ++k) add(tokens[k].lower);
                q = out;
                fired = true;
            }
            if (fired) break;
        }
        if (!fired) break;
    }
    return q;
}

}  // namespace unllm
