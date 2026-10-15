// The exam: fifty questions with known correct readings, frozen at
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

int main() {
    Analyzer an;
    an.set_programs({"blender", "firefox", "chromium", "ollama", "audacity", "claude", "ktorrent"});

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
        {"why is my computer sluggish", 0, 0, "slow", ""},
        {"did anything freeze yesterday", T(10,12,0,0), T(10,13,0,0), "slow", ""},
        {"was it swapping last night", T(10,12,18,0), T(10,13,6,0), "memory", ""},
        {"is something leaking memory", 0, 0, "memory", ""},
        {"what was hammering the disk yesterday", T(10,12,0,0), T(10,13,0,0), "disk", ""},
        {"who used the internet last night", T(10,12,18,0), T(10,13,6,0), "network", ""},
        {"is anything torrenting", 0, 0, "sharing", ""},
        {"what got installed yesterday", T(10,12,0,0), T(10,13,0,0), "update", ""},
        {"did my settings change last week", T(10,5,0,0), T(10,12,0,0), "config", ""},
        {"what crashed today", T(10,13,0,0), NOW, "crash", ""},
        {"is this machine safe", 0, 0, "trust", ""},
        {"what drained the battery yesterday", T(10,12,0,0), T(10,13,0,0), "battery", ""},
        {"what starts at boot", 0, 0, "startup", ""},
        // ---- entities (the gazetteers) ----
        {"what did blender do this morning", T(10,13,5,0), T(10,13,12,0), "", "program:blender"},
        {"what did blendr do yesterday", T(10,12,0,0), T(10,13,0,0), "", "program:blender"},
        {"is chromium safe", 0, 0, "trust", "program:chromium"},
        {"is chromum safe", 0, 0, "trust", "program:chromium"},
        {"what is pid 2671", 0, 0, "", "pid:2671"},
        {"did it hit 90 degrees yesterday", T(10,12,0,0), T(10,13,0,0), "heat", "quantity:90 C"},
        {"when was the cpu over 80% today", T(10,13,0,0), NOW, "slow", "quantity:80 %"},
        {"what was ktorrent uploading last night", T(10,12,18,0), T(10,13,6,0), "network", "program:ktorrent"},
        // ---- the realistic blend ----
        {"why did the fan scream while firefox was open last night", T(10,12,18,0), T(10,13,6,0), "heat", "program:firefox"},
        {"did claude make it slow this afternoon", T(10,13,12,0), T(10,13,18,0), "slow", "program:claude"},
        {"how hot did it get during the update yesterday", T(10,12,0,0), T(10,13,0,0), "heat,update", ""},
        {"did anything change since tuesday", T(10,6,0,0), NOW, "config", ""},
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
        ++checks;
        if (!ok) {
            ++failures;
            ++qfail;
            std::printf("  EXAM %02d \"%s\"\n        window %lld..%lld stated=%d  intents=[%s]  entities=[%s]\n",
                        qnum, e.question, (long long)(r.window.from - NOW), (long long)(r.window.to - NOW),
                        int(r.window.stated), intents_of(r).c_str(), entities_of(r).c_str());
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
        // Without the table, the same question scores low: the layer earns it.
        CHECK(an.read("why is my computer lethargic", NOW).domain < 0.5);
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
