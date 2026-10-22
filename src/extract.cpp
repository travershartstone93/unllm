#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>

#include "unllm/extract.h"
#include "unllm/knowledge.h"

// The answer extractor's pieces. A sentence is cut into words once, by a
// small scanner that keeps byte offsets, and every finder works on those
// words: capitalized runs for names and titles, digits and number words for
// amounts, date_key() for dates. No regular expressions: a sentence costs a
// few microseconds.

namespace unllm {

namespace {

std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(trim(cur));
    return out;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return is_digit(c); });
}

// "2000000", "1.5", "-500": no grouping, no trailing zeros.
std::string fmt(double v) {
    char buf[64];
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15)
        std::snprintf(buf, sizeof buf, "%.0f", std::round(v));
    else
        std::snprintf(buf, sizeof buf, "%.12g", v);
    return buf;
}

// ---- the word scanner ----

// UTF-8 punctuation that matters in prose: curly quotes and apostrophes,
// dashes, the no-break space. Returns the plain mark it stands for (' ' for
// a space, '~' for other punctuation), or 0 when the bytes are a letter.
char utf8_mark(const std::string& s, std::size_t i, std::size_t& len) {
    unsigned char b0 = static_cast<unsigned char>(s[i]);
    len = 1;
    if (b0 < 0x80) return 0;
    unsigned char b1 = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
    unsigned char b2 = i + 2 < s.size() ? static_cast<unsigned char>(s[i + 2]) : 0;
    if (b0 == 0xC2 && b1 == 0xA0) return len = 2, ' ';
    if (b0 == 0xC2 && (b1 == 0xAB || b1 == 0xBB)) return len = 2, '"';
    if (b0 == 0xE2 && b1 == 0x80) {
        len = 3;
        if (b2 == 0x98 || b2 == 0x99) return '\'';
        if (b2 == 0x9C || b2 == 0x9D) return '"';
        if (b2 >= 0x90 && b2 <= 0x95) return '-';
        return '~';
    }
    if (b0 == 0xE2 && b1 == 0x88 && b2 == 0x92) return len = 3, '-';  // minus sign
    // A letter: step over the whole code point.
    if (b0 >= 0xF0) len = 4;
    else if (b0 >= 0xE0) len = 3;
    else if (b0 >= 0xC0) len = 2;
    if (i + len > s.size()) len = s.size() - i;
    return 0;
}

bool letter_at(const std::string& s, std::size_t i) {
    if (i >= s.size()) return false;
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return std::isalnum(c) != 0;
    std::size_t len;
    return utf8_mark(s, i, len) == 0;
}

bool alpha_at(const std::string& s, std::size_t i) {
    if (i >= s.size()) return false;
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return std::isalpha(c) != 0;
    std::size_t len;
    return utf8_mark(s, i, len) == 0;
}

// Capitals outside ASCII: Latin-1, Latin Extended-A, Greek, Cyrillic.
bool utf8_upper(const std::string& s, std::size_t i) {
    unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0xC3 || b0 > 0xD4 || i + 1 >= s.size()) return false;
    unsigned cp = ((b0 & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
    if (cp >= 0xC0 && cp <= 0xDE) return cp != 0xD7;
    if (cp >= 0x100 && cp <= 0x17F) return cp % 2 == 0;
    if (cp >= 0x391 && cp <= 0x3A9) return true;
    if (cp >= 0x400 && cp <= 0x42F) return true;
    return false;
}

struct Tok {
    std::size_t start = 0, end = 0;
    std::string lower;  // a word lowercased, or the punctuation mark ("," "'s" "-")
    bool word = false;
    bool cap = false;          // starts with a capital letter
    bool digits = false;       // "14", "2,000,000", "1.5"
    bool plain = false;        // digits only, no comma or point
    bool space_before = true;  // whitespace or the start of the text before it
};

// An apostrophe (straight or curly) at i, and its byte length.
bool apostrophe_at(const std::string& s, std::size_t i, std::size_t& len) {
    len = 1;
    if (i >= s.size()) return false;
    if (s[i] == '\'') return true;
    return utf8_mark(s, i, len) == '\'';
}

std::vector<Tok> scan(const std::string& s) {
    std::vector<Tok> out;
    std::size_t i = 0, n = s.size();
    bool space = true;
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        char mark = c < 0x80 ? 0 : utf8_mark(s, i, len);
        if (std::isspace(c) || mark == ' ') {
            space = true;
            i += len;
            continue;
        }
        // "'s" after a word or a name: its own token, so a possessive ends a
        // name and a statement's "{X}'s" can match it.
        std::size_t alen;
        if (apostrophe_at(s, i, alen) && i + alen < n && (s[i + alen] == 's' || s[i + alen] == 'S') &&
            !alpha_at(s, i + alen + 1)) {
            Tok t;
            t.start = i;
            t.end = i + alen + 1;
            t.lower = "'s";
            t.space_before = space;
            out.push_back(t);
            space = false;
            i = t.end;
            continue;
        }
        if (!letter_at(s, i)) {
            Tok t;
            t.start = i;
            t.end = i + len;
            char m = c < 0x80 ? char(c) : mark;
            if (m == '\'' && len == 1) m = '\'';
            t.lower = std::string(1, m);
            t.space_before = space;
            out.push_back(t);
            space = false;
            i += len;
            continue;
        }
        Tok t;
        t.start = i;
        t.word = true;
        t.space_before = space;
        t.cap = (c >= 'A' && c <= 'Z') || utf8_upper(s, i);
        bool numeric = is_digit(char(c)), point = false, comma = false;
        std::size_t j = i;
        while (j < n) {
            std::size_t l = 1;
            unsigned char d = static_cast<unsigned char>(s[j]);
            if (d < 0x80 ? std::isalnum(d) != 0 : utf8_mark(s, j, l) == 0) {
                if (!is_digit(char(d))) numeric = false;
                j += l;
                continue;
            }
            bool prev_digit = j > i && is_digit(s[j - 1]);
            if (d == '-' && j > i && letter_at(s, j - 1) && letter_at(s, j + 1)) {
                numeric = false;
                ++j;
                continue;
            }
            if (d == '/' && j > i && letter_at(s, j - 1) && letter_at(s, j + 1)) {
                numeric = false;
                ++j;
                continue;
            }
            if (d == '.' && numeric && !point && prev_digit && j + 1 < n && is_digit(s[j + 1])) {
                point = true;
                ++j;
                continue;
            }
            // A comma joins digit groups only when exactly three digits
            // follow it: "2,000,000", but not "1990, 300" or "12,3456".
            if (d == ',' && numeric && !point && prev_digit && j + 3 < n && is_digit(s[j + 1]) &&
                is_digit(s[j + 2]) && is_digit(s[j + 3]) && !letter_at(s, j + 4)) {
                comma = true;
                j += 4;
                continue;
            }
            std::size_t al;
            if (apostrophe_at(s, j, al) && j > i && alpha_at(s, j - 1) && alpha_at(s, j + al)) {
                // "'s" closing a word is split off above; "O'Brien" and
                // "don't" stay whole.
                if ((s[j + al] == 's' || s[j + al] == 'S') && !alpha_at(s, j + al + 1)) break;
                numeric = false;
                j += al;
                continue;
            }
            break;
        }
        t.end = j;
        t.lower = lower(s.substr(i, j - i));
        t.digits = numeric;
        t.plain = numeric && !point && !comma;
        out.push_back(t);
        space = false;
        i = j;
    }
    return out;
}

// ---- small word tables ----

const std::set<std::string>& month_names() {
    static const std::set<std::string> s = {"january", "february", "march",     "april",   "may",      "june",
                                            "july",    "august",   "september", "october", "november", "december"};
    return s;
}

bool month_or_weekday(const std::string& w) {
    static const std::set<std::string> days = {"monday", "tuesday", "wednesday", "thursday",
                                               "friday", "saturday", "sunday"};
    return month_names().count(w) > 0 || days.count(w) > 0;
}

// 1..12 for a month name, or its 3 or 4 letter abbreviation; 0 otherwise.
int month_number(const std::string& w) {
    static const char* names[] = {"january", "february", "march",     "april",   "may",      "june",
                                  "july",    "august",   "september", "october", "november", "december"};
    for (int m = 0; m < 12; ++m) {
        std::string full = names[m];
        if (w == full) return m + 1;
        if ((w.size() == 3 || w.size() == 4) && full.compare(0, w.size(), w) == 0) return m + 1;
    }
    return 0;
}

