#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

#include "unllm/knowledge.h"
#include "unllm/local_model.h"
#include "unllm/meaning.h"
#include "unllm/reader.h"
#include "unllm/reranker.h"

// Answering from reference knowledge, unllm's own way: rerank the search's
// candidates by meaning, pick the sentence the question's shape asks for,
// compose up to three sentences from the sources, and cite them. Nothing is
// generated; every sentence comes from a passage.

namespace unllm {

namespace {

const std::set<std::string>& cue_words() {
    // Words that shape a question without naming its subject.
    static const std::set<std::string> s = {
        "what", "whats", "why", "how", "when", "where", "who", "which", "define", "definition", "meaning", "mean",
        "means", "explain", "describe", "tell", "work", "works", "does", "do", "is", "are", "was", "were", "poem",
        "poems", "write", "me", "about", "please", "can", "you", "could", "would", "should", "the", "a", "an",
        "of", "to", "in", "on", "for", "and", "or", "it", "its", "this", "that", "stand", "happen",
    };
    return s;
}

// Which of unllm's answering layers run on top of plain search.
// UNLLM_LAYERS="fact,super,..." turns on exactly those ("all" = every one,
// "" = plain search only); unset, the default set below. For measuring.
// Chosen on 300 Natural Questions (tools/layers.py, 2026-10-16): plain
// search beats the old search ("oldsearch") there but loses on the
// held-out general questions; pooling both ("union") wins on both. The
// rerank carries most of the gain, and quoting the article's opening
// passages ("opening") costs 10 of 300 right answers. The rest are neutral
// there and kept for the question shapes it lacks. After the title and
// section reindex (2026-10-17): "floorwide" and "song" add 3 of 200 on the
// clean dev set; "wide" and "inside" add nothing; "extract" with "abstain"
// raises dev precision from 61% to 75% but loses 19 right answers, so it
// stays off until the choice is made.
// The pipeline (a reranker installed, UNLLM_LAYERS unset): no rule layer runs.
thread_local bool g_pipeline = false;

bool layer(const char* name) {
    if (g_pipeline) return false;
    static const std::set<std::string> on = [] {
        std::set<std::string> s;
        const char* env = std::getenv("UNLLM_LAYERS");
        std::string list = env ? env
                               : "kinds,fact,super,roman,phrase,named,howmany,howto,of,rerank,leadfirst,attr,qa,"
                                 "defs,floor,union,floorwide,song,wide";
        std::string cur;
        for (char c : list + ",") {
            if (c == ',') {
                if (!cur.empty()) s.insert(cur);
                cur.clear();
            } else if (c != ' ') {
                cur += c;
            }
        }
        return s;
    }();
    return on.count("all") || on.count(name);
}

std::string lower(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = char(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

std::vector<std::string> content_words(const std::string& text) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& t : tokenize(text)) {
        if (t.lower.size() < 2 || cue_words().count(t.lower) || stop_word(t.lower)) continue;
        if (seen.insert(t.lower).second) out.push_back(t.lower);
    }
    return out;
}

bool has_year_or_date(const std::string& s) {
    static const char* months[] = {"January", "February", "March", "April", "May", "June", "July", "August",
                                   "September", "October", "November", "December"};
    for (const char* m : months)
        if (s.find(m) != std::string::npos) return true;
    for (std::size_t i = 0; i + 3 < s.size(); ++i) {
        if (i > 0 && std::isalnum(static_cast<unsigned char>(s[i - 1]))) continue;
        std::size_t j = i;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
        if ((j - i == 4 || j - i == 3) && (j == s.size() || !std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == 's'))
            return true;
    }
    return s.find("century") != std::string::npos;
}

// A measured amount: a number with a unit ("8,849 metres", "37%"), not a
// bare year.
bool has_quantity(const std::string& s) {
    // "There are eight planets": a number in words is a number too.
    static const std::set<std::string> number_words = {
        "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen",
        "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty", "thirty", "forty",
        "fifty", "sixty", "seventy", "eighty", "ninety", "hundred", "thousand", "million", "billion", "dozen"};
    for (const auto& t : tokenize(s))
        if (number_words.count(t.lower)) return true;
    static const std::set<std::string> units = {
        "m", "metres", "meters", "metre", "meter", "km", "kilometres", "kilometers", "cm", "mm", "feet", "ft",
        "foot", "inches", "inch", "miles", "mile", "kg", "kilograms", "g", "grams", "tonnes", "tons", "pounds",
        "lb", "lbs", "ounces", "oz", "percent", "degrees", "years", "year", "days", "hours", "minutes", "seconds",
        "people", "inhabitants", "million", "billion", "thousand", "hundred", "trillion", "litres", "liters",
        "gallons", "mph", "kph", "au", "light", "square", "sq", "acres", "hectares", "km2", "calories", "watts",
        "volts", "hz", "ghz", "mhz", "gb", "mb", "tb", "kelvin", "k", "c", "f"};
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) continue;
        std::size_t j = i;
        while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == ',' || s[j] == '.')) ++j;
        while (j > i && (s[j - 1] == ',' || s[j - 1] == '.')) --j;
        std::size_t k = j;
        if (k < s.size() && (s[k] == '%' || s[k] == '\xc2')) return true;  // 37%, 5°
        while (k < s.size() && s[k] == ' ') ++k;
        std::size_t e = k;
        while (e < s.size() && std::isalpha(static_cast<unsigned char>(s[e]))) ++e;
        if (e > k && units.count(lower(s.substr(k, e - k)))) return true;
        i = j;
    }
    return false;
}

// "It is about  high": the source lost a measurement (unit templates are
// dropped from the dump), leaving a double space where the number was.
bool number_hole(const std::string& s) {
    static const std::set<std::string> before = {"about", "at", "of", "around", "nearly", "over", "under", "is",
                                                  "are", "was", "be", "approximately", "roughly", "almost", "from",
                                                  "to", "than", "only", "some", "by", "reaches", "reached"};
    for (std::size_t at = s.find("  "); at != std::string::npos; at = s.find("  ", at + 2)) {
        std::size_t b = at;
        while (b > 0 && std::isalpha(static_cast<unsigned char>(s[b - 1]))) --b;
        if (before.count(lower(s.substr(b, at - b)))) return true;
        if (at + 2 < s.size() && (s[at + 2] == '.' || s[at + 2] == ',' || s[at + 2] == ')')) return true;
    }
    return false;
}

// "Napoleon Bonaparte () was": brackets emptied by the source's own markup;
// "Vatican City (/ˈvætɪkən/ (listen); officially ...)": pronunciation guides.
std::string tidy(std::string s) {
    for (std::size_t a; (a = s.find(" (listen)")) != std::string::npos;) s.erase(a, 9);
    for (std::size_t a = s.find("(/"); a != std::string::npos; a = s.find("(/", a)) {
        const std::size_t b = s.find('/', a + 2);
        if (b == std::string::npos || b - a > 80) break;
        std::size_t e = b + 1;
        while (e < s.size() && (s[e] == ';' || s[e] == ' ')) ++e;
        if (e < s.size() && s[e] == ')') {
            s.erase(a, e + 1 - a);  // the bracket held only the pronunciation
        } else {
            s.erase(a + 1, e - a - 1);  // keep the bracket, drop the guide
        }
    }
    for (const char* junk : {" ()", "()", " ( )", " (, )", " (; )", "(, ", "(; "}) {
        std::size_t at;
        const std::string j = junk;
        while ((at = s.find(j)) != std::string::npos) s.replace(at, j.size(), j.back() == ' ' ? "(" : "");
    }
    std::string out;
    for (char c : s)
        if (!(c == ' ' && !out.empty() && out.back() == ' ')) out += c;
    std::size_t at;
    while ((at = out.find(" ,")) != std::string::npos) out.erase(at, 1);
    return out;
}

// A list or a cut-off fragment, not a sentence: brackets that never close,
// or mostly capitalized words with almost nothing in lowercase between them
// ("Track listing Teenage Frankenstein School's Out"). A sentence full of
// names still has its ordinary words: "Albert Einstein (14 March 1879 - 18
// April 1955) was a German-American scientist" keeps "was" and "scientist".
bool fragment(const std::string& s) {
    int open = 0;
    for (char c : s) open += c == '(' ? 1 : c == ')' ? -1 : 0;
    if (open != 0) return true;
    static const std::set<std::string> joiners = {"and", "the", "of", "for", "from", "with", "in", "on",
                                                  "at", "to", "de", "la", "le", "von", "van", "der", "del"};
    static const std::set<std::string> verbs = {"is", "was", "are", "were", "be", "been", "has", "have", "had",
                                                "became", "becomes", "born", "died", "won", "wrote", "made",
                                                "lived", "worked", "played", "served", "founded", "located"};
    auto toks = tokenize(s);
    int words = 0, caps = 0, plain = 0;
    for (const auto& t : toks) {
        if (verbs.count(t.lower)) return false;
        if (t.text.empty() || !std::isalpha(static_cast<unsigned char>(t.text[0]))) continue;
        ++words;
        if (std::isupper(static_cast<unsigned char>(t.text[0]))) ++caps;
        else if (t.text.size() >= 3 && !joiners.count(t.lower)) ++plain;
    }
    return words >= 6 && caps * 10 > words * 6 && plain < 2;
}

bool has_cause(const std::string& s) {
    const std::string l = lower(s);
    for (const char* c : {"because", "due to", "as a result", "caused by", "leads to", "results in", "so that",
                          "the reason", "since the", "this is why"})
        if (l.find(c) != std::string::npos) return true;
    return false;
}

// "the titanic sank": the plain form of an irregular verb, so a question
// asking when it sinks finds the sentence where it sank.
std::string base_form(const std::string& w) {
    static const std::unordered_map<std::string, std::string> irregular = {
        {"sank", "sink"}, {"sunk", "sink"}, {"began", "begin"}, {"begun", "begin"}, {"wrote", "write"},
        {"written", "write"}, {"built", "build"}, {"ran", "run"}, {"went", "go"}, {"gone", "go"}, {"fell", "fall"},
        {"fallen", "fall"}, {"rose", "rise"}, {"risen", "rise"}, {"won", "win"}, {"flew", "fly"}, {"flown", "fly"},
        {"drove", "drive"}, {"driven", "drive"}, {"ate", "eat"}, {"eaten", "eat"}, {"took", "take"},
        {"taken", "take"}, {"gave", "give"}, {"given", "give"}, {"came", "come"}, {"became", "become"},
        {"made", "make"}, {"said", "say"}, {"found", "find"}, {"thought", "think"}, {"brought", "bring"},
        {"bought", "buy"}, {"caught", "catch"}, {"taught", "teach"}, {"fought", "fight"}, {"held", "hold"},
        {"told", "tell"}, {"sold", "sell"}, {"stood", "stand"}, {"lost", "lose"}, {"met", "meet"}, {"paid", "pay"},
        {"sent", "send"}, {"spent", "spend"}, {"struck", "strike"}, {"swam", "swim"}, {"sang", "sing"},
        {"sung", "sing"}, {"drank", "drink"}, {"froze", "freeze"}, {"frozen", "freeze"}, {"chose", "choose"},
        {"spoke", "speak"}, {"spoken", "speak"}, {"broke", "break"}, {"broken", "break"}, {"grew", "grow"},
        {"grown", "grow"}, {"knew", "know"}, {"known", "know"}, {"threw", "throw"}, {"drew", "draw"},
        {"drawn", "draw"}, {"shot", "shoot"}, {"led", "lead"}, {"fed", "feed"}, {"fled", "flee"}, {"dug", "dig"},
        {"hung", "hang"}, {"laid", "lay"}, {"died", "die"}, {"landed", "land"}, {"invented", "invent"},
        {"discovered", "discover"}, {"painted", "paint"}, {"founded", "found"}, {"signed", "sign"}};
    auto it = irregular.find(w);
    return it == irregular.end() ? normalize_word(w) : it->second;
}

