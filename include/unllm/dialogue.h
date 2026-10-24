#pragma once
// Conversation state: what was just said, by whom, about what. A reading
// alone remembers the last QUESTION; a card remembers the last ANSWER, so a
// bare "why?" or "when was that" has something to point at.

#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include "unllm/unllm.h"

namespace unllm {

// One answer, described for the turns that follow it. The caller fills it
// while it answers; the dialogue keeps the last few.
struct Card {
    std::string question;                  // as answered, after spelling and rewrites
    Reading reading;                       // the reading that was answered
    std::string subject;                   // the main thing the answer named ("blender", "the CPU")
    std::vector<std::string> subjects_named;  // every program the answer named, in order
    std::string metric, unit;              // "memory", "GB"
    double value = NAN;
    int64_t moment = 0;                    // when the answered thing happened
    int64_t event_id = 0;                  // the record it rests on, when there is one
    Intent topic = Intent::Story;
    TimeWindow window;
    std::vector<std::string> receipts;
    std::string answer, branch;            // branch: which handler answered ("mask:heat")
    bool refused = false;
    bool clarify = false;                  // the answer asked "are you asking about X?"
    std::vector<Intent> offered;           // the topics that clarifying question offered
    std::vector<std::string> offered_names;  // "which one, firefox or chromium?": the candidates
    int64_t at = 0;                        // when the answer was given
    int world_ref = 0;                     // reference answers: the caller's handle on the passage shown
};

// What a short follow-up asks of the previous answer.
enum class Act { None, Why, When, More, Normal, Which, HowLong, Evidence, Wrong, Thanks };
const char* act_name(Act a);

// The follow-up table (data/followups.txt): "ACT: phrase ; phrase", matched
// on the whole normalized question.
class FollowUps {
public:
    bool load(const std::string& path);
    Act match(const std::string& question) const;
    std::size_t size() const { return table_.size(); }

private:
    std::map<std::string, Act> table_;
};

// How the dialogue read the newest turn.
struct Turn {
    enum Kind {
        Fresh,        // a question that stands alone (pronouns may still bind)
        FollowUp,     // a bare follow-up about the last answer (act says which)
        NoCard,       // a follow-up with nothing before it to point at
        ClarifyYes,   // a reply choosing one of the offered topics
        ClarifyNo,    // a reply declining them
        Correction,   // "no, I meant firefox": the last question with one thing swapped
        TopicReturn,  // "back to the memory thing": an earlier answer's question again
        Ambiguous,    // "it" could mean two things: ask which
    };
    Kind kind = Fresh;
    Reading reading;            // what to answer (patched for corrections and returns)
    Act act = Act::None;
    const Card* card = nullptr;  // the answer a follow-up, correction or return points at
    std::string note;           // words to put in front of the answer, or the question to ask
};
const char* turn_name(Turn::Kind k);

class Dialogue {
public:
    void set_followups(const FollowUps* f) { followups_ = f; }
    // Reads the newest turn against the conversation so far. Does not change
    // the conversation: call remember() with the answer's card afterwards.
    Turn interpret(const std::string& question, int64_t now, const Analyzer& an) const;
    void remember(Card c);
    const Card* last() const { return history_.empty() ? nullptr : &history_.front(); }
    const Card* nth(std::size_t i) const { return i < history_.size() ? &history_[i] : nullptr; }  // 0 is the newest
    const Card* find(Intent topic) const;  // newest answer on that topic
    void reset() { history_.clear(); }

private:
    std::deque<Card> history_;  // newest first, at most five
    const FollowUps* followups_ = nullptr;
};

}  // namespace unllm