bool pronoun(const std::string& w) {
    static const std::set<std::string> s = {"he",  "she",   "it",    "they",  "his", "her",
                                            "its", "their", "this", "these", "those"};
    return s.count(w) > 0;
}

const std::set<std::string>& org_suffixes() {
    static const std::set<std::string> s = {
        "studio", "studios", "records", "inc",     "ltd",    "company", "corporation", "group",      "band",
        "club",   "fc",      "united",  "university", "college", "school", "academy",  "institute",  "party",
        "airlines", "bank",  "museum",  "hospital", "church", "society", "association", "council", "foundation",
        "choir",  "orchestra"};
    return s;
}

// Last words that make a run a place, not a person. "Park" is left out: it
// is a common surname ("Lee Park"), and a park is rarely the answer to a
// "who" question anyway.
const std::set<std::string>& place_suffixes() {
    static const std::set<std::string> s = {
        "city",   "town",   "river",  "lake",   "sea",     "ocean",  "bay",    "island",  "islands", "mountain",
        "mountains", "hill", "hills", "valley", "street",  "road",   "square", "hall",    "bridge",
        "tower",  "castle", "palace", "stadium", "arena",  "theatre", "point", "coast",   "desert",  "forest",
        "county", "province", "republic", "kingdom"};
    return s;
}

const std::set<std::string>& place_prefixes() {
    static const std::set<std::string> s = {"mount", "lake", "port", "fort", "cape", "glen", "isle", "gulf", "strait"};
    return s;
}

const std::set<std::string>& company_suffixes() {
    static const std::set<std::string> s = {"inc",   "ltd",  "records", "company", "corporation", "party",
                                            "airlines", "band", "choir", "orchestra", "fc", "group"};
    return s;
}

const std::set<std::string>& joiners(AnswerType t) {
    static const std::set<std::string> person = {"de", "van", "von", "der", "den", "da", "del", "di",
                                                 "du", "la",  "le",  "bin", "ibn", "al", "ap"};
    static const std::set<std::string> place = {"of", "the", "de", "la", "upon", "on", "del"};
    static const std::set<std::string> org = {"of", "the", "and", "for", "de", "&"};
    static const std::set<std::string> title = {"of", "the", "and", "in", "on", "a", "an",
                                                "to", "for", "at", "with", "from"};
    switch (t) {
    case AnswerType::Person: return person;
    case AnswerType::Place: return place;
    case AnswerType::Organization: return org;
    default: return title;
    }
}

int small_number(const std::string& w) {
    static const char* names[] = {"zero",    "one",     "two",       "three",    "four",    "five",    "six",
                                  "seven",   "eight",   "nine",      "ten",      "eleven",  "twelve",  "thirteen",
                                  "fourteen", "fifteen", "sixteen",  "seventeen", "eighteen", "nineteen"};
    for (int i = 0; i < 20; ++i)
        if (w == names[i]) return i;
    return -1;
}

int tens_number(const std::string& w) {
    static const char* names[] = {"twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
    for (int i = 0; i < 8; ++i)
        if (w == names[i]) return 20 + 10 * i;
    return -1;
}

double scale_number(const std::string& w) {
    if (w == "thousand") return 1e3;
    if (w == "million") return 1e6;
    if (w == "billion") return 1e9;
    if (w == "trillion") return 1e12;
    return 0;
}

// Words a spelled-out number can be made of ("twenty-one" counts when both
// halves do).
bool number_vocabulary(const std::string& w) {
    if (small_number(w) >= 0 || tens_number(w) >= 0 || scale_number(w) > 0) return true;
    if (w == "hundred" || w == "and" || w == "a" || w == "an" || w == "half" || w == "dozen") return true;
    std::size_t h = w.find('-');
    if (h != std::string::npos && h > 0)
        return number_vocabulary(w.substr(0, h)) && number_vocabulary(w.substr(h + 1));
    return false;
}

// Time units for durations, singular -> itself, plural -> singular.
std::string time_unit(const std::string& w) {
    static const std::map<std::string, std::string> m = {
        {"second", "second"},     {"seconds", "second"}, {"minute", "minute"}, {"minutes", "minute"},
        {"hour", "hour"},         {"hours", "hour"},     {"day", "day"},       {"days", "day"},
        {"week", "week"},         {"weeks", "week"},     {"fortnight", "fortnight"}, {"fortnights", "fortnight"},
        {"month", "month"},       {"months", "month"},   {"year", "year"},     {"years", "year"},
        {"decade", "decade"},     {"decades", "decade"}, {"century", "century"}, {"centuries", "century"}};
    auto it = m.find(w);
    return it == m.end() ? "" : it->second;
}

// ---- units for numbers (data/units.txt) ----

struct UnitTable {
    std::map<std::string, std::string> display;  // alias -> display name
    std::size_t longest = 1;
    bool loaded = false;
};

UnitTable& unit_table() {
    static UnitTable t;
    return t;
}

bool read_units(const std::string& path, UnitTable& t) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto cols = split(line, '|');
        if (cols.size() < 5) continue;
        for (const auto& alias : split(cols[0], ';')) {
            std::string a = lower(alias);
            // "in" (inches) would swallow "300 in 1990": a unit never
            // stands where a stop word could.
            if (a.empty() || stop_word(a)) continue;
            t.display[a] = cols[4];
            t.longest = std::max(t.longest, std::size_t(std::count(a.begin(), a.end(), ' ') + 1));
        }
    }
    t.loaded = !t.display.empty();
    return t.loaded;
}

const UnitTable& units() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (!unit_table().loaded) read_units("data/units.txt", unit_table());
    });
    return unit_table();
}

// The unit that starts at token j, longest alias first ("light years"
// before "light"). Returns how many tokens it takes, 0 for none.
std::size_t unit_at(const std::string& s, const std::vector<Tok>& t, std::size_t j, std::string& display) {
    const UnitTable& u = units();
    if (j >= t.size() || !t[j].word) return 0;
    for (std::size_t len = std::min(u.longest, t.size() - j); len >= 1; --len) {
        std::string key;
        bool ok = true;
        for (std::size_t k = j; k < j + len; ++k) {
            if (!t[k].word || (k > j && !t[k].space_before)) {
                ok = false;
                break;
            }
            if (k > j) key += ' ';
            key += t[k].lower;
        }
        if (ok) {
            auto it = u.display.find(key);
            // A one-letter unit must be written lowercase: "5 m", but not
            // the "C" of a name.
            if (it != u.display.end() && !(key.size() == 1 && std::isupper(static_cast<unsigned char>(s[t[j].start])))) {
                display = it->second;
                return len;
            }
        }
    }
    return 0;
}

// A lowercase plural noun that can follow a number: "people", "visitors".
bool plural_noun(const Tok& t) {
    static const std::set<std::string> irregular = {"people", "men",   "women", "children", "cattle",
                                                    "mice",   "geese", "teeth", "police",   "staff"};
    if (!t.word || t.cap || t.digits) return false;
    const std::string& w = t.lower;
    if (irregular.count(w)) return true;
    if (w.size() < 3 || w.back() != 's') return false;
    if (w.compare(w.size() - 2, 2, "ss") == 0 || w.compare(w.size() - 2, 2, "us") == 0 ||
        w.compare(w.size() - 2, 2, "is") == 0)
        return false;
    if (!std::all_of(w.begin(), w.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '-'; }))
        return false;
    return !stop_word(w);
}

bool adjacent_mark(const std::vector<Tok>& t, std::size_t j, const char* mark) {
    return j < t.size() && !t[j].word && !t[j].space_before && t[j].lower == mark;
}

// A plain 3 or 4 digit year, 100 to 2099.
bool year_token(const Tok& t) {
    if (!t.plain || t.lower.size() < 3 || t.lower.size() > 4) return false;
    int v = std::atoi(t.lower.c_str());
    return v >= 100 && v <= 2099;
}

// A number starting at token i: digits (with scale words after them) or
// the longest stretch of number words parse_number_words() accepts.
struct NumberAt {
    std::size_t last = 0;  // last token of the number
    double value = 0;
    bool plain = false;  // a bare integer written in digits
};