// Prose a sentence can be quoted from: not the question that found a Q&A
// thread, not a table's rows, not a code block.
bool quotable(const std::string& role) { return role != "question" && role != "table" && role != "code"; }

// The subject a question asks about and the shape of what it asks
// ("who founded", "when born", "how tall", or a bare attribute such as
// "currency"), when the question has one of those shapes.
std::pair<std::string, std::string> entity_shape(const Reading& r, const std::string& question) {
    std::vector<std::string> w;
    for (const auto& t : tokenize(question)) w.push_back(t.lower);
    const std::size_t n = w.size();
    const std::string& subj = r.subject;
    static const std::set<std::string> made = {"founded", "started", "created", "invented", "wrote", "painted",
                                               "composed", "designed", "built", "discovered", "directed"};
    static const std::set<std::string> done = {"born", "die", "died", "built", "founded", "established", "created",
                                               "made", "completed", "opened", "released", "published", "written",
                                               "started", "formed"};
    static const std::set<std::string> adj = {"tall", "high", "big", "large", "old", "deep", "long", "heavy"};
    if (subj.empty()) {
        // "what language is spoken in brazil"
        if (n >= 5 && w[0] == "what" && (w[1] == "language" || w[1] == "languages"))
            for (std::size_t i = 2; i + 1 < n; ++i)
                if (w[i] == "in") {
                    std::string e;
                    for (std::size_t k = i + 1; k < n; ++k) e += (e.empty() ? "" : " ") + w[k];
                    return {e, "language"};
                }
        return {"", ""};
    }
    if (n >= 3 && w[0] == "who" && made.count(w[1])) return {subj, "who " + w[1]};
    if (n >= 4 && (w[0] == "when" || w[0] == "where") && done.count(w[n - 1]))
        return {subj, w[0] + " " + (w[n - 1] == "die" ? std::string("died") : w[n - 1])};
    if (n >= 4 && w[0] == "how" && adj.count(w[1])) return {subj, "how " + w[1]};
    for (const char* sep : {" of ", " in ", " for "}) {
        const std::size_t at = subj.find(sep);
        if (at == std::string::npos) continue;
        std::string attr = subj.substr(0, at), ent = subj.substr(at + std::strlen(sep));
        if (ent.rfind("the ", 0) == 0) ent = ent.substr(4);
        // "the freezing point of water in fahrenheit": the unit is how to say it.
        for (const char* unit : {" in fahrenheit", " in celsius", " in kelvin", " in centigrade", " in metres",
                                 " in meters", " in feet", " in miles", " in kilometres", " in kilometers", " in km",
                                 " in pounds", " in kilograms", " in kg"}) {
            const std::size_t u = ent.rfind(unit);
            if (u != std::string::npos && u + std::strlen(unit) == ent.size()) ent = ent.substr(0, u);
        }
        if (!attr.empty() && !ent.empty()) return {ent, attr};
    }
    return {"", ""};
}

// A fact box value made readable: link markup and list dashes removed,
// sub-values ("name · date · place") joined with semicolons.
std::string clean_fact(std::string v) {
    for (std::size_t a; (a = v.find("[[")) != std::string::npos;) {
        std::size_t b = v.find("]]", a);
        if (b == std::string::npos) break;
        std::string inner = v.substr(a + 2, b - a - 2);
        std::size_t bar = inner.rfind('|');
        v.replace(a, b - a + 2, bar == std::string::npos ? inner : inner.substr(bar + 1));
    }
    if (v.rfind("- ", 0) == 0) v = v.substr(2);
    for (std::size_t a; (a = v.find(" - ")) != std::string::npos;) v.replace(a, 3, ", ");
    for (std::size_t a; (a = v.find(" \xc2\xb7 ")) != std::string::npos;) v.replace(a, 4, "; ");
    std::string out;
    for (char c : v)
        if (!(c == ' ' && !out.empty() && out.back() == ' ')) out += c;
    while (!out.empty() && (out.back() == ' ' || out.back() == ';' || out.back() == ',')) out.pop_back();
    if (out.size() > 220) out = out.substr(0, out.rfind(' ', 220)) + "...";
    return out;
}

bool starts_with_pronoun(const std::string& s) {
    for (const char* p : {"He ", "She ", "It ", "They ", "This ", "These ", "Those ", "That ", "His ", "Her ", "Its ",
                          "Their "})
        if (s.rfind(p, 0) == 0) return true;
    return false;
}

std::string attribution(const Passage& p) {
    std::string a = !p.attribution.empty() ? p.attribution
                    : p.pack.empty()       ? std::string("From \"{title}\"")
                                           : "From " + p.pack + ", \"{title}\"";
    auto rep = [&](const std::string& key, const std::string& val) {
        for (std::size_t at = a.find(key); at != std::string::npos; at = a.find(key, at + val.size()))
            a.replace(at, key.size(), val);
    };
    rep("{title}", p.title);
    rep("{author}", p.author.empty() ? "an unknown author" : p.author);
    if (p.kind == "qa" && !p.author.empty()) a += " (answer by " + p.author + ")";
    return a + ".";
}

std::string receipt(const Passage& p) {
    return (p.pack.empty() ? "" : p.pack + ": ") + p.title + (p.url.empty() ? "" : "  " + p.url) +
           (p.license.empty() ? "" : "  [" + p.license + "]");
}

std::string capitalize(std::string s) {
    if (!s.empty()) s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

double jaccard(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() || b.empty()) return 0;
    std::set<std::string> sa(a.begin(), a.end()), sb(b.begin(), b.end());
    int inter = 0;
    for (const auto& x : sa) inter += int(sb.count(x));
    return double(inter) / double(sa.size() + sb.size() - std::size_t(inter));
}

}  // namespace

std::vector<std::string> sentences_of(const std::string& text) {
    static const std::set<std::string> abbrev = {"u.s", "e.g", "i.e", "dr", "mr", "mrs", "ms", "st", "no", "vs", "etc",
                                                 "approx", "inc", "ltd", "jr", "sr", "fig", "mt", "ft", "c", "ca"};
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < text.size(); ++i) {
        cur += text[i];
        char c = text[i];
        if ((c == '.' || c == '!' || c == '?') && i + 1 < text.size() && text[i + 1] == ' ' && i + 2 < text.size() &&
            (std::isupper(static_cast<unsigned char>(text[i + 2])) || std::isdigit(static_cast<unsigned char>(text[i + 2])) ||
             text[i + 2] == '"')) {
            // the word before the stop: an abbreviation or an initial does not end a sentence
            std::size_t ws = cur.find_last_of(' ', cur.size() - 2);
            std::string word = lower(cur.substr(ws == std::string::npos ? 0 : ws + 1));
            if (!word.empty() && word.back() == '.') word.pop_back();
            bool initial = word.size() == 1 && std::isalpha(static_cast<unsigned char>(word[0]));
            if (c == '.' && (abbrev.count(word) || initial)) continue;
            out.push_back(cur);
            cur.clear();
            ++i;  // the space
        }
    }
    if (!cur.empty()) out.push_back(cur);
    for (auto& s : out) {
        std::size_t a = s.find_first_not_of(' ');
        s = a == std::string::npos ? "" : s.substr(a);
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& s) { return s.size() < 2; }), out.end());
    return out;
}

void World::rerank(const Reading& r, const std::vector<std::string>& words, std::vector<Passage>& ps,
                   bool meaning) const {
    double top_bm25 = 1e-9;
    for (const auto& p : ps) top_bm25 = std::max(top_bm25, p.bm25);
    const auto subject_words = content_words(r.subject);
    std::unordered_map<std::string, double> sim_cache;
    auto sim = [&](const std::string& a, const std::string& b) -> double {
        if (a == b) return 1.0;
        if (normalize_word(a) == normalize_word(b)) return 0.95;
        if (!vec_ || !vec_->ok()) return 0;
        std::string key = a < b ? a + "|" + b : b + "|" + a;
        auto it = sim_cache.find(key);
        if (it != sim_cache.end()) return it->second;
        double s = std::max(0.0, vec_->similarity(a, b));
        sim_cache[key] = s;
        return s;
    };
    for (auto& p : ps) {
        // Late interaction: every question word finds its best partner word
        // in the passage; the passage scores the average best match.
        std::vector<std::string> pw;
        std::set<std::string> seen;
        int count = 0;
        for (const auto& t : tokenize(p.title + " " + p.text)) {
            if (t.lower.size() < 2 || stop_word(t.lower)) continue;
            if (seen.insert(t.lower).second) pw.push_back(t.lower);
            if (++count > 160) break;
        }
        double total = 0;
        for (const auto& q : words) {
            double best = 0;
            for (const auto& w : pw) {
                best = std::max(best, sim(q, w));
                if (best >= 1.0) break;
            }
            total += best;
        }
        p.late = words.empty() ? 0 : total / double(words.size());
        auto tw = content_words(p.title);
        // Exact means exact: the title stage's own matches, or the same key.
        // Word overlap alone ("World War" for "world war 2") stays below it.
        const bool exact = p.title_score >= 0.99 || (title_key(p.title) == title_key(r.subject) && !r.subject.empty());
        double title = exact ? 1.0 : p.named ? 0.95 : std::min(0.9, jaccard(subject_words, tw));
        p.title_score = title;
        // A document titled exactly like the subject came from the title
        // stage, which has no text score: it counts as the best match.
        if (p.title_score >= 0.99 || p.named) p.bm25 = std::max(p.bm25, top_bm25);
        double prior = p.role == "lead" || p.role == "accepted" ? 1.0
                       : p.role == "definition" || p.role == "form" ? (r.frame == Frame::Define ? 1.0 : 0.6)
                       : p.role == "verse" ? (r.frame == Frame::Poem ? 1.0 : 0.0)
                       : p.role == "question" ? 0.4
                                              : 0.3;
        double kind = 0;
        if (r.frame == Frame::Define && (p.kind == "definitions" || p.kind == "encyclopedia")) kind = 1;
        // "what does X mean" asks the dictionary; "what is X" the encyclopedia.
        if (r.frame == Frame::Define && meaning && p.kind == "definitions") kind = 2;
        if (r.frame == Frame::HowTo && p.kind == "qa") kind = 1;
        if (r.frame == Frame::Poem && p.kind == "poems") kind = 1;
        if ((r.frame == Frame::Explain || r.frame == Frame::None) && p.kind == "encyclopedia") kind = 1;
        p.score = 0.40 * (p.bm25 / top_bm25) + 0.35 * p.late + 0.15 * p.title_score + 0.05 * prior + 0.05 * kind +
                  (p.phrase ? 0.12 : 0);
        // On by default since 2026-10-19 (validated: sealed 200, answer paragraph first 104 -> 116);
        // 0 turns each off. Title coverage: the share of the title's content words
        // that the question's content words account for. Article size: a saturating prior.
        static const double w_cov = std::getenv("UNLLM_W_TITLECOV") ? std::atof(std::getenv("UNLLM_W_TITLECOV")) : 0.15;
        static const double w_size = std::getenv("UNLLM_W_SIZE") ? std::atof(std::getenv("UNLLM_W_SIZE")) : 0.05;
        if (w_cov > 0 && !tw.empty()) {
            std::size_t hit = 0;
            for (const auto& t : tw)
                for (const auto& w : words)
                    if (normalize_word(t) == normalize_word(w)) {
                        ++hit;
                        break;
                    }
            p.score += w_cov * double(hit) / double(tw.size());
        }
        if (w_size > 0) {
            const int n = lib_.doc_size(p.shard, p.doc_id);
            p.score += w_size * std::min(1.0, std::log1p(double(n)) / std::log1p(500.0));
        }
    }
    std::stable_sort(ps.begin(), ps.end(), [](const Passage& a, const Passage& b) { return a.score > b.score; });
}

