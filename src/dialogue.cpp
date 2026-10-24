#include "unllm/dialogue.h"

#include <fstream>

namespace unllm {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// Lowercased words without apostrophes or punctuation: "What's that?" and
// "whats that" are the same follow-up.
std::string key_of(const std::string& text) {
    std::string out;
    for (const auto& t : tokenize(text)) {
        std::string w;
        for (char c : t.lower)
            if (c != '\'') w += c;
        if (!w.empty()) out += (out.empty() ? "" : " ") + w;
    }
    return out;
}

std::vector<std::string> words_of(const std::string& text) {
    std::vector<std::string> out;
    for (const auto& t : tokenize(text)) out.push_back(t.lower);
    return out;
}

std::string join_from(const std::vector<std::string>& w, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < w.size(); ++i) out += (out.empty() ? "" : " ") + w[i];
    return out;
}

// The dimensions a correction may swap: time, program, topic, shape.
struct Dims {
    bool window = false, program = false, topic = false, shape = false;
    int count() const { return int(window) + int(program) + int(topic) + int(shape); }
};

Dims dims_of(const Reading& r) {
    Dims d;
    d.window = r.window.stated;
    for (const auto& e : r.entities) d.program |= e.type == "program";
    d.topic = !r.intents.empty();
    d.shape = r.form != Form::Open || r.degree != Degree::None;
    return d;
}

}  // namespace

const char* act_name(Act a) {
    switch (a) {
    case Act::None: return "none";
    case Act::Why: return "why";
    case Act::When: return "when";
    case Act::More: return "more";
    case Act::Normal: return "normal";
    case Act::Which: return "which";
    case Act::HowLong: return "howlong";
    case Act::Evidence: return "evidence";
    case Act::Wrong: return "wrong";
    case Act::Thanks: return "thanks";
    }
    return "?";
}

const char* turn_name(Turn::Kind k) {
    switch (k) {
    case Turn::Fresh: return "fresh";
    case Turn::FollowUp: return "followup";
    case Turn::NoCard: return "nocard";
    case Turn::ClarifyYes: return "clarify-yes";
    case Turn::ClarifyNo: return "clarify-no";
    case Turn::Correction: return "correction";
    case Turn::TopicReturn: return "topic-return";
    case Turn::Ambiguous: return "ambiguous";
    }
    return "?";
}

bool FollowUps::load(const std::string& path) {
    static const std::map<std::string, Act> acts = {
        {"WHY", Act::Why},       {"WHEN", Act::When},         {"MORE", Act::More},
        {"NORMAL", Act::Normal}, {"WHICH", Act::Which},       {"HOWLONG", Act::HowLong},
        {"EVIDENCE", Act::Evidence}, {"WRONG", Act::Wrong},   {"THANKS", Act::Thanks},
    };
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto act = acts.find(trim(line.substr(0, colon)));
        if (act == acts.end()) continue;
        std::string rest = line.substr(colon + 1);
        std::size_t i = 0;
        while (i <= rest.size()) {
            std::size_t j = rest.find(';', i);
            if (j == std::string::npos) j = rest.size();
            std::string k = key_of(trim(rest.substr(i, j - i)));
            if (!k.empty()) table_[k] = act->second;
            i = j + 1;
        }
    }
    return !table_.empty();
}

Act FollowUps::match(const std::string& question) const {
    auto it = table_.find(key_of(question));
    return it == table_.end() ? Act::None : it->second;
}

void Dialogue::remember(Card c) {
    history_.push_front(std::move(c));
    while (history_.size() > 5) history_.pop_back();
}

const Card* Dialogue::find(Intent topic) const {
    for (const auto& c : history_)
        if (!c.refused && c.topic == topic) return &c;
    return nullptr;
}

