#include <algorithm>
#include <ctime>
#include <fstream>

#include "unllm/unllm.h"

namespace unllm {

const char* wants_name(Wants w) {
    switch (w) {
    case Wants::Fact: return "fact";
    case Wants::Time: return "time";
    case Wants::Duration: return "duration";
    case Wants::Count: return "count";
    case Wants::Quantity: return "quantity";
    case Wants::Name: return "name";
    case Wants::Cause: return "cause";
    case Wants::YesNo: return "yesno";
    }
    return "?";
}

const char* frame_name(Frame f) {
    switch (f) {
    case Frame::None: return "none";
    case Frame::Define: return "define";
    case Frame::Explain: return "explain";
    case Frame::HowTo: return "howto";
    case Frame::Choice: return "choice";
    case Frame::Condition: return "condition";
    case Frame::Worry: return "worry";
    case Frame::Advice: return "advice";
    case Frame::Summary: return "summary";
    case Frame::Calc: return "calc";
    case Frame::Poem: return "poem";
    case Frame::Opinion: return "opinion";
    case Frame::Creative: return "creative";
    case Frame::NeedsNetwork: return "network";
    case Frame::Personal: return "personal";
    }
    return "?";
}

const char* intent_name(Intent i) {
    switch (i) {
    case Intent::Heat: return "heat";
    case Intent::Slow: return "slow";
    case Intent::Memory: return "memory";
    case Intent::Disk: return "disk";
    case Intent::Network: return "network";
    case Intent::Sharing: return "sharing";
    case Intent::Update: return "update";
    case Intent::Config: return "config";
    case Intent::Crash: return "crash";
    case Intent::Trust: return "trust";
    case Intent::Battery: return "battery";
    case Intent::Startup: return "startup";
    case Intent::Devices: return "devices";
    case Intent::Flash: return "flash";
    case Intent::Gpu: return "gpu";
    case Intent::Diagnose: return "diagnose";
    case Intent::Identity: return "identity";
    case Intent::Help: return "help";
    case Intent::Story: return "story";
    }
    return "?";
}

namespace {

// Topic vocabularies, over normalized words. The craft is in these tables;
// they grow one exam failure at a time.
const std::vector<std::pair<Intent, std::vector<std::string>>>& vocab() {
    static const std::vector<std::pair<Intent, std::vector<std::string>>> v = {
        {Intent::Heat, {"hot", "heat", "temperature", "fan", "scream", "thermal", "overheat", "burn", "degree", "cook", "toasty", "shutdown", "loud", "noisy", "spin", "whir"}},
        {Intent::Slow, {"slow", "sluggish", "lag", "laggy", "freeze", "spike", "cpu", "busy", "stutter", "hang", "unresponsive", "churn", "performance"}},
        {Intent::Memory, {"memory", "ram", "swap", "leak", "oom"}},
        {Intent::Disk, {"disk", "drive", "io", "nvme", "ssd", "write", "read", "space", "full", "fill"}},
        {Intent::Network, {"network", "internet", "upload", "download", "connection", "connect", "wifi", "online", "traffic", "talk"}},
        {Intent::Sharing, {"share", "sharing", "torrent", "seed", "peer"}},
        {Intent::Update, {"update", "updated", "upgrade", "upgraded", "install", "installed", "package", "pacman"}},
        {Intent::Config, {"config", "configuration", "setting", "change", "modify", "different", "edit"}},
        {Intent::Crash, {"crash", "die", "segfault", "core", "abort", "quit", "exit", "kill"}},
        {Intent::Trust, {"trust", "safe", "suspicious", "malware", "virus", "weird", "legit", "sketchy", "dodgy"}},
        // Sleep and wake are power events (the session log); bare "sleep"
        // stays out, it is too common outside the machine.
        {Intent::Battery, {"battery", "power", "charge", "drain", "plug", "unplug", "watt", "wake", "woke", "suspend",
                           "resume", "lid", "hibernate", "hibernation"}},
        {Intent::Startup, {"boot", "startup", "login", "autostart", "start"}},
        {Intent::Devices, {"usb", "device", "peripheral", "webcam", "dongle", "bluetooth"}},
        {Intent::Flash, {"flash", "esp32", "arduino", "microcontroller"}},
        {Intent::Gpu, {"gpu", "graphics", "vram", "nvidia", "radeon", "amdgpu", "igpu", "cuda"}},
        {Intent::Diagnose, {"diagnose", "diagnosis", "wrong", "broken", "unhealthy"}},
        {Intent::Identity, {"os", "kernel", "distro", "hostname", "host", "uptime", "processor",
                            "spec", "specs", "hardware", "vendor", "motherboard", "bios", "firmware"}},
        {Intent::Help, {"help"}},
    };
    return v;
}

const std::set<std::string>& vocab_words() {
    static const std::set<std::string> all = [] {
        std::set<std::string> v;
        for (const auto& [intent, words] : vocab())
            for (const auto& w : words) v.insert(w);
        return v;
    }();
    return all;
}

const std::set<std::string>& stop_words() {
    static const std::set<std::string> s = {
        "the", "a", "an", "and", "or", "of", "to", "in", "on", "at", "it", "my", "is", "be", "do", "have",
        "what", "why", "how", "when", "who", "which", "that", "this", "there", "did", "was", "were", "so",
        "me", "i", "you", "we", "they", "them", "its", "with", "for", "from", "up", "down", "out", "about",
        "get", "go", "make", "made", "thing", "anything", "can", "will", "would", "could", "should",
        "something", "everything", "just", "really", "much", "lot", "please", "keep", "take",
    };
    return s;
}

bool is_stop(const Token& t) {
    // The stemmer can mangle a stop word ("everything" -> "everyth"), so
    // both forms are checked.
    return stop_words().count(t.norm) || stop_words().count(t.lower);
}

// The machine itself, by any of its names. Not stop words: a question that
// names the machine is a question about the domain, and the refusal wording
// depends on knowing that.
const std::set<std::string>& self_words() {
    static const std::set<std::string> s = {"machine", "computer", "laptop", "system", "pc",
                                            "desktop", "box", "rig"};
    return s;
}

// Domain words that name no topic: they prove the question is on home
// ground ("which program", "did it run") without steering it anywhere.
const std::set<std::string>& generic_words() {
    static const std::set<std::string> s = {
        "program", "process", "app", "application", "software", "run", "use", "open", "day",
        "happen", "eat", "monitor", "check", "see", "show", "watch", "usage", "need", "more",
        "less", "close", "cursor", "mouse", "keyboard", "screen", "sudden", "touch", "last",
        "first", "recent", "recently", "ever", "left", "remaining", "remain", "long",
        "before", "after", "earlier", "later", "nearly", "almost", "explain", "lately",
        "summarize", "summary", "overview", "recap", "rundown", "roundup", "doing", "going",
    };
    return s;
}

const std::set<std::string>& negation_words() {
    static const std::set<std::string> s = {"not", "never", "no", "none", "nothing", "didnt", "didn't",
                                            "dont", "don't", "doesnt", "doesn't", "wasnt", "wasn't",
                                            "isnt", "isn't", "arent", "aren't", "hasnt", "hasn't",
                                            "havent", "haven't", "wont", "won't", "cant", "can't",
                                            "couldnt", "couldn't", "neither", "nor"};
    return s;
}

// Checked on the typed word, before the stemmer folds "hottest" to "hot".
Degree degree_of(const std::string& lower) {
    static const std::set<std::string> most = {"hottest", "worst", "slowest", "loudest", "biggest",
                                               "largest", "longest", "highest", "busiest", "heaviest",
                                               "most", "peak", "maximum", "max", "top", "record"};
    static const std::set<std::string> least = {"least", "lowest", "coolest", "quietest", "minimum", "min"};
    if (most.count(lower)) return Degree::Most;
    if (least.count(lower)) return Degree::Least;
    return Degree::None;
}

bool polar_start(const std::string& lower) {
    static const std::set<std::string> s = {"did", "was", "is", "were", "are", "does", "do", "has",
                                            "have", "am", "can", "will", "would", "could", "should"};
    return s.count(lower) > 0;
}

// Health-check frames with no vocabulary word of their own, matched on the
// normalized join; written in POST-STEMMER form ("is" is "be", "this" is
// "thi", "everything" is "everyth").
const std::set<std::string>& diagnose_phrases() {
    static const std::set<std::string> s = {
        "be thi machine ok", "be everyth ok", "be the machine ok", "health check",
        "run a health check", "how healthy be thi machine",
    };
    return s;
}

// Meta-questions about the tool itself, matched on the normalized join.
const std::set<std::string>& help_phrases() {
    static const std::set<std::string> s = {
        "what can i ask you", "what can i ask", "what can you do", "what do you know",
        "what can you answer", "what can you tell me", "what are you able to answer",
    };
    return s;
}

bool is_quantity(const Token& t, const Token* next, Entity* e) {
    const std::string& w = t.lower;
    std::size_t digits = 0;
    while (digits < w.size() && isdigit(static_cast<unsigned char>(w[digits]))) ++digits;
    if (digits == 0) return false;
    std::string num = w.substr(0, digits), rest = w.substr(digits);
    if (rest == "%" || (rest.empty() && next && next->lower == "percent")) {
        *e = {"quantity", num + " %"};
        return true;
    }
    if (rest == "c" || rest == "°c" ||
        (rest.empty() && next && (next->norm == "degree" || next->lower == "c"))) {
        *e = {"quantity", num + " C"};
        return true;
    }
    if (rest == "gb" || rest == "mb") {
        *e = {"quantity", num + " " + (rest == "gb" ? "GB" : "MB")};
        return true;
    }
    return false;
}

}  // namespace

// Two questions in one line: the second starts at a comma, a semicolon or "and" whose next word opens a question, a
// wh-word or the verb a yes/no question starts with ("..., is it running hot", "... and how fast is it draining"). Each
// side keeps three words or more, so "..., and for how long" and "... and how much" stay part of the first.
std::size_t second_question(const std::string& question) {
    const auto t = tokenize(question);
    for (std::size_t i = 3; i + 3 <= t.size(); ++i) {
        const std::string& w = t[i].norm;
        const bool opens = w == "what" || w == "how" || w == "why" || w == "when" || w == "where" || w == "which" ||
                           w == "who" || polar_start(t[i].lower);
        if (!opens) continue;
        const bool with_and = t[i - 1].lower == "and";
        const std::size_t end = t[i - 1].start + t[i - 1].text.size();
        const std::string gap = question.substr(end, t[i].start - end);
        if ((with_and || gap.find(',') != std::string::npos || gap.find(';') != std::string::npos) && (with_and ? i - 1 : i) >= 3)
            return t[i].start;
    }
    return std::string::npos;
}

std::map<std::string, std::string> load_synonyms(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string canonical = line.substr(0, colon);
        std::string rest = line.substr(colon + 1);
        std::size_t i = 0;
        while (i < rest.size()) {
            while (i < rest.size() && rest[i] == ' ') ++i;
            std::size_t j = i;
            while (j < rest.size() && rest[j] != ' ') ++j;
            if (j > i) out[rest.substr(i, j - i)] = canonical;
            i = j;
        }
    }
    return out;
}

