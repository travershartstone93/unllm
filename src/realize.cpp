#include <algorithm>
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

// Past participles that differ from the simple past: been, not was.
const std::map<std::string, std::string>& irregular_participle() {
    static const std::map<std::string, std::string> m = {
        {"be", "been"},   {"run", "run"},     {"grow", "grown"},   {"go", "gone"},
        {"come", "come"}, {"rise", "risen"},  {"fall", "fallen"},  {"begin", "begun"},
        {"write", "written"}, {"take", "taken"}, {"eat", "eaten"}, {"freeze", "frozen"},
        {"wake", "woken"}, {"do", "done"},
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

bool np_plural(const NP& np) { return np.plural || (np.count >= 0 && np.count != 1 && !np.proper); }

std::string participle(const std::string& verb) {
    auto it = irregular_participle().find(verb);
    if (it != irregular_participle().end()) return it->second;
    auto ip = irregular_past().find(verb);  // held, kept, sent, got, left...
    if (ip != irregular_past().end()) return ip->second;
    return past_tense(verb, false);
}

std::string verb_phrase(const Clause& c, bool plural) {
    std::string v;
    if (c.tense == Tense::PresentPerfect) {
        // "has crashed twice since monday": the tense a since-window needs.
        v = (plural ? "have" : "has") + std::string(c.negated ? " not " : " ") + participle(c.verb);
    } else if (c.negated) {
        // "be" negates itself; everything else borrows "do": did not crash.
        if (c.verb == "be")
            v = (c.tense == Tense::Past ? past_tense("be", plural) : present_tense("be", plural)) + " not";
        else if (c.tense == Tense::Past)
            v = "did not " + c.verb;
        else
            v = (plural ? "do not " : "does not ") + c.verb;
    } else {
        v = c.tense == Tense::Past ? past_tense(c.verb, plural) : present_tense(c.verb, plural);
    }
    return c.rest.empty() ? v : v + " " + c.rest;
}

std::string capitalize(std::string s) {
    if (!s.empty()) s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

}  // namespace

std::string realize(const std::vector<Clause>& clauses) {
    // Pre-pass: adjacent clauses sharing their whole predicate merge into
    // one plural subject ("Firefox and chromium crashed.").
    std::vector<Clause> cl(clauses);
    for (std::size_t k = 0; k < cl.size(); ++k) {
        if (cl[k].contrast || cl[k].subject.count >= 0) continue;
        std::vector<std::string> names = {cl[k].subject.noun};
        std::size_t e = k + 1;
        while (e < cl.size() && !cl[e].contrast && cl[e].subject.count < 0 &&
               cl[e].verb == cl[k].verb && cl[e].rest == cl[k].rest &&
               cl[e].tense == cl[k].tense && cl[e].negated == cl[k].negated &&
               std::find(names.begin(), names.end(), cl[e].subject.noun) == names.end()) {
            names.push_back(cl[e].subject.noun);
            ++e;
        }
        if (names.size() >= 2) {
            cl[k].subject = NP{join_list(names), true, -1, "", true};
            cl.erase(cl.begin() + k + 1, cl.begin() + e);
        }
    }
    std::string out;
    std::set<std::string> already_named;
    std::string last_subject;  // a pronoun may only mean the previous subject
    std::size_t i = 0;
    while (i < cl.size()) {
        // Merge the run of clauses that share this subject and tense.
        std::size_t j = i;
        std::vector<std::string> predicates;
        std::vector<bool> contrasts;
        bool plural = np_plural(cl[i].subject);
        while (j < cl.size() && cl[j].subject.noun == cl[i].subject.noun &&
               cl[j].tense == cl[i].tense) {
            predicates.push_back(verb_phrase(cl[j], plural));
            contrasts.push_back(cl[j].contrast);
            ++j;
        }
        const NP& subj = cl[i].subject;
        const bool run_contrast = cl[i].contrast;
        std::string head;
        if (subj.noun == last_subject && !run_contrast) {
            head = plural ? "they" : "it";
            if (predicates.size() == 1) predicates[0] = "also " + predicates[0];
        } else {
            head = np_text(subj);
            // Named before, but something else spoke in between: repeat the
            // name (a pronoun would be ambiguous) and still say "also".
            if (!run_contrast && already_named.count(subj.noun) && predicates.size() == 1)
                predicates[0] = "also " + predicates[0];
            already_named.insert(subj.noun);
        }
        last_subject = subj.noun;
        // A contrast predicate joins with ", but" instead of the list comma.
        std::string preds;
        for (std::size_t p = 0; p < predicates.size(); ++p) {
            if (p == 0) preds = predicates[p];
            else if (contrasts[p]) preds += ", but " + predicates[p];
            else if (p + 1 == predicates.size()) preds += " and " + predicates[p];
            else preds += ", " + predicates[p];
        }
        if (run_contrast && !out.empty()) {
            // The contrast hangs off the previous sentence: "..., but
            // firefox did not crash."
            out.pop_back();
            out += ", but " + head + " " + preds + ".";
        } else {
            if (!out.empty()) out += " ";
            out += capitalize(head + " " + preds + ".");
        }
        i = j;
    }
    return out;
}

std::string realize_polar(bool yes, const std::vector<Clause>& clauses) {
    std::string body = realize(clauses);
    // "No crashes were recorded" is already the no; never say "No, no".
    if (!yes && body.compare(0, 3, "No ") == 0) return body;
    // Only a bare name keeps its capital after the comma: "No, Firefox did
    // not crash", but "Yes, the CPU reached 99 C" and "No, it is plugged in".
    bool name_like = false;
    if (!clauses.empty()) {
        const NP& s0 = clauses[0].subject;
        name_like = s0.proper && s0.noun.find(' ') == std::string::npos &&
                    s0.noun != "it" && s0.noun != "they" && s0.noun != "this" && s0.noun != "nothing";
    }
    if (!name_like && !body.empty()) body[0] = char(std::tolower(static_cast<unsigned char>(body[0])));
    return (yes ? "Yes, " : "No, ") + body;
}

std::string refuse(const Reading& r, const std::string& domain_name) {
    const std::string about = domain_name.empty() ? "this machine" : domain_name;
    // The harsh branch is earned only when nothing at all was recognized and
    // the question did not name the machine: a question that says "machine"
    // is never told it was not about the machine.
    if (r.domain <= 0.25 && !r.self_named && r.intents.empty() && r.entities.empty()) {
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
