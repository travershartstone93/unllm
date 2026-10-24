// The exam: questions with known correct readings, frozen at
// "now" = Tuesday 2026-10-13 21:30 local. The library is not done until the
// exam says so, and every table in the library grows one failure at a time.

#include <chrono>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "unllm/unllm.h"

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

struct Expect {
    const char* question;
    int64_t from, to;       // 0,0: expect the unstated default (last 24 h)
    const char* intents;    // comma list, "" means none (story)
    const char* entities;   // comma list of type:value, "" means none
    const char* flags = ""; // comma list: polar,count,most,least,neg,live,self
    const char* wants = ""; // expected answer shape; "" means unchecked
    const char* judge = ""; // expected judgment frame; "" means unchecked
    const char* frame = ""; // expected request frame; "" means unchecked
};

static std::string intents_of(const Reading& r) {
    std::string s;
    for (auto i : r.intents) s += (s.empty() ? "" : ",") + std::string(intent_name(i));
    return s;
}
static std::string entities_of(const Reading& r) {
    std::string s;
    for (const auto& e : r.entities) s += (s.empty() ? "" : ",") + e.type + ":" + e.value;
    return s;
}
static std::string wants_of(const Reading& r) { return wants_name(r.wants); }
static std::string judge_of(const Reading& r) {
    switch (r.judge) {
    case Judge::None: return "none";
    case Judge::Normal: return "normal";
    case Judge::Compare: return "compare";
    case Judge::Trend: return "trend";
    case Judge::Changed: return "changed";
    }
    return "?";
}
static std::string flags_of(const Reading& r) {
    std::string s;
    auto add = [&](const char* f) { s += (s.empty() ? "" : ",") + std::string(f); };
    if (r.form == Form::Polar) add("polar");
    if (r.form == Form::Count) add("count");
    if (r.degree == Degree::Most) add("most");
    if (r.degree == Degree::Least) add("least");
    if (r.negated) add("neg");
    if (r.window.live) add("live");
    if (r.self_named) add("self");
    return s;
}