void Analyzer::set_synonyms(std::map<std::string, std::string> table) { synonyms_ = std::move(table); }

void Analyzer::set_vectors(const Vectors* v, double threshold) {
    vectors_ = v;
    vector_threshold_ = threshold;
}

void Analyzer::set_programs(const std::vector<std::string>& names) {
    programs_ = names;
    for (auto& p : programs_) {
        std::string low;
        for (char c : p) low += char(std::tolower(static_cast<unsigned char>(c)));
        p = low;
    }
}

// "world war i", "henry i", "part i": the roman numeral one, not the pronoun.
static bool head_is(const std::vector<std::string>& w, const char* a, const char* b) {
    return w.size() >= 2 && w[0] == a && w[1] == b;
}

bool roman_one(const std::vector<Token>& tokens, std::size_t i) {
    static const std::set<std::string> before = {
        "war", "part", "chapter", "volume", "book", "act", "phase", "henry", "elizabeth", "charles", "james",
        "george", "william", "edward", "richard", "louis", "john", "mary", "peter", "frederick", "alexander",
        "nicholas", "paul", "napoleon", "leo", "pius", "gregory", "clement", "benedict", "francis", "philip"};
    return tokens[i].lower == "i" && i > 0 && before.count(tokens[i - 1].lower) > 0;
}

bool stop_word(const std::string& lower) {
    return stop_words().count(lower) > 0 || stop_words().count(normalize_word(lower)) > 0;
}

const char* route_name(Route r) {
    switch (r) {
    case Route::Domain: return "domain";
    case Route::World: return "world";
    case Route::Category: return "category";
    }
    return "?";
}

Route route(const Reading& r) {
    // Requests with a fixed honest reply come first: no record answers them.
    if (r.frame == Frame::NeedsNetwork || r.frame == Frame::Opinion || r.frame == Frame::Creative ||
        r.frame == Frame::Personal)
        return Route::Category;
    // Anything tied to this machine is answered from the machine, always.
    if (r.anchored) return Route::Domain;
    if (r.frame == Frame::Define || r.frame == Frame::Explain || r.frame == Frame::HowTo || r.frame == Frame::Poem)
        return Route::World;
    // "how tall is mount everest": a fact about a subject that is not the machine.
    if (!r.subject.empty() &&
        (r.wants == Wants::Time || r.wants == Wants::Name || r.wants == Wants::Quantity || r.wants == Wants::Fact))
        return Route::World;
    return Route::Domain;
}

std::vector<std::string> Analyzer::known_words(bool include_programs) const {
    std::set<std::string> all;
    for (const auto* s : {&vocab_words(), &stop_words(), &self_words(), &generic_words(), &negation_words()})
        all.insert(s->begin(), s->end());
    for (const char* w : {"hottest", "worst", "slowest", "loudest", "biggest", "largest", "longest", "highest",
                          "busiest", "heaviest", "most", "peak", "maximum", "max", "top", "record", "least",
                          "lowest", "coolest", "quietest", "minimum", "min", "today", "yesterday", "tonight",
                          "tomorrow", "morning", "afternoon", "evening", "night", "breakfast", "lunch", "lunchtime",
                          "dinner", "week", "month", "year",
                          "hour", "minute", "second", "ago", "since", "between", "around", "now", "currently",
                          "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday",
                          "january", "february", "march", "april", "june", "july", "august", "september",
                          "october", "november", "december", "times", "often", "many", "where", "why",
                          "when", "which", "who", "how", "what"})
        all.insert(w);
    for (const auto& [word, canonical] : synonyms_) {
        all.insert(word);
        all.insert(canonical);
    }
    if (include_programs) all.insert(programs_.begin(), programs_.end());
    return {all.begin(), all.end()};
}

Reading Analyzer::read(const std::string& question, int64_t now, const Reading* prev) const {
    return read(question, now, prev, nullptr);
}

