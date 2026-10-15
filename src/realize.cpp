#include <cctype>

#include "unllm/unllm.h"

// The realizer: facts in, grammatical English out, entirely by rule. The
// compute cost of a sentence here is a few hundred table lookups, which is
// the point of the whole library.

namespace unllm {

namespace {

bool vowel_start(const std::string& w) {
    if (w.empty()) return false;
    char c = char(std::tolower(static_cast<unsigned char>(w[0])));
    if (w.size() >= 4 && (w.substr(0, 4) == "hour" || w.substr(0, 4) == "hono")) return true;
    if (c == 'u' && w.size() >= 3 && (w.substr(0, 3) == "use" || w.substr(0, 3) == "uni")) return false;
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

bool is_vowel(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }

const std::map<std::string, std::string>& irregular_past() {
    static const std::map<std::string, std::string> m = {
        {"be", "was"},     {"run", "ran"},    {"grow", "grew"},   {"hold", "held"},  {"get", "got"},
        {"go", "went"},    {"come", "came"},  {"rise", "rose"},   {"fall", "fell"},  {"begin", "began"},
        {"keep", "kept"},  {"send", "sent"},  {"write", "wrote"}, {"read", "read"},  {"spend", "spent"},
        {"take", "took"},  {"eat", "ate"},    {"freeze", "froze"},{"shut", "shut"},  {"sit", "sat"},
        {"wake", "woke"},  {"leave", "left"}, {"have", "had"},    {"do", "did"},     {"make", "made"},
    };
    return m;
}

const std::map<std::string, std::string>& irregular_plural() {
    static const std::map<std::string, std::string> m = {
        {"child", "children"}, {"life", "lives"}, {"foot", "feet"},
    };
    return m;
}

}  // namespace

std::string join_list(const std::vector<std::string>& items) {
    std::string s;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) s += i + 1 == items.size() ? " and " : ", ";
        s += items[i];
    }
    return s;
}

std::string plural_noun(const std::string& noun, long n) {
    if (n == 1 || noun.empty()) return noun;
    auto it = irregular_plural().find(noun);
    if (it != irregular_plural().end()) return it->second;
    std::string s = noun;
    char last = s.back();
    char prev = s.size() > 1 ? s[s.size() - 2] : ' ';
    if (last == 'y' && !is_vowel(prev)) return s.substr(0, s.size() - 1) + "ies";  // memory -> memories
    if (last == 's' || last == 'x' || last == 'z' ||
        (last == 'h' && (prev == 'c' || prev == 's')))
        return s + "es";  // process -> processes, crash -> crashes
    return s + "s";
}

std::string number_word(long n) {
    static const char* small[] = {"no", "one", "two", "three", "four", "five", "six",
                                  "seven", "eight", "nine", "ten", "eleven", "twelve"};
    if (n >= 0 && n <= 12) return small[n];
    return std::to_string(n);
}

std::string past_tense(const std::string& verb, bool plural_subject) {
    if (verb == "be") return plural_subject ? "were" : "was";
    auto it = irregular_past().find(verb);
    if (it != irregular_past().end()) return it->second;
    std::string s = verb;
    char last = s.back();
    char prev = s.size() > 1 ? s[s.size() - 2] : ' ';
    char prev2 = s.size() > 2 ? s[s.size() - 3] : ' ';
    if (last == 'e') return s + "d";                                     // use -> used
    if (last == 'y' && !is_vowel(prev)) return s.substr(0, s.size() - 1) + "ied";  // try -> tried
    // short verbs double a final consonant after a single vowel: stop -> stopped
    if (!is_vowel(last) && is_vowel(prev) && !is_vowel(prev2) && s.size() <= 4 &&
        last != 'w' && last != 'x' && last != 'y')
        return s + last + "ed";
    return s + "ed";
}

std::string present_tense(const std::string& verb, bool plural_subject) {
    if (verb == "be") return plural_subject ? "are" : "is";
    if (verb == "have") return plural_subject ? "have" : "has";
    if (plural_subject) return verb;
    char last = verb.back();
    char prev = verb.size() > 1 ? verb[verb.size() - 2] : ' ';
    if (last == 'y' && !is_vowel(prev)) return verb.substr(0, verb.size() - 1) + "ies";
    if (last == 's' || last == 'x' || last == 'z' || (last == 'h' && (prev == 'c' || prev == 's')) || last == 'o')
        return verb + "es";
    return verb + "s";
}

std::string indefinite(const std::string& noun) {
    return (vowel_start(noun) ? "an " : "a ") + noun;
}

namespace {

std::string np_text(const NP& np) {
    std::string s;
    if (np.count >= 0 && !np.proper) {
        s = number_word(np.count) + " " + plural_noun(np.noun, np.count);
    } else {
        s = np.noun;
    }
    if (!np.detail.empty()) s += " " + np.detail;
    return s;
}

bool np_plural(const NP& np) { return np.count >= 0 && np.count != 1 && !np.proper; }

std::string verb_phrase(const Clause& c, bool plural) {
    std::string v = c.tense == Tense::Past ? past_tense(c.verb, plural) : present_tense(c.verb, plural);
    return c.rest.empty() ? v : v + " " + c.rest;
}

std::string capitalize(std::string s) {
    if (!s.empty()) s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

}  // namespace

std::string realize(const std::vector<Clause>& clauses) {
    std::string out;
    std::set<std::string> already_named;
    std::string last_subject;  // a pronoun may only mean the previous subject
    std::size_t i = 0;
    while (i < clauses.size()) {
        // Merge the run of clauses that share this subject and tense.
        std::size_t j = i;
        std::vector<std::string> predicates;
        bool plural = np_plural(clauses[i].subject);
        while (j < clauses.size() && clauses[j].subject.noun == clauses[i].subject.noun &&
               clauses[j].tense == clauses[i].tense) {
            predicates.push_back(verb_phrase(clauses[j], plural));
            ++j;
        }
        const NP& subj = clauses[i].subject;
        std::string head;
        if (subj.noun == last_subject) {
            head = plural ? "they" : "it";
            if (predicates.size() == 1) predicates[0] = "also " + predicates[0];
        } else {
            head = np_text(subj);
            // Named before, but something else spoke in between: repeat the
            // name (a pronoun would be ambiguous) and still say "also".
            if (already_named.count(subj.noun) && predicates.size() == 1) predicates[0] = "also " + predicates[0];
            already_named.insert(subj.noun);
        }
        last_subject = subj.noun;
        if (!out.empty()) out += " ";
        out += capitalize(head + " " + join_list(predicates) + ".");
        i = j;
    }
    return out;
}

std::string refuse(const Reading& r, const std::string& domain_name) {
    const std::string about = domain_name.empty() ? "this machine" : domain_name;
    if (r.domain <= 0.25 || (r.terms.empty() && r.intents.empty() && r.entities.empty())) {
        return "I can't answer that: it doesn't seem to be a question about " + about +
               ", and " + about + " is all I know.";
    }
    // Partial understanding: say what was recognized, so the refusal teaches.
    std::vector<std::string> knew;
    for (auto i : r.intents) knew.push_back(std::string(intent_name(i)));
    for (const auto& e : r.entities) knew.push_back(e.value);
    std::string s = "I can't answer that one";
    if (!knew.empty()) s += "; I recognized " + join_list(knew) + ", but not the rest";
    s += ". I only know about " + about + ": try asking what happened, what changed, or what a program did.";
    return s;
}

}  // namespace unllm