// skip, when given, is set past a stretch that must not be read as numbers
// at all.
std::optional<NumberAt> number_at(const std::vector<Tok>& t, std::size_t i, bool allow_one,
                                  std::size_t* skip = nullptr) {
    if (i >= t.size() || !t[i].word) return std::nullopt;
    if (t[i].digits) {
        auto v = parse_number_words(t[i].lower);
        if (!v) return std::nullopt;
        NumberAt r{i, *v, t[i].plain};
        std::string phrase = t[i].lower;
        for (std::size_t k = i + 1; k < t.size() && t[k].word && scale_number(t[k].lower) > 0; ++k) {
            auto w = parse_number_words(phrase + " " + t[k].lower);
            if (!w) break;
            phrase += " " + t[k].lower;
            r = {k, *w, false};
        }
        return r;
    }
    if (t[i].cap && t[i].lower != "one" && small_number(t[i].lower) < 0 && tens_number(t[i].lower) < 0 &&
        t[i].lower.find('-') == std::string::npos)
        return std::nullopt;
    if (!number_vocabulary(t[i].lower) || t[i].lower == "and") return std::nullopt;
    std::size_t end = i;
    while (end < t.size() && end - i < 12 && t[end].word && number_vocabulary(t[end].lower) &&
           (end == i || t[end].space_before))
        ++end;
    for (std::size_t k = end; k > i; --k) {
        std::string phrase;
        for (std::size_t m = i; m < k; ++m) phrase += (m > i ? " " : "") + t[m].lower;
        if (phrase == "a" || phrase == "an" || (phrase == "one" && !allow_one)) continue;
        auto v = parse_number_words(phrase);
        if (!v) continue;
        if (phrase == "one" && i + 1 < t.size() && t[i + 1].lower == "of") return std::nullopt;
        // "nineteen eighty-four" is a year read aloud: neither 19 nor 84.
        if (k < end && (small_number(t[k].lower) >= 0 || tens_number(t[k].lower) >= 0 ||
                        (t[k].lower.find('-') != std::string::npos && t[k].lower != "and"))) {
            if (skip) *skip = end;
            return std::nullopt;
        }
        return NumberAt{k - 1, *v, false};
    }
    return std::nullopt;
}

// ---- span finders, one per kind of answer ----

struct Cand {
    std::size_t start, end;
    std::string key;
    int order = 0;  // breaks ties at the same start: quoted text first
};

void add_cand(std::vector<Cand>& out, const std::vector<Tok>& t, std::size_t first, std::size_t last,
              std::string key, int order = 1) {
    out.push_back({t[first].start, t[last].end, std::move(key), order});
}

void name_runs(const std::vector<Tok>& t, AnswerType type, std::vector<Cand>& out) {
    const auto& join = joiners(type);
    auto cap_word = [&](std::size_t k) { return k < t.size() && t[k].word && t[k].cap && !month_or_weekday(t[k].lower); };
    auto initial = [&](std::size_t k) { return t[k].word && t[k].cap && t[k].end - t[k].start == 1; };
    std::size_t i = 0;
    while (i < t.size()) {
        if (!cap_word(i)) {
            ++i;
            continue;
        }
        std::size_t last = i, j = i + 1;
        while (j < t.size()) {
            // "J. R. Quill": an initial's full stop stays inside the name.
            if (!t[j].word && t[j].lower == "." && initial(last) && cap_word(j + 1)) {
                last = j + 1;
                j += 2;
                continue;
            }
            if (cap_word(j) && t[j].space_before) {
                last = j;
                ++j;
                continue;
            }
            // "Apollo 11", but not a year glued onto a name.
            if (t[j].word && t[j].plain && t[j].space_before && !year_token(t[j]) && t[j].lower.size() <= 4) {
                last = j;
                ++j;
                continue;
            }
            std::size_t k = j;
            while (k < t.size() && k - j < 3 && !t[k].cap && join.count(t[k].lower) && t[k].space_before) ++k;
            if (k > j && cap_word(k) && t[k].space_before) {
                last = k;
                j = k + 1;
                continue;
            }
            break;
        }
        std::size_t run_start = i, run_last = last;
        i = last + 1;
        std::vector<std::size_t> words;
        for (std::size_t k = run_start; k <= run_last; ++k)
            if (t[k].word) words.push_back(k);
        if (type == AnswerType::Person && t[words[0]].lower == "the") continue;
        // A leading function word is not part of the name: "In", "He", or
        // a joiner left at the front. "The" stays before a capitalized word
        // in organizations, places and titles ("The Quiet Harbours").
        while (!words.empty()) {
            const Tok& f = t[words[0]];
            if (f.lower == "the" && type != AnswerType::Person && words.size() > 1 && t[words[1]].cap) break;
            if (!f.cap || f.digits || stop_word(f.lower) || pronoun(f.lower)) {
                words.erase(words.begin());
                continue;
            }
            break;
        }
        if (words.empty()) continue;
        const std::string& first = t[words.front()].lower;
        const std::string& tail = t[words.back()].lower;
        if (type == AnswerType::Person) {
            if (org_suffixes().count(tail) || place_suffixes().count(tail) || place_prefixes().count(first) ||
                words.size() > 5)
                continue;
        } else if (type == AnswerType::Place) {
            if (company_suffixes().count(tail)) continue;
        }
        out.push_back({t[words.front()].start, t[words.back()].end, "", 1});
    }
}

void quoted_runs(const std::string& s, const std::vector<Tok>& t, std::vector<Cand>& out) {
    std::size_t i = 0;
    while (i < t.size()) {
        bool dbl = !t[i].word && t[i].lower == "\"";
        bool sgl = !t[i].word && t[i].lower == "'" && t[i].space_before && i + 1 < t.size() && !t[i + 1].space_before;
        if (!dbl && !sgl) {
            ++i;
            continue;
        }
        std::size_t k = i + 1;
        while (k < t.size() && !(!t[k].word && t[k].lower == t[i].lower)) {
            if (!t[k].word && (t[k].lower == ";" || t[k].lower == "(" || t[k].lower == ")")) break;
            ++k;
        }
        if (k >= t.size() || t[k].lower != t[i].lower) {
            ++i;
            continue;
        }
        // The words inside, without punctuation at either end ("Lanterns.").
        std::size_t a = i + 1, b = k;
        while (a < b && !t[a].word) ++a;
        while (b > a && !t[b - 1].word) --b;
        std::size_t words = 0;
        for (std::size_t m = a; m < b; ++m) words += t[m].word;
        bool has_space = false;
        for (std::size_t m = a + 1; m < b; ++m) has_space |= t[m].space_before;
        if (words >= 1 && words <= 12 && (dbl || has_space) && !(sgl && k + 1 < t.size() && t[k + 1].word && !t[k + 1].space_before)) {
            out.push_back({t[a].start, t[b - 1].end, "", 0});
            i = k + 1;
        } else {
            ++i;
        }
    }
    (void)s;
}

void number_spans(const std::vector<Tok>& t, const std::string& s, std::vector<Cand>& out) {
    std::size_t i = 0;
    while (i < t.size()) {
        std::size_t skip = 0;
        auto n = number_at(t, i, false, &skip);
        if (!n) {
            i = std::max(i + 1, skip);
            continue;
        }
        std::size_t last = n->last, j = last + 1;
        std::string key = fmt(n->value), display;
        bool joined = false;
        if (adjacent_mark(t, j, "%")) {
            key += " %";
            last = j;
            joined = true;
        } else if (std::size_t len = unit_at(s, t, j, display)) {
            key += " " + display;
            last = j + len - 1;
            joined = true;
        } else if (j < t.size() && plural_noun(t[j]) && t[j].space_before) {
            last = j;
            joined = true;
        }
        // A bare 1000..2099 is a year, and a day next to a month is part of
        // a date, not an amount.
        bool year = n->plain && !joined && n->value >= 1000 && n->value <= 2099;
        bool day = n->plain && t[i].lower.size() <= 2 &&
                   ((i > 0 && month_number(t[i - 1].lower) && t[i - 1].cap) ||
                    (n->last + 1 < t.size() && month_number(t[n->last + 1].lower) && t[n->last + 1].cap));
        if (!year && !day) add_cand(out, t, i, last, key);
        i = last + 1;
    }
}

// Every date stretch: token ranges and their keys.
struct DateHit {
    std::size_t first, last;
    std::string key;
};