bool World::load_fact_keys(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    auto split = [](const std::string& s) {
        std::vector<std::string> out;
        std::size_t i = 0;
        while (i <= s.size()) {
            std::size_t semi = s.find(';', i);
            std::string part = s.substr(i, (semi == std::string::npos ? s.size() : semi) - i);
            std::size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
            if (a != std::string::npos) out.push_back(lower(part.substr(a, b - a + 1)));
            if (semi == std::string::npos) break;
            i = semi + 1;
        }
        return out;
    };
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::size_t sep = line.find("::");
        if (sep == std::string::npos) continue;
        auto keys = split(line.substr(sep + 2));
        for (const auto& shape : split(line.substr(0, sep))) fact_keys_[shape] = keys;
    }
    return !fact_keys_.empty();
}

WorldAnswer World::fact_answer(const Reading& r, const std::string& question) const {
    WorldAnswer out;
    if (fact_keys_.empty()) return out;
    auto [entity, shape] = entity_shape(r, question);
    if (entity.empty() || shape.empty()) return out;
    // The whole shape first ("chemical symbol"), then its last word ("symbol").
    auto it = fact_keys_.find(shape);
    if (it == fact_keys_.end() && shape.rfind(' ') != std::string::npos)
        it = fact_keys_.find(shape.substr(shape.rfind(' ') + 1));
    if (it == fact_keys_.end()) return out;
    static const std::set<std::string> measured = {"population", "inhabitants", "area", "height", "elevation"};
    const bool dated = shape.rfind("when ", 0) == 0;
    const bool numeric = dated || shape.rfind("how ", 0) == 0 || measured.count(it->first);
    for (const auto& d : lib_.titled(entity, {"encyclopedia"}, 6)) {
        for (const auto& [key, value] : lib_.fact_box(d.shard, d.doc_id)) {
            const std::string k = lower(key);
            bool hit = false;
            for (const auto& want : it->second) hit |= k == want || k.rfind(want + " ", 0) == 0;
            if (!hit) continue;
            std::string v = clean_fact(value);
            // "Johannes ... Mozart; 27 January 1756; Salzburg": a date
            // question takes the part with the date in it.
            if (dated) {
                std::string part;
                std::size_t i = 0;
                while (i <= v.size() && part.empty()) {
                    std::size_t semi = v.find("; ", i);
                    std::string seg = v.substr(i, (semi == std::string::npos ? v.size() : semi) - i);
                    if (seg.find_first_of("0123456789") != std::string::npos) part = seg;
                    if (semi == std::string::npos) break;
                    i = semi + 2;
                }
                v = part;
            }
            // Map coordinates ride along with places ("Canberra; 35°18′29″S ...").
            {
                std::string kept;
                std::size_t i = 0;
                while (i <= v.size()) {
                    std::size_t semi = v.find("; ", i);
                    std::string seg = v.substr(i, (semi == std::string::npos ? v.size() : semi) - i);
                    if (!(seg.find("\xc2\xb0") != std::string::npos && seg.find("\xe2\x80\xb2") != std::string::npos))
                        kept += (kept.empty() ? "" : "; ") + seg;
                    if (semi == std::string::npos) break;
                    i = semi + 2;
                }
                v = kept;
            }
            if (v.empty() || (numeric && v.find_first_of("0123456789") == std::string::npos)) continue;
            Passage lead = lib_.lead_of(d.shard, d.doc_id);
            if (lead.doc_id == 0) continue;
            out.kind = WorldAnswer::Answer;
            out.confidence = 0.85;
            std::string label;  // "official language · and national language" reads without the dot
            for (char c : k)
                if (!(c == ' ' && !label.empty() && label.back() == ' ')) label += c;
            for (std::size_t a; (a = label.find("\xc2\xb7 ")) != std::string::npos;) label.erase(a, 3);
            out.text = d.title + ", " + label + ": " + v + ". " + attribution(lead);
            out.used = {lead};
            out.receipts = {receipt(lead)};
            return out;
        }
    }
    return out;
}

namespace {

// Words that make a question ask for the extreme of a kind, with the words
// sources use for the same thing; and kinds sources name in other words.
const std::vector<std::vector<std::string>> kSuperlatives = {
    {"tallest", "highest"}, {"largest", "biggest"}, {"smallest", "tiniest"}, {"closest", "nearest"},
    {"farthest", "furthest"}, {"longest"}, {"shortest"}, {"deepest"}, {"oldest"}, {"youngest"}, {"hottest"},
    {"coldest"}, {"fastest"}, {"slowest"}, {"heaviest"}, {"lightest"}, {"brightest"}, {"richest"}, {"busiest"},
    {"driest"}, {"wettest"}, {"lowest"}, {"first"}};
const std::vector<std::vector<std::string>> kKinds = {
    {"person", "human", "man", "woman"}, {"man", "person", "human"}, {"woman", "person", "human"},
    {"mountain", "peak"}, {"country", "nation", "state"}, {"building", "skyscraper", "structure"}};
const std::vector<std::vector<std::string>> kAround = {
    {"world", "earth", "planet", "globe"}, {"earth", "world", "planet"}, {"space", "orbit"}, {"body", "human"}};

}  // namespace

WorldAnswer World::superlative_answer(const Reading& r, const std::string& question) const {
    WorldAnswer out;
    std::vector<std::string> w;
    for (const auto& t : tokenize(question)) w.push_back(t.lower);
    const std::size_t n = w.size();
    static const std::set<std::string> asks = {"what", "which", "who", "where", "when", "whats", "name"};
    if (n < 4 || !asks.count(w[0])) return out;
    std::size_t s = 0;
    const std::vector<std::string>* sup = nullptr;
    for (std::size_t i = 1; i + 1 < n && !sup; ++i)
        for (const auto& g : kSuperlatives)
            if (g[0] == w[i] || (g.size() > 1 && g[1] == w[i])) {
                s = i;
                sup = &g;
                break;
            }
    if (!sup) return out;
    const std::string thing = w[s + 1];
    if (stop_word(thing)) return out;
    // What the question says around it ("in the world", "of the human body"),
    // and the kind of answer it wants ("which country has the ...").
    std::vector<std::string> around;
    for (std::size_t i = s + 2; i < n; ++i)
        if (!stop_word(w[i])) around.push_back(w[i]);
    std::string kind_word = (w[0] == "which" || w[0] == "what") && s >= 3 && !stop_word(w[1]) ? w[1] : "";
    std::vector<std::string> heads = {thing};
    for (const auto& g : kKinds)
        if (g[0] == thing) heads = g;
    std::vector<std::string> phrases;
    for (const auto& sw : *sup)
        for (const auto& h : heads) phrases.push_back(sw + " " + h);
    if (thing == "population" && sup->front() == "largest") {
        phrases.push_back("most populous");
        phrases.push_back("most populated");
    }
    auto present = [](const std::set<std::string>& words, const std::string& word) {
        if (words.count(word) || words.count(normalize_word(word))) return true;
        for (const auto& g : kAround)
            if (g[0] == word)
                for (const auto& alt : g)
                    if (words.count(alt)) return true;
        return false;
    };
    // "the third highest", "one of the largest", "Australia's longest": a
    // ranking or a local winner, not the one asked about.
    static const std::set<std::string> ordinals = {"second", "third", "fourth", "fifth", "sixth", "seventh", "eighth",
                                                   "ninth", "tenth", "2nd", "3rd", "4th", "5th", "6th", "7th"};
    static const std::set<std::string> global = {"world's", "world\xe2\x80\x99s", "earth's", "planet's"};
    auto local_or_ranked = [&](const std::string& low, std::size_t pos) {
        std::string before = low.substr(0, pos);
        while (!before.empty() && (before.back() == ' ' || before.back() == '-')) before.pop_back();
        std::size_t a = before.find_last_of(" (,;");
        const std::string last = before.substr(a == std::string::npos ? 0 : a + 1);
        if (ordinals.count(last)) return true;
        // "11th", "33rd"
        if (last.size() >= 3 && std::isdigit(static_cast<unsigned char>(last[0]))) {
            const std::string tail = last.substr(last.size() - 2);
            if (tail == "st" || tail == "nd" || tail == "rd" || tail == "th") return true;
        }
        if ((last.size() > 2 && (last.compare(last.size() - 2, 2, "'s") == 0 ||
                                 (last.size() > 4 && last.compare(last.size() - 4, 4, "\xe2\x80\x99s") == 0))) &&
            !global.count(last))
            return true;
        for (const char* tail : {"one of the", "among the", "not the"})
            if (before.size() >= std::strlen(tail) &&
                before.compare(before.size() - std::strlen(tail), std::strlen(tail), tail) == 0)
                return true;
        return false;
    };
    const std::pair<Passage, int>* best = nullptr;
    double best_score = 0;
    int best_at = 0;
    // First every word the question puts around the phrase must be there
    // (few matches, quick to rank); only if that finds nothing, any of them.
    std::vector<std::vector<std::string>> each;
    std::vector<std::string> any;
    for (const auto& a : around) {
        std::vector<std::string> group = {a};
        for (const auto& g : kAround)
            if (g[0] == a) group.insert(group.end(), g.begin() + 1, g.end());
        each.push_back(group);
        any.insert(any.end(), group.begin(), group.end());
    }
    if (!kind_word.empty()) {
        each.push_back({kind_word});
        any.push_back(kind_word);
    }
    std::vector<std::pair<Passage, int>> cands = lib_.leads_with(phrases, each, {"encyclopedia"}, 200, true, 700);
    std::pair<Passage, int> chosen;
    for (int pass = 0; pass < 2 && !best; ++pass) {
    if (pass == 1) {
        if (any.empty() || each.size() < 2) break;
        cands = lib_.leads_with(phrases, {any}, {"encyclopedia"}, 200);
    }
    for (const auto& c : cands) {
        const auto ss = sentences_of(c.first.text);
        int at = -1;
        bool worldwide = false, itself = false;
        for (std::size_t i = 0; i < ss.size() && at < 0; ++i) {
            const std::string low = lower(ss[i]);
            for (const auto& ph : phrases) {
                for (std::size_t pos = low.find(ph); pos != std::string::npos; pos = low.find(ph, pos + 1)) {
                    // whole words only ("nation" is not "national"); a plural is fine
                    std::size_t end = pos + ph.size();
                    if (end < low.size() && low[end] == 's') ++end;
                    if (end < low.size() && std::isalpha(static_cast<unsigned char>(low[end]))) continue;
                    if (pos > 0 && std::isalpha(static_cast<unsigned char>(low[pos - 1]))) continue;
                    if (local_or_ranked(low, pos)) continue;
                    // "the first human object to fly": the phrase only describes
                    // another thing. What follows must end it.
                    {
                        std::size_t a = end;
                        while (a < low.size() && low[a] == ' ') ++a;
                        std::size_t b = a;
                        while (b < low.size() && (std::isalpha(static_cast<unsigned char>(low[b])) || low[b] == '-')) ++b;
                        const std::string next = low.substr(a, b - a);
                        static const std::set<std::string> enders = {
                            "to", "in", "on", "of", "into", "at", "by", "for", "from", "who", "which", "that", "ever",
                            "and", "among", "as", "with", "since", "until", "known", "recorded", "measured", "anywhere"};
                        if (!next.empty() && !enders.count(next) && next != kind_word && next != thing &&
                            !(next.size() > 1 && next.back() == 's' && next.substr(0, next.size() - 1) == thing))
                            continue;
                    }
                    at = int(i);
                    // "Gagarin was the first human": said of the article's own subject.
                    {
                        std::string before = low.substr(0, pos);
                        while (!before.empty() && before.back() == ' ') before.pop_back();
                        if (before.size() >= 3 && before.compare(before.size() - 3, 3, "the") == 0) {
                            const std::string lead_in = " " + before.substr(before.size() >= 30 ? before.size() - 30 : 0);
                            for (const char* v : {" is ", " was ", " are ", " were ", " became ", " becomes ", " remains "})
                                itself |= lead_in.find(v) != std::string::npos;
                        }
                    }
                    const std::string after = low.substr(end, 40);
                    worldwide = after.find("world") != std::string::npos || after.find("earth") != std::string::npos;
                    break;
                }
                if (at >= 0) break;
            }
        }
        if (at < 0) continue;
        std::set<std::string> words;
        for (const auto& t : tokenize(c.first.text)) {
            words.insert(t.lower);
            words.insert(normalize_word(t.lower));
        }
        double cover = 1.0;
        if (!around.empty()) {
            int hit = 0;
            for (const auto& a : around) hit += present(words, a) ? 1 : 0;
            cover = double(hit) / double(around.size());
        }
        if (cover < 0.5) continue;
        const double score = (at == 0 ? 2.0 : 1.0) + 1.5 * cover + (!kind_word.empty() && present(words, kind_word) ? 0.5 : 0) +
                             (worldwide ? 0.8 : 0) + (itself ? 1.0 : 0) + 0.3 * std::log10(1.0 + c.second);
        if (score > best_score) {
            best_score = score;
            chosen = c;
            best = &chosen;
            best_at = at;
        }
    }
    }
    if (!best) return out;
    const Passage& lead = best->first;
    // "when did the first man land on the moon": the step has named the
    // subject; the ordinary answer about that subject gives the date.
    if (r.wants == Wants::Time || r.wants == Wants::Quantity || r.wants == Wants::Count) {
        Reading about = r;
        about.subject = lower(lead.title);
        // "when did the first man land on the moon" -> "when did neil armstrong land on the moon"
        std::string asked;
        const std::size_t from = (s > 0 && w[s - 1] == "the") ? s - 1 : s;
        for (std::size_t i = 0; i < n; ++i) {
            if (i == from) asked += (asked.empty() ? "" : " ") + about.subject;
            if (i >= from && i <= s + 1) continue;
            asked += (asked.empty() ? "" : " ") + w[i];
        }
        WorldAnswer inner = answer(about, asked, false);
        if (inner.kind != WorldAnswer::None && inner.confidence >= 0.6) return inner;
    }
    const auto ss = sentences_of(lead.text);
    std::string text = tidy(ss.empty() ? lead.text : ss[0]);
    if (best_at > 0 && std::size_t(best_at) < ss.size()) text += " " + tidy(ss[std::size_t(best_at)]);
    out.kind = WorldAnswer::Answer;
    out.confidence = best_at == 0 ? 0.8 : 0.65;
    out.text = text + " " + attribution(lead);
    out.used = {lead};
    out.receipts = {receipt(lead)};
    return out;
}

