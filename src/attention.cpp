#include <algorithm>
#include <set>

#include "unllm/unllm.h"

// Attention, the arithmetic kind: each word looks at the topic words and at
// the other words of the question, and weighs itself by how strongly it
// relates to them. The vectors are fixed (the meaning map), so the same
// question always gets the same weights.

namespace unllm {

namespace {

// Words that pad a question without steering it.
const std::set<std::string>& filler() {
    static const std::set<std::string> s = {
        "honestly", "basically", "like", "kinda", "kind", "sort", "sorta", "anyway", "anyways", "actually",
        "literally", "seriously", "really", "pretty", "just", "stuff", "thing", "things", "idea", "guess",
        "wondering", "sitting", "lol", "okay", "ok", "well", "maybe", "probably", "somehow", "whatever",
        "gonna", "wanna", "gotta", "dude", "man", "bro", "hey", "hi", "hello", "please", "thanks",
    };
    return s;
}

// The topic anchors attention measures against: one plain word per topic.
const std::vector<std::string>& anchors() {
    static const std::vector<std::string> a = {"hot", "slow", "memory", "disk", "network", "crash", "update",
                                               "battery", "boot", "fan", "cpu", "program", "computer"};
    return a;
}

}  // namespace

std::vector<double> attend(const std::vector<Token>& tokens, const Vectors* v,
                           double (*idf)(const std::string&)) {
    std::vector<double> w(tokens.size(), 0.0);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (filler().count(t.lower)) {
            w[i] = 0.05;
            continue;
        }
        double rel = 0;
        if (v && v->ok()) {
            for (const auto& a : anchors()) rel = std::max(rel, v->similarity(t.lower, a));
            double ctx = 0;
            int n = 0;
            for (std::size_t j = 0; j < tokens.size(); ++j) {
                if (j == i || filler().count(tokens[j].lower)) continue;
                ctx += v->similarity(t.lower, tokens[j].lower);
                ++n;
            }
            if (n) rel = std::max(rel, ctx / n);
        }
        double base = 0.4 + 0.6 * std::clamp(rel, 0.0, 1.0);
        if (idf) base *= std::max(0.2, idf(t.norm));
        w[i] = base;
    }
    return w;
}

}  // namespace unllm