std::vector<DateHit> date_hits(const std::string& s, const std::vector<Tok>& t) {
    std::vector<DateHit> out;
    std::size_t i = 0;
    while (i < t.size()) {
        const Tok& w = t[i];
        bool may_start = w.word && ((w.digits && w.lower.find(',') == std::string::npos) ||
                                    (w.cap && month_number(w.lower)) || w.lower == "circa" ||
                                    (w.lower == "c" && i + 1 < t.size() && t[i + 1].lower == ".") ||
                                    (is_digit(w.lower[0]) && (w.lower.back() == 's' || w.lower.find('-') != std::string::npos)));
        if (!may_start) {
            ++i;
            continue;
        }
        bool found = false;
        for (std::size_t e = std::min(t.size(), i + 9); e-- > i;) {
            if (!t[e].word) continue;
            std::string key = date_key(s.substr(w.start, t[e].end - w.start));
            if (key.empty()) continue;
            // A lone year followed by a unit, a percent or a plural noun is
            // an amount ("1500 soldiers").
            if (e == i && w.plain) {
                std::string display;
                if (adjacent_mark(t, i + 1, "%") || unit_at(s, t, i + 1, display) ||
                    (i + 1 < t.size() && plural_noun(t[i + 1])))
                    break;
            }
            out.push_back({i, e, key});
            i = e;
            found = true;
            break;
        }
        ++i;
        (void)found;
    }
    return out;
}

void year_spans(const std::string& s, const std::vector<Tok>& t, std::vector<Cand>& out) {
    auto dates = date_hits(s, t);
    std::vector<int> in_date(t.size(), -1);
    for (std::size_t d = 0; d < dates.size(); ++d)
        for (std::size_t k = dates[d].first; k <= dates[d].last; ++k) in_date[k] = int(d);
    auto era_bc = [&](std::size_t k) {
        if (k >= t.size() || !t[k].word) return false;
        return t[k].lower == "bc" || t[k].lower == "bce";
    };
    for (std::size_t i = 0; i < t.size(); ++i) {
        const Tok& w = t[i];
        if (!w.word) continue;
        // Decades: "1990s", "1990's".
        if (w.lower.size() >= 4 && w.lower.size() <= 5 && w.lower.back() == 's' &&
            all_digits(w.lower.substr(0, w.lower.size() - 1)) && w.lower[w.lower.size() - 2] == '0') {
            add_cand(out, t, i, i, w.lower);
            continue;
        }
        if (w.plain && w.lower.size() >= 3 && w.lower.size() <= 4 && adjacent_mark(t, i + 1, "'s") &&
            w.lower.back() == '0') {
            out.push_back({w.start, t[i + 1].end, w.lower + "s", 1});
            continue;
        }
        if (!w.plain) continue;
        int v = std::atoi(w.lower.c_str());
        if (in_date[i] >= 0) {
            const std::string& key = dates[in_date[i]].key;
            if (!key.empty() && key[0] == '-' && key[1] != '-') {
                // "500 BC": the era belongs to the year.
                std::size_t last = era_bc(i + 1) ? i + 1 : i;
                add_cand(out, t, i, last, "-" + w.lower);
                continue;
            }
            if (w.lower.size() >= 3 && v >= 100) add_cand(out, t, i, i, w.lower);
            continue;
        }
        if (!year_token(w)) continue;
        std::string display;
        if (adjacent_mark(t, i + 1, "%") || unit_at(s, t, i + 1, display) ||
            (i + 1 < t.size() && plural_noun(t[i + 1])))
            continue;
        add_cand(out, t, i, i, w.lower);
    }
}

void duration_spans(const std::string& s, const std::vector<Tok>& t, std::vector<Cand>& out) {
    auto tok_is = [&](std::size_t k, const char* w) { return k < t.size() && t[k].word && t[k].lower == w; };
    auto yr = [&](std::size_t k) { return k < t.size() && year_token(t[k]); };
    auto range = [&](std::size_t first, std::size_t a, std::size_t b, std::size_t last) {
        int ya = std::atoi(t[a].lower.c_str()), yb = std::atoi(t[b].lower.c_str());
        if (yb < ya) return false;
        add_cand(out, t, first, last, t[a].lower + ".." + t[b].lower);
        return true;
    };
    std::size_t i = 0;
    while (i < t.size()) {
        // Year ranges: "from 1801 to 1838", "between 1801 and 1838",
        // "1801 to 1838", "1801-1838".
        if ((tok_is(i, "from") && yr(i + 1) && tok_is(i + 2, "to") && yr(i + 3)) ||
            (tok_is(i, "between") && yr(i + 1) && tok_is(i + 2, "and") && yr(i + 3))) {
            if (range(i, i + 1, i + 3, i + 3)) {
                i += 4;
                continue;
            }
        }
        if (yr(i) && (tok_is(i + 1, "to") || (i + 1 < t.size() && t[i + 1].lower == "-")) && yr(i + 2)) {
            if (range(i, i, i + 2, i + 2)) {
                i += 3;
                continue;
            }
        }
        if (t[i].word && !t[i].digits && t[i].lower.size() >= 7 && is_digit(t[i].lower[0])) {
            std::size_t h = t[i].lower.find('-');
            std::string a = h == std::string::npos ? "" : t[i].lower.substr(0, h);
            std::string b = h == std::string::npos ? "" : t[i].lower.substr(h + 1);
            if (all_digits(a) && all_digits(b) && a.size() >= 3 && a.size() <= 4 && b.size() >= 3 && b.size() <= 4 &&
                std::atoi(b.c_str()) >= std::atoi(a.c_str()) && std::atoi(b.c_str()) <= 2099) {
                add_cand(out, t, i, i, a + ".." + b);
                ++i;
                continue;
            }
        }
        std::size_t skip = 0;
        auto n = number_at(t, i, true, &skip);
        if (n && n->last + 1 < t.size()) {
            std::string unit = time_unit(t[n->last + 1].lower);
            if (!unit.empty() && t[n->last + 1].word) {
                add_cand(out, t, i, n->last + 1, fmt(n->value) + " " + unit);
                i = n->last + 2;
                continue;
            }
        }
        i = n ? n->last + 1 : std::max(i + 1, skip);
    }
    (void)s;
}

// Sentence order, each key once at its first place.
std::vector<Span> finish(const std::string& s, std::vector<Cand>& cands, AnswerType type) {
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return a.start != b.start ? a.start < b.start : a.order < b.order;
    });
    std::vector<Span> out;
    std::set<std::string> seen;
    for (auto& c : cands) {
        if (c.key.empty()) continue;
        if (!seen.insert(c.key).second) continue;
        out.push_back({s.substr(c.start, c.end - c.start), c.key, type, c.start, c.end});
    }
    return out;
}

}  // namespace

// ---- names of types ----

const char* answer_type_name(AnswerType t) {
    switch (t) {
    case AnswerType::None: return "none";
    case AnswerType::Person: return "person";
    case AnswerType::Place: return "place";
    case AnswerType::Date: return "date";
    case AnswerType::Year: return "year";
    case AnswerType::Number: return "number";
    case AnswerType::Duration: return "duration";
    case AnswerType::Organization: return "organization";
    case AnswerType::Title: return "title";
    case AnswerType::List: return "list";
    case AnswerType::Description: return "description";
    }
    return "?";
}

AnswerType answer_type_from_name(const std::string& name) {
    for (AnswerType t : {AnswerType::Person, AnswerType::Place, AnswerType::Date, AnswerType::Year,
                         AnswerType::Number, AnswerType::Duration, AnswerType::Organization, AnswerType::Title,
                         AnswerType::List, AnswerType::Description})
        if (name == answer_type_name(t)) return t;
    return AnswerType::None;
}

// ---- spans ----

bool load_span_units(const std::string& path) {
    UnitTable t;
    if (!read_units(path, t)) return false;
    unit_table() = std::move(t);
    return true;
}

