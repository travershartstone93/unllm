#include "unllm/symbolic/importers.h"
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace unllm::native;
static void check(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed, "Expected rejection");
}
static void same(const Json &actual, const Json &expected, const char *where) {
  check(canonical(actual) == canonical(expected),
        std::string(where) + "\nactual: " + canonical(actual) +
            "\nexpected: " + canonical(expected));
}
static void write(const std::filesystem::path &p, const std::string &s) {
  std::ofstream out(p);
  out << s;
  check(bool(out), "Write fixture");
}
static void sql(const std::filesystem::path &p, const std::string &s) {
  sqlite3 *db = nullptr;
  check(sqlite3_open(p.c_str(), &db) == SQLITE_OK, "Open pack fixture");
  auto rc = sqlite3_exec(db, s.c_str(), nullptr, nullptr, nullptr);
  auto error = std::string(sqlite3_errmsg(db));
  sqlite3_close(db);
  check(rc == SQLITE_OK, error);
}
static Json snapshot(FactStore &store) {
  auto out = Json::object(), claims = Json::array();
  for (auto &c : store.claims(1234567890))
    claims.push(c.data());
  out.set("claims", claims);
  out.set("entities", store.entities());
  out.set("revision", Json::number(store.revision()));
  return out;
}
static std::vector<std::string> texts(const Json &a) {
  std::vector<std::string> out;
  for (auto v : a.items())
    out.push_back(v.text());
  return out;
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  auto dir = std::filesystem::temp_directory_path() /
             ("unllm-importers-" + std::to_string(getpid()));
  try {
    auto f =
        Json::parse(read_file(std::filesystem::path(argv[1]) /
                              "tests/cpp_migration/fixtures/importers.json"));
    auto pack = dir / "packs/demo", cache = dir / "cache";
    std::filesystem::create_directories(pack);
    std::filesystem::create_directories(cache);
    write(pack / "manifest.json", f["manifest"].dump());
    sql(pack / "shard-00.sqlite", f["pack_sql"].text());
    for (auto pair : f["title_keys"].items())
      check(title_key(pair.at(0).text()) == pair.at(1).text(),
            "Title key parity");
    rejects([&] { title_key(std::string(1, char(0xff))); });
    auto titles = texts(f["titles"]);
    same(find_documents(dir / "packs", titles), f["documents"],
         "Pack discovery");
    check(find_documents(dir / "packs", titles, {"absent"}).size() == 0,
          "Pack filter");
    auto pack_hash = sha256(read_file(pack / "shard-00.sqlite"));
    {
      FactStore store(dir / "packs.db", argv[1]);
      same(import_packs(store, dir / "packs", titles), f["pack_result"],
           "Pack import");
      same(snapshot(store), f["pack_snapshot"], "Pack snapshot");
    }
    check(sha256(read_file(pack / "shard-00.sqlite")) == pack_hash,
          "Pack input must remain unchanged");
    for (auto &[qid, data] : f["wikidata"].members())
      write(cache / (qid + ".json"), data.dump());
    int index = 0;
    for (auto c : f["wiki_cases"].items()) {
      FactStore store(dir / ("wiki-" + std::to_string(index++) + ".db"),
                      argv[1]);
      same(import_wikidata(store, texts(c["qids"]), cache, c["depth"].integer(),
                           c["budget"].integer(), false),
           c["result"], "Wikidata import");
      same(snapshot(store), c["snapshot"], "Wikidata snapshot");
    }
    {
      FactStore store(dir / "invalid.db", argv[1]);
      auto before = store.revision();
      rejects([&] { import_wikidata(store, {"../Q90"}, cache, 1, 50, false); });
      rejects([&] { import_wikidata(store, {"Q90"}, cache, 4, 50, false); });
      rejects([&] { import_wikidata(store, {"Q90"}, cache, 1, 201, false); });
      write(cache / "Q777.json", "{\"entities\":{}}");
      rejects([&] { import_wikidata(store, {"Q777"}, cache, 1, 50, false); });
      write(cache / "Q777.json", "{");
      rejects([&] { import_wikidata(store, {"Q777"}, cache, 1, 50, false); });
      write(cache / "Q777.json", std::string(8 * 1024 * 1024 + 1, ' '));
      rejects([&] { import_wikidata(store, {"Q777"}, cache, 1, 50, false); });
      check(store.revision() == before,
            "Invalid imports must not mutate store");
    }
    // Unlike the reference's last-wins dictionary, duplicate labels cannot
    // select an arbitrary identity for a structured relation.
    sql(pack / "shard-00.sqlite",
        "INSERT INTO docs "
        "VALUES(4,'France','france','{\"wikidata\":\"Q999\"}','');");
    {
      FactStore store(dir / "ambiguous.db", argv[1]);
      auto result = import_packs(store, dir / "packs", titles);
      check(result["claims"].integer() == 1 && result["skipped"].size() == 2,
            "Ambiguous name must be skipped");
      for (auto &c : store.claims(1234567890))
        check(c.predicate != "country", "No guessed country relation");
    }
    std::filesystem::remove_all(dir);
    std::cout << "Pack and offline Wikidata parity, limits, provenance and "
                 "ambiguity checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::filesystem::remove_all(dir);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
