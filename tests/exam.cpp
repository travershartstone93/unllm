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
        {"how hot did it get during the update yesterday", T(10,12,0,0), T(10,13,0,0), "heat,update", ""},
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
