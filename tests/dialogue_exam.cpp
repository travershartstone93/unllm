// The dialogue exam: conversations, not single questions. Each block builds
// the cards a caller would remember and checks how the next turn is read.

#include <cstdio>
#include <ctime>

#include "unllm/dialogue.h"

using namespace unllm;

static int checks = 0, failures = 0;
#define CHECK(cond)                                                             \
    do {                                                                        \
        ++checks;                                                               \
        if (!(cond)) {                                                          \
            ++failures;                                                         \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                       \
    } while (0)

static int64_t T(int mon, int day, int h, int mi) {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_isdst = -1;
    return int64_t(std::mktime(&tm));
}
static const int64_t NOW = T(10, 13, 21, 30);

static std::string programs_of(const Reading& r) {
    std::string s;
    for (const auto& e : r.entities)
        if (e.type == "program") s += (s.empty() ? "" : ",") + e.value;
    return s;
}

// The card a caller would keep after answering q.
static Card card_for(const Analyzer& an, const char* q, std::vector<std::string> named = {}) {
    Card c;
    c.question = q;
    c.reading = an.read(q, NOW);
    c.topic = c.reading.intents.empty() ? Intent::Story : c.reading.intents[0];
    c.subjects_named = named;
    if (!named.empty()) c.subject = named[0];
    c.at = NOW;
    return c;
}

int main() {
    Analyzer an;
    an.set_programs({"blender", "firefox", "chromium", "ollama", "llama-server", "ktorrent"});
    FollowUps fu;
    CHECK(fu.load("data/followups.txt") && fu.size() >= 40);

    // ---- the follow-up table itself ----
    CHECK(fu.match("why?") == Act::Why);
    CHECK(fu.match("Why is that?") == Act::Why);
    CHECK(fu.match("when was that") == Act::When);
    CHECK(fu.match("tell me more") == Act::More);
    CHECK(fu.match("is that normal?") == Act::Normal);
    CHECK(fu.match("are you sure") == Act::Evidence);
    CHECK(fu.match("which one") == Act::Which);
    CHECK(fu.match("how long") == Act::HowLong);
    CHECK(fu.match("what's the reason") == Act::Why);
    CHECK(fu.match("why did the fan scream") == Act::None);  // a real question, not a follow-up

    // ---- a follow-up with nothing before it ----
    {
        Dialogue d;
        d.set_followups(&fu);
        Turn t = d.interpret("why?", NOW, an);
        CHECK(t.kind == Turn::NoCard && t.act == Act::Why);
    }

    // ---- follow-ups point at the last answer ----
    {
        Dialogue d;
        d.set_followups(&fu);
        d.remember(card_for(an, "what used the most memory this week", {"llama-server"}));
        Turn t = d.interpret("why?", NOW, an);
        CHECK(t.kind == Turn::FollowUp && t.act == Act::Why && t.card && t.card->subject == "llama-server");
        CHECK(d.interpret("when was that", NOW, an).act == Act::When);
        CHECK(d.interpret("is that normal", NOW, an).act == Act::Normal);
        CHECK(d.interpret("tell me more", NOW, an).act == Act::More);
    }

    // ---- "it" means what the answer named, most recent first ----
    {
        Dialogue d;
        d.set_followups(&fu);
        d.remember(card_for(an, "what was using the cpu last night", {"blender"}));
        Turn t = d.interpret("did it crash", NOW, an);
        CHECK(t.kind == Turn::Fresh && programs_of(t.reading) == "blender");
        // The machine-scoped "it" stays the machine.
        Turn hot = d.interpret("how hot is it right now", NOW, an);
        CHECK(programs_of(hot.reading).empty());
    }
    {
        Dialogue d;
        d.set_followups(&fu);
        d.remember(card_for(an, "what crashed this week", {"firefox", "chromium"}));
        Turn t = d.interpret("why did it crash", NOW, an);
        CHECK(t.kind == Turn::Ambiguous && t.note == "Which one, firefox or chromium?");
        CHECK(programs_of(t.reading).empty());
        // The caller asks which one; the reply picks it and the question runs.
        Card ask_back;
        ask_back.question = "why did it crash";
        ask_back.reading = t.reading;
        ask_back.clarify = true;
        ask_back.refused = true;
        ask_back.offered_names = t.reading.ambiguous;
        d.remember(ask_back);
        Turn pick = d.interpret("chromium", NOW, an);
        CHECK(pick.kind == Turn::ClarifyYes && programs_of(pick.reading) == "chromium");
        CHECK(!pick.reading.intents.empty() && pick.reading.intents[0] == Intent::Crash);
    }

    // ---- clarifying questions understand their answer ----
    {
        Dialogue d;
        d.set_followups(&fu);
        Card c = card_for(an, "how do i cook a steak");
        c.clarify = true;
        c.refused = true;
        c.offered = {Intent::Heat};
        d.remember(c);
        Turn yes = d.interpret("yes", NOW, an);
        CHECK(yes.kind == Turn::ClarifyYes && yes.reading.intents.size() == 1 &&
              yes.reading.intents[0] == Intent::Heat);
        CHECK(d.interpret("no", NOW, an).kind == Turn::ClarifyNo);
    }
    {
        Dialogue d;
        d.set_followups(&fu);
        Card c = card_for(an, "what was the machine like");
        c.clarify = true;
        c.refused = true;
        c.offered = {Intent::Heat, Intent::Memory};
        d.remember(c);
        Turn second = d.interpret("the second one", NOW, an);
        CHECK(second.kind == Turn::ClarifyYes && second.reading.intents[0] == Intent::Memory);
        Turn word = d.interpret("memory", NOW, an);
        CHECK(word.kind == Turn::ClarifyYes && word.reading.intents[0] == Intent::Memory);
    }

    // ---- corrections swap one thing into the last question ----
    {
        Dialogue d;
        d.set_followups(&fu);
        d.remember(card_for(an, "did blender run yesterday", {"blender"}));
        Turn t = d.interpret("no i meant firefox", NOW, an);
        CHECK(t.kind == Turn::Correction && programs_of(t.reading) == "firefox");
        CHECK(t.reading.window.from == T(10,12,0,0) && t.reading.window.to == T(10,13,0,0));
        Turn w = d.interpret("no, i meant last night", NOW, an);
        CHECK(w.kind == Turn::Correction && programs_of(w.reading) == "blender" &&
              w.reading.window.from == T(10,12,18,0));
        Turn n = d.interpret("not blender, firefox", NOW, an);
        CHECK(n.kind == Turn::Correction && programs_of(n.reading) == "firefox");
        // A whole new question that starts with "actually" is just a new question.
        Turn fresh = d.interpret("actually what crashed today", NOW, an);
        CHECK(fresh.kind == Turn::Fresh);
    }

    // ---- going back to an earlier topic ----
    {
        Dialogue d;
        d.set_followups(&fu);
        d.remember(card_for(an, "what used the most memory this week", {"llama-server"}));
        d.remember(card_for(an, "what crashed today"));
        Turn t = d.interpret("back to the memory thing", NOW, an);
        CHECK(t.kind == Turn::TopicReturn && t.card && t.card->topic == Intent::Memory &&
              t.note == "Back to memory: ");
        Turn none = d.interpret("back to the battery thing", NOW, an);
        CHECK(none.kind == Turn::TopicReturn && !none.card && none.note == "We haven't talked about battery yet.");
    }

    // ---- "again" with nothing earlier on that topic is a question of its own ----
    {
        Dialogue d;
        d.set_followups(&fu);
        Turn t = d.interpret("the torrent app didnt crash again did it", NOW, an);
        bool crash = false;
        for (auto i : t.reading.intents) crash |= i == Intent::Crash;
        CHECK(t.kind == Turn::Fresh && t.reading.window.to == NOW && crash);
        Turn y = d.interpret("what crashed yesterday again", NOW, an);
        CHECK(y.kind == Turn::Fresh && y.reading.window.stated && y.reading.window.from == T(10,12,0,0));
        // With an earlier crash answer, "again" still goes back to it.
        d.remember(card_for(an, "what crashed today"));
        Turn back = d.interpret("the crashes again", NOW, an);
        CHECK(back.kind == Turn::TopicReturn && back.card && back.card->topic == Intent::Crash);
    }

    // ---- a full new question inherits nothing ----
    {
        Dialogue d;
        d.set_followups(&fu);
        d.remember(card_for(an, "what did blender do yesterday", {"blender"}));
        Turn t = d.interpret("what crashed in firefox today", NOW, an);
        CHECK(t.kind == Turn::Fresh && programs_of(t.reading) == "firefox" && t.reading.window.from == T(10,13,0,0));
    }

    // ---- a refused answer is no context ----
    {
        Dialogue d;
        d.set_followups(&fu);
        Card c = card_for(an, "asdf qwer", {});
        c.refused = true;
        d.remember(c);
        Turn t = d.interpret("and yesterday?", NOW, an);
        CHECK(t.kind == Turn::Fresh);
    }

    std::printf("dialogue: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