int main() {
    Analyzer an;
    // kswapd0 stands for the kernel threads callers add from culprit lists.
    an.set_programs({"blender", "firefox", "chromium", "ollama", "audacity", "claude", "ktorrent", "kswapd0"});

    const Expect exam[] = {
        // ---- time windows (the phrase book) ----
        {"what happened yesterday", T(10,12,0,0), T(10,13,0,0), "", ""},
        {"what happened today", T(10,13,0,0), NOW, "", ""},
        {"what happened last night", T(10,12,18,0), T(10,13,6,0), "", ""},
        {"what happened this morning", T(10,13,5,0), T(10,13,12,0), "", ""},
        {"what happened this afternoon", T(10,13,12,0), T(10,13,18,0), "", ""},
        {"what happened yesterday evening", T(10,12,18,0), T(10,12,23,0), "", ""},
        {"what happened 2 hours ago", NOW-2*3600-45*60, NOW-2*3600+45*60, "", ""},
        {"what happened two hours ago", NOW-2*3600-45*60, NOW-2*3600+45*60, "", ""},
        {"what happened last tuesday", T(10,6,0,0), T(10,7,0,0), "", ""},
        {"what happened last friday", T(10,9,0,0), T(10,10,0,0), "", ""},
        {"what happened since monday", T(10,12,0,0), NOW, "", ""},
        {"what happened on october 10", T(10,10,0,0), T(10,11,0,0), "", ""},
        {"what happened on october 10th", T(10,10,0,0), T(10,11,0,0), "", ""},
        {"what happened last week", T(10,5,0,0), T(10,12,0,0), "", ""},
        {"what happened the day before yesterday", T(10,11,0,0), T(10,12,0,0), "", ""},
        {"what happened between 9 and 11 last night", T(10,12,21,0), T(10,12,23,0), "", ""},
        {"what happened around 3pm yesterday", T(10,12,14,15), T(10,12,15,45), "", ""},
        {"what happened in the past 10 minutes", NOW-600, NOW, "", ""},
        {"what happened in the last 3 hours", NOW-3*3600, NOW, "", ""},
        {"what happened this month", T(10,1,0,0), NOW, "", ""},
        {"what happened this week", T(10,12,0,0), NOW, "", ""},
        {"what happened", 0, 0, "", ""},
        // ---- intents (the topic tables) ----
        {"why did the fan scream last night", T(10,12,18,0), T(10,13,6,0), "heat", ""},
        {"why was it so hot yesterday", T(10,12,0,0), T(10,13,0,0), "heat", ""},
        {"how close did it get to shutting down", 0, 0, "heat", ""},
        {"why was everything slow this morning", T(10,13,5,0), T(10,13,12,0), "slow", ""},
        {"why is my computer sluggish", 0, 0, "slow", "", "self"},
        {"did anything freeze yesterday", T(10,12,0,0), T(10,13,0,0), "slow", "", "polar"},
        {"was it swapping last night", T(10,12,18,0), T(10,13,6,0), "memory", "", "polar"},
        {"is something leaking memory", 0, 0, "memory", "", "polar"},
        {"what was hammering the disk yesterday", T(10,12,0,0), T(10,13,0,0), "disk", ""},
        {"who used the internet last night", T(10,12,18,0), T(10,13,6,0), "network", ""},
        {"is anything torrenting", 0, 0, "sharing", "", "polar"},
        {"what got installed yesterday", T(10,12,0,0), T(10,13,0,0), "update", ""},
        {"did my settings change last week", T(10,5,0,0), T(10,12,0,0), "config", "", "polar"},
        {"what crashed today", T(10,13,0,0), NOW, "crash", ""},
        {"is this machine safe", 0, 0, "trust", "", "polar,self"},
        {"what drained the battery yesterday", T(10,12,0,0), T(10,13,0,0), "battery", ""},
        {"what starts at boot", 0, 0, "startup", ""},
        // ---- entities (the gazetteers) ----
        {"what did blender do this morning", T(10,13,5,0), T(10,13,12,0), "", "program:blender"},
        {"what did blendr do yesterday", T(10,12,0,0), T(10,13,0,0), "", "program:blender"},
        {"is chromium safe", 0, 0, "trust", "program:chromium", "polar"},
        {"is chromum safe", 0, 0, "trust", "program:chromium", "polar"},
        {"what is pid 2671", 0, 0, "", "pid:2671"},
        {"did it hit 90 degrees yesterday", T(10,12,0,0), T(10,13,0,0), "heat", "quantity:90 C", "polar"},
        {"when was the cpu over 80% today", T(10,13,0,0), NOW, "slow", "quantity:80 %"},
        {"what was ktorrent uploading last night", T(10,12,18,0), T(10,13,6,0), "network", "program:ktorrent"},
        // ---- the realistic blend ----
        // "while firefox was open" is a condition on the events, not the subject.
        {"why did the fan scream while firefox was open last night", T(10,12,18,0), T(10,13,6,0), "heat", "", "", "cause", "", "condition"},
        {"did claude make it slow this afternoon", T(10,13,12,0), T(10,13,18,0), "slow", "program:claude", "polar"},
        {"how hot did it get during the update yesterday", T(10,12,0,0), T(10,13,0,0), "heat", ""},  // the update: an event (event_during)
        {"did anything change since tuesday", T(10,6,0,0), NOW, "config", "", "polar"},
        // ---- facing users: identity, help, live, polar, counts, degrees ----
        {"what is this machine", 0, 0, "identity", "", "self"},
        {"what cpu does this machine have", 0, 0, "slow,identity", "", "self"},
        {"how much ram does it have", 0, 0, "memory,identity", ""},
        {"which os is this", 0, 0, "identity", ""},
        {"how long has it been up", 0, 0, "identity", ""},
        {"did this machine crash last night", T(10,12,18,0), T(10,13,6,0), "crash", "", "polar,self"},
        {"how hot is it right now", NOW-60, NOW, "heat", "", "live"},
        {"what is using the cpu now", NOW-60, NOW, "slow", "", "live"},
        {"am i on battery", 0, 0, "battery", "", "polar"},
        {"what was the hottest it has been", 0, 0, "heat", "", "most"},
        {"which program used the most memory this week", T(10,12,0,0), NOW, "memory", "", "most"},
        {"how many times did firefox crash this month", T(10,1,0,0), NOW, "crash", "program:firefox", "count"},
        {"how often does the fan max out", 0, 0, "heat", "", "count,most"},
        {"did blender run yesterday", T(10,12,0,0), T(10,13,0,0), "", "program:blender", "polar"},
        {"was there a crash last night", T(10,12,18,0), T(10,13,6,0), "crash", "", "polar"},
        {"it never got hot this week", T(10,12,0,0), NOW, "heat", "", "neg"},
        {"what can i ask you", 0, 0, "help", ""},
        {"help", 0, 0, "help", ""},
        // ---- the second review's misses ----
        // The subject the question names first leads; "busy" alone must not
        // outrank the disk the question is actually about.
        {"why was the disk busy this afternoon", T(10,13,12,0), T(10,13,18,0), "disk,slow", ""},
        {"what usb devices did i plug in today", T(10,13,0,0), NOW, "devices", ""},
        {"when did i flash the esp32", 0, 0, "flash", ""},
        {"was the kernel updated", 0, 0, "update", "", "polar"},
        {"what made it nearly shut down", 0, 0, "heat", "", "", "cause"},
        {"did it almost shut down last night", T(10,12,18,0), T(10,13,6,0), "heat", "", "polar"},
        // ---- the answer's shape: when wants a moment, why wants the cause ----
        {"when did the fan first scream", 0, 0, "heat", "", "", "time"},
        {"what was the last crash", 0, 0, "crash", "", "", "time"},
        {"why did everything freeze this morning", T(10,13,5,0), T(10,13,12,0), "slow", "", "", "cause"},
        {"how long was blender running yesterday", T(10,12,0,0), T(10,13,0,0), "", "program:blender", "", "duration"},
        {"how often does it overheat", 0, 0, "heat", "", "count", "count"},
        {"how hot did it get yesterday", T(10,12,0,0), T(10,13,0,0), "heat", "", "", "quantity"},
        {"which program hammered the disk yesterday", T(10,12,0,0), T(10,13,0,0), "disk", "", "", "name"},
        {"did ktorrent upload anything last night", T(10,12,18,0), T(10,13,6,0), "network", "program:ktorrent", "polar", "yesno"},
        {"what crashed yesterday", T(10,12,0,0), T(10,13,0,0), "crash", "", "", "fact"},
        // ---- naming the unknown: "what is X" asks about X itself ----
        // "what is X" asks for a definition; the name entity tells the caller
        // which thing, so it can answer from its own notes first.
        {"what is snapperd", 0, 0, "", "name:snapperd", "", "fact", "", "define"},
        {"what is kswapd0", 0, 0, "", "program:kswapd0", "", "fact", "", "define"},
        // ---- judgment: normal, compared, trending, changed ----
        {"is this cpu usage normal", 0, 0, "slow", "", "polar", "yesno", "normal"},
        {"is 70% memory a lot", 0, 0, "memory", "quantity:70 %", "polar", "yesno", "normal"},
        {"is the machine getting slower", 0, 0, "slow", "", "polar,self", "", "trend"},
        {"has it been getting hotter", 0, 0, "heat", "", "polar", "", "trend"},
        {"did memory use change after october 10", T(10,10,0,0), T(10,11,0,0), "memory,config", "", "polar", "", "changed"},
        // The word "change" alone is a config question, never a judgment.
        {"did my settings change last week", T(10,5,0,0), T(10,12,0,0), "config", "", "polar", "", "none"},
        // ---- the health check ----
        {"what is wrong with this machine", 0, 0, "diagnose", "", "self"},
        {"is this machine ok", 0, 0, "diagnose", "", "polar,self"},
        {"diagnose this machine", 0, 0, "diagnose", "", "self"},
        // Narrowed or aimed questions are never hijacked by the rule book.
        {"what went wrong yesterday", T(10,12,0,0), T(10,13,0,0), "", ""},
        {"is my fan broken", 0, 0, "heat", "", "polar"},
        // ---- the chat assessment's misses ----
        {"how is the gpu doing", 0, 0, "gpu", ""},
        {"is the graphics card hot", 0, 0, "gpu,heat", "", "polar", "yesno"},
        {"summarize this week for me", T(10,12,0,0), NOW, "", "", "", "", "", "summary"},
        {"what did i do on tuesday", T(10,13,0,0), NOW, "", ""},   // NOW is a Tuesday: today so far
        {"what happened on friday", T(10,9,0,0), T(10,10,0,0), "", ""},
        {"should i be worried about the temperature", 0, 0, "heat", "", "polar", "yesno", "", "worry"},
        {"is my ssd healthy", 0, 0, "disk", "", "polar", "yesno", "", "worry"},
        {"what should i do about the heat", 0, 0, "heat", "", "", "", "", "advice"},
        {"explain why kswapd0 was busy", 0, 0, "slow", "program:kswapd0", "", "cause"},
        {"which uses more cpu, firefox or chromium", 0, 0, "slow", "program:firefox,program:chromium", "", "", "", "choice"},
        {"how hot does it get when blender is running", 0, 0, "heat", "", "", "quantity", "", "condition"},
        {"what time did the machine wake up today", T(10,13,0,0), NOW, "battery", "", "self", "time"},
        {"when did the lid close last night", T(10,12,18,0), T(10,13,6,0), "battery", "", "", "time"},
        {"how long will the battery last", 0, 0, "battery", "", "", "duration"},
    };

    int qnum = 0, qfail = 0;
    for (const auto& e : exam) {
        ++qnum;
        Reading r = an.read(e.question, NOW);
        bool ok = true;
        if (e.from == 0 && e.to == 0) {
            ok &= !r.window.stated && r.window.from == NOW - 24 * 3600 && r.window.to == NOW;
        } else {
            ok &= r.window.stated && r.window.from == e.from && r.window.to == e.to;
        }
        ok &= intents_of(r) == e.intents;
        ok &= entities_of(r) == e.entities;
        ok &= flags_of(r) == e.flags;
        if (*e.wants) ok &= wants_of(r) == e.wants;
        if (*e.judge) ok &= judge_of(r) == e.judge;
        if (*e.frame) ok &= std::string(frame_name(r.frame)) == e.frame;
        ++checks;
        if (!ok) {
            ++failures;
            ++qfail;
            std::printf("  EXAM %02d \"%s\"\n        window %lld..%lld stated=%d  intents=[%s]  entities=[%s]  flags=[%s]"
                        "  wants=%s judge=%s frame=%s\n",
                        qnum, e.question, (long long)(r.window.from - NOW), (long long)(r.window.to - NOW),
                        int(r.window.stated), intents_of(r).c_str(), entities_of(r).c_str(), flags_of(r).c_str(),
                        wants_of(r).c_str(), judge_of(r).c_str(), frame_name(r.frame));
        }
    }
    std::printf("exam: %d/%d questions correct\n", qnum - qfail, qnum);

    // ---- follow-up memory ----
    {
        Reading prev = an.read("what happened yesterday", NOW);
        Reading r = an.read("and the day before?", NOW, &prev);
        CHECK(r.window.from == T(10,11,0,0) && r.window.to == T(10,12,0,0));
        Reading r2 = an.read("what about the day after?", NOW, &prev);
        CHECK(r2.window.from == T(10,13,0,0) && r2.window.to == T(10,14,0,0));
    }

    // ---- follow-ups carry the subject, not just the window ----
    {
        Reading prev = an.read("what did blender do yesterday", NOW);
        Reading r = an.read("and this morning?", NOW, &prev);
        CHECK(r.window.from == T(10,13,5,0) && r.window.to == T(10,13,12,0));
        CHECK(entities_of(r) == "program:blender");
        Reading prev2 = an.read("why did the fan scream last night", NOW);
        Reading r2 = an.read("what about this morning?", NOW, &prev2);
        CHECK(intents_of(r2) == "heat");
        // A fresh question is not a follow-up: nothing may leak into it.
        Reading r3 = an.read("what crashed today", NOW, &prev);
        CHECK(entities_of(r3).empty() && intents_of(r3) == "crash");
    }

    // ---- fragments: a terse follow-up patches the previous question ----
    {
        Reading prev = an.read("what did blender do yesterday", NOW);
        // A bare window re-runs the question in the new window, on prev's day.
        Reading r1 = an.read("just the evening", NOW, &prev);
        CHECK(r1.window.from == T(10,12,18,0) && r1.window.to == T(10,12,23,0));
        CHECK(entities_of(r1) == "program:blender" && r1.domain >= 0.9);
        // A bare program swaps the subject, keeps the window.
        Reading r2 = an.read("and firefox?", NOW, &prev);
        CHECK(entities_of(r2) == "program:firefox");
        CHECK(r2.window.from == T(10,12,0,0) && r2.window.to == T(10,13,0,0));
        // A bare topic swaps the intent, keeps subject and window.
        Reading r3 = an.read("what about memory?", NOW, &prev);
        CHECK(intents_of(r3) == "memory" && entities_of(r3) == "program:blender");
        CHECK(r3.window.from == T(10,12,0,0));
        // A bare count form recounts the previous question.
        Reading r4 = an.read("how many times?", NOW, &prev);
        CHECK(r4.form == Form::Count && entities_of(r4) == "program:blender");
        CHECK(r4.window.from == T(10,12,0,0));
        // Real short questions are never fragments.
        Reading h = an.read("help", NOW, &prev);
        CHECK(intents_of(h) == "help" && entities_of(h).empty());
        Reading s = an.read("what happened", NOW, &prev);
        CHECK(entities_of(s).empty() && intents_of(s).empty());
    }

    // ---- spelling: chat-speak and typos, never real words ----
    {
        Speller sp;
        CHECK(sp.load_chatspeak("data/chatspeak.txt"));
        sp.set_known(an.known_words(false));
        std::vector<std::pair<std::string, std::string>> ch;
        std::string fixed = sp.fix("wat crashd 2day", &ch);
        CHECK(fixed == "what crash today");
        CHECK(ch.size() == 1 && ch[0].first == "crashd" && ch[0].second == "crash");
        Reading r = an.read(fixed, NOW);
        CHECK(intents_of(r) == "crash" && r.window.from == T(10,13,0,0) && r.domain >= 0.9);
        // Real words are left alone, even when a machine word is one key away.
        for (const char* q : {"how do i cook a steak", "what is the capital of france", "how do i bake bread",
                              "tell me about fusion"})
            CHECK(sp.fix(q) == q);
        CHECK(an.read(sp.fix("what is the capital of france"), NOW).domain <= 0.25);
        // A known word with an ending the stemmer takes off is that word, not a typo: the plural stays.
        CHECK(sp.fix("whats the python thing that keeps running in the evenings") ==
              "what is the python thing that keeps running in the evenings");
        CHECK(sp.fix("my settings") == "my settings");
    }

    // ---- the morning that has not started yet ----
    {
        Reading r = an.read("what happened this morning", T(10,14,2,38));
        CHECK(r.window.shifted && r.window.from == T(10,13,5,0) && r.window.to == T(10,13,12,0));
        Reading ok = an.read("what happened this morning", NOW);
        CHECK(!ok.window.shifted);
    }

    // ---- anchored: machine questions never go to reference knowledge ----
    {
        auto anchored = [&](const char* q) { return an.read(q, NOW).anchored; };
        CHECK(anchored("how hot is it right now"));
        CHECK(anchored("did blender crash yesterday"));
        CHECK(anchored("why is my memory full"));
        CHECK(anchored("what is this machine"));
        CHECK(anchored("am i on battery"));
        CHECK(!anchored("how do i cook a steak"));
        CHECK(!anchored("what is a computer"));
        CHECK(!anchored("how does computer memory work"));
    }

    // ---- the router: the machine first, the world for everything else ----
    {
        struct RouteExpect { const char* q; const char* route; const char* frame; };
        const RouteExpect pairs[] = {
            // machine questions, and their look-alikes about the world
            {"how hot is it right now", "domain", ""},
            {"how hot is the sun", "world", ""},
            {"am i on battery", "domain", ""},
            {"how do batteries work", "world", "explain"},
            {"what is using the cpu now", "domain", ""},
            {"what is a cpu", "world", "define"},
            {"what is this machine", "domain", ""},
            {"what is a computer", "world", "define"},
            {"why is my memory full", "domain", ""},
            {"how does computer memory work", "world", "explain"},
            {"what is kswapd0", "domain", ""},
            {"what is photosynthesis", "world", "define"},
            {"is my ssd healthy", "domain", "worry"},
            {"what is an ssd", "world", "define"},
            {"what was the hottest the cpu got today", "domain", ""},
            {"what is the hottest planet", "world", "define"},
            {"when did i last update the system", "domain", ""},
            {"when was the first computer made", "world", ""},
            {"why was kswapd0 busy", "domain", ""},
            {"why is the sky blue", "world", "explain"},
            {"how do i cook a steak", "world", "howto"},
            {"which uses more cpu, firefox or chromium", "domain", "choice"},
            {"which is better, tea or coffee", "category", "opinion"},
            {"is my cpu temperature normal", "domain", ""},
            {"what's the weather today", "category", "network"},
            {"who won the football last night", "category", "network"},
            {"write me a poem about the sea", "world", "poem"},
            {"write me a story about a dragon", "category", "creative"},
            {"who invented the telephone", "world", "explain"},
            {"what does ephemeral mean", "world", "define"},
            {"why did the fan scream last night", "domain", ""},
            {"what happened yesterday", "domain", ""},
            {"is it raining outside right now", "category", "network"},
            {"what time is it in tokyo right now", "category", "network"},
            {"what are the winning lottery numbers for tonight", "category", "network"},
            {"what is my home address", "category", "personal"},
            {"what did i eat for breakfast", "category", "personal"},
            {"how many unread emails do i have", "category", "personal"},
            // Past results and general "can i" questions are not refused; with no
            // machine word, the front end answers a domain route from the books.
            {"who won the battle of saratoga", "domain", "none"},
            {"can i eat raw eggs", "domain", "none"},
        };
        for (const auto& p : pairs) {
            Reading r = an.read(p.q, NOW);
            bool ok = std::string(route_name(route(r))) == p.route;
            if (*p.frame) ok &= std::string(frame_name(r.frame)) == p.frame;
            CHECK(ok);
            if (!ok)
                std::printf("    route \"%s\": got %s/%s, want %s/%s\n", p.q, route_name(route(r)), frame_name(r.frame),
                            p.route, p.frame);
        }
        // The subject is carried for the reference search.
        CHECK(an.read("what is a photon", NOW).subject == "photon");
        CHECK(an.read("how does computer memory work", NOW).subject == "computer memory");
        CHECK(an.read("why is the sky blue", NOW).subject == "sky");
        CHECK(an.read("write me a poem about the sea", NOW).subject == "the sea");
    }

    // ---- compare: the referent window steps aside for the comparison ----
    {
        Reading r = an.read("was it hotter than yesterday", NOW);
        CHECK(judge_of(r) == "compare" && !r.window.stated);
        CHECK(r.compare_window && r.compare_window->from == T(10,12,0,0) &&
              r.compare_window->to == T(10,13,0,0));
        bool heat = false;
        for (auto i : r.intents) heat |= i == Intent::Heat;
        CHECK(heat && r.domain >= 0.9);
        Reading u = an.read("is it hotter than usual", NOW);
        CHECK(judge_of(u) == "normal");
    }

    // ---- pronouns: "it" is the one program we were just talking about ----
    {
        Reading prev = an.read("what did blender do yesterday", NOW);
        Reading r = an.read("did it crash", NOW, &prev);
        CHECK(entities_of(r) == "program:blender" && intents_of(r) == "crash");
        CHECK(r.form == Form::Polar);
        // Machine-scoped "it" is the machine, never the program.
        Reading hot = an.read("how hot is it right now", NOW, &prev);
        CHECK(entities_of(hot).empty() && intents_of(hot) == "heat");
        // Two candidate programs: never guess.
        Reading prev2 = an.read("did blender or firefox crash yesterday", NOW);
        Reading amb = an.read("what did it do", NOW, &prev2);
        CHECK(entities_of(amb).empty());
    }

    // ---- rewrites: politeness coats come off before analysis ----
    {
        Rewrites rw;
        CHECK(rw.load("data/rewrites.txt") && rw.size() >= 8);
        auto same = [&](const char* coated, const char* plain) {
            Reading a = an.read(rw.apply(coated), NOW);
            Reading b = an.read(plain, NOW);
            return a.window.from == b.window.from && a.window.to == b.window.to &&
                   intents_of(a) == intents_of(b) && entities_of(a) == entities_of(b) &&
                   flags_of(a) == flags_of(b) && a.domain >= 0.9;
        };
        CHECK(same("could you please tell me what crashed yesterday", "what crashed yesterday"));
        CHECK(same("i was wondering why the fan screamed last night", "why did the fan scream last night"));
        CHECK(same("do you know when i last updated the system", "when did i last update the system"));
        // Off-domain stays off-domain, coat or no coat.
        CHECK(an.read(rw.apply("could you tell me the capital of france"), NOW).domain <= 0.25);
        // The classics keep their exact shape for the banter table.
        CHECK(rw.apply("tell me a joke") == "tell me a joke");
    }

    // ---- the small parts on their own ----
    {
        auto t = tokenize("Why did Blender crash at 3pm?");
        CHECK(t.size() == 6 && t[2].lower == "blender" && t[5].lower == "3pm");
        CHECK(normalize_word("crashes") == "crash" && normalize_word("crashed") == "crash" &&
              normalize_word("crashing") == "crash");
        CHECK(normalize_word("ran") == "run" && normalize_word("memories") == "memory");
        CHECK(normalize_word("swapping") == "swap" && normalize_word("disks") == "disk");
        CHECK(edit_distance("blendr", "blender", 2) == 1);
        CHECK(edit_distance("chrome", "chromium", 2) > 2);
        CHECK(edit_distance("firefox", "firefox", 2) == 0);
    }

    // ---- nonsense and off-topic: know when you are out of your depth ----
    {
        auto d = [&](const char* q) { return an.read(q, NOW).domain; };
        // Garbage and off-topic must never crash, must keep the default
        // window, and must score low so the caller can refuse to guess.
        for (const char* q : {"", "   ", "??", "42", "asdf qwer zxcv poiu",
                              "what is the capital of france", "how do i bake bread",
                              "tell me a joke", "colorless green ideas sleep furiously"}) {
            Reading r = an.read(q, NOW);
            CHECK(!r.window.stated && r.intents.empty() && r.entities.empty());
            CHECK(r.domain <= 0.25);
        }
        // Domain words used off-domain score middling: a signal, not a verdict.
        CHECK(d("how do i cook a steak") <= 0.6);
        CHECK(d("update me on the news") <= 0.6);
        CHECK(d("who won the football last night") <= 0.6);
        // Real questions about the machine score high.
        CHECK(d("why did the fan scream last night") >= 0.9);
        CHECK(d("did blender crash yesterday") >= 0.9);
        CHECK(d("what happened yesterday") >= 0.9);
        // Questions about the machine itself are about the machine.
        CHECK(d("what is this machine") >= 0.9);
        CHECK(d("did blender run yesterday") >= 0.9);
        CHECK(d("which program used the most memory this week") >= 0.9);
        CHECK(d("it never got hot this week") >= 0.9);
        CHECK(d("am i on battery") >= 0.9);
        // New rules must not dent the misses' own domain scores.
        CHECK(d("what usb devices did i plug in today") >= 0.9);
        CHECK(d("when did i flash the esp32") >= 0.9);
        CHECK(d("what made it nearly shut down") >= 0.9);
        // Non-ASCII text is simply not understood yet: empty, low, no crash.
        Reading jp = an.read("\xe8\xad\xa6\xe5\x91\x8a \xe6\xb8\xa9\xe5\xba\xa6", NOW);
        CHECK(jp.terms.empty() && jp.domain == 0.0);
    }

    // ---- the realizer: grammar by rule ----
    {
        CHECK(join_list({"a"}) == "a");
        CHECK(join_list({"a", "b"}) == "a and b");
        CHECK(join_list({"a", "b", "c"}) == "a, b and c");
        CHECK(plural_noun("process", 3) == "processes");
        CHECK(plural_noun("memory", 2) == "memories");
        CHECK(plural_noun("program", 1) == "program");
        CHECK(plural_noun("crash", 5) == "crashes");
        CHECK(number_word(3) == "three" && number_word(0) == "no" && number_word(26) == "26");
        CHECK(past_tense("crash", false) == "crashed");
        CHECK(past_tense("use", false) == "used");
        CHECK(past_tense("grow", false) == "grew");
        CHECK(past_tense("stop", false) == "stopped");
        CHECK(past_tense("try", false) == "tried");
        CHECK(past_tense("be", false) == "was" && past_tense("be", true) == "were");
        CHECK(present_tense("hold", false) == "holds" && present_tense("be", true) == "are");
        CHECK(present_tense("crash", false) == "crashes");
        CHECK(indefinite("event") == "an event" && indefinite("program") == "a program");

        // Aggregation: one subject, two facts, one sentence.
        Clause a1{{"blender", true}, "use", "74% of the CPU", Tense::Past};
        Clause a2{{"blender", true}, "grow", "by 1.4 GB", Tense::Past};
        CHECK(realize({a1, a2}) == "Blender used 74% of the CPU and grew by 1.4 GB.");

        // Plural subject with a count agrees with its verb.
        Clause b1{{"program", false, 3, "you started"}, "be", "responsible", Tense::Past};
        CHECK(realize({b1}) == "Three programs you started were responsible.");

        // A returning subject becomes a pronoun.
        Clause c1{{"blender", true}, "use", "74% of the CPU", Tense::Past};
        Clause c2{{"llama-server", true}, "hold", "6.4 GB of video memory", Tense::Past};
        Clause c3{{"blender", true}, "grow", "by 1.4 GB", Tense::Past};
        CHECK(realize({c1, c2, c3}) ==
              "Blender used 74% of the CPU. Llama-server held 6.4 GB of video memory. Blender also grew by 1.4 GB.");
        // The pronoun is used when its owner was the previous subject.
        CHECK(realize({c1, c3, c2}) ==
              "Blender used 74% of the CPU and grew by 1.4 GB. Llama-server held 6.4 GB of video memory.");
        Clause d1{{"firefox", true}, "connect", "to 94 computers", Tense::Past};
        Clause d2{{"firefox", true}, "upload", "nothing", Tense::Past};
        Clause d3{{"ktorrent", true}, "share", "with 49 computers", Tense::Past};
        CHECK(realize({d1, d2, d3}) ==
              "Firefox connected to 94 computers and uploaded nothing. Ktorrent shared with 49 computers.");

        // Shared predicates merge into one plural subject.
        Clause m1{{"firefox", true}, "crash", "", Tense::Past};
        Clause m2{{"chromium", true}, "crash", "", Tense::Past};
        CHECK(realize({m1, m2}) == "Firefox and chromium crashed.");

        // Contrast joins with ", but" instead of a new sentence.
        Clause k1{{"blender", true}, "use", "74% of the CPU", Tense::Past};
        Clause k2{{"blender", true}, "crash", "", Tense::Past};
        k2.negated = true;
        k2.contrast = true;
        CHECK(realize({k1, k2}) == "Blender used 74% of the CPU, but did not crash.");
        Clause k3{{"firefox", true}, "crash", "", Tense::Past};
        k3.negated = true;
        k3.contrast = true;
        CHECK(realize({k1, k3}) == "Blender used 74% of the CPU, but firefox did not crash.");

        // The present perfect: the natural tense for "since" windows.
        Clause pp{{"it", true}, "be", "up for two days", Tense::PresentPerfect};
        CHECK(realize({pp}) == "It has been up for two days.");
        Clause pc{{"firefox", true}, "crash", "twice since monday", Tense::PresentPerfect};
        CHECK(realize({pc}) == "Firefox has crashed twice since monday.");
        Clause pn{{"it", true}, "overheat", "", Tense::PresentPerfect};
        pn.negated = true;
        CHECK(realize({pn}) == "It has not overheated.");

        // An explicitly plural subject agrees even without a count.
        Clause pl{{"the disks", true, -1, "", true}, "be", "quiet right now", Tense::Present};
        CHECK(realize({pl}) == "The disks are quiet right now.");

        // Negation, by rule.
        Clause n1{{"firefox", true}, "crash", "", Tense::Past};
        n1.negated = true;
        CHECK(realize({n1}) == "Firefox did not crash.");
        Clause n2{{"it", true}, "be", "on battery", Tense::Present};
        n2.negated = true;
        CHECK(realize({n2}) == "It is not on battery.");

        // Yes/no answers wrap the evidence sentence.
        CHECK(realize_polar(false, {n1}) == "No, Firefox did not crash.");
        Clause y1{{"crash", false, 3}, "be", "recorded", Tense::Past};
        CHECK(realize_polar(true, {y1}) == "Yes, three crashes were recorded.");
        // A zero count already reads as a no; never say "No, no".
        Clause z0{{"crash", false, 0}, "be", "recorded last night", Tense::Past};
        CHECK(realize_polar(false, {z0}) == "No crashes were recorded last night.");
        // Only bare names keep their capital after the comma.
        Clause y2{{"the CPU", true}, "reach", "99 C", Tense::Past};
        CHECK(realize_polar(true, {y2}) == "Yes, the CPU reached 99 C.");
        Clause y3{{"it", true}, "be", "plugged in", Tense::Present};
        CHECK(realize_polar(false, {y3}) == "No, it is plugged in.");

        // The refusal guarantee: off-domain questions always get words back.
        for (const char* q : {"how do i cook a steak", "what is the capital of france",
                              "asdf qwer", "", "tell me a joke"}) {
            Reading r = an.read(q, NOW);
            std::string reply = refuse(r, "this machine");
            CHECK(!reply.empty() && reply.find("I can't answer that") != std::string::npos);
        }
        Reading steak = an.read("how do i cook a steak", NOW);
        std::string sr = refuse(steak, "this machine");
        CHECK(sr.find("heat") != std::string::npos);  // it names what it recognized

        // A refusal must never deny the machine to a question that names it.
        Reading selfq;
        selfq.domain = 0.1;
        selfq.self_named = true;
        std::string selfr = refuse(selfq, "this machine");
        CHECK(selfr.find("I can't answer") != std::string::npos);
        CHECK(selfr.find("doesn't seem to be a question about") == std::string::npos);
    }

    // ---- banter: the classics get a palatable fixed reply, not a cold no ----
    {
        Banter b;
        CHECK(b.load("data/banter.txt"));
        CHECK(!b.match(tokenize("tell me a joke")).empty());
        CHECK(b.match(tokenize("what is the meaning of life")).find("42") != std::string::npos);
        CHECK(!b.match(tokenize("who are you")).empty());
        CHECK(!b.match(tokenize("what are you thinking")).empty());
        CHECK(!b.match(tokenize("are you alive")).empty());
        CHECK(!b.match(tokenize("hello")).empty());
        CHECK(!b.match(tokenize("thanks")).empty());
        // Real questions and noisy greetings are never banter.
        CHECK(b.match(tokenize("why is it hot")).empty());
        CHECK(b.match(tokenize("hello why is it hot")).empty());
        CHECK(b.match(tokenize("what is the meaning of this error")).empty());
    }

    // ---- categories: news, opinions and new writing get an honest fixed reply ----
    {
        Categories c;
        CHECK(c.load("data/categories.txt"));
        CHECK(c.reply("network", tokenize("what is the weather today")).find("weather") != std::string::npos);
        CHECK(c.reply("network", tokenize("who won the match")).find("Results need") == 0);
        CHECK(c.reply("network", tokenize("anything at all")).find("needs the internet") != std::string::npos);
        CHECK(c.reply("opinion", tokenize("tea or coffee")).find("opinions") != std::string::npos);
        CHECK(c.reply("creative", tokenize("write me a story")).find("stories") != std::string::npos);
        CHECK(c.reply("nonsense", tokenize("hello")).empty());
        CHECK(an.read("how to write a github workflow that checks a docker image", NOW).frame != Frame::Creative);
        CHECK(an.read("write me a story about a dragon", NOW).frame == Frame::Creative);
        CHECK(an.read("tea or coffee", NOW).frame == Frame::Opinion);
        CHECK(an.read("is the sky blue or green", NOW).frame != Frame::Opinion);
        Reading w = an.read("what is the weather today", NOW);
        CHECK(route(w) == Route::Category && std::string(frame_name(w.frame)) == "network");
    }

    // ---- search terms: topic words stay, topic-free words never pollute ----
    {
        // "happen" proves the question is on home ground but matches no
        // record; with it in the terms, retrieval misses and the caller's
        // tell-everything fallback never fires.
        CHECK(an.read("what happened last night", NOW).terms.empty());
        auto fan = an.read("why did the fan scream last night", NOW);
        bool has_fan = false, has_scream = false;
        for (const auto& t : fan.terms) { has_fan |= t == "fan"; has_scream |= t == "scream"; }
        CHECK(has_fan && has_scream);
        // Device words are real search terms: logs name the hardware.
        auto usb = an.read("what usb devices did i plug in today", NOW);
        bool has_usb = false;
        for (const auto& t : usb.terms) has_usb |= t == "usb";
        CHECK(has_usb);
    }

    // Short words must not fuzzy-match program names.
    {
        Analyzer tricky;
        tricky.set_programs({"sed", "node"});
        Reading r = tricky.read("who used the network yesterday", NOW);
        CHECK(r.entities.empty());
        Reading r2 = tricky.read("what did sed do yesterday", NOW);  // exact still works
        CHECK(r2.entities.size() == 1 && r2.entities[0].value == "sed");
    }

    // A topic word must not fuzzy-match a program name.
    {
        Analyzer tricky;
        tricky.set_programs({"steam", "scrcpy"});
        Reading r = tricky.read("why did the fan scream last night", NOW);
        CHECK(r.entities.empty());
        bool heat = false;
        for (auto i : r.intents) heat |= i == Intent::Heat;
        CHECK(heat);
    }

    // ---- synonyms: words it was never taught reach the right topic ----
    {
        Analyzer syn;
        syn.set_programs({"blender"});
        syn.set_synonyms(load_synonyms("data/synonyms.txt"));
        auto has = [&](const char* q, Intent want) {
            Reading r = syn.read(q, NOW);
            for (auto i : r.intents)
                if (i == want) return true;
            return false;
        };
        CHECK(has("why is my computer lethargic", Intent::Slow));
        CHECK(has("the laptop was roasting yesterday", Intent::Heat));
        CHECK(has("blender bricked itself", Intent::Crash));
        CHECK(has("did any settings change", Intent::Config));
        CHECK(has("how much juice did it use", Intent::Battery));
        // The synonym raises the domain score too: understood is understood.
        CHECK(syn.read("why is my computer lethargic", NOW).domain >= 0.9);
        // Without the table, the same question stays under the answer gate:
        // the layer earns its keep ("computer" counts, "lethargic" cannot).
        CHECK(an.read("why is my computer lethargic", NOW).domain < 0.6);
    }

    // ---- retrieval: the right records surface, in the right window ----
    {
        Index ix;
        CHECK(ix.ok());
        ix.add(1, T(10,12,22,46), "event", "CPU peaked at 47%, the CPU reached 96 C with the fan at 80%, mostly blender");
        ix.add(2, T(10,12,20,35), "pacman", "upgraded 281 packages; installed cjson");
        ix.add(3, T(10,12,22,47), "log", "blender crashed");
        ix.add(4, T(10,5,12,0),  "event", "the fan stepped up while firefox was open");
        ix.add(5, T(10,13,9,0),  "log", "Plugged in: USB drive");
        auto r = an.read("why did the fan scream last night", NOW);
        auto hits = ix.search(r.terms, r.window.from, r.window.to);
        CHECK(hits.size() == 1 && hits[0].id == 1);  // the old fan event is outside last night
        hits = ix.search({"crash"}, T(10,12,0,0), T(10,13,0,0));
        CHECK(hits.size() == 1 && hits[0].id == 3);  // porter: "crash" finds "crashed"
        hits = ix.search({"blender"}, T(10,12,0,0), T(10,13,0,0));
        CHECK(hits.size() == 2);
        hits = ix.search({"upgrade", "package"}, T(10,12,0,0), T(10,13,0,0));
        CHECK(!hits.empty() && hits[0].id == 2);
        CHECK(ix.search({}, 0, NOW).empty());
        CHECK(ix.search({"\"weird\" syntax ("}, 0, NOW).empty());  // never FTS errors
    }

    // ---- the meaning map: arithmetic, not inference ----
    {
        // A tiny hand-built table proves the mechanics: slow-ish words point
        // one way, hot-ish words another, fruit points nowhere useful.
        std::string fx = "/tmp/unllm-fixture-vectors.bin";
        {
            std::ofstream f(fx, std::ios::binary);
            auto put32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
            f.write("UNLV", 4);
            put32(5); put32(3);
            auto word = [&](const char* w, float x, float y, float z) {
                uint8_t len = uint8_t(std::strlen(w));
                f.write(reinterpret_cast<char*>(&len), 1);
                f.write(w, len);
                float v[3] = {x, y, z};
                f.write(reinterpret_cast<char*>(v), 12);
            };
            word("slow", 1, 0.1f, 0);
            word("dawdling", 0.9f, 0.2f, 0);
            word("hot", 0, 1, 0);
            word("sweltering", 0.1f, 0.95f, 0);
            word("banana", 0, 0, 1);
        }
        Vectors vx;
        CHECK(vx.load(fx) && vx.ok() && vx.size() == 5);
        CHECK(vx.similarity("dawdling", "slow") > 0.9);
        CHECK(vx.similarity("banana", "slow") < 0.1);
        CHECK(vx.similarity("missing", "slow") == 0.0);

        Analyzer va;
        va.set_vectors(&vx);
        auto r = va.read("why is everything dawdling", NOW);
        bool has_slow = false;
        for (auto i : r.intents) has_slow |= i == Intent::Slow;
        CHECK(has_slow && r.domain >= 0.9);
        // Fruit stays unanswerable even with the map loaded.
        CHECK(va.read("i would like a banana", NOW).domain <= 0.25);
        std::remove(fx.c_str());
    }

    // ---- the shipped meaning map, if present: sanity, not precision ----
    {
        Vectors real;
        if (real.load("data/vectors.bin")) {
            CHECK(real.size() >= 200);
            CHECK(real.similarity("sluggish", "slow") > real.similarity("sluggish", "keyboard"));
            CHECK(real.similarity("roasting", "hot") > real.similarity("roasting", "disk"));
            CHECK(real.similarity("frozen", "stuck") > real.similarity("frozen", "battery"));
            CHECK(real.similarity("slow", "slow") > 0.99);
        }
    }

    // ---- the 50-question faults (2026-10-23), frozen at Fri 2026-10-23 00:01:30 ----
    {
        const int64_t N = T(10, 23, 0, 1) + 30;  // 1792728090 here: the clock of the traced runs
        auto at = [&](const char* q) { return an.read(q, N); };
        auto span = [](const Reading& r, int64_t a, int64_t b) {
            return r.window.stated && r.window.from == a && r.window.to == b;
        };
        auto second = [](const Reading& r, int64_t a, int64_t b) {
            return r.second_window && r.second_window->from == a && r.second_window->to == b;
        };
        // 1. A clock or a part of the day combines with the day it is on, in either order.
        CHECK(span(at("what was keeping the laptop busy at 3am on tuesday"), T(10,20,2,15), T(10,20,3,45)));
        CHECK(span(at("what was keeping the laptop busy at 3am tuesday"), T(10,20,2,15), T(10,20,3,45)));
        CHECK(span(at("what was keeping the laptop busy on tuesday at 3am"), T(10,20,2,15), T(10,20,3,45)));
        Reading lunch = at("was it slow at lunch yesterday");
        CHECK(span(lunch, T(10,22,12,0), T(10,22,14,0)) && !lunch.window.shifted);
        Reading morning = at("was it slow in the morning yesterday");
        CHECK(span(morning, T(10,22,5,0), T(10,22,12,0)) && !morning.window.shifted);
        CHECK(span(at("was it slow yesterday at lunch"), T(10,22,12,0), T(10,22,14,0)));
        CHECK(span(at("did i restart yesterday around 1"), T(10,22,12,15), T(10,22,13,45)));
        // "this morning" asked at 00:01 still moves back a day, and says so.
        Reading early = at("what happened this morning");
        CHECK(span(early, T(10,22,5,0), T(10,22,12,0)) && early.window.shifted);
        // 2. An hour without am/pm: the part of the day named with it, else the waking day.
        CHECK(span(at("did i restart around 1 yesterday? all my windows closed on me"), T(10,22,12,15), T(10,22,13,45)));
        CHECK(span(at("what happened at 11 last night"), T(10,22,22,15), T(10,22,23,45)));
        CHECK(span(at("what happened at 9 yesterday"), T(10,22,8,15), T(10,22,9,45)));
        CHECK(span(at("what happened about 2 hours ago"), N - 2 * 3600 - 45 * 60, N - 2 * 3600 + 45 * 60));
        // 3. "between A and B" respects "morning", am/pm and the bare-hour rule.
        CHECK(span(at("how much cpu did ollama use between 9 and 10 yesterday morning"), T(10,22,9,0), T(10,22,10,0)));
        CHECK(span(at("how much cpu did ollama use between 9 and 10 yesterday"), T(10,22,9,0), T(10,22,10,0)));
        // am/pm as a word of its own belongs to the clock before it, so the day after it still counts.
        CHECK(span(at("what happened at 9 pm on wednesday"), T(10,21,20,15), T(10,21,21,45)));
        CHECK(span(at("what happened between 5 and 9:36 pm on wednesday"), T(10,21,17,0), T(10,21,21,36)));
        CHECK(span(at("what happened between 5 pm and 9 pm on wednesday"), T(10,21,17,0), T(10,21,21,0)));
        CHECK(at("how much memory am i using").window.present);
        CHECK(span(at("how much cpu did ollama use between 9am and 10am yesterday"), T(10,22,9,0), T(10,22,10,0)));
        CHECK(span(at("what was the biggest memory hog between 7 and 8 yesterday morning"), T(10,22,7,0), T(10,22,8,0)));
        CHECK(span(at("what happened between 9 and 11 last night"), T(10,22,21,0), T(10,22,23,0)));
        // 4. "9:20" stays one clock; a clock to the minute is a 30-minute window.
        CHECK(tokenize("around 9:20 yesterday").size() == 3);
        CHECK(span(at("what was using the gpu around 9:20 yesterday"), T(10,22,9,5), T(10,22,9,35)));
        // 5. Before and after a part of the day.
        CHECK(span(at("the laptop was crawling right before lunch yesterday, was it the ram or the swap"),
                   T(10,22,10,0), T(10,22,12,0)));
        CHECK(span(at("was it slow yesterday before lunch"), T(10,22,5,0), T(10,22,12,0)));
        CHECK(span(at("was it slow after lunch yesterday"), T(10,22,14,0), T(10,22,18,0)));
        CHECK(span(at("was it slow right after lunch yesterday"), T(10,22,14,0), T(10,22,16,0)));
        // 6. "the other day / night / week": recent, but not the latest.
        CHECK(span(at("what was the laptop up to the other day"), T(10,16,0,0), T(10,22,0,0)));
        CHECK(span(at("wat was the laptop up to the other night before i shut the lid"), T(10,16,18,0), T(10,22,6,0)));
        CHECK(span(at("how much memory was blender using when it got killed the other week"), T(10,2,0,0), T(10,16,0,0)));
        // 7. Two windows compared: both are read; a time is never the thing compared.
        Reading both = at("was it running hotter yesterday morning or last night");
        CHECK(span(both, T(10,22,5,0), T(10,22,12,0)) && second(both, T(10,22,18,0), T(10,23,6,0)) &&
              both.judge == Judge::Compare);
        Reading than2 = at("was it hotter yesterday than last night");
        CHECK(span(than2, T(10,22,0,0), T(10,23,0,0)) && second(than2, T(10,22,18,0), T(10,23,6,0)) &&
              than2.judge == Judge::Compare && than2.compare_window && than2.compare_window->from == T(10,22,18,0));
        Reading progs = at("did claude use more cpu than ollama yesterday");
        CHECK(span(progs, T(10,22,0,0), T(10,23,0,0)) && !progs.compare_window && progs.judge == Judge::None &&
              progs.frame == Frame::Choice);
        // 8. "<duration> before/after <clause>": the length and the clause, for ask to place.
        Reading half = at("what was eating the cpu in the half hour before the terminal got killed yesterday morning");
        CHECK(span(half, T(10,22,5,0), T(10,22,12,0)) && half.event_span == 1800 && !half.event_after &&
              half.event_clause == "the terminal got killed");
        Reading ten = at("how hot did it get in the 10 minutes after firefox crashed yesterday");
        CHECK(span(ten, T(10,22,0,0), T(10,23,0,0)) && ten.event_span == 600 && ten.event_after &&
              ten.event_clause == "firefox crashed" && entities_of(ten).empty());
        // "when <clause>" inside a question about the past: the event's moment, for ask to place; "when" there asks
        // no time.
        Reading off = at("what was running when the machine shut off on monday evening");
        CHECK(span(off, T(10,19,18,0), T(10,19,23,0)) && off.event_at && !off.event_after && off.event_span == 0 &&
              off.event_clause == "the machine shut off" && off.wants != Wants::Time);
        Reading cut = at("how hot was it when the laptop cut out yesterday");
        CHECK(cut.event_at && cut.event_clause == "the laptop cut out" && span(cut, T(10,22,0,0), T(10,23,0,0)));
        // A question that asks when, and a condition in the present, are not.
        CHECK(!at("when did the machine shut off").event_at && at("when did the machine shut off").wants == Wants::Time);
        CHECK(at("how hot does it get when blender is running").event_clause.empty());
        CHECK(!at("so when did it crash yesterday").event_at);
        // 9. "while I <verb> <thing>": a condition the caller names a program for.
        Reading movie = at("how hard was the cpu working while i watched the movie last night");
        CHECK(span(movie, T(10,22,18,0), T(10,23,6,0)) && movie.frame == Frame::Condition && movie.condition.empty() &&
              movie.condition_clause == "i watched the movie" && movie.condition_thing == "movie");
        CHECK(at("how hot did the gpu get while i was gaming").condition_thing == "gaming");
        CHECK(at("was it slow while i was on a zoom call").condition_thing == "zoom call");
        // A known program in the clause is the condition itself.
        Reading game = at("how hot did it get while i played blender");
        CHECK(game.frame == Frame::Condition && game.condition == "blender" && entities_of(game).empty());
        // Another frame stays; the clause is still kept.
        Reading worry = at("should i be worried about the temperature while gaming");
        CHECK(worry.frame == Frame::Worry && worry.condition_clause == "gaming");
        // 11. "when did i ..." with no time: across all history; the window keeps the last-day default.
        for (const char* q : {"when did i set up the cpu power cap and what does it actually do",
                              "when did i put cuda on here and what version is it",
                              "when did i last do a full system update"}) {
            Reading r = at(q);
            CHECK(r.window.all_history && !r.window.stated && r.window.from == N - 24 * 3600 && r.window.to == N);
        }
        CHECK(!at("what crashed").window.all_history);
        CHECK(!at("when did it crash yesterday").window.all_history);
        // 12. Words the time reader took never become the yes/no head word.
        CHECK(both.predicate == "hot");
        CHECK(than2.predicate == "hot");
        CHECK(at("is ollama still running since it got killed yesterday").predicate == "run");
        // 13. A part of the day said in the plural is a habit, not today's: the latest whole one, never moved
        // back with a note. The singular keeps its old reading.
        Reading evenings = at("whats the python thing that keeps running in the evenings");
        CHECK(span(evenings, T(10,22,18,0), T(10,22,23,0)) && !evenings.window.shifted);
        Reading mornings = at("what keeps waking the fan in the mornings");
        CHECK(span(mornings, T(10,22,5,0), T(10,22,12,0)) && !mornings.window.shifted);
        Reading one = at("whats the python thing that was running in the evening");
        CHECK(span(one, T(10,22,18,0), T(10,22,23,0)) && one.window.shifted);
        CHECK(at("what was running this evening").window.shifted);
        // Asked at 20:00, today's evening has begun: the singular is today's, the plural yesterday's whole one.
        CHECK(span(an.read("what was running in the evening", T(10,23,20,0)), T(10,23,18,0), T(10,23,23,0)));
        Reading late = an.read("what keeps running in the evenings", T(10,23,20,0));
        CHECK(span(late, T(10,22,18,0), T(10,22,23,0)) && !late.window.shifted);
        // A span named with the habit holds it: the evenings of last week are last week.
        CHECK(span(at("what was hot in the mornings last week"), T(10,12,0,0), T(10,19,0,0)));
        // 14. "or" between two things that are not programs: both sides, as typed, for ask to tell apart.
        Reading which = at("the laptop was crawling right before lunch yesterday, was it the ram or the swap thats the problem");
        CHECK(which.alternatives.size() == 2 && which.alternatives[0] == "ram" && which.alternatives[1] == "swap" &&
              which.choices.empty());
        CHECK(at("was it the cpu temperature or the gpu fan").alternatives == std::vector<std::string>({"cpu temperature", "gpu fan"}));
        // Two programs stay choices; two times stay windows.
        CHECK(at("is blender or firefox worse for heat").alternatives.empty());
        CHECK(both.alternatives.empty());
        CHECK(at("did the laptop go to sleep while i was out at lunch yesterday or did it stay on the whole time").alternatives.empty());
        // 15. A named moment keeps the moment itself, the window centred on it ("what was it doing at 3am": then,
        // not the 90 minutes around it); a span keeps none.
        Reading three = at("what was the laptop doing at 3am on tuesday");
        CHECK(three.window.at == T(10,20,3,0) && span(three, T(10,20,3,0) - 45 * 60, T(10,20,3,0) + 45 * 60));
        CHECK(at("what was using the gpu around 9:20 yesterday").window.at == T(10,22,9,20));
        CHECK(at("what happened 2 hours ago").window.at == N - 2 * 3600);
        CHECK(at("what happened yesterday").window.at == 0);
        CHECK(at("how much cpu did ollama use between 9 and 10 yesterday morning").window.at == 0);
    }

    // ---- the new 50's time faults (2026-10-23), frozen at Fri 2026-10-23 13:59:08 ----
    {
        const int64_t N = T(10, 23, 13, 59) + 8;  // 1792778348 here: the clock of the new 50's runs
        auto at = [&](const char* q) { return an.read(q, N); };
        auto span = [](const Reading& r, int64_t a, int64_t b) {
            return r.window.stated && r.window.from == a && r.window.to == b && !r.window.shifted;
        };
        // 1. "the past hour", "the last week": a unit with no number is one of it, back from now. "last week"
        // and "last month" without "the" stay the calendar's.
        CHECK(span(at("how has the cpu temperature been over the past hour"), N - 3600, N));
        Reading hour = at("what happened in the last hour");
        CHECK(span(hour, N - 3600, N) && !hour.window.all_history);
        CHECK(span(at("what was the cpu doing over the past week"), N - 7 * 86400, N));
        CHECK(span(at("how hot did it get the past day"), N - 86400, N));
        CHECK(span(at("what crashed in the last week"), N - 7 * 86400, N));
        CHECK(span(at("what was running in the last minute"), N - 60, N));
        CHECK(span(at("what crashed last week"), T(10,12,0,0), T(10,19,0,0)));
        // Before or after an event it counts back from the event: the length, for ask to place.
        Reading before = at("how hot did it get in the last hour before firefox crashed yesterday");
        CHECK(span(before, T(10,22,0,0), T(10,23,0,0)) && before.event_span == 3600 && !before.event_after);
        // 2. A vague count: a couple is 2, a few 3, several 5.
        CHECK(span(at("what happened the past few days"), N - 3 * 86400, N));
        CHECK(span(at("what crashed the last couple of days"), N - 2 * 86400, N));
        CHECK(span(at("what crashed in the last couple days"), N - 2 * 86400, N));
        CHECK(span(at("what was busy the last several hours"), N - 5 * 3600, N));
        CHECK(span(at("how hot did it get over the past few hours"), N - 3 * 3600, N));
        // 3. The weekend is Saturday 00:00 to Monday 00:00. "The weekend" (over, on, at, this) is the latest
        // that has begun, up to now while it lasts; "last weekend" and "this past weekend" the latest that has
        // ended; "since the weekend" runs from its start; "weekends" is a habit, the latest whole one.
        for (const char* q : {"what did the laptop get up to over the weekend", "what happened the weekend",
                              "what happened on the weekend", "what was the laptop doing at the weekend",
                              "what happened this weekend", "what happened last weekend",
                              "what happened this past weekend", "what keeps running on weekends"})
            CHECK(span(at(q), T(10,17,0,0), T(10,19,0,0)));
        CHECK(span(at("how much processor time has ollama used since the weekend"), T(10,17,0,0), N));
        CHECK(span(at("how hot has it been since last weekend"), T(10,17,0,0), N));
        const int64_t sunday = T(10, 18, 15, 0);
        CHECK(span(an.read("what happened over the weekend", sunday), T(10,17,0,0), sunday));
        CHECK(span(an.read("what happened this weekend", sunday), T(10,17,0,0), sunday));
        CHECK(span(an.read("what happened last weekend", sunday), T(10,10,0,0), T(10,12,0,0)));
        CHECK(span(an.read("what happened this past weekend", sunday), T(10,10,0,0), T(10,12,0,0)));
        // 4. "lately", "recently": the last 7 days. "Most recently" orders, it names no span.
        CHECK(span(at("what keeps crashing on this machine lately"), N - 7 * 86400, N));
        CHECK(span(at("what crashed recently"), N - 7 * 86400, N));
        CHECK(span(at("has it been running hot lately"), N - 7 * 86400, N));
        CHECK(!at("which program started most recently").window.stated);
        // 5. A bare weekday that is today, with a time in it that has not come: the latest one that has
        // begun, last week's. One that has begun stays today's.
        CHECK(span(at("what was running when the machine shut off on friday evening"), T(10,16,18,0), T(10,16,23,0)));
        CHECK(span(at("what happened friday night"), T(10,16,18,0), T(10,17,6,0)));
        CHECK(span(at("what happened on friday at 3pm"), T(10,16,14,15), T(10,16,15,45)));
        CHECK(span(at("what happened friday afternoon"), T(10,23,12,0), T(10,23,18,0)));
        CHECK(span(at("what happened friday morning"), T(10,23,5,0), T(10,23,12,0)));
        CHECK(span(at("what happened on friday"), T(10,23,0,0), N));
        CHECK(span(at("what happened thursday evening"), T(10,22,18,0), T(10,22,23,0)));
        // 6. "when did i last ...": all history, no span read.
        Reading charger = at("when did i last plug the charger in");
        CHECK(charger.window.all_history && !charger.window.stated);
        // 7. A bare hour takes am or pm from the part of the day named with it; for a clock the morning reaches
        // back to midnight and the evening on to it. A time not yet come today is yesterday's, as before.
        CHECK(span(at("what was the laptop doing at 3 in the morning"), T(10,23,2,15), T(10,23,3,45)));
        CHECK(span(at("what happened at 3 in the morning yesterday"), T(10,22,2,15), T(10,22,3,45)));
        CHECK(span(at("what happened yesterday at 4 in the morning"), T(10,22,3,15), T(10,22,4,45)));
        CHECK(span(at("what happened at 3 this morning"), T(10,23,2,15), T(10,23,3,45)));
        CHECK(span(at("what happened between 2 and 4 in the morning"), T(10,23,2,0), T(10,23,4,0)));
        CHECK(span(at("what happened at 6 in the morning"), T(10,23,5,15), T(10,23,6,45)));
        CHECK(span(at("what happened at 9 in the evening"), T(10,22,20,15), T(10,22,21,45)));
        CHECK(span(at("what happened at 11 in the evening"), T(10,22,22,15), T(10,22,23,45)));
        CHECK(span(at("what happened at 1 in the afternoon"), T(10,23,12,15), T(10,23,13,45)));
        CHECK(span(at("what happened at 2 in the afternoon"), T(10,22,13,15), T(10,22,14,45)));
        CHECK(span(at("what happened at 11 at night"), T(10,22,22,15), T(10,22,23,45)));
        CHECK(span(at("what happened at 2 at night"), T(10,23,1,15), T(10,23,2,45)));
    }

    // ---- two questions in one; a question about what is doing something ----
    {
        // The second question starts at a comma, a semicolon or "and" whose next word opens a question (a wh-word, or
        // the verb a yes/no question starts with); each side keeps three words or more.
        auto second = [](const char* q) {
            const std::string s = q;
            const std::size_t at = second_question(s);
            return at == std::string::npos ? std::string() : s.substr(at);
        };
        CHECK(second("what's the battery health and how fast is it draining") == "how fast is it draining");
        CHECK(second("What's my CPU temp right now, is it running hot?") == "is it running hot?");
        CHECK(second("how much ram is free; what is using the rest") == "what is using the rest");
        CHECK(second("how warm is the gpu and did the fan spin up today") == "did the fan spin up today");
        CHECK(second("was blender running on wednesday, and for how long").empty());
        CHECK(second("what was reading and writing the ssd at 2am").empty());
        CHECK(second("what happened between 2 and 4 in the morning").empty());
        CHECK(second("is the fan loud and hot").empty());
        CHECK(second("which apps use the network and how much").empty());
        // "what's using my bandwidth": "what" as the subject of a verb in its -ing form asks what is doing it.
        CHECK(an.read("what's using my bandwidth atm", NOW).asks_doer);
        CHECK(an.read("what is hammering the disk right now", NOW).asks_doer);
        CHECK(an.read("what was reading and writing the ssd at 2am", NOW).asks_doer);
        CHECK(!an.read("what is my cpu temperature", NOW).asks_doer);
        CHECK(!an.read("what was the laptop doing at 3am", NOW).asks_doer);
        CHECK(!an.read("how much bandwidth am i using", NOW).asks_doer);
    }

    // ---- the traced 95's time faults (2026-10-23), frozen at Fri 2026-10-23 13:59:08 ----
    {
        const int64_t N = T(10, 23, 13, 59) + 8;
        auto at = [&](const char* q) { return an.read(q, N); };
        auto span = [](const Reading& r, int64_t a, int64_t b) {
            return r.window.stated && r.window.from == a && r.window.to == b && !r.window.shifted;
        };
        // 1. "Overnight" is the night just past, as "last night"; with a day named, that day's night.
        CHECK(span(at("did steam upload anything overnight"), T(10,22,18,0), T(10,23,6,0)));
        CHECK(span(at("did anything phone home overnight?"), T(10,22,18,0), T(10,23,6,0)));
        CHECK(span(at("what ran overnight on tuesday"), T(10,20,18,0), T(10,21,6,0)));
        CHECK(span(at("what ran tuesday overnight"), T(10,20,18,0), T(10,21,6,0)));
        CHECK(span(at("how hot did it get overnight at 2am"), T(10,23,1,15), T(10,23,2,45)));
        CHECK(span(at("what changed since overnight"), T(10,22,18,0), N));
        // 2. Noon, midday and midnight are clocks: "at noon yesterday" is that moment.
        Reading noon = at("was there a spike in disk io at noon yesterday");
        CHECK(span(noon, T(10,22,11,15), T(10,22,12,45)) && noon.window.at == T(10,22,12,0));
        CHECK(span(at("what was running around midday on tuesday"), T(10,20,11,15), T(10,20,12,45)));
        CHECK(span(at("what happened at midnight"), T(10,22,23,15), T(10,23,0,45)));
        CHECK(span(at("what happened at midnight last night"), T(10,22,23,15), T(10,23,0,45)));
        CHECK(span(at("how hot has it been since noon"), T(10,23,12,0), N));
        CHECK(span(at("what ran between noon and 2pm yesterday"), T(10,22,12,0), T(10,22,14,0)));
        CHECK(span(at("what happened yesterday noon"), T(10,22,11,15), T(10,22,12,45)));
        // 3. The person's own sleep ("since i went to bed", "while i was asleep") is in no record: it reads as the
        // night just past, as "last night", never as the laptop going to sleep, and its words name nothing.
        Reading bed = at("ok so what changed on the machine since i went to bed");
        CHECK(span(bed, T(10,22,18,0), N) && bed.event_clause.empty());
        CHECK(span(at("what was the fan doing while i slept"), T(10,22,18,0), T(10,23,6,0)));
        CHECK(span(at("did anything run after i fell asleep"), T(10,22,18,0), T(10,23,6,0)));
        CHECK(span(at("what was running when i went to bed"), T(10,22,18,0), T(10,23,6,0)));
        CHECK(span(at("was it loud while we were sleeping"), T(10,22,18,0), T(10,23,6,0)));
        CHECK(span(at("what has run since bedtime"), T(10,22,18,0), N));
        CHECK(span(at("did it get hot while i was asleep at 2am"), T(10,23,1,15), T(10,23,2,45)));
        Analyzer sl;
        sl.set_programs({"sleep", "blender"});
        Reading asleep = sl.read("how hot did the cpu get last night while i was asleep", N);
        CHECK(span(asleep, T(10,22,18,0), T(10,23,6,0)) && asleep.condition.empty() && asleep.condition_clause.empty() &&
              entities_of(asleep).empty());
        Reading slept = sl.read("what was the cpu doing while i was asleep", N);
        CHECK(span(slept, T(10,22,18,0), T(10,23,6,0)) && slept.condition.empty() && entities_of(slept).empty());
        // The laptop's own sleep is an event for the caller to place, not the night.
        Reading lid = at("what ran since the laptop went to sleep");
        CHECK(!lid.window.stated && lid.event_clause == "the laptop went to sleep");
        Reading it = at("what happened after it went to sleep");
        CHECK(!it.window.stated && it.event_clause == "it went to sleep");
        CHECK(!at("should i put the laptop to sleep").window.stated);
        // 4. "during <event>": the event's own time, for the caller to place (T36); a part of the day stays a time.
        Reading upd = at("disk writes during update");
        CHECK(upd.event_clause == "update" && upd.event_during && !upd.event_at && !upd.event_after && !upd.window.stated);
        Reading upd2 = at("what was the cpu doing during the update yesterday");
        CHECK(upd2.event_clause == "the update" && upd2.event_during && span(upd2, T(10,22,0,0), T(10,23,0,0)));
        CHECK(at("did it crash during the zoom call").event_clause == "the zoom call");
        CHECK(at("how hot did it get during the night").event_clause.empty());
        CHECK(at("what happened during lunch yesterday").event_clause.empty());
        CHECK(!at("what was running when the machine shut off on monday evening").event_during);
    }

    // ---- the budget: the whole point is that this costs nothing ----
    {
        const char* q = "why did the fan scream while firefox was open last night";
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 10000; ++i) {
            Reading r = an.read(q, NOW);
            (void)r;
        }
        double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 10000.0;
        std::printf("speed: %.1f microseconds per question\n", us);
        CHECK(us < 1000.0);  // the budget: under a millisecond, always
    }

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