std::vector<Span> spans_of(const std::string& sentence, AnswerType type) {
    if (sentence.empty()) return {};
    if (type == AnswerType::List || type == AnswerType::Description || type == AnswerType::None) return {};
    auto t = scan(sentence);
    std::vector<Cand> cands;
    switch (type) {
    case AnswerType::Person:
    case AnswerType::Place:
    case AnswerType::Organization:
    case AnswerType::Title: {
        name_runs(t, type, cands);
        for (auto& c : cands) c.key = title_key(sentence.substr(c.start, c.end - c.start));
        if (type == AnswerType::Title) {
            // Quoted titles win over a capitalized run of the same words.
            std::vector<Cand> quoted;
            quoted_runs(sentence, t, quoted);
            std::set<std::string> qkeys;
            for (auto& q : quoted) {
                q.key = title_key(sentence.substr(q.start, q.end - q.start));
                qkeys.insert(q.key);
            }
            cands.erase(std::remove_if(cands.begin(), cands.end(), [&](const Cand& c) { return qkeys.count(c.key) > 0; }),
                        cands.end());
            cands.insert(cands.end(), quoted.begin(), quoted.end());
        }
        break;
    }
    case AnswerType::Number: number_spans(t, sentence, cands); break;
    case AnswerType::Date:
        for (auto& d : date_hits(sentence, t)) add_cand(cands, t, d.first, d.last, d.key);
        break;
    case AnswerType::Year: year_spans(sentence, t, cands); break;
    case AnswerType::Duration: duration_spans(sentence, t, cands); break;
    default: break;
    }
    return finish(sentence, cands, type);
}

// ---- numbers in words ----

namespace {

// "14", "1.5", "2,000,000": groups of exactly three after a comma, one
// decimal point at most.
std::optional<double> digits_value(const std::string& w) {
    std::size_t i = 0, n = w.size(), lead = 0;
    while (i < n && is_digit(w[i])) ++i, ++lead;
    if (lead == 0) return std::nullopt;
    bool grouped = false;
    while (i < n && w[i] == ',') {
        if (!grouped && lead > 3) return std::nullopt;
        grouped = true;
        if (i + 3 >= n || !is_digit(w[i + 1]) || !is_digit(w[i + 2]) || !is_digit(w[i + 3])) return std::nullopt;
        i += 4;
        if (i < n && is_digit(w[i])) return std::nullopt;
    }
    if (i < n && w[i] == '.') {
        ++i;
        std::size_t frac = 0;
        while (i < n && is_digit(w[i])) ++i, ++frac;
        if (frac == 0) return std::nullopt;
    }
    if (i != n) return std::nullopt;
    std::string plain;
    for (char c : w)
        if (c != ',') plain += c;
    return std::strtod(plain.c_str(), nullptr);
}

}  // namespace

std::optional<double> parse_number_words(const std::string& phrase) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : phrase) {
        char l = char(std::tolower(static_cast<unsigned char>(c)));
        if (l == ' ' || l == '-' || l == '\t') {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        } else {
            cur += l;
        }
    }
    if (!cur.empty()) w.push_back(cur);
    if (w.empty()) return std::nullopt;

    enum Prev { Start, Digits, Unit, Teen, Tens, Hundred, Scale, And, A, Dozen, Half };
    Prev prev = Start;
    double total = 0, group = 0, last_scale = 1e18;
    auto scale_or_hundred = [&](std::size_t k) {
        return k < w.size() && (scale_number(w[k]) > 0 || w[k] == "hundred" || w[k] == "dozen");
    };
    for (std::size_t i = 0; i < w.size(); ++i) {
        const std::string& t = w[i];
        if (is_digit(t[0])) {
            if (prev != Start) return std::nullopt;
            auto v = digits_value(t);
            if (!v) return std::nullopt;
            group = *v;
            prev = Digits;
            continue;
        }
        int u = small_number(t), tens = tens_number(t);
        double sc = scale_number(t);
        if (u >= 0) {
            if (prev == Tens) {
                if (u < 1 || u > 9) return std::nullopt;  // "twenty fifteen"
            } else if (prev != Start && prev != Hundred && prev != Scale && prev != And) {
                return std::nullopt;
            }
            group += u;
            prev = u < 10 ? Unit : Teen;
        } else if (tens >= 0) {
            // A tens word after a teen or another tens word is a year read
            // aloud ("nineteen eighty-four"): refused, not guessed.
            if (prev != Start && prev != Hundred && prev != Scale && prev != And) return std::nullopt;
            group += tens;
            prev = Tens;
        } else if (t == "hundred") {
            if (prev != Unit && prev != Teen && prev != Tens && prev != A) return std::nullopt;
            double small = std::fmod(group, 1000.0);
            if (small >= 100 || small <= 0) return std::nullopt;
            group = group - small + small * 100;
            prev = Hundred;
        } else if (sc > 0) {
            if (prev != Unit && prev != Teen && prev != Tens && prev != Hundred && prev != Digits && prev != A &&
                prev != Dozen && prev != Half)
                return std::nullopt;
            if (sc >= last_scale || group <= 0) return std::nullopt;
            total += group * sc;
            group = 0;
            last_scale = sc;
            prev = Scale;
        } else if (t == "and") {
            if (i + 2 < w.size() && (w[i + 1] == "a" || w[i + 1] == "an") && w[i + 2] == "half" &&
                (i + 3 == w.size() || scale_number(w[i + 3]) > 0) &&
                (prev == Unit || prev == Teen || prev == Tens || prev == Digits || prev == Hundred || prev == Dozen)) {
                group += 0.5;
                i += 2;
                prev = Half;
                continue;
            }
            if (prev != Hundred && prev != Scale) return std::nullopt;
            prev = And;
        } else if (t == "a" || t == "an") {
            if (prev != Start || !scale_or_hundred(i + 1)) return std::nullopt;
            group = 1;
            prev = A;
        } else if (t == "half") {
            // "half a million"
            if (prev != Start || i + 2 >= w.size() || (w[i + 1] != "a" && w[i + 1] != "an") ||
                !scale_or_hundred(i + 2))
                return std::nullopt;
            group = 0.5;
            ++i;
            prev = A;
        } else if (t == "dozen") {
            if (prev != Unit && prev != Teen && prev != Tens && prev != A && prev != Digits) return std::nullopt;
            group *= 12;
            prev = Dozen;
        } else {
            return std::nullopt;
        }
    }
    if (prev == And || prev == A || prev == Start) return std::nullopt;
    double v = total + group;
    if (v > 1e15) return std::nullopt;
    return v;
}

// ---- dates ----

namespace {

bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int days_in(int month, int year, bool year_known) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) return (!year_known || leap(year)) ? 29 : 28;
    return d[month - 1];
}

// "4", "04", "4th", "1st": a day of the month, or 0.
int day_number(const std::string& w, bool* ordinal = nullptr) {
    std::size_t i = 0;
    while (i < w.size() && is_digit(w[i])) ++i;
    if (i == 0 || i > 2) return 0;
    std::string rest = w.substr(i);
    if (!rest.empty() && rest != "st" && rest != "nd" && rest != "rd" && rest != "th") return 0;
    if (ordinal) *ordinal = !rest.empty();
    int v = std::atoi(w.substr(0, i).c_str());
    return v >= 1 && v <= 31 ? v : 0;
}

}  // namespace