Reading Analyzer::read(const std::string& question, int64_t now, const Reading* prev,
                       const std::vector<std::vector<std::string>>* salience) const {
    Reading r;
    auto tokens = tokenize(question);
    std::set<std::size_t> consumed;
    std::vector<bool> matched(tokens.size(), false);

    // 1. time
    auto tw = find_time(tokens, now, &consumed);
    // A second time phrase after the first ("hotter yesterday morning or last
    // night"): read too, so its words never stand for what is asked, and kept
    // when the two are compared (1c).
    const std::set<std::size_t> first_time = consumed;
    std::optional<TimeWindow> tw2;
    std::set<std::size_t> second_time;
    if (tw && !consumed.empty()) {
        const std::size_t from = *consumed.rbegin() + 1;
        std::vector<Token> tail(tokens.begin() + std::min(from, tokens.size()), tokens.end());
        std::set<std::size_t> sub;
        if ((tw2 = find_time(tail, now, &sub)))
            for (std::size_t k : sub) second_time.insert(from + k);
        consumed.insert(second_time.begin(), second_time.end());
    }
    const std::set<std::size_t> timed = consumed;  // every word the time reader took

    // Follow-ups: "and the day/hour before?" shifts the previous window back.
    if (!tw && prev && prev->window.stated) {
        bool before = false, after = false;
        for (const auto& t : tokens) {
            if (t.lower == "before" || t.lower == "earlier") before = true;
            if (t.lower == "after" || t.lower == "later" || t.lower == "next") after = true;
        }
        if (before || after) {
            int64_t len = prev->window.to - prev->window.from;
            TimeWindow w = prev->window;
            if (before) { w.from -= len; w.to -= len; }
            else { w.from += len; w.to += len; }
            tw = w;
        }
    }
    if (tw) r.window = *tw;
    else {
        // No time named: the verb's tense decides. "is the disk busy", "what
        // is using the cpu" ask about now; "was", "did", "crashed" ask about
        // the last day, the default window (measured 2026-10-22: 49 -> 78 of
        // 85 windows right on the ask pool).
        r.window.from = now - 24 * 3600;
        r.window.to = now;
        r.window.stated = false;
        r.window.present = !past_tense(tokens);  // not "live": that would send "how hot is the sun" to the machine
    }

    // 1b. the question's shape: polar or counting, superlatives, negation,
    // the machine named as itself. All found on the typed words, before the
    // stemmer and the gazetteer get a say, and all marked as understood.
    if (!tokens.empty() && polar_start(tokens[0].lower)) r.form = Form::Polar;
    bool has_how = false, has_long = false, has_up = false;
    std::size_t long_idx = 0;
    std::string norm_join;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& low = tokens[i].lower;
        const std::string next = i + 1 < tokens.size() ? tokens[i + 1].lower : "";
        norm_join += (norm_join.empty() ? "" : " ") + tokens[i].norm;
        if (low == "how" && (next == "many" || next == "often" || next == "frequently")) {
            r.form = Form::Count;
            matched[i] = true;
            matched[i + 1] = true;
        }
        if (low == "times") matched[i] = true;
        if (!consumed.count(i)) {
            if (Degree g = degree_of(low); g != Degree::None) {
                r.degree = g;
                matched[i] = true;
            }
        }
        if (negation_words().count(low)) {
            r.negated = true;
            matched[i] = true;
        }
        if (self_words().count(low)) {
            r.self_named = true;
            matched[i] = true;
        }
        if (generic_words().count(tokens[i].norm) || generic_words().count(low)) matched[i] = true;
        if (low == "how") has_how = true;
        if (low == "long" && has_how) { has_long = true; long_idx = i; }
        if (low == "up" || low == "on" || low == "running" || low == "awake") has_up = true;
    }
    // "how long has it been up": a duration question about the machine.
    bool uptime_frame = false;
    if (has_how && has_long) {
        matched[long_idx] = true;
        uptime_frame = has_up;
    }
    const bool help_phrase = help_phrases().count(norm_join) > 0;
    const bool diagnose_phrase = diagnose_phrases().count(norm_join) > 0;

    // 1c. Judgment frames. "than <window>" moves that window aside as the
    // comparison referent; "than usual", "is X normal" and "a lot" ask for
    // the baseline; "getting hotter" asks for the trend. The caller answers
    // all of these with statistics over its own records.
    {
        std::size_t than_idx = tokens.size();
        bool getting = false, comparative = false, usualish = false, alot = false, more = false;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const std::string& low = tokens[i].lower;
            const std::string next = i + 1 < tokens.size() ? tokens[i + 1].lower : "";
            if (low == "than") than_idx = i;
            if (low == "getting" || low == "trending") getting = true;
            if (low == "hotter" || low == "slower" || low == "louder" || low == "warmer" ||
                low == "worse" || low == "noisier" || low == "higher" || low == "busier")
                comparative = true;
            if (low == "more" || low == "less") more = true;
            if (tokens[i].norm == "normal" || tokens[i].norm == "usual" || low == "usually" ||
                tokens[i].norm == "typical" || tokens[i].norm == "expect") {
                usualish = true;
                matched[i] = true;
            }
            if ((low == "a" && next == "lot") || (low == "too" && (next == "much" || next == "high" ||
                                                                   next == "hot" || next == "slow"))) {
                alot = true;
                matched[i] = true;
                matched[i + 1] = true;
            }
        }
        if (than_idx < tokens.size()) {
            matched[than_idx] = true;
            // The referent is a time named right after "than" ("than yesterday",
            // "than it was last night"); "more cpu than ollama yesterday"
            // compares programs over yesterday.
            std::size_t ref = than_idx + 1;
            while (ref < tokens.size() && !timed.count(ref) && is_stop(tokens[ref])) ++ref;
            const bool referent_window = first_time.count(ref) > 0;
            if (usualish) {
                r.judge = Judge::Normal;
            } else if (second_time.count(ref) && tw2->stated) {
                // "hotter today than yesterday": both named; the window stays.
                r.judge = Judge::Compare;
                r.compare_window = *tw2;
                r.second_window = *tw2;
            } else if (referent_window && r.window.stated) {
                r.judge = Judge::Compare;
                r.compare_window = r.window;
                r.window.from = now - 24 * 3600;
                r.window.to = now;
                r.window.stated = false;
                r.window.live = false;
            }
        } else if (getting && comparative) {
            r.judge = Judge::Trend;
        } else if (r.form == Form::Polar && (usualish || alot)) {
            r.judge = Judge::Normal;
            r.excess = alot && !usualish;
        }
        // "hotter yesterday morning or last night": two times set side by side.
        if (r.judge == Judge::None && tw2 && tw2->stated && !second_time.empty()) {
            bool versus = false, either = false;
            for (std::size_t k = *first_time.rbegin() + 1; k < *second_time.begin(); ++k) {
                versus |= tokens[k].lower == "vs" || tokens[k].lower == "versus" || tokens[k].lower == "compared";
                either |= tokens[k].lower == "or";
            }
            if (versus || (either && (comparative || more || r.degree != Degree::None))) {
                r.judge = Judge::Compare;
                r.second_window = *tw2;
            }
        }
    }

    // 2. entities
    std::set<std::string> seen_programs;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (consumed.count(i)) continue;
        const Token& t = tokens[i];
        if (t.lower == "pid" && i + 1 < tokens.size()) {
            const std::string& n = tokens[i + 1].lower;
            if (!n.empty() && std::all_of(n.begin(), n.end(), [](char c) { return isdigit(static_cast<unsigned char>(c)); })) {
                r.entities.push_back({"pid", n});
                consumed.insert(i);
                consumed.insert(i + 1);
                continue;
            }
        }
        Entity q;
        const Token* next = i + 1 < tokens.size() ? &tokens[i + 1] : nullptr;
        if (is_quantity(t, next, &q)) {
            r.entities.push_back(q);
            matched[i] = true;
            if (next && (next->norm == "degree" || next->lower == "percent" || next->lower == "c")) matched[i + 1] = true;
            continue;
        }
        // A topic word is never a misspelled program name: "scream" must
        // not fuzzy-match some program and silently eat the question.
        bool topical = vocab_words().count(t.norm) || vocab_words().count(t.lower) ||
                       synonyms_.count(t.norm) || synonyms_.count(t.lower);
        // Grammar: right after a pronoun, a modal or "to", a word is what the person does, not a program ("how can
        // i find out", "help me find"); "find" and "make" are programs too, and must not eat such a question.
        auto verb_slot = [&](std::size_t k) {
            static const std::set<std::string> before = {"i", "you", "we", "they", "me", "to", "can", "could", "should",
                                                         "would", "will", "please", "cant", "can't", "cannot", "help"};
            return k > 0 && before.count(tokens[k - 1].lower) > 0;
        };
        if (t.lower.size() >= 3 && !is_stop(t) && !topical && !matched[i] && !verb_slot(i)) {
            // Exact matches at any length; fuzziness only for longer words.
            // "used" is one edit from the program "sed", and a question must
            // never be silently filtered to a program nobody named.
            int limit = t.lower.size() >= 8 ? 2 : t.lower.size() >= 5 ? 1 : 0;
            for (const auto& p : programs_) {
                if (seen_programs.count(p)) continue;
                if (t.lower == p || edit_distance(t.lower, p, limit) <= limit) {
                    r.entities.push_back({"program", p});
                    seen_programs.insert(p);
                    consumed.insert(i);
                    break;
                }
            }
        }
    }

    // 2b. "what is kswapd0": three words, the last one unknown to every
    // table. That word is the subject being asked about, not noise.
    const bool what_is3 = tokens.size() == 3 &&
                          (tokens[0].lower == "what" || tokens[0].lower == "who") &&
                          (tokens[1].lower == "is" || tokens[1].lower == "was");
    bool name_entity = false;
    if (what_is3 && r.entities.empty() && !consumed.count(2)) {
        const Token& t = tokens[2];
        bool known = is_stop(t) || self_words().count(t.lower) || generic_words().count(t.norm) ||
                     generic_words().count(t.lower) || vocab_words().count(t.norm) ||
                     vocab_words().count(t.lower);
        if (!known && t.lower.size() >= 3) {
            r.entities.push_back({"name", t.lower});
            matched[2] = true;
            name_entity = true;
        }
    }

    // 2b'. Frames: what kind of request this is beyond its topic.
    {
        auto program_at = [&](std::size_t i) -> std::string {
            if (i >= tokens.size() || !consumed.count(i)) return "";
            for (const auto& e : r.entities)
                if (e.type == "program" && edit_distance(tokens[i].lower, e.value, 2) <= 2) return e.value;
            return "";
        };
        // "when/while blender is running": the program is a condition on the
        // records, not the subject, and "when" here asks no time.
        for (std::size_t i = 0; i + 3 < tokens.size() && r.frame == Frame::None; ++i) {
            const std::string& low = tokens[i].lower;
            if (low != "when" && low != "while" && low != "whenever") continue;
            std::string prog = program_at(i + 1);
            const std::string& be = tokens[i + 2].lower;
            const std::string& state = tokens[i + 3].lower;
            if (prog.empty() || (be != "is" && be != "was" && be != "are" && be != "were")) continue;
            if (state != "running" && state != "open" && state != "on" && state != "active" && state != "rendering")
                continue;
            r.frame = Frame::Condition;
            r.condition = prog;
            consumed.insert(i);
            matched[i + 2] = matched[i + 3] = true;
            std::vector<Entity> kept;
            for (const auto& e : r.entities)
                if (!(e.type == "program" && e.value == prog)) kept.push_back(e);
            r.entities = kept;
        }
        // Clauses that tie the question to something only the records can
        // place: "while i watched the movie", "the half hour before the
        // terminal got killed". A clause runs up to a time phrase, a joining
        // word or punctuation; its words are read, so they never stand for
        // what is asked. A program it names is the condition or the event,
        // not the subject.
        auto clause_end = [&](std::size_t a) {
            static const std::set<std::string> joins = {"and", "or", "but", "so", "then", "while", "when",
                                                        "whenever", "because", "if", "before", "after", "since"};
            std::size_t b = a;
            for (; b < tokens.size() && !timed.count(b) && !joins.count(tokens[b].lower); ++b)
                if (b > a && question.find_first_of(",;?!.", tokens[b - 1].start + tokens[b - 1].text.size()) <
                                 tokens[b].start)
                    break;
            return b;
        };
        auto take_clause = [&](std::size_t a, std::size_t b, std::string* text) {
            std::string prog;
            for (std::size_t k = a; k < b; ++k) {
                *text += (text->empty() ? "" : " ") + tokens[k].lower;
                if (prog.empty()) prog = program_at(k);
                consumed.insert(k);
                matched[k] = true;
            }
            std::vector<Entity> kept;
            for (const auto& e : r.entities)
                if (!(e.type == "program" && e.value == prog)) kept.push_back(e);
            r.entities = kept;
            return prog;
        };
        auto has_content = [&](std::size_t a, std::size_t b) {
            for (std::size_t k = a; k < b; ++k)
                if (!is_stop(tokens[k])) return true;
            return false;
        };
        // "the half hour before the terminal got killed", "since it got
        // killed": a span tied to an event. A clause, not a noun ("after the
        // update" stays a topic): it opens with who did it or says what
        // happened.
        static const std::set<std::string> subjects = {"i", "we", "you", "he", "she", "they", "it"};
        for (std::size_t i = 0; i + 1 < tokens.size() && r.event_clause.empty(); ++i) {
            const std::string& low = tokens[i].lower;
            // "what was running when the machine shut off": "when" inside a question that speaks of the past ties
            // it to the event's moment (a question in the present, "how hot does it get when ...", states a
            // condition; "when" leading the question asks the time).
            const bool moment = low == "when" && i > 0 && !consumed.count(i) &&
                                past_tense(std::vector<Token>(tokens.begin(), tokens.begin() + i));
            // "disk writes during the update": "during" takes the event itself, a noun, as its span.
            const bool during = low == "during" && !timed.count(i);
            if (!moment && !during && low != "since" && ((low != "before" && low != "after") || timed.count(i))) continue;
            const std::size_t a = i + 1, b = clause_end(a);
            if (!has_content(a, b)) continue;
            if (!moment && !during && !subjects.count(tokens[a].lower) &&
                !past_tense(std::vector<Token>(tokens.begin() + a, tokens.begin() + b)))
                continue;
            // "during the night": a part of the day is a time, not an event (alone, "night" names no night to read).
            if (during) {
                std::size_t s = a;
                while (s < b && is_stop(tokens[s])) ++s;
                std::set<std::size_t> probe;
                if (s < b && find_time(tokenize("last " + tokens[s].lower), now, &probe) && probe.count(1)) continue;
            }
            std::size_t first = i;
            if (low != "since" && !moment && !during) r.event_span = span_before(tokens, i, &first);
            r.event_after = low != "before" && !moment && !during;
            r.event_at = moment;
            r.event_during = during;
            for (std::size_t k = first; k <= i; ++k) {
                consumed.insert(k);
                matched[k] = true;
            }
            take_clause(a, b, &r.event_clause);
        }
        // "is blender or firefox worse for heat": two programs set side by side.
        std::vector<std::string> progs;
        for (const auto& e : r.entities)
            if (e.type == "program") progs.push_back(e.value);
        bool joiner = false, comparing = r.degree != Degree::None;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const std::string& low = tokens[i].lower;
            if (low == "or" || low == "vs" || low == "versus" || low == "compared" || low == "than") {
                joiner = true;
                matched[i] = true;
            }
            static const std::set<std::string> cmp = {"which", "worse", "better", "more", "less", "hotter",
                                                      "heavier", "faster", "slower", "bigger", "worst", "best"};
            if (cmp.count(low)) {
                comparing = true;
                matched[i] = true;
            }
        }
        if (r.frame == Frame::None && progs.size() == 2 && joiner && comparing) {
            r.frame = Frame::Choice;
            r.choices = progs;
        }
        // "was it the ram or the swap": two other things side by side, the content words next to "or" (up to
        // three on each side; a stop word or a time ends them).
        for (std::size_t i = 1; i + 1 < tokens.size() && r.choices.empty() && r.alternatives.empty(); ++i) {
            if (tokens[i].lower != "or") continue;
            auto content = [&](std::size_t k) { return k < tokens.size() && !is_stop(tokens[k]) && !consumed.count(k); };
            std::size_t a = i;
            while (a > 0 && i - a < 3 && content(a - 1)) --a;
            std::size_t b = i + 1;
            while (b < tokens.size() && is_stop(tokens[b]) && !consumed.count(b)) ++b;
            std::size_t e = b;
            while (e - b < 3 && content(e)) ++e;
            if (a == i || e == b) continue;
            std::string left, right;
            for (std::size_t k = a; k < i; ++k) left += (left.empty() ? "" : " ") + tokens[k].lower;
            for (std::size_t k = b; k < e; ++k) right += (right.empty() ? "" : " ") + tokens[k].lower;
            r.alternatives = {left, right};
        }
        // Worries, advice, summaries and "explain".
        // Not "ok" or "fine": "ok what is eating my cpu" is a live question.
        static const std::set<std::string> worry = {"worry", "worried", "worrying", "concern", "concerned",
                                                    "concerning", "healthy", "health", "dying", "failing",
                                                    "dangerous"};
        bool worried = false, advice = false, summary = false, explain = false, why = false;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const std::string& low = tokens[i].lower;
            const std::string next = i + 1 < tokens.size() ? tokens[i + 1].lower : "";
            const std::string next2 = i + 2 < tokens.size() ? tokens[i + 2].lower : "";
            if (worry.count(low)) {
                worried = true;
                matched[i] = true;
            }
            if ((low == "is" || low == "was") && (next == "it" || next == "that") && next2 == "bad") {
                worried = true;
                matched[i + 2] = true;
            }
            if (low == "anything" && (next == "wrong" || next == "bad")) worried = true;
            if (low == "should" && next == "i" && (next2 == "do" || next2 == "try")) advice = true;
            static const std::set<std::string> fixes = {"fix", "stop", "reduce", "lower", "free", "cool",
                                                        "speed", "prevent", "solve", "improve", "clean"};
            if (fixes.count(low) && i >= 2) {
                const std::string& a = tokens[i - 2].lower;
                const std::string& b = tokens[i - 1].lower;
                if ((a == "how" && b == "to") || ((a == "do" || a == "can" || a == "should") && b == "i")) {
                    advice = true;
                    matched[i] = true;
                }
            }
            static const std::set<std::string> sums = {"summarize", "summary", "overview", "recap", "rundown",
                                                       "roundup"};
            if (sums.count(low)) summary = true;
            if (low == "explain") explain = true;
            if (low == "why") why = true;
        }
        if (r.frame == Frame::None) {
            if (advice) r.frame = Frame::Advice;
            else if (worried) r.frame = Frame::Worry;
            else if (summary) r.frame = Frame::Summary;
            else if (explain && !why) r.frame = Frame::Explain;
        }
        // "while i watched the movie": a condition no program name settles;
        // the caller names the program from the clause and what it is about.
        // It is the frame unless the question already has one ("should i be
        // worried ... while gaming" stays a worry, with the clause kept).
        static const std::set<std::string> determiners = {"the", "a", "an", "my", "our", "your", "his", "her",
                                                          "their", "its", "this", "that", "these", "those", "some"};
        for (std::size_t i = 0; i + 1 < tokens.size() && r.condition.empty() && r.condition_clause.empty(); ++i) {
            if (tokens[i].lower != "while" && tokens[i].lower != "whenever") continue;
            const std::size_t a = i + 1, b = clause_end(a);
            if (!has_content(a, b)) continue;
            // What it is about: the subject when the clause opens with one
            // ("the movie was playing", "firefox was open"), else the object
            // after the verb ("i watched the movie"), else the verb alone ("i
            // was gaming"). The verb is the first content word right after its
            // subject, a helper verb or nothing ("a zoom call" has none).
            std::vector<std::string> words;
            const std::size_t s = determiners.count(tokens[a].lower) ? a + 1 : a;
            std::size_t e = s;
            while (e < b && !is_stop(tokens[e])) ++e;
            if (e > s && (s > a || (e < b && (tokens[e].norm == "be" || tokens[e].norm == "have")))) {
                for (std::size_t k = s; k < e; ++k) words.push_back(tokens[k].lower);
            } else {
                for (std::size_t k = a; k < b; ++k)
                    if (!is_stop(tokens[k])) words.push_back(tokens[k].lower);
                std::size_t v = a;
                while (v < b && is_stop(tokens[v])) ++v;
                if (words.size() > 1 && (v == a || !determiners.count(tokens[v - 1].lower)))
                    words.erase(words.begin());
            }
            for (const auto& x : words) r.condition_thing += (r.condition_thing.empty() ? "" : " ") + x;
            if (r.frame == Frame::None) r.frame = Frame::Condition;
            r.condition = take_clause(a, b, &r.condition_clause);
            consumed.insert(i);
        }
    }

    // 2c. The answer's shape, decided on the typed words, fixed precedence:
    // cause > time > duration > count > quantity > name > yes/no > fact.
    {
        bool has_when = false, has_why = false, has_cause = false, which_name = false, has_last = false,
             how_adj = false;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const std::string& low = tokens[i].lower;
            const std::string next = i + 1 < tokens.size() ? tokens[i + 1].lower : "";
            if (!consumed.count(i) && low == "when") has_when = true;
            if (low == "what" && next == "time") has_when = true;
            if (!consumed.count(i) && (low == "last" || low == "latest")) has_last = true;
            if (low == "why") has_why = true;
            if ((low == "what" || low == "who") && i + 1 < tokens.size() &&
                (tokens[i + 1].norm == "cause" || tokens[i + 1].norm == "make"))
                has_cause = true;
            if (low == "which" || low == "who") which_name = true;
            if (low == "what" && (next == "program" || next == "process" || next == "app")) which_name = true;
            // "what's using my bandwidth", "what was reading the disk": "what" as the subject of a verb in its -ing form
            // asks what is doing it ("what's" carries its "is"; "what is" has it apart).
            if (tokens[i].norm == "what" && i + 1 < tokens.size()) {
                std::size_t v = i + 1;
                if (low == "what" && tokens[v].norm == "be" && v + 1 < tokens.size()) ++v;
                const std::string& verb = tokens[v].lower;
                if (verb.size() > 4 && verb.compare(verb.size() - 3, 3, "ing") == 0 && tokens[v].norm != verb) r.asks_doer = true;
            }
            if (low == "how" && !next.empty()) {
                static const std::set<std::string> adj = {"hot",  "warm", "much",  "high", "fast",
                                                          "full", "big",  "heavy", "loud", "cold",
                                                          "bad",  "far",  "close", "tall",
                                                          "deep", "wide", "old",   "large", "heavy"};
                if (adj.count(next)) how_adj = true;
            }
        }
        bool has_program0 = false;
        for (const auto& e : r.entities) has_program0 |= e.type == "program";
        // "How long will the battery last": "last" is the verb there, so
        // "how long" outranks it; "when" still outranks both.
        if (has_why || has_cause) r.wants = Wants::Cause;
        else if (has_when) r.wants = Wants::Time;
        else if (has_how && has_long) r.wants = Wants::Duration;
        else if (has_last) r.wants = Wants::Time;
        else if (r.form == Form::Count) r.wants = Wants::Count;
        else if (how_adj) r.wants = Wants::Quantity;
        else if (which_name || name_entity || (what_is3 && has_program0)) r.wants = Wants::Name;
        else if (r.form == Form::Polar) r.wants = Wants::YesNo;
    }
    // "When did i set up the power cap": a past-tense when-question with no
    // time named asks for a moment anywhere in the records. The window keeps
    // the last-day default (measured on other questions); the flag says so.
    if (!r.window.stated && !r.window.live && r.wants == Wants::Time && past_tense(tokens))
        r.window.all_history = true;

    // 3. intents and leftover terms. Each intent keeps a tally: how many
    // tokens reached it, and the earliest one. The lead intent is the one
    // with the most evidence, earliest named: "why was the disk busy" is a
    // disk question that happens to say busy, not the other way round.
    std::map<Intent, std::pair<int, std::size_t>> found;  // count, first token
    auto note = [&](Intent in, std::size_t pos) {
        auto [it, fresh] = found.try_emplace(in, 1, pos);
        if (!fresh) {
            ++it->second.first;
            if (pos < it->second.second) it->second.second = pos;
        }
    };
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (consumed.count(i)) continue;
        const std::string& n = tokens[i].norm;
        const std::string& low = tokens[i].lower;
        // "shutting down" / "shut down" is one idea, and it is a heat idea here
        if ((n == "shut" || low == "shutdown") ||
            (n == "shut" && i + 1 < tokens.size() && tokens[i + 1].norm == "down")) {
            note(Intent::Heat, i);
            matched[i] = true;
        }
        // "video card", "graphics card": the GPU by its everyday name.
        if ((low == "video" || low == "graphics") && i + 1 < tokens.size() && tokens[i + 1].norm == "card") {
            note(Intent::Gpu, i);
            matched[i] = matched[i + 1] = true;
        }
        // The word itself, then what the synonym table says it means.
        std::string via;
        auto syn = synonyms_.find(n);
        if (syn == synonyms_.end()) syn = synonyms_.find(low);
        if (syn != synonyms_.end()) via = syn->second;
        for (const auto& [intent, words] : vocab())
            // both forms: the stemmer is light and words like "installed"
            // keep their raw form in the tables
            if (std::find(words.begin(), words.end(), n) != words.end() ||
                std::find(words.begin(), words.end(), low) != words.end() ||
                (!via.empty() && std::find(words.begin(), words.end(), via) != words.end())) {
                note(intent, i);
                matched[i] = true;
            }
        // Last resort: the meaning map. Only for words nothing recognized,
        // so the exact layers always win and the cost stays near zero.
        if (!matched[i] && vectors_ && vectors_->ok() && !is_stop(tokens[i]) && low.size() >= 3) {
            for (const auto& [intent, words] : vocab())
                for (const auto& w : words)
                    if (vectors_->similarity(low, w) >= vector_threshold_) {
                        note(intent, i);
                        matched[i] = true;
                        break;
                    }
        }
        // Topic-free words ("happen", "nearly") prove the question is on
        // home ground but match no record; they must not steer retrieval.
        if (!is_stop(tokens[i]) && n.size() >= 3 && !self_words().count(low) &&
            !generic_words().count(n) && !generic_words().count(low))
            r.terms.push_back(n);
    }
    // "start" is in Startup's vocabulary but "what started the heat" is not a
    // boot question; only keep Startup when it has boot-ish company or stands alone.
    if (found.count(Intent::Startup) && found.size() > 1) {
        bool strong = false;
        for (const auto& t : tokens)
            if (t.norm == "boot" || t.norm == "login" || t.norm == "autostart" || t.norm == "startup") strong = true;
        if (!strong) found.erase(Intent::Startup);
    }
    // "what usb devices did i plug in" is device talk; "plug" alone must not
    // drag Battery in. Battery needs battery-ish company when Devices is present.
    if (found.count(Intent::Devices) && found.count(Intent::Battery)) {
        bool strong = false;
        for (const auto& t : tokens)
            if (t.norm == "battery" || t.norm == "power" || t.norm == "charge" ||
                t.norm == "drain" || t.norm == "watt") strong = true;
        if (!strong) found.erase(Intent::Battery);
    }

    // Identity: questions about the machine itself rather than its records.
    bool has_program = false;
    for (const auto& e : r.entities) has_program |= e.type == "program";
    {
        bool wh_naming = false, wh_capacity = false, have_word = false, capacity_noun = false;
        for (const auto& t : tokens) {
            if (t.lower == "what" || t.lower == "which" || t.lower == "who") wh_naming = true;
            if (t.lower == "what" || t.lower == "which" || t.lower == "how") wh_capacity = true;
            if (t.norm == "have") have_word = true;
            if (t.lower == "cpu" || t.lower == "ram" || t.norm == "memory" || t.norm == "disk" ||
                t.norm == "processor" || t.norm == "core") capacity_noun = true;
        }
        // "how long has it been up", "what is this machine": the machine itself.
        if (uptime_frame && found.empty() && !has_program) note(Intent::Identity, tokens.size());
        if (r.self_named && wh_naming && found.empty() && !has_program) note(Intent::Identity, tokens.size());
        // "what cpu does this machine have": the part, not its workload.
        if (wh_capacity && have_word && capacity_noun) note(Intent::Identity, tokens.size());
        // A stated time window asks about the records, never about the parts,
        // and so does a yes/no question about an event: "was the kernel
        // updated" wants the update history, not the kernel's name.
        if (found.count(Intent::Identity) && found.size() > 1 &&
            (r.window.stated || r.form == Form::Polar))
            found.erase(Intent::Identity);
    }
    // "help my disk is full" is a disk question, not a manual request.
    if (found.count(Intent::Help) && found.size() > 1) found.erase(Intent::Help);
    // "Wrong" and "broken" only mean the health check when the machine
    // itself is the subject and no time window narrows the question; "what
    // went wrong yesterday" is a story, "is my fan broken" is a heat question.
    if (found.count(Intent::Diagnose)) {
        bool strong = false;
        for (const auto& t : tokens)
            if (t.norm == "diagnose" || t.norm == "diagnosis") strong = true;
        if (r.window.stated || (!strong && (!r.self_named || found.size() > 1)))
            found.erase(Intent::Diagnose);
    }

    // Strongest evidence first; equal evidence goes to the earlier mention.
    std::vector<std::pair<Intent, std::pair<int, std::size_t>>> ranked(found.begin(), found.end());
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.second.first != b.second.first) return a.second.first > b.second.first;
        return a.second.second < b.second.second;
    });
    for (const auto& [in, tally] : ranked) r.intents.push_back(in);
    if (help_phrase) r.intents = {Intent::Help};
    if (diagnose_phrase) r.intents = {Intent::Diagnose};

    // "Did memory use change after october 10": a metric with a dated
    // change-point is a judgment about the records. Without a metric,
    // "change" keeps meaning configuration.
    if (r.judge == Judge::None && r.window.stated) {
        bool changeish = false, anchor = false, metric = false;
        for (const auto& t : tokens) {
            if (t.norm == "change" || t.norm == "differ") changeish = true;
            if (t.lower == "after" || t.lower == "since") anchor = true;
        }
        for (auto in : r.intents)
            metric |= in == Intent::Heat || in == Intent::Slow || in == Intent::Memory ||
                      in == Intent::Disk || in == Intent::Network || in == Intent::Battery;
        if (changeish && anchor && metric) {
            r.judge = Judge::Changed;
            r.compare_window = r.window;
        }
    }

    // Pronouns: "it/that" is the one program we were just talking about,
    // most recent mention first (what the previous ANSWER named, then the
    // previous question's programs), unless the question is about the
    // machine itself. Never guess between two candidates: name both.
    if ((prev || salience) && !has_program) {
        bool pronoun = false;
        for (const auto& t : tokens) pronoun |= t.lower == "it" || t.lower == "that";
        std::vector<std::vector<std::string>> groups;
        if (salience) groups = *salience;
        if (prev) {
            std::vector<std::string> g;
            for (const auto& e : prev->entities)
                if (e.type == "program") g.push_back(e.value);
            groups.push_back(g);
        }
        const std::vector<std::string>* first = nullptr;
        for (const auto& g : groups)
            if (!g.empty()) {
                first = &g;
                break;
            }
        Intent lead0 = r.intents.empty() ? Intent::Story : r.intents[0];
        bool machine = r.self_named || r.window.live || lead0 == Intent::Heat ||
                       lead0 == Intent::Battery || lead0 == Intent::Identity || lead0 == Intent::Trust;
        if (pronoun && first && !machine) {
            if (first->size() == 1) {
                r.entities.push_back({"program", (*first)[0]});
                has_program = true;
            } else {
                r.ambiguous = *first;
            }
        }
    }

    // "Which program was the worst" after a memory question: a superlative
    // that names no topic keeps the conversation's topic.
    if (prev && r.intents.empty() && r.degree == Degree::Most && r.wants == Wants::Name && !has_program) {
        for (auto in : prev->intents)
            if (in != Intent::Help && in != Intent::Identity) r.intents.push_back(in);
    }

    // Fragments: a terse follow-up patches ONE dimension of the previous
    // question instead of standing alone. "and firefox?" swaps the program;
    // "just the evening" moves the window; "what about memory?" swaps the
    // topic; "how many times?" recounts.
    if (prev && !tokens.empty() && tokens.size() <= 5 && !help_phrase) {
        bool lead = tokens[0].lower == "and" || tokens[0].lower == "just" || tokens[0].lower == "only" ||
                    (tokens.size() >= 2 && (tokens[0].lower == "what" || tokens[0].lower == "how") &&
                     tokens[1].lower == "about");
        static const std::set<std::string> count_frags = {"how many", "how many time", "how often"};
        const bool count_frag = count_frags.count(norm_join) > 0;
        int dims = int(r.window.stated) + int(has_program) + int(!r.intents.empty()) +
                   int(r.form != Form::Open || r.degree != Degree::None);
        const bool helpish = !r.intents.empty() && r.intents[0] == Intent::Help;
        if (((lead && dims == 1) || count_frag) && !helpish) {
            Reading merged = *prev;
            if (count_frag) {
                merged.form = Form::Count;
                merged.wants = Wants::Count;
            } else if (r.window.stated) {
                TimeWindow w = r.window;
                // "just the evening" after a question about yesterday: the
                // day part belongs to the day under discussion, unless the
                // fragment anchored itself ("and this morning?").
                bool anchored = false;
                for (const auto& t : tokens)
                    anchored |= t.lower == "this" || t.lower == "today" || t.lower == "yesterday" ||
                                t.lower == "tonight" || t.lower == "before" || t.lower == "after";
                if (!anchored && prev->window.stated && w.to - w.from < 86400) {
                    auto day0 = [](int64_t t) {
                        std::time_t tt = t;
                        std::tm tm{};
                        localtime_r(&tt, &tm);
                        tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
                        tm.tm_isdst = -1;
                        return int64_t(std::mktime(&tm));
                    };
                    int64_t shift = day0(prev->window.from) - day0(w.from);
                    w.from += shift;
                    w.to += shift;
                }
                merged.window = w;
            } else if (has_program) {
                std::vector<Entity> kept;
                for (const auto& e : merged.entities)
                    if (e.type != "program") kept.push_back(e);
                for (const auto& e : r.entities)
                    if (e.type == "program") kept.push_back(e);
                merged.entities = kept;
            } else if (!r.intents.empty()) {
                merged.intents = r.intents;
            } else {
                merged.form = r.form;
                merged.degree = r.degree;
                merged.wants = r.wants;
            }
            merged.domain = 1.0;  // the fragment parsed, by definition
            return merged;
        }
    }

    // A yes/no question's head word ("is my ssd healthy" -> healthy): the
    // caller only answers "Yes," or "No," when its evidence speaks to it.
    // Words the time reader took, and the "than" of a comparison, never are.
    if (r.form == Form::Polar) {
        for (std::size_t i = tokens.size(); i-- > 1;) {
            const Token& t = tokens[i];
            if (is_stop(t) || consumed.count(i) || self_words().count(t.lower) || t.lower == "than" ||
                t.lower == "vs" || t.lower == "versus")
                continue;
            r.predicate = t.norm;
            break;
        }
    }
    // The machine as the subject: "this machine", "my laptop", "the pc".
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string& before = tokens[i - 1].lower;
        if (self_words().count(tokens[i].lower) &&
            (before == "this" || before == "my" || before == "our" || before == "the"))
            r.self_deictic = true;
    }
    // Anchored: tied to this machine by a time, a program, the machine named
    // as itself, a measurement, or "my <part>". Anchored questions are always
    // answered from the machine, never from reference knowledge.
    {
        bool mine = false, first_person = false;
        for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
            const Token& t = tokens[i + 1];
            if (tokens[i].lower == "my" &&
                (vocab_words().count(t.norm) || vocab_words().count(t.lower) || self_words().count(t.lower)))
                mine = true;
        }
        // "how do i cook a steak" is a how-to, not a question about me, and
        // "world war i" is a number, not a person.
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const std::string& low = tokens[i].lower;
            bool howto_i = low == "i" && i >= 2 && tokens[i - 2].lower == "how" &&
                           (tokens[i - 1].lower == "do" || tokens[i - 1].lower == "can" ||
                            tokens[i - 1].lower == "should" || tokens[i - 1].lower == "would");
            first_person |= (low == "i" && !howto_i && !roman_one(tokens, i)) || low == "my" || low == "am";
        }
        bool pid = false;
        for (const auto& e : r.entities) pid |= e.type == "pid";
        r.anchored = r.window.stated || r.window.live || r.self_deictic || has_program || pid ||
                     r.judge != Judge::None || mine || !r.condition.empty() || !r.condition_clause.empty() ||
                     !r.event_clause.empty() || !r.choices.empty() ||
                     ((r.form == Form::Polar || first_person) && !r.intents.empty());
    }

    // "How do i fix a flat tire on my bike": advice about something that is
    // not the machine is a how-to for the reference packs.
    if (r.frame == Frame::Advice && r.intents.empty()) r.frame = Frame::None;

    // World shapes: definitions, explanations, how-tos, poems, and the
    // requests that get a fixed honest reply. Each records the subject.
    if (r.frame == Frame::None && !tokens.empty()) {
        std::vector<std::string> w;
        for (const auto& t : tokens) w.push_back(t.lower);
        auto rest_from = [&](std::size_t i, std::size_t end) {
            std::string s;
            for (; i < end && i < w.size(); ++i) s += (s.empty() ? "" : " ") + w[i];
            return s;
        };
        static const std::set<std::string> pronoun = {"it", "this", "that", "these", "those", "my", "me", "i",
                                                      "you", "your", "we", "our", "they", "them", "its"};
        auto machine_head = [&](const std::string& word) {
            return self_words().count(word) || vocab_words().count(word) || vocab_words().count(normalize_word(word)) ||
                   pronoun.count(word);
        };
        auto has_pronoun = [&](std::size_t from) {
            for (std::size_t i = from; i < w.size(); ++i)
                if (pronoun.count(w[i]) && !roman_one(tokens, i)) return true;
            return false;
        };
        const std::size_t n = w.size();
        // Requests answered with a fixed honest reply.
        static const std::set<std::string> network = {"weather", "forecast", "news", "headlines", "headline",
                                                      "stock", "stocks", "score", "scores", "price", "prices",
                                                      "election", "traffic"};
        // ("the weather today" names a day, but no machine topic; "the price
        // of bitcoin right now" is live, but still not about the machine.)
        for (const auto& x : w)
            if (network.count(x) && r.intents.empty()) r.frame = Frame::NeedsNetwork;
        // "the traffic on the highway" is road traffic, not this machine's network.
        {
            static const std::set<std::string> road = {"highway", "road", "roads", "street", "streets", "motorway",
                                                       "freeway", "commute", "rush"};
            bool traffic = false, roads = false;
            for (const auto& x : w) {
                traffic |= x == "traffic";
                roads |= road.count(x) > 0;
            }
            if (traffic && roads && !r.anchored) r.frame = Frame::NeedsNetwork;
        }
        // Live facts: what is true right now, or will be. A reference book
        // cannot know them ("who won the battle of saratoga" it can).
        const std::string all = " " + rest_from(0, n) + " ";
        bool live = false;
        for (const char* p : {" right now ", " today ", " tonight ", " tomorrow ", " yesterday ", " last night ",
                              " this morning ", " this afternoon ", " this evening ", " this week ", " this weekend ",
                              " next week ", " this month ", " next month ", " this year ", " next year ",
                              " currently ", " at the moment ", " latest ", " most recent ", " trending ",
                              " lottery numbers ", " exchange rate ", " time is it "})
            live |= all.find(p) != std::string::npos;
        // ("what happened yesterday" asks about the machine's own records.)
        if (live && r.intents.empty() && !head_is(w, "what", "happened"))
            r.frame = Frame::NeedsNetwork;
        // Questions about the asker's own life: no reference book holds them.
        // ("how do i", "can i", "should i" ask for general knowledge.)
        if (r.intents.empty() && r.frame != Frame::NeedsNetwork && n >= 2) {
            const std::string head = w[0] + " " + w[1];
            const bool general = head == "how do" || head == "how can" || head == "how to" || head == "can i" ||
                                 head == "should i" || head == "how should" || head == "is it" || w[0] == "why" ||
                                 w[0] == "who";  // "who sang my girl": a title, not the asker's life
            bool own = false;
            for (const char* p : {" my ", " did i ", " do i have ", " am i ", " was i ", " have i ", " i ate ",
                                  " i had ", " i did ", " i have "})
                own |= all.find(p) != std::string::npos;
            if (own && !general && r.frame != Frame::HowTo && r.frame != Frame::Opinion && r.frame != Frame::Advice)
                r.frame = Frame::Personal;
        }
        if (r.frame == Frame::None) {
            const std::string j = rest_from(0, n);
            if (j.find("what do you think") != std::string::npos || j.find("do you like") != std::string::npos ||
                j.find("your favorite") != std::string::npos || j.find("your favourite") != std::string::npos ||
                (j.rfind("which is better", 0) == 0 && r.choices.empty()) || j.rfind("should i buy", 0) == 0)
                r.frame = Frame::Opinion;
        }
        // "tea or coffee", "cats or dogs": a preference, not a fact.
        if (r.frame == Frame::None && n >= 3 && n <= 6 && r.intents.empty() && r.entities.empty() &&
            r.choices.empty()) {
            static const std::set<std::string> asks = {"is", "are", "was", "were", "do", "does", "did", "can", "what",
                                                       "how", "why", "when", "who", "where", "whats"};
            bool has_or = false;
            for (std::size_t i = 1; i + 1 < n; ++i) has_or |= w[i] == "or";
            if (has_or && !asks.count(w[0])) r.frame = Frame::Opinion;
        }
        // Poems: a real one, retrieved; anything else creative: honest note.
        for (std::size_t i = 0; r.frame == Frame::None && i < n; ++i)
            if (w[i] == "poem" || w[i] == "poems" || w[i] == "verse" || w[i] == "poetry") {
                r.frame = Frame::Poem;
                for (std::size_t k = i + 1; k + 1 < n; ++k)
                    if (w[k] == "about" || w[k] == "on") r.subject = rest_from(k + 1, n);
            }
        if (r.frame == Frame::None && n >= 2) {
            static const std::set<std::string> make = {"write", "compose", "make", "create", "generate", "draw"};
            static const std::set<std::string> works = {"story", "song", "essay", "letter", "speech", "novel",
                                                        "script", "lyrics", "limerick", "haiku"};
            static const std::set<std::string> drawn = {"picture", "image", "drawing", "painting"};
            static const std::set<std::string> draw = {"draw", "paint", "generate", "create", "make"};
            bool m = false, wk = false, d = false;
            for (const auto& x : w) {
                m |= make.count(x) > 0;
                d |= draw.count(x) > 0;
                wk |= works.count(x) > 0;
            }
            for (const auto& x : w) wk |= d && drawn.count(x) > 0;
            // "how to write a workflow that checks a docker image" asks how,
            // it does not ask for new writing.
            static const std::set<std::string> asking = {"how", "what", "why", "when", "where", "which", "who",
                                                         "is", "are", "does", "do", "did", "whats"};
            if (m && wk && !asking.count(w[0])) r.frame = Frame::Creative;
        }
        // "define X", "what does X mean", "meaning of X".
        if (r.frame == Frame::None && w[0] == "define" && n >= 2) {
            r.frame = Frame::Define;
            r.subject = rest_from(1, n);
        }
        if (r.frame == Frame::None && n >= 4 && w[0] == "what" && (w[1] == "does" || w[1] == "do") &&
            (w[n - 1] == "mean" || w[n - 1] == "stand")) {
            r.frame = Frame::Define;
            r.subject = rest_from(2, w[n - 1] == "stand" ? n - 1 : n - 1);
        }
        // "what is a photon", "what are black holes", "what is ram".
        if (r.frame == Frame::None && n >= 3 && w[0] == "what" && (w[1] == "is" || w[1] == "are") &&
            !has_pronoun(2) && !r.self_deictic) {
            const bool article = w[2] == "a" || w[2] == "an";
            const bool the = w[2] == "the";
            std::size_t from = article || the ? 3 : 2;
            std::string subj = rest_from(from, n);
            // The noun phrase's head is its last word: "the hottest planet"
            // is about a planet, "the cpu temperature" about the machine.
            if (!subj.empty() && (article || !the || !machine_head(w[n - 1]))) {
                r.frame = Frame::Define;
                r.subject = subj;
                // "what is photosynthesis" wants a definition, not a name.
                if (r.wants == Wants::Name) r.wants = Wants::Fact;
            }
        }
        // "how does X work", "how do batteries work".
        if (r.frame == Frame::None && n >= 4 && w[0] == "how" && (w[1] == "does" || w[1] == "do") &&
            (w[n - 1] == "work" || w[n - 1] == "function" || w[n - 1] == "happen")) {
            std::size_t from = (w[2] == "a" || w[2] == "an" || w[2] == "the") ? 3 : 2;
            if (!has_pronoun(2) && !r.self_deictic) {
                r.frame = Frame::Explain;
                r.subject = rest_from(from, n - 1);
            }
        }
        // "why is the sky blue", "why do cats purr": not about this machine.
        if (r.frame == Frame::None && n >= 3 && w[0] == "why" &&
            (w[1] == "is" || w[1] == "are" || w[1] == "do" || w[1] == "does" || w[1] == "was" || w[1] == "were")) {
            std::size_t from = (w[2] == "a" || w[2] == "an" || w[2] == "the") ? 3 : 2;
            // "why is the sky blue": the last word is what is said of the
            // subject, not part of it; "why is beijing so hot" drops the "so".
            std::size_t end = n - from >= 2 ? n - 1 : n;
            static const std::set<std::string> intensifier = {"so", "very", "really", "too", "that", "always"};
            while (end > from + 1 && intensifier.count(w[end - 1])) --end;
            bool machine_subject = false;
            for (std::size_t i = from; i < end; ++i) machine_subject |= machine_head(w[i]);
            if (from < n && !machine_subject && !has_pronoun(2) && !r.anchored) {
                r.frame = Frame::Explain;
                r.subject = rest_from(from, end);
            }
        }
        // "who invented the telephone", "history of rome", "explain gravity".
        static const std::set<std::string> made = {"invented", "discovered", "wrote", "created", "founded",
                                                   "painted", "built", "designed", "composed"};
        if (r.frame == Frame::None && n >= 3 && w[0] == "who" && made.count(w[1])) {
            r.frame = Frame::Explain;
            r.subject = rest_from(w[2] == "the" || w[2] == "a" ? 3 : 2, n);
        }
        for (std::size_t i = 0; r.frame == Frame::None && i + 2 < n; ++i)
            if ((w[i] == "history" || w[i] == "origin") && w[i + 1] == "of") {
                r.frame = Frame::Explain;
                r.subject = rest_from(i + 2, n);
            }
        if (r.frame == Frame::None && n >= 2 && w[0] == "explain" && !has_pronoun(1) && !r.anchored) {
            r.frame = Frame::Explain;
            r.subject = rest_from(w[1] == "the" || w[1] == "a" ? 2 : 1, n);
        }
        // "how do i cook a steak", "how to tie a tie".
        if (r.frame == Frame::None && n >= 4 && w[0] == "how" &&
            ((w[1] == "do" || w[1] == "can" || w[1] == "should" || w[1] == "would") && w[2] == "i"))
            r.frame = Frame::HowTo, r.subject = rest_from(3, n);
        if (r.frame == Frame::None && n >= 3 && w[0] == "how" && w[1] == "to")
            r.frame = Frame::HowTo, r.subject = rest_from(2, n);
        // "how tall is mount everest", "when was the eiffel tower built",
        // "who wrote hamlet": a fact about something that is not this machine.
        if (r.frame == Frame::None && r.subject.empty() && n >= 3 && !r.anchored) {
            std::size_t from = 0;
            if (w[0] == "how" && n >= 4 && (w[2] == "is" || w[2] == "are" || w[2] == "was" || w[2] == "were")) from = 3;
            if ((w[0] == "when" || w[0] == "where" || w[0] == "who") &&
                (w[1] == "is" || w[1] == "was" || w[1] == "were" || w[1] == "are" || w[1] == "did"))
                from = 2;
            if (from && from < n) {
                if (w[from] == "the" || w[from] == "a" || w[from] == "an") ++from;
                // "when did world war 2 end", "when was the eiffel tower
                // built": the last word is the verb, not the subject.
                static const std::set<std::string> done = {
                    "built", "made", "invented", "founded", "born", "discovered", "created", "written",
                    "released", "completed", "finished", "opened", "signed", "killed", "elected", "published",
                    "launched", "painted", "named", "started", "established", "formed", "destroyed", "found"};
                bool verb_last = w[1] == "did" || ((w[1] == "was" || w[1] == "were") && done.count(w[n - 1]));
                std::size_t end = (verb_last && n - from >= 2) ? n - 1 : n;
                if (from < n && !machine_head(w[from]) && !has_pronoun(from)) r.subject = rest_from(from, end);
            }
        }
    }

    // The domain score: of the words that carry meaning, how many did the
    // library recognize as time, topic, program or quantity? A short word
    // counts only once something recognized it ("os", "90").
    int content = 0, hits = 0;
    std::vector<std::size_t> content_idx;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (is_stop(tokens[i])) continue;
        bool known = matched[i] || consumed.count(i);
        if (tokens[i].lower.size() < 3 && !known) continue;
        ++content;
        content_idx.push_back(i);
        if (known) ++hits;
    }
    r.domain = content ? double(hits) / content : 0.0;
    // Long, rambling questions: weigh each word by how much it carries
    // (attention over the meaning map), so filler cannot sink a real
    // question. Short questions keep the plain count.
    if (content >= 10 && vectors_ && vectors_->ok()) {
        std::vector<Token> ct;
        for (auto i : content_idx) ct.push_back(tokens[i]);
        auto w = attend(ct, vectors_);
        double num = 0, den = 0;
        for (std::size_t k = 0; k < content_idx.size(); ++k) {
            bool known = matched[content_idx[k]] || consumed.count(content_idx[k]);
            num += w[k] * (known ? 1.0 : 0.0);
            den += w[k];
        }
        if (den > 0) r.domain = std::max(r.domain, num / den);
    }
    if (help_phrase) r.domain = 1.0;
    return r;
}

}  // namespace unllm
