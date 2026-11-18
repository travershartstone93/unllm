#include "unllm/symbolic/inference.h"
#include "unllm/symbolic/store.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace unllm::native;
static void check(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
static void write(const std::filesystem::path &p, const std::string &s) {
  std::ofstream out(p);
  out << s;
  check(bool(out), "Write fixture");
}
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  auto dir = std::filesystem::temp_directory_path() /
             ("unllm-facts-cli-" + std::to_string(getpid()));
  try {
    std::filesystem::create_directories(dir);
    auto database = dir / "facts.sqlite";
    auto run = [&](const std::vector<std::string> &args, bool success = true) {
      std::vector<std::string> command{argv[2], argv[1], "--database",
                                       database.string()};
      command.insert(command.end(), args.begin(), args.end());
      Process p(command);
      auto result = p.finish(after_ms(10000));
      check((result.exit_code == 0) == success, result.out + result.err);
      return Json::parse(result.out);
    };
    auto initial = run({"status"});
    auto document = Json::parse(
        R"({"source":{"id":"cli:locations","title":"Fixture locations"},"entities":[{"id":"p","name":"Paris","type":"city"},{"id":"f","name":"France","type":"country"},{"id":"e","name":"Europe","type":"place"}],"claims":[{"predicate":"located_in","subject":"p","object":"f"},{"predicate":"located_in","subject":"f","object":"e"}]})");
    write(dir / "facts.json", document.dump());
    auto imported = run({"import-json", (dir / "facts.json").string()});
    check(imported.size() == 1 && imported.at(0)["claims"].integer() == 5,
          "JSON import through executable");
    auto status = run({"status"});
    check(status["revision"].integer() == initial["revision"].integer() + 1 &&
              status["sources"].integer() == initial["sources"].integer() + 1 &&
              status["claims"].integer() == initial["claims"].integer() + 5 &&
              status["database"].text() == database.string(),
          "Status counts");
    auto prepared = run({"prepare"});
    check(prepared["status"].text() == "complete" &&
              prepared["tuples"].integer() > 0,
          "Prepare snapshot");
    {
      FactStore store(database, argv[1]);
      NativeEngine engine(argv[1], argv[3]);
      auto now = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
      auto facts = Json::array();
      for (auto &c : store.claims(now))
        facts.push(c.data());
      auto relations = store.relations();
      check(engine.input_key(facts, relations) == prepared["snapshot"].text(),
            "Maintenance and query cache keys agree");
      auto cached = store.prepared(prepared["snapshot"].text());
      check(!cached.null(), "Saved prepared snapshot");
      auto goals = Json::parse(
          R"([{"predicate":"located_in","subject":"p","object":"e","negative":false}])");
      auto answer = engine.query(facts, relations, store.completeness(), goals,
                                 after_ms(5000), 100, cached);
      check(answer["status"].text() == "complete" &&
                answer["solutions"].size() == 1,
            "Prepared transitive answer");
    }
    check(run({"prepare", "--logic-worker", "/nonexistent-worker"},
              false)["status"]
                  .text() == "error",
          "Failed preparation is an error");
    check(run({"import-wikidata", "Q90", "--depth", "1garbage", "--offline"},
              false)["status"]
                  .text() == "error",
          "Strict numeric options");
    run({"status", "extra"}, false);
    run({"import-packs", "--titles"}, false);
    write(dir / "invalid.json", "{");
    run({"import-json", (dir / "invalid.json").string()}, false);
    check(run({"status"})["revision"].integer() == status["revision"].integer(),
          "Failed commands do not change revision");
    auto fixtures =
        Json::parse(read_file(std::filesystem::path(argv[1]) /
                              "tests/cpp_migration/fixtures/importers.json"));
    auto pack = dir / "packs/demo", cache = dir / "cache";
    std::filesystem::create_directories(pack);
    std::filesystem::create_directories(cache);
    write(pack / "manifest.json", fixtures["manifest"].dump());
    sqlite3 *db = nullptr;
    check(sqlite3_open((pack / "shard-00.sqlite").c_str(), &db) == SQLITE_OK,
          "Open pack");
    int rc = sqlite3_exec(db, fixtures["pack_sql"].text().c_str(), nullptr,
                          nullptr, nullptr);
    sqlite3_close(db);
    check(rc == SQLITE_OK, "Populate pack");
    check(run({"import-packs", "--packs", (dir / "packs").string(), "--pack",
               "demo", "--titles", "Paris", "France",
               "The Notebook"})["documents"]
                  .integer() == 3,
          "Pack CLI import");
    for (auto &[qid, data] : fixtures["wikidata"].members())
      write(cache / (qid + ".json"), data.dump());
    auto wiki =
        run({"import-wikidata", "Q90", "--cache", cache.string(), "--offline"});
    check(wiki["documents"].integer() == 2 && wiki["claims"].integer() == 4,
          "Offline Wikidata CLI import");
    auto repeated =
        run({"import-wikidata", "Q90", "--cache", cache.string(), "--offline"});
    check(repeated["claims"].integer() == 4, "Repeated import succeeds");
    std::filesystem::remove_all(dir);
    std::cout << "Facts CLI import, status, prepare, cache reuse and error "
                 "checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::filesystem::remove_all(dir);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