std::string date_key(const std::string& text) {
    // Words, lowercase, commas as separators. A full stop is allowed only
    // after a month abbreviation or "c", and inside "B.C." and "A.D.".
    std::vector<std::string> w;
    {
        std::string cur;
        auto flush = [&] {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        };
        for (char c : text) {
            if (c == ' ' || c == ',' || c == '\t') flush();
            else cur += char(std::tolower(static_cast<unsigned char>(c)));
        }
        flush();
    }
    if (w.empty()) return "";
    for (auto& x : w) {
        if (x.find('.') == std::string::npos) continue;
        std::string nodots;
        for (char c : x)
            if (c != '.') nodots += c;
        if (nodots == "bc" || nodots == "bce" || nodots == "ad" || nodots == "ce") {
            x = nodots;
        } else if (x.back() == '.' && x.find('.') == x.size() - 1 &&
                   (nodots == "c" || nodots == "ca" || (month_number(nodots) && nodots.size() <= 4))) {
            x = nodots;
        } else {
            return "";
        }
    }
    for (const auto& x : w)
        for (char c : x)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '\'')) return "";

    std::size_t p = 0, n = w.size();
    bool circa = false, the = false;
    if (w[p] == "c" || w[p] == "ca" || w[p] == "circa" || w[p] == "around") circa = true, ++p;
    if (p < n && w[p] == "the") the = true, ++p;
    if (p >= n) return "";
    auto year_of = [](const std::string& s, bool era) -> int {
        if (!all_digits(s)) return -1;
        if (era ? s.size() > 4 : (s.size() < 3 || s.size() > 4)) return -1;
        int v = std::atoi(s.c_str());
        return v >= 1 ? v : -1;
    };
    auto era_word = [](const std::string& s) { return s == "bc" || s == "bce" || s == "ad" || s == "ce"; };
    char buf[48];

    // Decades: "1990s", "the 1990s", "1990's".
    if (p + 1 == n && !circa) {
        std::string d = w[p];
        if (d.size() >= 3 && d.compare(d.size() - 2, 2, "'s") == 0) d = d.substr(0, d.size() - 2) + "s";
        if (d.size() >= 4 && d.size() <= 5 && d.back() == 's' && all_digits(d.substr(0, d.size() - 1)) &&
            d[d.size() - 2] == '0')
            return d;
    }
    // ISO: "1776-07-04", "1776-07".
    if (p + 1 == n && !circa && !the && w[p].find('-') != std::string::npos) {
        auto parts = split(w[p], '-');
        if ((parts.size() == 3 || parts.size() == 2) && parts[0].size() == 4 && all_digits(parts[0]) &&
            parts[1].size() == 2 && all_digits(parts[1])) {
            int y = std::atoi(parts[0].c_str()), m = std::atoi(parts[1].c_str());
            if (m < 1 || m > 12) return "";
            if (parts.size() == 2) {
                std::snprintf(buf, sizeof buf, "%d-%02d", y, m);
                return buf;
            }
            if (parts[2].size() != 2 || !all_digits(parts[2])) return "";
            int d = std::atoi(parts[2].c_str());
            if (d < 1 || d > days_in(m, y, true)) return "";
            std::snprintf(buf, sizeof buf, "%d-%02d-%02d", y, m, d);
            return buf;
        }
        return "";
    }
    // A year on its own, with an optional era: "1776", "500 BC", "AD 79".
    {
        std::size_t q = p;
        bool era_before = false;
        if (q < n && (w[q] == "ad" || w[q] == "ce")) era_before = true, ++q;
        if (q < n && !the) {
            bool era_after = q + 2 == n && era_word(w[q + 1]);
            if ((q + 1 == n || era_after) && !(era_before && era_after)) {
                int y = year_of(w[q], era_before || era_after);
                if (y > 0) {
                    bool bc = era_after && (w[q + 1] == "bc" || w[q + 1] == "bce");
                    return bc ? "-" + std::to_string(y) : std::to_string(y);
                }
                return "";
            }
        }
    }
    if (circa) return "";
    int day = 0, month = 0, year = -1;
    bool ordinal = false;
    if ((day = day_number(w[p], &ordinal))) {
        // "4 July 1776", "4th of July", "the 4th of July, 1776".
        ++p;
        if (p < n && w[p] == "of") {
            if (!ordinal && !the) return "";
            ++p;
        }
        if (p >= n || !(month = month_number(w[p]))) return "";
        ++p;
    } else if ((month = month_number(w[p]))) {
        // "July 4, 1776", "July 1776", "Sept. 9 2001".
        if (the) return "";
        ++p;
        if (p < n && (day = day_number(w[p]))) ++p;
        if (day == 0 && p == n) return "";  // a month alone: "May"
    } else {
        return "";
    }
    if (the && !ordinal) return "";
    bool has_year = p < n;
    if (has_year) {
        year = year_of(w[p], p + 2 == n && era_word(w[p + 1]));
        if (year < 0) return "";
        ++p;
        bool bc = false;
        if (p < n && era_word(w[p])) bc = w[p] == "bc" || w[p] == "bce", ++p;
        if (p != n) return "";
        if (bc) year = -year;
    }
    if (day > 0 && day > days_in(month, year, has_year && year > 0)) return "";
    if (!has_year) {
        std::snprintf(buf, sizeof buf, "--%02d-%02d", month, day);
        return buf;
    }
    if (day == 0) std::snprintf(buf, sizeof buf, "%d-%02d", year, month);
    else std::snprintf(buf, sizeof buf, "%d-%02d-%02d", year, month, day);
    return buf;
}

bool keys_agree(const std::string& a, const std::string& b) {
    if (a == b) return true;
    auto datey = [](const std::string& s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return is_digit(c) || c == '-'; });
    };
    if (!datey(a) || !datey(b)) return false;
    auto starts = [](const std::string& shortk, const std::string& longk) {
        return longk.size() > shortk.size() && longk.compare(0, shortk.size(), shortk) == 0 &&
               longk[shortk.size()] == '-';
    };
    if (a.compare(0, 2, "--") != 0 && b.compare(0, 2, "--") != 0) return starts(a, b) || starts(b, a);
    // "--07-04" agrees with a full date ending in "-07-04".
    auto day_month = [](const std::string& dm, const std::string& full) {
        if (dm.size() != 7 || full.compare(0, 2, "--") == 0 || full.size() <= dm.size()) return false;
        return full.compare(full.size() - 6, 6, dm, 1, 6) == 0 &&
               std::count(full.begin() + (full[0] == '-' ? 1 : 0), full.end(), '-') == 2;
    };
    return day_month(a, b) || day_month(b, a);
}

// ---- answer types ----

namespace {

// The question's words: lowercase, punctuation dropped, apostrophes kept
// inside words ("testland's").
std::vector<std::string> question_words(const std::string& q) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        while (!cur.empty() && (cur.back() == '\'' || cur.back() == '-')) cur.pop_back();
        std::size_t a = 0;
        while (a < cur.size() && (cur[a] == '\'' || cur[a] == '-')) ++a;
        if (a < cur.size()) out.push_back(cur.substr(a));
        cur.clear();
    };
    for (std::size_t i = 0; i < q.size();) {
        unsigned char c = static_cast<unsigned char>(q[i]);
        std::size_t len = 1;
        char mark = c < 0x80 ? 0 : utf8_mark(q, i, len);
        if (c < 0x80 ? (std::isalnum(c) || c == '\'' || c == '-') : (mark == 0 || mark == '\'')) {
            if (mark == '\'') cur += '\'';
            else if (c < 0x80) cur += char(std::tolower(c));
            else cur += q.substr(i, len);
        } else {
            flush();
        }
        i += len;
    }
    flush();
    return out;
}

bool bare_name_word(const std::string& w) {
    if (stop_word(w)) return false;
    auto ends = [&](const char* s) {
        std::size_t k = std::strlen(s);
        return w.size() > k && w.compare(w.size() - k, k, s) == 0;
    };
    return !ends("ing") && !ends("ed");
}

bool match_pattern(const std::vector<std::string>& pat, std::size_t pi, const std::vector<std::string>& q,
                   std::size_t qi) {
    if (pi == pat.size()) return qi == q.size();
    const std::string& p = pat[pi];
    if (p == "*") {
        for (std::size_t k = qi + 1; k <= q.size(); ++k)
            if (match_pattern(pat, pi + 1, q, k)) return true;
        return false;
    }
    if (p == "{N}") {
        if (qi >= q.size() || q[qi] == "the" || q[qi] == "a" || q[qi] == "an") return false;
        for (std::size_t len = 1; len <= 4 && qi + len <= q.size(); ++len) {
            if (!bare_name_word(q[qi + len - 1])) return false;
            if (match_pattern(pat, pi + 1, q, qi + len)) return true;
        }
        return false;
    }
    return qi < q.size() && q[qi] == p && match_pattern(pat, pi + 1, q, qi + 1);
}

bool plural_form(const std::string& typed, const std::string& singular) {
    static const std::set<std::string> not_plural = {"series", "species", "news", "means",
                                                     "physics", "mathematics", "athletics"};
    return typed != singular && typed.size() > 1 && typed.back() == 's' && !not_plural.count(typed);
}

}  // namespace

bool AnswerTypes::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        add(line, section);
    }
    return !rules_.empty();
}

