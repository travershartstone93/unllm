// The world exam: reference answers from small packs built here, at test
// time, from text written for this test (no dataset text in the repo).

#include <sqlite3.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "unllm/knowledge.h"

using namespace unllm;
namespace fs = std::filesystem;

static int checks = 0, failures = 0;
#define CHECK(cond)                                                             \
    do {                                                                        \
        ++checks;                                                               \
        if (!(cond)) {                                                          \
            ++failures;                                                         \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                       \
    } while (0)

struct Doc {
    std::string title, url, author;
    std::vector<std::pair<std::string, std::string>> passages;  // role, text
    std::vector<std::pair<std::string, std::string>> facts = {};  // fact box: key, value
};

// A shard in the ingest tool's format (tools/unllm_ingest.py).
static void make_pack(const std::string& packs, const std::string& name, const std::string& kind,
                      const std::string& attribution, const std::vector<Doc>& docs) {
    fs::create_directories(packs + "/" + name);
    std::string path = packs + "/" + name + "/shard-000.sqlite";
    fs::remove(path);
    sqlite3* db = nullptr;
    sqlite3_open(path.c_str(), &db);
    sqlite3_exec(db,
                 "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);"
                 "CREATE TABLE docs(doc_id INTEGER PRIMARY KEY, title TEXT NOT NULL, title_key TEXT NOT NULL, url TEXT,"
                 " author TEXT, license TEXT, tags TEXT, score INTEGER, extra TEXT);"
                 "CREATE TABLE passages(pid INTEGER PRIMARY KEY, doc_id INTEGER NOT NULL, ord INTEGER NOT NULL,"
                 " section TEXT, role TEXT NOT NULL, text TEXT NOT NULL);"
                 "CREATE TABLE infobox(doc_id INTEGER NOT NULL, box TEXT, key TEXT NOT NULL, value TEXT NOT NULL);",
                 nullptr, nullptr, nullptr);
    auto meta = [&](const std::string& k, const std::string& v) {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db, "INSERT INTO meta VALUES(?,?)", -1, &st, nullptr);
        sqlite3_bind_text(st, 1, k.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, v.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    };
    meta("name", name);
    meta("kind", kind);
    meta("attribution", attribution);
    meta("license", "test text");
    int doc_id = 0, pid = 0;
    for (const auto& d : docs) {
        ++doc_id;
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db, "INSERT INTO docs VALUES(?,?,?,?,?,'test text','',0,'')", -1, &st, nullptr);
        sqlite3_bind_int(st, 1, doc_id);
        sqlite3_bind_text(st, 2, d.title.c_str(), -1, SQLITE_TRANSIENT);
        std::string key = title_key(d.title);
        sqlite3_bind_text(st, 3, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, d.url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, d.author.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
        for (const auto& [key, value] : d.facts) {
            sqlite3_prepare_v2(db, "INSERT INTO infobox VALUES(?,'',?,?)", -1, &st, nullptr);
            sqlite3_bind_int(st, 1, doc_id);
            sqlite3_bind_text(st, 2, key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, value.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        int ord = 0;
        for (const auto& [role, text] : d.passages) {
            sqlite3_prepare_v2(db, "INSERT INTO passages VALUES(?,?,?,'',?,?)", -1, &st, nullptr);
            sqlite3_bind_int(st, 1, ++pid);
            sqlite3_bind_int(st, 2, doc_id);
            sqlite3_bind_int(st, 3, ord++);
            sqlite3_bind_text(st, 4, role.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 5, text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    sqlite3_exec(db,
                 "CREATE INDEX docs_title_key ON docs(title_key);"
                 "CREATE VIRTUAL TABLE passages_fts USING fts5(text, content='passages', content_rowid='pid',"
                 " tokenize='porter unicode61 remove_diacritics 2');"
                 "CREATE VIRTUAL TABLE titles_fts USING fts5(title, content='docs', content_rowid='doc_id',"
                 " tokenize='porter unicode61 remove_diacritics 2');"
                 "INSERT INTO passages_fts(passages_fts) VALUES('rebuild');"
                 "INSERT INTO titles_fts(titles_fts) VALUES('rebuild');",
                 nullptr, nullptr, nullptr);
    meta("passages", std::to_string(pid));
    sqlite3_close(db);
}

int main() {
    const std::string packs = (fs::temp_directory_path() / "unllm-world-exam").string();
    fs::remove_all(packs);
    make_pack(packs, "mini-encyclopedia", "encyclopedia", "From Mini Encyclopedia, \"{title}\"",
              {{"Photosynthesis", "https://example.org/photosynthesis", "",
                {{"lead", "Photosynthesis is the process plants use to turn light, water and carbon dioxide into sugar "
                          "and oxygen. It happens in the green parts of the plant."},
                 {"body", "Without photosynthesis there would be almost no oxygen in the air."}}},
               {"Eiffel Tower", "https://example.org/eiffel", "",
                {{"lead", "The Eiffel Tower is an iron tower in Paris."},
                 {"body", "Work on the tower started in 1887. The tower was finished in 1889, in time for a world fair."}}},
               {"Sky", "https://example.org/sky", "",
                {{"lead", "The sky is everything above the ground."},
                 {"body", "The sky looks blue because air scatters blue sunlight more than red sunlight."}}},
               {"Mount Example", "https://example.org/mount", "",
                {{"lead", "Mount Example () is a mountain in Testland. It is about  high. The mountain was named in 1901. "
                          "Its summit is 1,234 metres tall."},
                 {"table", "Year, Climbers, Height reached\n1901, 2, 1,234 metres tall"},
                 {"body", "Climbers still visit it every summer."}}},
               {"Ada Example", "https://example.org/ada", "",
                {{"lead", "Ada Example (10 December 1815 - 27 November 1852) was an English Mathematician from London. "
                          "She wrote the first published program."},
                 {"body", "In 1843 Ada Example translated a paper by Luigi Menabrea and added her own notes."}}},
               {"Testland", "https://example.org/testland", "",
                {{"lead", "Testland is a small country on the shore of the Test Sea."}},
                {{"Capital", "Testville \xc2\xb7 12\xc2\xb0" "30\xe2\x80\xb2" "N 45\xc2\xb0" "00\xe2\x80\xb2" "E"},
                 {"Currency", "Test dollar ([[ISO 4217|TSD]])"},
                 {"Population", "Website \xc2\xb7 www.testland.example"},
                 {"\xe2\x80\xa2 total", "1,234,567"},
                 {"Founders", "- Ann Smith - Bob Jones"},
                 {"Founded", "Ann Smith \xc2\xb7 4 July 1901 \xc2\xb7 Testville"}}},
               {"Random-access memory", "https://example.org/ram", "",
                {{"lead", "Random-access memory is the fast working memory of a computer."}}}});
    make_pack(packs, "mini-qa", "qa", "From the fixture Q&A, \"{title}\"",
              {{"How do I cook a steak?", "https://example.org/q/1", "Sam Cook",
                {{"question", "How do I cook a steak? I want it brown outside and pink inside."},
                 {"accepted", "Take the steak out of the fridge half an hour before cooking."},
                 {"accepted", "Heat a heavy pan until it smokes, then sear each side for three minutes."},
                 {"accepted", "Let it rest for five minutes before cutting."}}},
               {"How do I update my packages?", "https://example.org/q/2", "Pat Admin",
                {{"question", "How do I update my packages? The software center is broken."},
                 {"code", "Open a terminal and type:\nsudo apt update\nsudo apt upgrade"},
                 {"accepted", "Reboot when the upgrade finishes."}}}});
    make_pack(packs, "mini-words", "definitions", "From the fixture dictionary",
              {{"ephemeral", "", "", {{"definition", "ephemeral (adjective): lasting a very short time."}}}});
    make_pack(packs, "mini-poems", "poems", "From {title} by {author} (Project Gutenberg)",
              {{"Songs of the Shore", "https://example.org/poems", "A. Poet",
                {{"verse", "The sea rolls in beneath the moon,\nThe gulls go quiet, the tide comes soon,\n"
                           "And every wave that finds the sand\nCarries a word from a far-off land."}}}});

    Library lib;
    CHECK(lib.open(packs));
    CHECK(lib.packs().size() == 4);
    auto lead = lib.by_title("photosynthesis", {}, 3);
    CHECK(!lead.empty() && lead[0].role == "lead" && lead[0].title == "Photosynthesis");
    CHECK(title_key("The Eiffel Tower") == "eiffel tower");

    Analyzer an;
    World world(lib, nullptr);
    CHECK(world.load_fact_keys("data/factkeys.txt"));
    auto ask = [&](const char* q) { return world.answer(an.read(q, 0), q); };

    // A definition from the encyclopedia: the article's own opening line first.
    WorldAnswer ph = ask("what is photosynthesis");
    CHECK(ph.kind == WorldAnswer::Answer);
    CHECK(ph.text.rfind("Photosynthesis is the process plants use", 0) == 0);
    CHECK(ph.text.find("From Mini Encyclopedia, \"Photosynthesis\".") != std::string::npos);
    CHECK(!ph.receipts.empty() && ph.receipts[0].find("https://example.org/photosynthesis") != std::string::npos);

    // "When": the sentence with the date.
    WorldAnswer et = ask("when was the eiffel tower finished");
    CHECK(et.kind != WorldAnswer::None && et.text.find("1889") != std::string::npos);

    // "How tall": the sentence with a height, never a year; holes and
    // empty brackets left by the source never show.
    WorldAnswer mt = ask("how tall is mount example");
    CHECK(mt.text.rfind("Its summit is 1,234 metres tall.", 0) == 0);
    CHECK(mt.text.find("about  high") == std::string::npos && mt.text.find("()") == std::string::npos);
    CHECK(mt.text.find("Mount Example is a mountain") != std::string::npos);
    // Table rows are searchable but never quoted, here or in "more".
    CHECK(mt.text.find("Climbers,") == std::string::npos);
    CHECK(world.more(mt.used[0]).text.find("Climbers still visit it") == 0);

    // "Who was": the article's own opening line, though it is full of names.
    WorldAnswer ada = ask("who was ada example");
    CHECK(ada.text.rfind("Ada Example (10 December 1815 - 27 November 1852) was an English", 0) == 0);

    // Fact boxes: the row that answers, made readable; numbers only where a
    // number was asked for; coordinates and link markup gone.
    WorldAnswer cur = ask("what is the currency of testland");
    CHECK(cur.text.rfind("Testland, currency: Test dollar (TSD).", 0) == 0);
    CHECK(ask("what is the capital of testland").text.rfind("Testland, capital: Testville.", 0) == 0);
    CHECK(ask("what is the population of testland").text.rfind("Testland, population total: 1,234,567.", 0) == 0);
    CHECK(ask("who founded testland").text.rfind("Testland, founders: Ann Smith, Bob Jones.", 0) == 0);
    CHECK(ask("when was testland founded").text.rfind("Testland, founded: 4 July 1901.", 0) == 0);

    // "Why": the sentence with the reason.
    WorldAnswer sky = ask("why is the sky blue");
    CHECK(sky.text.rfind("The sky looks blue because", 0) == 0);

    // How-to: the accepted answer's steps, numbered, with its author.
    WorldAnswer steak = ask("how do i cook a steak");
    CHECK(steak.kind == WorldAnswer::HowTo);
    CHECK(steak.text.rfind("1. Take the steak out", 0) == 0 && steak.text.find("\n2. Heat a heavy pan") != std::string::npos);
    CHECK(steak.text.find("(answer by Sam Cook)") != std::string::npos);

    // Not phrased as a how-to: still the answer post, never the question text.
    WorldAnswer pink = ask("steak brown outside pink inside");
    CHECK(pink.text.find("I want it") == std::string::npos && pink.text.find("Take the steak out") == 0);

    // Commands keep their lines, indented under their step.
    WorldAnswer pkgs = ask("how do i update my packages");
    CHECK(pkgs.text.rfind("1. Open a terminal and type:\n   sudo apt update\n   sudo apt upgrade\n2. Reboot", 0) == 0);

    // A dictionary answer when the question asks what a word means.
    WorldAnswer eph = ask("what does ephemeral mean");
    CHECK(eph.kind == WorldAnswer::Definition && eph.text.rfind("Ephemeral (adjective): lasting a very short time.", 0) == 0);

    // A real poem, never a new one.
    WorldAnswer poem = ask("write me a poem about the sea");
    CHECK(poem.kind == WorldAnswer::Poem);
    CHECK(poem.text.rfind("Here is an excerpt from Songs of the Shore by A. Poet (Project Gutenberg), a real poem", 0) == 0);

    // Nothing relevant: no answer rather than a wrong one.
    WorldAnswer none = ask("what is the airspeed velocity of an unladen swallow");
    CHECK(none.kind == WorldAnswer::None || none.kind == WorldAnswer::Closest);

    // "Tell me more": the next passage of the same document.
    WorldAnswer more = world.more(ph.used[0]);
    CHECK(more.text.find("Without photosynthesis") != std::string::npos);

    // Sentences split at real sentence ends only.
    auto ss = sentences_of("Dr. Smith went to the U.S. in 1990. He liked it. J. R. Tolkien wrote books.");
    CHECK(ss.size() == 3 && ss[2] == "J. R. Tolkien wrote books.");

    // The large vector format: written here, read back, compared.
    {
        std::string p = packs + "/tiny.unl2";
        std::ofstream f(p, std::ios::binary);
        float vecs[3][2] = {{1, 0}, {0.96f, 0.28f}, {-1, 0}};
        uint32_t count = 3, dim = 2;
        f.write("UNL2", 4);
        f.write(reinterpret_cast<char*>(&count), 4);
        f.write(reinterpret_cast<char*>(&dim), 4);
        uint32_t off[4] = {0, 4, 12, 15};
        f.write(reinterpret_cast<char*>(off), sizeof off);
        float scales[3];
        int8_t rows[3][2];
        for (int i = 0; i < 3; ++i) {
            float n = std::sqrt(vecs[i][0] * vecs[i][0] + vecs[i][1] * vecs[i][1]);
            float mx = std::max(std::fabs(vecs[i][0] / n), std::fabs(vecs[i][1] / n));
            scales[i] = mx / 127.0f;
            for (int d = 0; d < 2; ++d) rows[i][d] = int8_t(std::lround(vecs[i][d] / n / scales[i]));
        }
        f.write(reinterpret_cast<char*>(scales), sizeof scales);
        f.write(reinterpret_cast<char*>(rows), sizeof rows);
        f.write("coldfreezinghot", 15);
        f.close();
        Vectors v;
        CHECK(v.load(p) && v.size() == 3 && v.contains("freezing") && !v.contains("warm"));
        CHECK(v.similarity("cold", "freezing") > 0.9 && v.similarity("cold", "hot") < -0.9);
    }

    fs::remove_all(packs);
    std::printf("world: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