bool World::load_extractor(const std::string& types, const std::string& reformulations, const std::string& units) {
    extract_ready_ = types_.load(types) && reform_.load(reformulations);
    load_span_units(units);
    return extract_ready_;
}

// The short answer: every candidate of the asked type in the ranked
// passages votes, weighted by its passage's rank score, by how close it sits
// to the question's words, and by whether a sentence pattern for the
// question ("X is a song by A") puts it in the answer slot. It answers only
// when one candidate clearly wins AND some sentence really supports it: a
// pattern matched, or the sentence holds half the question's words. A
// question with a name, place, date or number shape and no supported winner
// is the honest "I don't know" case (*ran stays true).
WorldAnswer World::extract(const Reading& r, const std::string& question, const std::vector<Passage>& ps,
                           bool* ran) const {
    WorldAnswer out;
    *ran = false;
    if (!extract_ready_ || ps.empty()) return out;
    const AnswerShape shape = answer_type(types_, question);
    if (shape.type == AnswerType::None || shape.type == AnswerType::Description || shape.type == AnswerType::List)
        return out;
    *ran = true;
    std::set<std::string> qset;
    for (const auto& w : content_words(question)) {
        qset.insert(w);
        qset.insert(normalize_word(w));
    }
    const std::size_t qcount = content_words(question).size();
    const auto patterns = reform_.bind(question, shape);
    double top_score = 1e-9;
    for (const auto& p : ps) top_score = std::max(top_score, p.score);

    struct Cand {
        double score = 0, pattern = 0, support_val = -1;
        bool supported = false;
        std::map<std::string, double> shown;
        const Passage* p = nullptr;
        std::string sentence;
        AnswerType type = AnswerType::None;
    };
    std::map<std::string, Cand> cands;
    std::map<std::pair<int64_t, std::string>, int> seen;  // (doc, key) occurrences
    std::set<std::pair<std::string, int64_t>> used_pids;
    int taken = 0;
    for (const auto& P : ps) {
        if (taken >= 20) break;
        if (!quotable(P.role) || !used_pids.insert({P.pack, P.pid}).second) continue;
        ++taken;
        const double w = std::max(0.1, P.score / top_score);
        std::string prev;
        for (std::string sentence : sentences_of(P.text)) {
            bool note = false;
            for (const char* h : {"This article is about", "For other uses", "See also", "Not to be confused",
                                  "Main article"})
                note |= sentence.rfind(h, 0) == 0;
            if (note || number_hole(sentence)) continue;
            sentence = tidy(sentence);
            MatchContext ctx{P.title, prev};
            const PreparedSentence prepared = prepare_sentence(sentence, &ctx);
            // Where the question's words sit in the sentence, by token.
            const auto toks = tokenize(sentence);
            std::vector<std::size_t> qpos;
            std::set<std::string> hit;
            for (std::size_t i = 0; i < toks.size(); ++i)
                if (qset.count(toks[i].lower) || qset.count(normalize_word(toks[i].lower))) {
                    qpos.push_back(i);
                    hit.insert(normalize_word(toks[i].lower));
                }
            const bool half = qcount > 0 && hit.size() * 2 >= qcount;
            for (AnswerType t : {shape.type, shape.also}) {
                if (t == AnswerType::None) continue;
                for (const Span& x : spans_of(sentence, t)) {
                    // The subject itself is never its own answer, nor a name
                    // half made of the question's words ("NBA Finals" for an
                    // NBA question). Small words aside: "say you won t let go"
                    // is the song asked about.
                    std::size_t words = 0, asked = 0;
                    for (const auto& kt : tokenize(x.key))
                        if (kt.lower.size() > 1 && !stop_word(kt.lower) && !cue_words().count(kt.lower)) {
                            ++words;
                            asked += qset.count(kt.lower) || qset.count(normalize_word(kt.lower)) > 0;
                        }
                    if (words == 0 || asked * 2 >= words) continue;
                    std::size_t at = 0;
                    while (at < toks.size() && toks[at].start < x.start) ++at;
                    double near = 0;
                    if (!qpos.empty()) {
                        std::size_t d = toks.size();
                        for (std::size_t q : qpos) d = std::min(d, q > at ? q - at : at - q);
                        near = 1.0 / (1.0 + double(d));
                    }
                    const double fit = t == shape.type ? 1.0 : 0.8;
                    const double pat = Reformulations::match(patterns, prepared, x);
                    // A nationality before a role noun describes someone ("English
                    // singer and songwriter James Arthur"); it is not the name.
                    if (x.key.find(' ') == std::string::npos && (t == AnswerType::Person || t == AnswerType::Organization)) {
                        static const std::set<std::string> roles = {
                            "singer", "songwriter", "actor", "actress", "writer", "author", "rapper", "band", "group",
                            "musician", "poet", "painter", "politician", "footballer", "player", "composer",
                            "director", "producer", "artist", "duo", "rock", "pop", "model", "comedian", "novelist",
                            "journalist", "scientist", "physicist", "chemist", "mathematician", "philosopher",
                            "king", "queen", "explorer", "inventor", "engineer", "businessman", "entrepreneur",
                            "television", "film", "record", "country", "hip", "jazz", "girl", "boy", "heavy",
                            "electronic", "folk", "soul", "punk", "indie", "dj", "professional", "former", "statesman",
                            "general", "emperor", "architect", "sculptor", "astronaut", "economist", "historian"};
                        // Up to four lowercase words after it ("ska/pop group").
                        std::size_t nx = x.end;
                        bool role = false;
                        for (int k = 0; k < 4 && !role; ++k) {
                            while (nx < sentence.size() && (sentence[nx] == ' ' || sentence[nx] == '-' || sentence[nx] == '/'))
                                ++nx;
                            if (nx >= sentence.size() || !std::islower(static_cast<unsigned char>(sentence[nx]))) break;
                            std::size_t ne = nx;
                            while (ne < sentence.size() && std::isalpha(static_cast<unsigned char>(sentence[ne]))) ++ne;
                            role = roles.count(sentence.substr(nx, ne - nx)) > 0;
                            nx = ne;
                        }
                        if (role) continue;
                    }
                    // One capitalized word opening a sentence ("Some", "After",
                    // "People") is only a name when a pattern puts it there.
                    if (x.start == 0 && x.key.find(' ') == std::string::npos && pat == 0 &&
                        (t == AnswerType::Person || t == AnswerType::Organization || t == AnswerType::Place ||
                         t == AnswerType::Title))
                        continue;
                    double val = w * (fit + near + 0.5 * pat);
                    if (qpos.empty()) val *= 0.3;
                    int& n = seen[{P.doc_id * 1000 + P.shard, x.key}];
                    if (n++ > 0) val *= 0.25;
                    Cand& c = cands[x.key];
                    c.score += val;
                    c.shown[x.text] += val;
                    c.pattern = std::max(c.pattern, pat);
                    c.type = t;
                    const bool sup = pat > 0 || half;
                    if (sup) c.supported = true;
                    // The sentence shown: a supporting one first, then the strongest.
                    const double rank_val = val + (sup ? 10 : 0);
                    if (rank_val > c.support_val) {
                        c.support_val = rank_val;
                        c.p = &P;
                        c.sentence = sentence;
                    }
                }
            }
            prev = sentence;
        }
    }
    if (cands.empty()) return out;
    // Agreeing dates pool onto the most specific; a surname alone gives its
    // votes to the full name ("quill" to "mara quill").
    std::vector<std::string> keys;
    for (const auto& [k, c] : cands) keys.push_back(k);
    for (const auto& a : keys) {
        auto& ca = cands[a];
        if (ca.score <= 0) continue;
        for (const auto& b : keys) {
            if (a == b) continue;
            auto& cb = cands[b];
            if (cb.score <= 0) continue;
            bool give = false;
            if ((ca.type == AnswerType::Date || ca.type == AnswerType::Year) && keys_agree(a, b) && b.size() > a.size())
                give = true;
            if (ca.type == AnswerType::Person && a.find(' ') == std::string::npos && b.size() > a.size() + 1 &&
                b.compare(b.size() - a.size() - 1, a.size() + 1, " " + a) == 0)
                give = true;
            if (give) {
                cb.score += ca.score;
                cb.pattern = std::max(cb.pattern, ca.pattern);
                cb.supported |= ca.supported;
                if (!cb.p) {
                    cb.p = ca.p;
                    cb.sentence = ca.sentence;
                }
                ca.score = 0;
                break;
            }
        }
    }
    std::vector<std::pair<std::string, const Cand*>> order;
    for (const auto& [k, c] : cands)
        if (c.score > 0) order.push_back({k, &c});
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) {
        return a.second->score != b.second->score ? a.second->score > b.second->score : a.first < b.first;
    });
    const Cand& best = *order[0].second;
    const double second = order.size() > 1 ? order[1].second->score : 0;
    static const bool debug = std::getenv("UNLLM_DEBUG") != nullptr;
    static const bool debug_all = debug && std::string(std::getenv("UNLLM_DEBUG")) == "2";  // every candidate
    if (debug)
        for (std::size_t i = 0; i < order.size() && i < 5; ++i)
            std::fprintf(stderr, "  span %zu: %.2f %s%s pattern %.0f \"%s\"\n", i, order[i].second->score,
                         order[i].first.c_str(), order[i].second->supported ? " supported" : "",
                         order[i].second->pattern, answer_type_name(shape.type));
    if (debug_all)
        for (std::size_t i = 0; i < order.size(); ++i) {
            const Cand& c = *order[i].second;
            std::fprintf(stderr, "  cand %zu\t%.2f\t%s\t%s\t%d\t%.0f\t%s\t%s\n", i, c.score, order[i].first.c_str(),
                         answer_type_name(c.type), c.supported ? 1 : 0, c.pattern, c.p ? c.p->title.c_str() : "",
                         c.p ? c.p->text.substr(0, 50).c_str() : "");
        }
    const bool clear = best.score >= 1.5 && (best.score >= 1.5 * second || best.pattern >= 4);
    if (!clear || !best.supported || !best.p) return out;
    std::string shown;
    double most = -1;
    for (const auto& [text, v] : best.shown)
        if (v > most || (v == most && text.size() > shown.size())) {
            most = v;
            shown = text;
        }
    out.kind = WorldAnswer::Answer;
    out.confidence = std::min(0.95, 0.5 + 0.1 * best.score);
    std::string support = best.sentence;
    out.text = capitalize(shown) + ". " + support + " " + attribution(*best.p);
    out.used = {*best.p};
    out.receipts = {receipt(*best.p)};
    (void)r;
    return out;
}