void AnswerTypes::add(const std::string& line, const std::string& section) {
    if (section == "events") {
        for (const auto& w : split(line, ';'))
            if (!w.empty()) events_.insert(lower(w));
        return;
    }
    std::size_t sep = line.rfind("::");
    if (sep == std::string::npos) return;
    std::string left = trim(line.substr(0, sep)), right = lower(trim(line.substr(sep + 2)));
    if (section == "heads") {
        AnswerType t = answer_type_from_name(right);
        if (t == AnswerType::None) return;
        // A word names one type only: the first line that lists it keeps it.
        for (const auto& w : split(left, ';'))
            if (!w.empty()) heads_.emplace(lower(w), t);
        return;
    }
    if (section != "patterns") return;
    Rule r;
    if (right == "head") {
        r.direction = Rule::Head;
    } else if (right.compare(0, 5, "head:") == 0) {
        r.direction = Rule::Head;
        r.type = answer_type_from_name(right.substr(5));
    } else if (right == "measure") {
        r.direction = Rule::Measure;
    } else if (right == "list") {
        r.direction = Rule::List;
    } else if (right == "person/organization") {
        r.type = AnswerType::Person;
        r.also = AnswerType::Organization;
    } else {
        r.type = answer_type_from_name(right);
        if (r.type == AnswerType::None && right != "none") return;
    }
    for (const auto& alt : split(left, ';')) {
        std::vector<std::string> words;
        for (const auto& w : split(alt, ' '))
            if (!w.empty()) words.push_back(w == "{N}" ? w : lower(w));
        if (!words.empty()) r.alternatives.push_back(words);
    }
    if (!r.alternatives.empty()) rules_.push_back(std::move(r));
}

AnswerType AnswerTypes::head_type(const std::string& noun) const {
    auto it = heads_.find(noun);
    if (it == heads_.end()) it = heads_.find(normalize_word(noun));
    return it == heads_.end() ? AnswerType::None : it->second;
}

AnswerShape AnswerTypes::classify(const std::string& question) const {
    AnswerShape shape;
    auto q = question_words(question);
    if (q.empty() || std::all_of(q.begin(), q.end(), [](const std::string& w) { return stop_word(w); }))
        return shape;
    const Rule* rule = nullptr;
    for (const auto& r : rules_) {
        for (const auto& alt : r.alternatives)
            if (match_pattern(alt, 0, q, 0)) {
                rule = &r;
                break;
            }
        if (rule) break;
    }
    if (!rule) return shape;

    if (rule->direction == Rule::Type) {
        shape.type = rule->type;
        shape.also = rule->also;
        return shape;
    }
    if (rule->direction == Rule::Measure) {
        // The measured thing: after "how long is", without an article, up to
        // the first stop word. An event lasts; anything else has a length.
        std::size_t i = 3;
        if (i < q.size() && (q[i] == "the" || q[i] == "a" || q[i] == "an")) ++i;
        shape.type = AnswerType::Number;
        for (; i < q.size() && !stop_word(q[i]); ++i)
            if (events_.count(q[i]) || events_.count(normalize_word(q[i]))) shape.type = AnswerType::Duration;
        return shape;
    }
    // Head noun: after the first "what" or "which" (the start when there is
    // none), past is/was/are/were and articles, within the next 3 words.
    std::size_t from = 0;
    for (std::size_t i = 0; i < q.size(); ++i)
        if (q[i] == "what" || q[i] == "which") {
            from = i + 1;
            break;
        }
    static const std::set<std::string> skip = {"is", "was", "are", "were", "the", "a", "an"};
    static const std::set<std::string> articles = {"of", "the", "a", "an"};
    std::string typed, head;
    AnswerType head_t = AnswerType::None;
    auto look = [&](std::size_t i) {
        for (std::size_t k = 0; k < 3 && i + k < q.size(); ++k) {
            const std::string& w = q[i + k];
            if (w == "of" || stop_word(w)) return std::string::npos;
            AnswerType t = head_type(w);
            if (t != AnswerType::None) {
                typed = w;
                head = heads_.count(w) ? w : normalize_word(w);
                head_t = t;
                return i + k;
            }
        }
        return std::string::npos;
    };
    std::size_t i = from;
    while (i < q.size() && skip.count(q[i])) ++i;
    std::size_t at = look(i);
    if (at != std::string::npos && head == "name") {
        // "the name of the river": the river decides.
        std::string keep_typed = typed, keep_head = head;
        AnswerType keep_t = head_t;
        std::size_t j = at + 1;
        while (j < q.size() && articles.count(q[j])) ++j;
        if (look(j) == std::string::npos) typed = keep_typed, head = keep_head, head_t = keep_t;
    }
    shape.head = head;
    if (rule->direction == Rule::List) {
        shape.type = AnswerType::List;
        shape.item = head_t == AnswerType::None ? AnswerType::Title : head_t;
        return shape;
    }
    if (head_t == AnswerType::None) {
        shape.type = rule->type == AnswerType::None ? AnswerType::Description : rule->type;
        return shape;
    }
    if (plural_form(typed, head)) {
        shape.type = AnswerType::List;
        shape.item = head_t;
        return shape;
    }
    shape.type = head_t;
    return shape;
}

AnswerShape answer_type(const AnswerTypes& table, const std::string& question) { return table.classify(question); }

// ---- reformulations ----

namespace {

using Item = Pattern::Item;

// A table pattern as items: literal words and marks, {X} {Y} {Z} {R} slots,
// {A} and {*}. words_only drops punctuation (the question side).
std::vector<Item> compile(const std::string& text, bool words_only) {
    std::vector<Item> out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t open = text.find('{', i);
        std::string chunk = text.substr(i, open == std::string::npos ? std::string::npos : open - i);
        for (const auto& t : scan(chunk)) {
            if (t.lower == "\"") continue;
            if (words_only && !t.word && t.lower != "'s") continue;
            Item it;
            it.word = t.lower;
            out.push_back(it);
        }
        if (open == std::string::npos) break;
        std::size_t close = text.find('}', open);
        if (close == std::string::npos) break;
        std::string name = text.substr(open + 1, close - open - 1);
        Item it;
        if (name == "A") it.kind = Item::Answer;
        else if (name == "*") it.kind = Item::Star;
        else if (name.size() == 1 && std::strchr("XYZR", name[0])) it.kind = Item::Slot, it.slot = name[0];
        else return {};
        out.push_back(it);
        i = close + 1;
    }
    return out;
}

std::size_t literal_count(const std::vector<Item>& items) {
    std::size_t n = 0;
    for (const auto& it : items) n += it.kind == Item::Word;
    return n;
}

bool match_question(const std::vector<Item>& pat, std::size_t pi, const std::vector<std::string>& q, std::size_t qi,
                    std::map<char, std::vector<std::string>>& bound) {
    if (pi == pat.size()) return qi == q.size();
    const Item& it = pat[pi];
    if (it.kind == Item::Word) return qi < q.size() && q[qi] == it.word && match_question(pat, pi + 1, q, qi + 1, bound);
    if (it.kind != Item::Slot) return false;
    for (std::size_t k = qi + 1; k <= q.size(); ++k) {
        bound[it.slot] = std::vector<std::string>(q.begin() + long(qi), q.begin() + long(k));
        if (match_question(pat, pi + 1, q, k, bound)) return true;
    }
    bound.erase(it.slot);
    return false;
}

// A binding without its article or a trailing time phrase ("glass lake
// each year" -> "glass lake").
void clean_binding(std::vector<std::string>& w) {
    if (w.size() > 1 && (w[0] == "the" || w[0] == "a" || w[0] == "an")) w.erase(w.begin());
    static const std::set<std::string> ones = {"annually", "today", "now", "ever"};
    static const std::set<std::string> lead = {"each", "a", "per", "every"};
    if (w.size() > 2 && w.back() == "year" && lead.count(w[w.size() - 2])) w.resize(w.size() - 2);
    else if (w.size() > 1 && ones.count(w.back())) w.pop_back();
}

bool contains_words(const std::string& padded, const std::vector<std::string>& words) {
    for (const auto& w : words)
        if (padded.find(" " + w + " ") == std::string::npos) return false;
    return !words.empty();
}

// May a leading pronoun stand for these words? When the document is about
// them, or the sentence before began with them.
bool pronoun_may_be(const PreparedSentence& ps, const std::vector<std::string>& words) {
    if (!ps.pronoun_first || words.empty()) return false;
    if (contains_words(" " + ps.title_key + " ", words)) return true;
    std::size_t at = !ps.previous.empty() && ps.previous[0] == "the" && words[0] != "the" ? 1 : 0;
    if (ps.previous.size() < at + words.size()) return false;
    return std::equal(words.begin(), words.end(), ps.previous.begin() + long(at));
}

struct Matcher {
    const PreparedSentence& ps;
    const std::vector<Item>& items;
    std::size_t a0, a1;
    bool tail_overlap;  // numbers may have absorbed the statement's next word ("two million people")