Turn Dialogue::interpret(const std::string& question, int64_t now, const Analyzer& an) const {
    Turn t;
    const Card* card = last();
    const auto w = words_of(question);
    const std::string key = key_of(question);

    // 1a. A reply to "which one, firefox or chromium?".
    if (card && card->clarify && !card->offered_names.empty()) {
        static const std::vector<std::string> ordinals = {"first", "second", "third"};
        int pick = -1;
        for (std::size_t n = 0; n < card->offered_names.size(); ++n) {
            if (key == card->offered_names[n] || key == "the " + card->offered_names[n]) pick = int(n);
            if (n < ordinals.size() && (key == ordinals[n] || key == "the " + ordinals[n] ||
                                        key == "the " + ordinals[n] + " one"))
                pick = int(n);
        }
        if (pick >= 0) {
            t.kind = Turn::ClarifyYes;
            t.card = card;
            t.reading = an.read(card->question, now);
            std::vector<Entity> kept;
            for (const auto& e : t.reading.entities)
                if (e.type != "program") kept.push_back(e);
            kept.push_back({"program", card->offered_names[pick]});
            t.reading.entities = kept;
            t.reading.ambiguous.clear();
            t.reading.domain = 1.0;
            return t;
        }
    }

    // 1b. A reply to "are you asking about heat?".
    if (card && card->clarify && !card->offered.empty()) {
        static const std::set<std::string> yes = {"yes", "yeah", "yep", "yup", "sure", "correct", "right",
                                                  "that one", "yes please", "exactly", "uh huh"};
        static const std::set<std::string> no = {"no", "nope", "nah", "not really", "neither"};
        static const std::vector<std::string> ordinals = {"first", "second", "third"};
        int pick = -1;
        if (yes.count(key)) pick = 0;
        for (std::size_t n = 0; n < ordinals.size(); ++n)
            if (key == ordinals[n] || key == "the " + ordinals[n] || key == "the " + ordinals[n] + " one")
                pick = int(n);
        if (pick < 0 && !no.count(key) && w.size() <= 3) {
            Reading reply = an.read(question, now);
            for (std::size_t n = 0; !reply.intents.empty() && n < card->offered.size(); ++n)
                if (card->offered[n] == reply.intents[0]) pick = int(n);
        }
        if (no.count(key)) {
            t.kind = Turn::ClarifyNo;
            t.card = card;
            return t;
        }
        if (pick >= 0 && pick < int(card->offered.size())) {
            t.kind = Turn::ClarifyYes;
            t.card = card;
            t.reading = an.read(card->question, now);
            t.reading.intents = {card->offered[pick]};
            t.reading.domain = 1.0;
            return t;
        }
    }

    // 2. "no, I meant firefox": swap one thing into the last question.
    if (card && !card->refused && !w.empty()) {
        std::size_t from = 0;
        if (w[0] == "no" || w[0] == "nope" || w[0] == "nah" || w[0] == "actually" || w[0] == "not") from = 1;
        if (from < w.size() && w[from] == "i" && from + 1 < w.size() &&
            (w[from + 1] == "meant" || w[from + 1] == "mean" || w[from + 1] == "said"))
            from += 2;
        else if (w.size() >= 2 && w[0] == "i" && (w[1] == "meant" || w[1] == "mean" || w[1] == "said"))
            from = 2;
        if (from > 0 && from < w.size()) {
            Reading rem = an.read(join_from(w, from), now);
            Dims d = dims_of(rem);
            if (d.count() == 1) {
                Reading patched = card->reading;
                if (d.window) {
                    patched.window = rem.window;
                } else if (d.program) {
                    std::string chosen;
                    for (const auto& e : rem.entities)
                        if (e.type == "program") chosen = e.value;  // "not blender, firefox": the last one
                    std::vector<Entity> kept;
                    for (const auto& e : patched.entities)
                        if (e.type != "program") kept.push_back(e);
                    kept.push_back({"program", chosen});
                    patched.entities = kept;
                } else if (d.topic) {
                    patched.intents = rem.intents;
                } else {
                    patched.form = rem.form;
                    patched.degree = rem.degree;
                    patched.wants = rem.wants;
                }
                patched.domain = 1.0;
                t.kind = Turn::Correction;
                t.card = card;
                t.reading = patched;
                return t;
            }
        }
    }

    // 3. A bare follow-up: "why?", "when was that", "tell me more".
    if (followups_) {
        Act act = followups_->match(question);
        if (act != Act::None) {
            t.act = act;
            if (act == Act::Thanks || card) {
                t.kind = Turn::FollowUp;
                t.card = card;
            } else {
                t.kind = Turn::NoCard;
            }
            return t;
        }
    }

    // 4. "Back to the memory thing", "the heat again". "Again" with nothing
    // earlier on that topic ("what crashed yesterday again") is a question
    // of its own.
    bool back = false, again = false;
    for (std::size_t i = 0; i + 1 < w.size(); ++i) back |= w[i] == "back" && w[i + 1] == "to";
    for (const auto& x : w) again |= x == "again";
    if (back || again) {
        Reading rem = an.read(question, now);
        if (!rem.intents.empty() && (back || find(rem.intents[0]))) {
            t.kind = Turn::TopicReturn;
            t.card = find(rem.intents[0]);
            std::string topic = intent_name(rem.intents[0]);
            if (t.card) {
                t.reading = t.card->reading;
                if (!t.reading.window.stated) {
                    t.reading.window.from = now - 24 * 3600;
                    t.reading.window.to = now;
                }
                t.note = "Back to " + topic + ": ";
            } else {
                t.reading = rem;  // for a caller that answers it as a new question
                t.note = "We haven't talked about " + topic + " yet.";
            }
            return t;
        }
    }

    // 5. A question of its own, with the conversation as context.
    std::vector<std::vector<std::string>> salience;
    if (card && !card->refused) salience.push_back(card->subjects_named);
    const Reading* prev = card && !card->refused ? &card->reading : nullptr;
    t.reading = an.read(question, now, prev, &salience);
    if (!t.reading.ambiguous.empty()) {
        t.kind = Turn::Ambiguous;
        std::string opts;
        for (std::size_t i = 0; i < t.reading.ambiguous.size(); ++i)
            opts += (i == 0 ? "" : i + 1 == t.reading.ambiguous.size() ? " or " : ", ") + t.reading.ambiguous[i];
        t.note = "Which one, " + opts + "?";
        return t;
    }
    t.kind = Turn::Fresh;
    return t;
}

}  // namespace unllm