WorldAnswer World::read_answer(const std::string& question, const std::vector<Passage>& ps, bool* ran) const {
    WorldAnswer out;
    *ran = false;
    if (!reader_ || !reader_->ready()) return out;
    // The passages the reader sees: one per article, best first, as
    // "Title: text" (the title often holds the answer's subject).
    std::vector<const Passage*> chosen;
    std::vector<std::string> texts;
    std::set<std::pair<std::string, int64_t>> seen;
    for (const auto& p : ps)
        if (quotable(p.role) && seen.insert({p.pack, p.doc_id}).second && chosen.size() < 20) {
            chosen.push_back(&p);
            texts.push_back(p.title + ": " + p.text);
        }
    const auto spans = reader_->read(question, texts);
    if (spans.empty()) return out;
    *ran = true;
    // The span the reader is surest of, and how many articles give the same
    // one. On the clean dev set (2026-10-17) two articles agreeing, or a
    // span probability of 0.9, was right 89% of the time; three articles or
    // 0.95, 95%. UNLLM_READER_GATE="votes,prob" overrides it, for measuring.
    static const std::pair<int, double> gate = [] {
        int v = 2;
        double pr = 0.9;
        if (const char* g = std::getenv("UNLLM_READER_GATE")) std::sscanf(g, "%d,%lf", &v, &pr);
        return std::pair<int, double>{v, pr};
    }();
    const ReaderSpan* best = nullptr;
    for (const auto& sp : spans)
        if (!best || sp.score - sp.null_score > best->score - best->null_score) best = &sp;
    const double margin = best->score - best->null_score;
    auto key = [](const std::string& t) {
        std::string k;
        for (const auto& w : tokenize(t))
            if (!stop_word(w.lower)) k += (k.empty() ? "" : " ") + w.lower;
        return k;
    };
    const std::string bk = key(best->text);
    int votes = 0;
    for (const auto& sp : spans) {
        if (sp.score - sp.null_score <= 0) continue;
        const std::string k = key(sp.text);
        if (!bk.empty() && !k.empty() &&
            (k == bk || (" " + k + " ").find(" " + bk + " ") != std::string::npos ||
             (" " + bk + " ").find(" " + k + " ") != std::string::npos))
            ++votes;
    }
    static const bool debug = std::getenv("UNLLM_DEBUG") != nullptr;
    if (debug)
        std::fprintf(stderr, "  reader: \"%s\" margin %.2f prob %.3f votes %d passage %zu\n", best->text.c_str(),
                     margin, best->prob, votes, best->passage);
    if (margin <= 0 || (votes < gate.first && best->prob < gate.second)) return out;
    std::string answer = best->text;
    while (!answer.empty() && std::strchr(" .,;:", answer.back())) answer.pop_back();
    if (answer.empty()) return out;
    const Passage& P = *chosen[best->passage];
    // The sentence that holds the answer, as the reason.
    std::string support;
    for (const auto& s : sentences_of(P.text))
        if (s.find(answer) != std::string::npos) {
            support = tidy(s);
            break;
        }
    if (support.empty()) support = tidy(sentences_of(P.text).empty() ? P.text : sentences_of(P.text)[0]);
    out.kind = WorldAnswer::Answer;
    out.confidence = std::min(0.95, 0.5 + 0.05 * margin);
    out.text = capitalize(answer) + ". " + support + " " + attribution(P);
    out.used = {P};
    out.receipts = {receipt(P)};
    return out;
}

// A passage as one line, runs of whitespace closed up: what the reranker and
// the reader see, and what the bench scripts measured on.
std::string squeeze(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (unsigned char c : text) {
        if (std::isspace(c)) {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += char(c);
    }
    return out;
}

// The pipeline's search (measured 2026-10-21, bench/pool50.py and pool_read50.py):
// the word search's own top 25 (SQLite candidates and the Tantivy list, in the
// word reranker's order) and the meaning search's top 25, pooled with the
// meaning list first and duplicates dropped, then put in the cross-encoder's
// order; the top 10 come back, scored by position so the composer below
// treats the first as the best.
std::vector<Passage> World::pipeline_passages(const Reading& r, const Query& q0, const std::string& question) const {
    constexpr std::size_t K = 25, KEEP = 10;
    Query q = q0;
    q.kinds = {};
    std::vector<Passage> words = lib_.candidates(q);
    if (lib_.has_ranked()) {
        std::string ws;
        for (const auto& w : q.words) ws += (ws.empty() ? "" : " ") + w;
        std::set<std::pair<std::string, int64_t>> have;
        for (const auto& p : words) have.insert({p.pack, p.pid});
        for (auto& p : lib_.ranked(ws, q.kinds, 100))
            if (have.insert({p.pack, p.pid}).second) words.push_back(std::move(p));
    }
    rerank(r, q.words, words, false);
    if (words.size() > K) words.resize(K);
    std::vector<Passage> found;
    if (meaning_ && meaning_->ready()) {
        std::vector<Library::Place> places;
        for (const auto& h : meaning_->search(meaning_->embed(question), int(K), 64))
            for (const auto& s : meaning_->shards())
                if (s.no == h.shard_no) places.push_back({s.pack, s.file_no, h.pid, h.score});
        found = lib_.by_place(places);
        if (found.size() > K) found.resize(K);
    }
    std::vector<Passage> pool;
    std::vector<std::string> docs;
    std::set<std::string> seen;
    for (const auto* list : {&found, &words})
        for (const auto& p : *list) {
            std::string doc = p.title + ": " + squeeze(p.text);
            if (!seen.insert(doc).second) continue;
            pool.push_back(p);
            docs.push_back(std::move(doc));
        }
    const std::vector<float> sc = reranker_ ? reranker_->scores(question, docs) : std::vector<float>{};
    std::vector<std::size_t> order(pool.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    if (sc.size() == pool.size())
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return sc[a] > sc[b]; });
    std::vector<Passage> out;
    for (std::size_t i = 0; i < order.size() && out.size() < KEEP; ++i) {
        out.push_back(pool[order[i]]);
        out.back().bm25 = out.back().score = 1000.0 - double(out.size() - 1);
        out.back().title_score = 0;
        out.back().late = sc.size() == pool.size() ? double(sc[order[i]]) : 0.0;
    }
    if (std::getenv("UNLLM_DEBUG")) {
        std::fprintf(stderr, "  pipeline: %zu by words, %zu by meaning, pool %zu, reranker %s\n", words.size(),
                     found.size(), pool.size(), sc.size() == pool.size() ? "ordered it" : "did not run");
        for (std::size_t k = 0; k < out.size() && k < 5; ++k)
            std::fprintf(stderr, "    %zu. %.2f %s \"%s\"\n", k + 1, out[k].late, out[k].pack.c_str(),
                         out[k].title.substr(0, 60).c_str());
    }
    return out;
}

WorldAnswer World::answer(const Reading& r, const std::string& question) const { return answer(r, question, true); }

WorldAnswer World::answer(const Reading& r, const std::string& question, std::vector<Passage>* best) const {
    return answer(r, question, true, best);
}