    bool slot_at(const Item& it, std::size_t pos, std::size_t& next_a, std::size_t& next_b, int& count) const {
        // Up to two ways to match: with and without a leading "the".
        count = 0;
        const auto& w = ps.words;
        auto words_at = [&](std::size_t p) {
            if (p + it.words.size() > w.size()) return false;
            for (std::size_t k = 0; k < it.words.size(); ++k)
                if (w[p + k].lower != it.words[k]) return false;
            return true;
        };
        if (pos == 0 && (it.slot == 'X' || it.slot == 'Y') && pronoun_may_be(ps, it.words)) {
            next_a = 1;
            count = 1;
        }
        if (words_at(pos)) {
            (count ? next_b : next_a) = pos + it.words.size();
            ++count;
        } else if (pos < w.size() && w[pos].lower == "the" && words_at(pos + 1)) {
            (count ? next_b : next_a) = pos + 1 + it.words.size();
            ++count;
        }
        return count > 0;
    }

    bool run(std::size_t idx, std::size_t pos) const {
        if (idx == items.size()) return true;
        const Item& it = items[idx];
        const auto& w = ps.words;
        switch (it.kind) {
        case Item::Word: return pos < w.size() && w[pos].lower == it.word && run(idx + 1, pos + 1);
        case Item::Slot: {
            std::size_t a = 0, b = 0;
            int count = 0;
            if (!slot_at(it, pos, a, b, count)) return false;
            return run(idx + 1, a) || (count > 1 && run(idx + 1, b));
        }
        case Item::Star:
            for (std::size_t k = 1; k <= 6 && pos + k <= w.size(); ++k)
                if (run(idx + 1, pos + k)) return true;
            return false;
        case Item::Answer: {
            // The candidate must stand exactly here; a statement's "the" may
            // be the title's own leading "The".
            if (!(pos == a0 || (pos == a0 + 1 && w[a0].lower == "the"))) return false;
            for (std::size_t q = a1; q > pos; --q) {
                if (q < a1 && (!tail_overlap || idx + 1 == items.size())) break;
                if (run(idx + 1, q)) return true;
            }
            return false;
        }
        }
        return false;
    }
};

}  // namespace

PreparedSentence prepare_sentence(const std::string& sentence, const MatchContext* context) {
    PreparedSentence ps;
    for (const auto& t : scan(sentence)) {
        if (t.lower == "\"") continue;  // quotes around a title do not break a statement
        ps.words.push_back({t.lower, t.start, t.end});
        if (t.word) ps.vocabulary.insert(t.lower);
    }
    if (!ps.words.empty()) {
        const std::string& f = ps.words[0].lower;
        ps.pronoun_first = f == "he" || f == "she" || f == "it" || f == "they";
    }
    if (context) {
        // "Rusty (character)" -> "rusty": the bracketed part only says which Rusty.
        std::string title;
        int depth = 0;
        for (char c : context->title) {
            if (c == '(') ++depth;
            else if (c == ')') depth = std::max(0, depth - 1);
            else if (depth == 0) title += c;
        }
        ps.title_key = title_key(title);
        for (const auto& t : scan(context->previous)) {
            if (!t.word) continue;
            ps.previous.push_back(t.lower);
            if (ps.previous.size() >= 8) break;
        }
    }
    return ps;
}

bool Reformulations::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        add(line);
    }
    return !lines_.empty();
}

void Reformulations::add(const std::string& text) {
    std::size_t sep = text.find("::");
    if (sep == std::string::npos) return;
    std::string left = text.substr(0, sep), right = text.substr(sep + 2);
    std::size_t arrow = left.find("=>");
    if (arrow == std::string::npos) return;
    std::string type = lower(trim(left.substr(arrow + 2)));
    Line L;
    if (type == "*") {
        L.keep_type = true;
    } else if (type == "person/organization") {
        L.type = AnswerType::Person;
        L.also = AnswerType::Organization;
    } else {
        L.type = answer_type_from_name(type);
        if (L.type == AnswerType::None) return;
    }
    for (const auto& q : split(left.substr(0, arrow), ';')) {
        auto items = compile(q, true);
        if (!items.empty()) L.questions.push_back(std::move(items));
    }
    for (const auto& s : split(right, ';')) {
        std::size_t at = s.rfind('@');
        if (at == std::string::npos) continue;
        int weight = std::atoi(s.c_str() + at + 1);
        std::string statement = trim(s.substr(0, at));
        auto items = compile(statement, false);
        bool has_answer = std::any_of(items.begin(), items.end(), [](const Item& it) { return it.kind == Item::Answer; });
        if (items.empty() || !has_answer || weight < 1) continue;
        L.statements.push_back({std::move(items), statement});
        L.weights.push_back(std::min(weight, 5));
    }
    if (!L.questions.empty() && !L.statements.empty()) lines_.push_back(std::move(L));
}

std::vector<Pattern> Reformulations::bind(const std::string& question, const AnswerShape& shape) const {
    std::vector<std::string> q;
    for (const auto& t : scan(lower(question)))
        if (t.word || t.lower == "'s") q.push_back(t.lower);
    std::vector<Pattern> out;
    for (const auto& L : lines_) {
        // The most specific question form of the line that matches:
        // "who sang the song {X}" over "who sang {X}".
        std::map<char, std::vector<std::string>> bound;
        std::size_t best = 0;
        bool found = false;
        for (const auto& form : L.questions) {
            std::map<char, std::vector<std::string>> b;
            std::size_t lit = literal_count(form);
            if ((!found || lit > best) && match_question(form, 0, q, 0, b)) {
                bound = b;
                best = lit;
                found = true;
            }
        }
        if (!found) continue;
        for (auto& [slot, words] : bound) clean_binding(words);
        for (std::size_t s = 0; s < L.statements.size(); ++s) {
            Pattern p;
            p.items = L.statements[s].first;
            p.statement = L.statements[s].second;
            p.weight = L.weights[s];
            p.type = L.keep_type ? shape.type : L.type;
            p.also = L.keep_type ? shape.also : L.also;
            bool ok = true, has_x = false;
            for (auto& it : p.items) {
                if (it.kind != Item::Slot) continue;
                auto b = bound.find(it.slot);
                if (b == bound.end()) {
                    ok = false;
                    break;
                }
                it.words = b->second;
                has_x |= it.slot == 'X';
            }
            if (!ok) continue;
            // "recorded by {A}" counts only in a sentence that names the song.
            if (!has_x && bound.count('X')) p.need = bound['X'];
            out.push_back(std::move(p));
        }
    }
    return out;
}

double Reformulations::match(const std::vector<Pattern>& patterns, const PreparedSentence& ps, const Span& candidate) {
    const auto& w = ps.words;
    std::size_t a0 = w.size(), a1 = w.size();
    for (std::size_t k = 0; k < w.size(); ++k) {
        if (a0 == w.size() && w[k].start == candidate.start) a0 = k;
        if (a0 != w.size() && w[k].end == candidate.end) {
            a1 = k + 1;
            break;
        }
    }
    if (a0 >= w.size() || a1 > w.size() || a1 <= a0) return 0;
    int best = 0;
    for (const auto& p : patterns) {
        if (p.weight <= best) continue;
        if (candidate.type != p.type && (p.also == AnswerType::None || candidate.type != p.also)) continue;
        if (!p.need.empty()) {
            bool held = std::all_of(p.need.begin(), p.need.end(), [&](const std::string& x) { return ps.vocabulary.count(x) > 0; });
            if (!held && !pronoun_may_be(ps, p.need)) continue;
        }
        Matcher m{ps, p.items, a0, a1, candidate.type == AnswerType::Number || candidate.type == AnswerType::Duration};
        bool hit = false;
        if (p.items[0].kind == Item::Answer) {
            hit = m.run(0, a0) || (w[a0].lower == "the" && a0 + 1 < a1 && m.run(0, a0 + 1));
        } else {
            for (std::size_t s = 0; s <= a0 && !hit; ++s) hit = m.run(0, s);
        }
        if (hit) best = p.weight;
    }
    return best;
}

double Reformulations::match(const std::vector<Pattern>& patterns, const std::string& sentence, const Span& candidate,
                             const MatchContext* context) {
    if (patterns.empty()) return 0;
    return match(patterns, prepare_sentence(sentence, context), candidate);
}

}  // namespace unllm
