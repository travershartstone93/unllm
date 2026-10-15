#include <algorithm>
#include <fstream>

#include "unllm/unllm.h"

namespace unllm {

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
    case Intent::Story: return "story";
    }
    return "?";
}

namespace {

// Topic vocabularies, over normalized words. The craft is in these tables;
// they grow one exam failure at a time.
const std::vector<std::pair<Intent, std::vector<std::string>>>& vocab() {
    static const std::vector<std::pair<Intent, std::vector<std::string>>> v = {
        {Intent::Heat, {"hot", "heat", "temperature", "fan", "scream", "thermal", "overheat", "burn", "degree", "cook", "toasty", "shutdown"}},
        {Intent::Slow, {"slow", "sluggish", "lag", "laggy", "freeze", "spike", "cpu", "busy", "stutter", "hang", "unresponsive", "churn"}},
        {Intent::Memory, {"memory", "ram", "swap", "leak", "oom"}},
        {Intent::Disk, {"disk", "drive", "io", "nvme", "ssd", "write", "read", "space", "full"}},
        {Intent::Network, {"network", "internet", "upload", "download", "connection", "connect", "wifi", "online", "traffic", "talk"}},
        {Intent::Sharing, {"share", "sharing", "torrent", "seed", "peer"}},
        {Intent::Update, {"update", "upgrade", "install", "installed", "package", "pacman"}},
        {Intent::Config, {"config", "configuration", "setting", "change", "modify", "different", "edit"}},
        {Intent::Crash, {"crash", "die", "segfault", "core", "abort", "quit", "exit", "kill"}},
        {Intent::Trust, {"trust", "safe", "suspicious", "malware", "virus", "weird", "legit", "sketchy", "dodgy"}},
        {Intent::Battery, {"battery", "power", "charge", "drain", "plug", "unplug", "watt"}},
        {Intent::Startup, {"boot", "startup", "login", "autostart", "start"}},
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
        "get", "go", "make", "made", "machine", "computer", "laptop", "system", "pc", "thing", "anything",
        "something", "everything", "happen", "just", "really", "much", "lot", "please",
    };
    return s;
}

bool is_stop(const Token& t) {
    // The stemmer can mangle a stop word ("everything" -> "everyth"), so
    // both forms are checked.
    return stop_words().count(t.norm) || stop_words().count(t.lower);
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

Reading Analyzer::read(const std::string& question, int64_t now, const Reading* prev) const {
    Reading r;
    auto tokens = tokenize(question);
    std::set<std::size_t> consumed;
    std::vector<bool> matched(tokens.size(), false);

    // 1. time
    auto tw = find_time(tokens, now, &consumed);

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
        r.window.from = now - 24 * 3600;  // the default: the last day
        r.window.to = now;
        r.window.stated = false;
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
        if (t.lower.size() >= 3 && !is_stop(t) && !topical) {
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

    // 3. intents and leftover terms
    std::set<Intent> intents;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (consumed.count(i)) continue;
        const std::string& n = tokens[i].norm;
        const std::string& low = tokens[i].lower;
        // "shutting down" / "shut down" is one idea, and it is a heat idea here
        if ((n == "shut" || low == "shutdown") ||
            (n == "shut" && i + 1 < tokens.size() && tokens[i + 1].norm == "down")) {
            intents.insert(Intent::Heat);
            matched[i] = true;
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
                intents.insert(intent);
                matched[i] = true;
            }
        // Last resort: the meaning map. Only for words nothing recognized,
        // so the exact layers always win and the cost stays near zero.
        if (!matched[i] && vectors_ && vectors_->ok() && !is_stop(tokens[i]) && low.size() >= 3) {
            for (const auto& [intent, words] : vocab())
                for (const auto& w : words)
                    if (vectors_->similarity(low, w) >= vector_threshold_) {
                        intents.insert(intent);
                        matched[i] = true;
                        break;
                    }
        }
        if (!is_stop(tokens[i]) && n.size() >= 3) r.terms.push_back(n);
    }
    // "start" is in Startup's vocabulary but "what started the heat" is not a
    // boot question; only keep Startup when it has boot-ish company or stands alone.
    if (intents.count(Intent::Startup) && intents.size() > 1) {
        bool strong = false;
        for (const auto& t : tokens)
            if (t.norm == "boot" || t.norm == "login" || t.norm == "autostart" || t.norm == "startup") strong = true;
        if (!strong) intents.erase(Intent::Startup);
    }
    r.intents.assign(intents.begin(), intents.end());

    // The domain score: of the words that carry meaning, how many did the
    // library recognize as time, topic, program or quantity?
    int content = 0, hits = 0;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (is_stop(tokens[i]) || tokens[i].lower.size() < 3) continue;
        ++content;
        if (matched[i] || consumed.count(i)) ++hits;
    }
    r.domain = content ? double(hits) / content : 0.0;
    return r;
}

}  // namespace unllm