WorldAnswer World::answer(const Reading& r, const std::string& question, bool resolve,
                          std::vector<Passage>* best) const {
    WorldAnswer out;
    if (lib_.empty()) return out;
    const bool pipeline = reranker_ && reranker_->ready() && std::getenv("UNLLM_LAYERS") == nullptr;
    g_pipeline = pipeline;
    static const bool timing = std::getenv("UNLLM_DEBUG") != nullptr;
    auto t0 = std::chrono::steady_clock::now();
    auto lap = [&](const char* what) {
        if (!timing) return;
        auto t1 = std::chrono::steady_clock::now();
        std::fprintf(stderr, "  time %-12s %6.0f ms\n", what, std::chrono::duration<double, std::milli>(t1 - t0).count());
        t0 = t1;
    };
    WorldAnswer fact = layer("fact") ? fact_answer(r, question) : WorldAnswer{};
    lap("fact box");
    if (fact.kind != WorldAnswer::None) return fact;
    if (resolve && layer("super")) {
        WorldAnswer top = superlative_answer(r, question);
        lap("superlative");
        if (top.kind != WorldAnswer::None) return top;
    }
    Query q;
    q.words = content_words(question);
    q.subject = r.subject;
    bool meaning = false;
    // "how tall": the words a sentence giving that measure tends to use.
    static const std::map<std::string, std::vector<std::string>> measure = {
        {"tall", {"tall", "high", "height", "elevation", "metres", "meters", "feet"}},
        {"high", {"high", "height", "elevation", "altitude", "metres", "meters", "feet"}},
        {"old", {"old", "age", "ago", "formed", "founded", "born", "built", "since"}},
        {"cold", {"cold", "coldest", "temperature", "degrees", "freezing", "below", "climate", "winter", "winters"}},
        {"hot", {"hot", "hottest", "temperature", "degrees", "heat", "climate", "summer", "summers"}},
        {"warm", {"warm", "temperature", "degrees"}},
        {"big", {"big", "size", "diameter", "radius", "area", "mass", "times", "large", "largest"}},
        {"large", {"large", "size", "diameter", "radius", "area", "mass", "times", "largest"}},
        {"far", {"far", "distance", "away", "light", "from"}},
        {"deep", {"deep", "depth", "deepest"}},
        {"wide", {"wide", "width", "across"}},
        {"heavy", {"heavy", "mass", "weight", "weighs"}},
        {"fast", {"fast", "speed", "per", "mph", "km/h"}}};
    const std::vector<std::string>* measure_words = nullptr;
    {
        auto qt = tokenize(question);
        for (std::size_t i = 0; i < qt.size(); ++i) {
            meaning |= qt[i].lower == "mean" || qt[i].lower == "meaning" || qt[i].lower == "define" ||
                       qt[i].lower == "definition";
            if (qt[i].lower == "how" && i + 1 < qt.size() && measure.count(qt[i + 1].lower))
                measure_words = &measure.at(qt[i + 1].lower);
            // "why is beijing so hot": the quality asked about is the last word.
            if (qt[0].lower == "why" && i + 1 == qt.size() && measure.count(qt[i].lower))
                measure_words = &measure.at(qt[i].lower);
        }
    }
    if (r.frame == Frame::Poem) q.kinds = {"poems"};
    else if (!layer("kinds")) q.kinds = {};
    else if (r.frame == Frame::HowTo) q.kinds = {"qa", "encyclopedia"};
    else if (r.frame == Frame::Define) q.kinds = {"definitions", "encyclopedia"};
    // Who, when, where, how many: Q&A threads were never right for these
    // (0 of 15 on Natural Questions and the clean exam, 2026-10-16).
    else if (r.wants == Wants::Time || r.wants == Wants::Name || r.wants == Wants::Count ||
             r.wants == Wants::Quantity || lower(question).rfind("where ", 0) == 0)
        q.kinds = {"encyclopedia"};
    else q.kinds = {"encyclopedia", "qa"};
    if (r.frame == Frame::Poem && !r.subject.empty()) q.words = content_words(r.subject);
    if (q.words.empty()) return out;
    std::vector<Passage> ps;
    // UNLLM_PASSAGES=file (measuring only): the candidate list comes from the file, one
    // "Title: text" passage per line in the file's order, instead of from the search.
    static const char* given = std::getenv("UNLLM_PASSAGES");
    if (given) {
        std::ifstream in(given);
        std::string line;
        std::set<std::string> titles;
        for (int64_t n = 0; std::getline(in, line); ++n) {
            auto sep = line.find(": ");
            if (sep == std::string::npos || line.empty()) continue;
            Passage p;
            p.pack = "";  // the file names no pack: the attribution shows the title alone
            p.kind = "encyclopedia";
            p.title = line.substr(0, sep);
            p.text = line.substr(sep + 2);
            p.role = titles.insert(p.title).second ? "lead" : "body";
            p.doc_id = int64_t(std::hash<std::string>{}(p.title) & 0x7fffffff);
            p.pid = n;
            p.bm25 = 1000.0 - double(n);  // the file's order, for the plain-order path
            p.score = p.bm25;
            ps.push_back(std::move(p));
        }
    }
    // Meaning search, when a vector index is installed: the question's vector against the
    // passages'. Its list is kept in the index's order, and the word reranker and the word
    // floor stay out of its way: both judge by the question's words, which a passage found by
    // meaning often lacks (measured 2026-10-19: 71 -> 113 right of 400 on the pilot).
    bool by_meaning = false;
    // The pipeline: both searches' top 25 pooled and put in the reranker's order.
    if (!given && pipeline) {
        ps = pipeline_passages(r, q, question);
        lap("pipeline search");
    }
    if (!given && !pipeline && meaning_ && meaning_->ready() && layer("meaning")) {
        std::vector<Library::Place> places;
        static const int nprobe = std::getenv("UNLLM_NPROBE") ? std::atoi(std::getenv("UNLLM_NPROBE")) : 64;  // measuring
        static const int fetch = std::getenv("UNLLM_MEANING_K") ? std::atoi(std::getenv("UNLLM_MEANING_K")) : 20;  // measuring
        for (const auto& h : meaning_->search(meaning_->embed(question), fetch, nprobe))
            for (const auto& s : meaning_->shards())
                if (s.no == h.shard_no) places.push_back({s.pack, s.file_no, h.pid, h.score});
        ps = lib_.by_place(places);
        for (std::size_t k = 0; k < ps.size(); ++k) ps[k].bm25 = ps[k].score = 1000.0 - double(k);
        by_meaning = !ps.empty();
    }
    static const bool trace = std::getenv("UNLLM_DEBUG") != nullptr;
    auto show = [&](const char* what, const std::vector<Passage>& list) {
        if (!trace) return;
        std::fprintf(stderr, "  %s: %zu passages\n", what, list.size());
        for (std::size_t k = 0; k < list.size() && k < 5; ++k)
            std::fprintf(stderr, "    %zu. %.2f %s/%s \"%s\" title %.2f\n", k + 1, list[k].bm25, list[k].pack.c_str(),
                         list[k].role.c_str(), list[k].title.substr(0, 50).c_str(), list[k].title_score);
    };
    if (trace) {
        std::string ws;
        for (const auto& w : q.words) ws += (ws.empty() ? "" : ", ") + w;
        std::fprintf(stderr, "  query words: [%s]; subject: \"%s\"; answer type: %s\n", ws.c_str(), q.subject.c_str(),
                     wants_name(r.wants));
    }
    if (!given && !pipeline && !by_meaning && (layer("oldsearch") || layer("union") || !lib_.has_ranked())) {
        ps = lib_.candidates(q);
        show("sqlite (title stage + all words + any words)", ps);
    }
    if (!given && !pipeline && !by_meaning && !layer("oldsearch") && lib_.has_ranked()) {
        std::string words;
        for (const auto& w : q.words) words += (words.empty() ? "" : " ") + w;
        std::set<std::pair<std::string, int64_t>> have;
        for (const auto& p : ps) have.insert({p.pack, p.pid});
        auto got = lib_.ranked(words, q.kinds, layer("wide") ? 100 : 20);
        show("tantivy", got);
        for (auto& p : got)
            if (have.insert({p.pack, p.pid}).second) ps.push_back(std::move(p));
    }
    lap("search");
    // "world war 2" is titled "World War II": try the roman numeral too.
    {
        static const char* roman[] = {"", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x"};
        std::string variant;
        bool changed = false;
        for (const auto& t : tokenize(r.subject)) {
            std::string w = t.lower;
            if (w.size() <= 2 && std::all_of(w.begin(), w.end(), ::isdigit)) {
                int n = std::atoi(w.c_str());
                if (n >= 1 && n <= 10) {
                    w = roman[n];
                    changed = true;
                }
            }
            variant += (variant.empty() ? "" : " ") + w;
        }
        if (changed && layer("roman")) {
            auto more = lib_.by_title(variant, q.kinds, 3);
            ps.insert(ps.begin(), more.begin(), more.end());
        }
    }
    // The subject word for word ("powerhouse of the cell") marks the
    // passages that say exactly that.
    bool titled_exactly = false;
    for (const auto& p : ps) titled_exactly |= p.title_score >= 0.99;
    if (layer("phrase") && content_words(r.subject).size() >= 2 && r.frame != Frame::Poem && !titled_exactly)
        for (auto& found : lib_.leads_with({lower(r.subject)}, {}, q.kinds, 30, false, 150)) {
            found.first.phrase = true;
            ps.push_back(std::move(found.first));
        }
    lap("phrase");
    // Asked by a short name with no article of exactly that title ("who was
    // mozart"): the best-known article whose title holds the name.
    {
        bool exact = false;
        for (const auto& p : ps) exact |= p.title_score >= 0.99;
        const auto sw = content_words(r.subject);
        if (layer("named") && !exact && !sw.empty() && sw.size() <= 3 && r.frame != Frame::Poem &&
            r.frame != Frame::HowTo) {
            auto docs = lib_.titled(r.subject, {"encyclopedia"}, 1);
            if (!docs.empty() && docs[0].passages >= 4) {
                Passage lead = lib_.lead_of(docs[0].shard, docs[0].doc_id);
                if (lead.doc_id) {
                    lead.named = true;
                    ps.insert(ps.begin(), lead);
                }
            }
        }
    }
    lap("by name");
    // "who sang let me tell you about the birds and the bees": the song is
    // an article titled with the rest of the question, often with
    // "(... song)" after it; its opening says who sang it.
    if (layer("song")) {
        auto qt = tokenize(question);
        static const std::set<std::string> verbs = {"sings", "sang", "sing", "sung", "wrote", "recorded",
                                                    "performed", "performs", "composed", "released"};
        std::size_t at = 0;
        if (!qt.empty() && qt[0].lower == "who") at = 1;
        if (at && at < qt.size() && (qt[at].lower == "originally" || qt[at].lower == "first")) ++at;
        if (at && at + 1 < qt.size() && verbs.count(qt[at].lower)) {
            std::string title;
            for (std::size_t i = at + 1; i < qt.size(); ++i)
                if (!(i == at + 1 && (qt[i].lower == "the" && i + 1 < qt.size() && qt[i + 1].lower == "song")) &&
                    !(qt[i].lower == "song" && i == at + 2 && qt[at + 1].lower == "the"))
                    title += (title.empty() ? "" : " ") + qt[i].lower;
            // Only articles titled exactly so, "(... song)" aside; songs and
            // singles first. Nothing found: drop leading words ("let me tell
            // you about the birds and the bees" is "The Birds and the Bees").
            std::vector<std::string> all;
            for (const auto& t : tokenize(title)) all.push_back(t.lower);
            std::vector<DocRef> found;
            for (std::size_t from = 0; found.empty() && from + 1 < all.size() + 1 && from < all.size(); ++from) {
                std::string want;
                for (std::size_t i = from; i < all.size(); ++i) want += (want.empty() ? "" : " ") + all[i];
                if (content_words(want).size() < 2 && from > 0) break;
                for (auto& d : lib_.titled(want, {"encyclopedia"}, 25)) {
                    std::string base = d.title;
                    const std::size_t paren = base.find(" (");
                    const std::string note = paren == std::string::npos ? "" : lower(base.substr(paren));
                    if (paren != std::string::npos) base = base.substr(0, paren);
                    if (title_key(base) != title_key(want)) continue;
                    d.exact = note.find("song") != std::string::npos || note.find("single") != std::string::npos;
                    found.push_back(d);
                }
            }
            std::stable_sort(found.begin(), found.end(), [](const DocRef& a, const DocRef& b) { return a.exact > b.exact; });
            if (found.size() > 3) found.resize(3);
            for (auto it = found.rbegin(); it != found.rend(); ++it) {
                Passage lead = lib_.lead_of(it->shard, it->doc_id);
                if (lead.doc_id) {
                    lead.named = true;
                    ps.insert(ps.begin(), lead);
                }
            }
        }
    }
    // "how many continents are there": the article on the thing counted.
    if (layer("howmany") && r.subject.empty() && (r.wants == Wants::Count || r.wants == Wants::Quantity)) {
        std::vector<std::string> w;
        for (const auto& t : tokenize(question)) w.push_back(t.lower);
        for (std::size_t i = 0; i + 2 < w.size(); ++i)
            if (w[i] == "how" && w[i + 1] == "many") {
                std::string thing = w[i + 2];
                if (thing.size() > 3 && thing.back() == 's') thing.pop_back();
                auto lead = lib_.by_title(thing, {"encyclopedia"}, 1);
                if (lead.empty()) lead = lib_.by_title(w[i + 2], {"encyclopedia"}, 1);
                for (auto& l : lead) {
                    l.named = true;
                    ps.insert(ps.begin(), l);
                }
                break;
            }
    }
    // A how-to: the Q&A threads whose titles hold the question's own words
    // ("How do I stop my chain from squeaking?"), dropping one word at a time
    // when all of them find nothing.
    if (layer("howto") && r.frame == Frame::HowTo) {
        const auto words = content_words(r.subject.empty() ? question : r.subject);
        auto join = [&](std::size_t skip) {
            std::string s;
            for (std::size_t i = 0; i < words.size(); ++i)
                if (i != skip) s += (s.empty() ? "" : " ") + words[i];
            return s;
        };
        if (words.size() >= 2) {
            auto docs = lib_.titled(join(words.size()), {"qa"}, 6);
            for (std::size_t skip = 0; docs.size() < 2 && words.size() >= 3 && skip < words.size(); ++skip) {
                auto more = lib_.titled(join(skip), {"qa"}, 4);
                docs.insert(docs.end(), more.begin(), more.end());
            }
            // The title closest to the question's own words first.
            std::stable_sort(docs.begin(), docs.end(), [&](const DocRef& a, const DocRef& b) {
                return jaccard(content_words(a.title), words) > jaccard(content_words(b.title), words);
            });
            if (docs.size() > 4) docs.resize(4);
            for (const auto& d : docs) {
                Passage head = lib_.lead_of(d.shard, d.doc_id);  // the thread's question
                if (head.doc_id) {
                    head.named = true;
                    ps.push_back(std::move(head));
                }
            }
        }
    }
    // "the capital of france": when no document is titled after the whole
    // subject, the thing after "of" is the document to read.
    std::string attr_word, entity_key;
    // "the temperature on the moon", "population in tokyo" read the same way.
    std::size_t of = std::string::npos, link = 0;
    // "the chemical symbol for gold" reads like "of".
    for (const char* sep : {" of ", " for ", " on ", " in "})
        if (std::strcmp(sep, " of ") == 0 || std::strcmp(sep, " for ") == 0 || r.frame == Frame::Define)
            if (of == std::string::npos && (of = r.subject.find(sep)) != std::string::npos) link = std::strlen(sep);
    if (layer("of") && of != std::string::npos && r.frame != Frame::HowTo) {  // "make a cup of tea" is a task, not a thing's part
        bool exact = false;
        for (const auto& p : ps) exact |= p.title_score >= 0.99;
        if (!exact) {
            std::string entity = r.subject.substr(of + link);
            if (entity.rfind("the ", 0) == 0) entity = entity.substr(4);
            attr_word = r.subject.substr(0, of);
            entity_key = title_key(entity);
            auto doc = lib_.by_title(entity, {"encyclopedia"}, 6);
            for (auto& d : doc) d.title_score = 0.9;
            ps.insert(ps.begin(), doc.begin(), doc.end());
        }
    }
    lap("titles");
    if (ps.empty()) return out;
    if (layer("rerank") && !by_meaning) {
        rerank(r, q.words, ps, meaning);
        // The right article is often found by a passage that does not hold
        // the answer: search again inside the top five articles, and let the
        // passages found there compete.
        if (layer("inside") && lib_.has_ranked()) {
            std::vector<DocRef> docs;
            std::set<std::pair<int, int64_t>> seen_doc;
            for (const auto& p : ps)
                if (p.shard >= 0 && seen_doc.insert({p.shard, p.doc_id}).second && docs.size() < 5)
                    docs.push_back({p.shard, p.doc_id, p.title, false, 0});
            std::string words;
            for (const auto& w : q.words) words += (words.empty() ? "" : " ") + w;
            std::set<std::pair<std::string, int64_t>> have;
            for (const auto& p : ps) have.insert({p.pack, p.pid});
            bool added = false;
            for (auto& p : lib_.ranked_in(words, docs, 15))
                if (have.insert({p.pack, p.pid}).second) {
                    ps.push_back(std::move(p));
                    added = true;
                }
            if (added) rerank(r, q.words, ps, meaning);
        }
    } else {  // plain order; the score is the search's own, scaled to the best
        double top_bm25 = 1e-9;
        for (const auto& p : ps) top_bm25 = std::max(top_bm25, p.bm25);
        for (auto& p : ps) p.score = p.title_score >= 0.99 || p.named ? 1.0 : p.bm25 / top_bm25;
    }
    lap("rerank");
    // Measuring layer "twostage": the list so far ranks articles; the top K distinct articles
    // (UNLLM_TWOSTAGE_K, default 3) are searched again on their own, and that search's order
    // replaces the list (its settings: UNLLM_INNER_TITLE_BOOST, UNLLM_INNER_MIN_MATCH).
    if (layer("twostage") && !by_meaning && lib_.has_ranked()) {
        static const int topk = std::getenv("UNLLM_TWOSTAGE_K") ? std::atoi(std::getenv("UNLLM_TWOSTAGE_K")) : 3;
        std::vector<DocRef> docs;
        std::set<std::pair<int, int64_t>> seen_doc;
        for (const auto& p : ps)
            if (p.shard >= 0 && seen_doc.insert({p.shard, p.doc_id}).second && int(docs.size()) < topk)
                docs.push_back({p.shard, p.doc_id, p.title, false, 0});
        std::string words;
        for (const auto& w : q.words) words += (words.empty() ? "" : " ") + w;
        auto inner = lib_.ranked_in(words, docs, 50);
        if (!inner.empty()) {
            // UNLLM_STAGE2 (measuring): "text" = the search's order; "lead" = the reranker's
            // text and opening-paragraph terms only, same weights (0.40 and 0.05); "rerank" =
            // the full reranker.
            static const std::string mode = std::getenv("UNLLM_STAGE2") ? std::getenv("UNLLM_STAGE2") : "text";
            if (mode == "rerank") {
                rerank(r, q.words, inner, meaning);
            } else if (mode == "lead") {
                double top = 1e-9;
                for (const auto& p : inner) top = std::max(top, p.bm25);
                for (auto& p : inner) p.score = 0.40 * (p.bm25 / top) + 0.05 * (p.role == "lead" ? 1.0 : 0.3);
                std::stable_sort(inner.begin(), inner.end(), [](const Passage& a, const Passage& b) { return a.score > b.score; });
            } else {
                for (std::size_t k = 0; k < inner.size(); ++k) inner[k].score = 1.0 - 0.001 * double(k);
            }
            ps = std::move(inner);
        }
    }
    // UNLLM_DEBUG=1: the ranking and the reason an answer ends, on stderr.
    static const bool debug = std::getenv("UNLLM_DEBUG") != nullptr;
    static const std::size_t debug_ranks = std::getenv("UNLLM_DEBUG_RANKS") ? std::size_t(std::atoi(std::getenv("UNLLM_DEBUG_RANKS"))) : 6;
    if (debug)
        for (std::size_t k = 0; k < ps.size() && k < debug_ranks; ++k)
            std::fprintf(stderr, "  rank %zu: %.3f %s/%s \"%s\" bm25 %.2f late %.2f title %.2f%s%s\n", k, ps[k].score,
                         ps[k].pack.c_str(), ps[k].role.c_str(), ps[k].title.substr(0, 40).c_str(), ps[k].bm25,
                         ps[k].late, ps[k].title_score, ps[k].phrase ? " phrase" : "", ps[k].named ? " named" : "");
    // The article named exactly after the subject leads, opening passage
    // first, unless the question asked what a word means.
    if (layer("leadfirst") && (!meaning || r.frame != Frame::Define)) {
        for (std::size_t k = 0; k < ps.size(); ++k)
            if ((ps[k].title_score >= 0.99 || ps[k].named) && ps[k].role == "lead" && ps[k].kind == "encyclopedia") {
                std::rotate(ps.begin(), ps.begin() + long(k), ps.begin() + long(k) + 1);
                ps[0].score = std::max(ps[0].score, ps.size() > 1 ? ps[1].score + 0.01 : ps[0].score);
                break;
            }
    }
    if (layer("attr") && !attr_word.empty()) {
        // The entity's own article first ("Moon", not "Sun Moon Lake"), and
        // within it the passages that mention the attribute.
        auto rank = [&](const Passage& a) {
            bool own = !entity_key.empty() && a.kind == "encyclopedia" && title_key(a.title) == entity_key;
            bool mentions = a.title_score >= 0.9 && lower(a.text).find(attr_word) != std::string::npos;
            return (a.phrase ? 4 : 0) + (own ? 2 : 0) + (mentions ? 1 : 0);
        };
        std::stable_sort(ps.begin(), ps.end(), [&](const Passage& a, const Passage& b) { return rank(a) > rank(b); });
    }
    if (best) {
        best->clear();
        // UNLLM_TOP=N (measuring only): the whole ranked list, N long, no one-per-document filter.
        static const int whole = std::getenv("UNLLM_TOP") ? std::atoi(std::getenv("UNLLM_TOP")) : 0;
        std::set<std::pair<std::string, int64_t>> seen;  // one passage per document
        for (const auto& p : ps) {
            if (whole > 0) {
                if (int(best->size()) < whole) best->push_back(p);
                continue;
            }
            if (quotable(p.role) && seen.insert({p.pack, p.doc_id}).second && best->size() < 20) best->push_back(p);
        }
    }
    // A short answer voted on across the passages; a question of that
    // shape with no supported winner is not guessed at ("abstain").
    // The reader model's answer, when it is installed ("reader"); with
    // "abstain", a question it read but could not answer is not guessed at.
    if (layer("reader")) {
        bool ran = false;
        WorldAnswer x = read_answer(question, ps, &ran);
        if (x.kind != WorldAnswer::None) return x;
        if (ran && layer("abstain")) return WorldAnswer{};
    }
    if (layer("extract")) {
        bool ran = false;
        WorldAnswer x = extract(r, question, ps, &ran);
        if (x.kind != WorldAnswer::None) return x;
        if (ran && layer("abstain")) return WorldAnswer{};
    }
    // The pipeline's reader: the local model answers from the top 3 passages
    // and names the one it read; its answer is shown with that passage's
    // attribution. No model, or none that finds the answer stated: the top
    // passage is quoted below, as before.
    if (pipeline && model_ && model_->enabled()) {
        std::vector<std::string> docs;
        for (std::size_t k = 0; k < ps.size() && k < 5; ++k) docs.push_back(ps[k].title + ": " + squeeze(ps[k].text));
        auto a = model_->ask(question, docs);
        lap("reader");
        if (a && !a->answer.empty()) {
            const Passage& c = a->passage >= 1 && std::size_t(a->passage) <= docs.size() ? ps[std::size_t(a->passage) - 1] : ps[0];
            out.kind = WorldAnswer::Answer;
            out.text = a->answer + " " + attribution(c);
            out.used = {c};
            out.receipts = {receipt(c)};
            out.confidence = 1;
            out.by_model = true;
            return out;
        }
    }
    const Passage& top = ps[0];

    // How much of the question the best passage covers, word by word.
    double coverage = 0;
    {
        std::set<std::string> pw;
        for (const auto& t : tokenize(top.title + " " + top.text)) pw.insert(t.lower);
        int hit = 0;
        for (const auto& w : q.words) hit += pw.count(w) ? 1 : 0;
        coverage = double(hit) / double(q.words.size());
    }
    double margin = ps.size() > 1 && top.score > 0 ? (top.score - ps[1].score) / top.score : 1.0;
    out.confidence = 0.5 * top.score + 0.3 * std::min(1.0, margin * 3) + 0.2 * coverage;
    if (top.title_score >= 0.99) out.confidence = std::max(out.confidence, 0.65);
    if (out.confidence < 0.35) return out;

    // Poems: a real one, never a new one.
    if (r.frame == Frame::Poem) {
        out.kind = WorldAnswer::Poem;
        out.text = "Here is an excerpt from " + top.title + (top.author.empty() ? "" : " by " + top.author) +
                   " (Project Gutenberg), a real poem; I don't write new ones:\n\n" + top.text;
        out.used = {top};
        out.receipts = {receipt(top)};
        return out;
    }
    // Any question a Q&A thread answers best gets that answer from its
    // start, commands included; numbered when the question asked how.
    if (layer("qa") && top.kind == "qa") {
        const bool numbered = r.frame == Frame::HowTo;
        Passage start = top;
        start.ord = 0;  // the thread's question; its answer follows
        std::vector<Passage> steps = lib_.neighbors(start, 6);
        std::string text;
        int n = 0;
        for (const auto& s : steps) {
            if (s.role == "question") continue;
            ++n;
            // A command block's later lines sit under its step number.
            std::string body = s.text;
            for (std::size_t at = 0; numbered && (at = body.find('\n', at)) != std::string::npos; at += 4)
                body.replace(at, 1, "\n   ");
            std::string line = numbered ? std::to_string(n) + ". " + body : body;
            if (text.size() + line.size() > 700 && n > 1) break;
            text += (text.empty() ? "" : "\n") + line;
            if (n >= 5) break;
        }
        if (!text.empty()) {
            out.kind = numbered ? WorldAnswer::HowTo : WorldAnswer::Answer;
            out.text = text + "\n" + attribution(steps.empty() ? top : steps[0]);
            out.used = {top};
            out.receipts = {receipt(top)};
            if (out.confidence < 0.6) out.text = "The closest thing I know is this:\n" + out.text;
            return out;
        }
    }
    // Definitions: the dictionary's senses for the word.
    if (layer("defs") && top.kind == "definitions") {
        std::vector<Passage> senses = {top};
        auto next = lib_.neighbors(top, 1);
        senses.insert(senses.end(), next.begin(), next.end());
        std::string text;
        for (const auto& s : senses) text += (text.empty() ? "" : " ") + capitalize(s.text);
        out.kind = WorldAnswer::Definition;
        out.text = text + " " + attribution(top);
        out.used = senses;
        out.receipts = {receipt(top)};
        if (out.confidence < 0.6) out.text = "The closest thing I know is: " + out.text;
        return out;
    }

    // Everything else: the best DOCUMENT first, then the sentence of that
    // document the question's shape asks for, then up to two more sentences
    // from it, in the document's own order. Another document only fills in
    // when the first has nothing more to say.
    // "the speed of light", "the boiling point of water": a measurement,
    // whatever the question word.
    Wants want = r.wants;
    {
        static const std::set<std::string> measures = {
            "speed", "distance", "height", "weight", "mass", "temperature", "population", "area", "length", "depth",
            "diameter", "radius", "age", "size", "density", "circumference", "volume", "boiling", "melting",
            "freezing", "elevation", "altitude", "width"};
        const auto sw = content_words(attr_word.empty() ? r.subject : attr_word);
        if ((want == Wants::Fact || want == Wants::Name) && !sw.empty() && measures.count(sw.front()))
            want = Wants::Quantity;
    }
    struct Cand { std::string s; double score; const Passage* p; int order; };
    std::vector<Cand> sents;
    std::vector<const Passage*> pool;
    // The document's opening passage always takes part, then its best others.
    for (const auto& p : ps)
        if (p.pack == top.pack && p.doc_id == top.doc_id && p.role == "lead") pool.push_back(&p);
    // A Q&A thread answers with its answer; the question text only found it.
    // Tables and code are found by search but never quoted as sentences.
    for (const auto& p : ps)
        if (p.pack == top.pack && p.doc_id == top.doc_id && p.role != "lead" && quotable(p.role) && pool.size() < 6)
            pool.push_back(&p);
    // The best document's opening passages always take part: key facts sit
    // there ("There are eight planets in our Solar System").
    std::vector<Passage> opening;
    if (layer("opening") && top.kind == "encyclopedia") {
        Passage start = top;
        start.ord = -1;
        for (auto& o : lib_.neighbors(start, 3))
            if (quotable(o.role)) opening.push_back(std::move(o));
        for (const auto& o : opening) {
            bool have = false;
            for (const auto* p : pool) have |= p->pid == o.pid;
            if (!have) pool.push_back(&o);
        }
    }
    // A one-passage hit still answers with its context: the next passages.
    std::vector<Passage> more;
    if (pool.size() < 2) {
        more = lib_.neighbors(top, 4);
        for (const auto& m : more)
            if (quotable(m.role) && pool.size() < 3) pool.push_back(&m);
    }
    for (std::size_t k = 0; k < ps.size() && k < 3; ++k)
        if ((ps[k].pack != top.pack || ps[k].doc_id != top.doc_id) && quotable(ps[k].role)) pool.push_back(&ps[k]);
    for (std::size_t k = 0; k < pool.size(); ++k) {
        const Passage& P = *pool[k];
        const bool primary = P.pack == top.pack && P.doc_id == top.doc_id;
        auto ss = sentences_of(P.text);
        for (std::size_t i = 0; i < ss.size(); ++i) {
            // A section heading glued to its first sentence ("History Alexander...").
            for (const char* h : {"History ", "Background ", "Construction ", "Overview ", "Description ", "Biography ",
                                  "Career ", "Events ", "Uses ", "Etymology ", "Geography ", "Origins ", "Origin "})
                if (ss[i].rfind(h, 0) == 0 && ss[i].size() > std::strlen(h) + 1 &&
                    std::isupper(static_cast<unsigned char>(ss[i][std::strlen(h)])))
                    ss[i] = ss[i].substr(std::strlen(h));
            // Editorial notes about the article itself are not answers.
            bool note = false;
            for (const char* h : {"This article is about", "This page is about", "For other uses", "For the ",
                                  "See also", "Not to be confused", "Main article", "This article", "This page "})
                note |= ss[i].rfind(h, 0) == 0;
            if (note) continue;
            if (number_hole(ss[i]) || (ss[i].size() > 1 && ss[i].back() == ':')) continue;
            ss[i] = tidy(ss[i]);
            if (fragment(ss[i])) continue;
            double fit = 0;
            switch (want) {
            case Wants::Time: fit = has_year_or_date(ss[i]) ? 1 : 0; break;
            case Wants::Quantity:
            case Wants::Count:
                fit = has_quantity(ss[i]) ? 0.5 : 0;
                if (measure_words) {
                    const std::string l = " " + lower(ss[i]) + " ";
                    bool on_measure = false;
                    for (const auto& m : *measure_words) on_measure |= l.find(" " + m + " ") != std::string::npos;
                    fit += on_measure ? (fit > 0 ? 1.0 : 0.3) : 0;
                } else {
                    fit *= 2;
                }
                break;
            case Wants::Cause:
                fit = has_cause(ss[i]) ? 1 : 0;
                if (measure_words) {
                    const std::string l = " " + lower(ss[i]) + " ";
                    for (const auto& m : *measure_words)
                        if (l.find(" " + m + " ") != std::string::npos) {
                            fit += 0.8;
                            break;
                        }
                }
                break;
            case Wants::Name: {
                // a capitalized name that is not the subject itself
                auto subj = content_words(r.subject);
                std::set<std::string> sj(subj.begin(), subj.end());
                auto toks = tokenize(ss[i]);
                for (std::size_t z = 1; z < toks.size(); ++z)
                    if (std::isupper(static_cast<unsigned char>(toks[z].text[0])) && !sj.count(toks[z].lower)) fit = 1;
                break;
            }
            default: fit = (primary && P.role == "lead" && i == 0 && attr_word.empty()) ? 1 : 0; break;
            }
            if (!attr_word.empty() && lower(ss[i]).find(attr_word) != std::string::npos) fit += 1;
            if (primary && P.role == "lead" && i == 0) fit += 0.3;  // the article's own opening line
            // words of the question the sentence itself contains
            std::set<std::string> sw;
            for (const auto& t : tokenize(ss[i])) {
                sw.insert(t.lower);
                sw.insert(normalize_word(t.lower));
                sw.insert(base_form(t.lower));
            }
            double cover = 0;
            for (const auto& w : q.words) cover += sw.count(w) || sw.count(normalize_word(w)) || sw.count(base_form(w)) ? 1 : 0;
            cover /= double(q.words.size());
            // "who painted the mona lisa": the sentence with the question's
            // own verb ("painted") holds the name, not the opening line.
            double cover_weight = r.wants == Wants::Name || r.wants == Wants::Time ? 1.0 : 0.4;
            // The best document's sentences outrank everyone else's.
            double sc = (primary ? 1.0 : 0.0) + 0.5 * fit + cover_weight * cover - 0.03 * double(i) -
                        0.02 * double(P.ord);
            sents.push_back({ss[i], sc, &P, int(P.ord * 1000 + int(i))});
        }
    }
    if (sents.empty()) return out;
    std::vector<Cand> ranked = sents;
    std::stable_sort(ranked.begin(), ranked.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    std::vector<Cand> chosen = {ranked[0]};
    std::size_t length = ranked[0].s.size();
    auto words_of = [](const std::string& s) {
        std::vector<std::string> v;
        for (const auto& t : tokenize(s))
            if (!stop_word(t.lower)) v.push_back(t.lower);
        return v;
    };
    for (std::size_t i = 1; i < ranked.size() && chosen.size() < 3; ++i) {
        const Cand& c = ranked[i];
        if (length + c.s.size() > 600) continue;
        if (starts_with_pronoun(c.s) && c.p->doc_id != chosen[0].p->doc_id) continue;
        bool redundant = false;
        for (const auto& k : chosen) redundant |= jaccard(words_of(k.s), words_of(c.s)) >= 0.5;
        if (redundant || c.score < ranked[0].score - 0.9) continue;
        chosen.push_back(c);
        length += c.s.size() + 1;
    }
    // The answering sentence first; then the best document's other
    // sentences in its own order; other documents last.
    std::stable_sort(chosen.begin() + 1, chosen.end(), [&](const Cand& a, const Cand& b) {
        const bool pa = a.p->pack == top.pack && a.p->doc_id == top.doc_id;
        const bool pb = b.p->pack == top.pack && b.p->doc_id == top.doc_id;
        if (pa != pb) return pa;
        return a.order < b.order;
    });
    std::string text;
    for (const auto& c : chosen) text += (text.empty() ? "" : " ") + c.s;
    out.kind = out.confidence >= 0.6 ? WorldAnswer::Answer : WorldAnswer::Closest;
    // A weak match must at least talk about what was asked: half of the
    // question's words in the sentences shown, or no answer at all.
    if (layer("floor") && !by_meaning && out.kind == WorldAnswer::Closest && !q.words.empty()) {
        std::set<std::string> tw;
        // Long questions rarely have half their words in two sentences:
        // "floorwide" counts the passages and titles the sentences came from.
        std::string seen_text = text;
        if (layer("floorwide"))
            for (const auto& c : chosen) seen_text += " " + c.p->title + " " + c.p->text;
        for (const auto& t : tokenize(seen_text)) {
            tw.insert(t.lower);
            tw.insert(normalize_word(t.lower));
        }
        std::size_t hit = 0;
        for (const auto& w : q.words) hit += tw.count(w) || tw.count(normalize_word(w)) ? 1 : 0;
        if (debug) std::fprintf(stderr, "  weak answer: %zu of %zu question words shown\n", hit, q.words.size());
        if (hit * 2 < q.words.size()) return WorldAnswer{};
        // One word in common is a coincidence, unless the article is the subject's own.
        bool own = top.title_score >= 0.99 || top.named;
        if (!own) {  // "Tide" for "what causes tides": the article is about a word of the question
            const auto tw = content_words(top.title);
            own = !tw.empty();
            for (const auto& t : tw) {
                bool in = false;
                for (const auto& w : q.words) in |= normalize_word(w) == normalize_word(t);
                own &= in;
            }
        }
        if (hit < 2 && !own) return WorldAnswer{};
    }
    std::set<std::string> cited;
    std::string cites;
    for (const auto& c : chosen) {
        if (cited.insert(c.p->title + "|" + c.p->pack).second) {
            cites += (cites.empty() ? "" : " ") + attribution(*c.p);
            out.receipts.push_back(receipt(*c.p));
            out.used.push_back(*c.p);
        }
    }
    out.text = (out.kind == WorldAnswer::Closest ? "The closest thing I know is: " : "") + text + " " + cites;
    return out;
}

WorldAnswer World::define(const std::string& word) const {
    Reading r;
    r.frame = Frame::Define;
    r.subject = word;
    return answer(r, "what is " + word);
}

WorldAnswer World::more(const Passage& p) const {
    WorldAnswer out;
    std::vector<Passage> next;
    for (auto& n : lib_.neighbors(p, 6))
        if (quotable(n.role) && next.size() < 2) next.push_back(std::move(n));
    if (next.empty()) return out;
    std::string text;
    for (const auto& n : next) text += (text.empty() ? "" : " ") + n.text;
    if (text.size() > 900) {
        auto ss = sentences_of(text);
        text.clear();
        for (const auto& s : ss) {
            if (text.size() + s.size() > 900) break;
            text += (text.empty() ? "" : " ") + s;
        }
    }
    out.kind = WorldAnswer::Answer;
    out.text = text + " " + attribution(next[0]);
    out.used = next;
    out.receipts = {receipt(next[0])};
    out.confidence = 1;
    return out;
}

}  // namespace unllm
