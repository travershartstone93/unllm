// Isolated maintenance endpoint; production command routing is not changed.
#include "unllm/symbolic/importers.h"
#include "unllm/symbolic/inference.h"
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace unllm::native;
namespace {
int integer(const std::string &s) {
  int n;
  auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
  if (ec != std::errc{} || end != s.data() + s.size())
    throw Unsupported("Expected integer option");
  return n;
}
struct Options {
  std::filesystem::path root, database, worker, packs, cache;
  std::string command;
  std::vector<std::string> files, qids, titles, pack_names;
  int depth = 1, budget = 50;
  bool fetch = true;
};
Options parse(int argc, char **argv) {
  if (argc < 3)
    throw Unsupported(
        "Usage: unllm_facts_native ROOT [--database PATH] "
        "{status|prepare|import-json|import-packs|import-wikidata} [OPTIONS]");
  Options o;
  o.root = std::filesystem::absolute(argv[1]);
  o.database = o.root / ".state/cpp-migration/facts.sqlite";
  o.cache = o.root / ".state/cpp-migration/wikidata";
  o.worker = std::filesystem::canonical("/proc/self/exe").parent_path() /
             "unllm_logic_worker";
  if (auto home = std::getenv("HOME"))
    o.packs = std::filesystem::path(home) / ".local/share/unllm/packs";
  int i = 2;
  auto value = [&](const std::string &flag) -> std::string {
    if (++i >= argc)
      throw Unsupported("Missing value for " + flag);
    return argv[i];
  };
  auto values = [&](std::vector<std::string> &out) {
    auto before = out.size();
    while (i + 1 < argc && !std::string(argv[i + 1]).starts_with("--"))
      out.push_back(argv[++i]);
    if (out.size() == before)
      throw Unsupported("Missing --titles values");
  };
  for (; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--database")
      o.database = value(arg);
    else if (arg == "--logic-worker")
      o.worker = value(arg);
    else if (o.command.empty()) {
      if (arg != "status" && arg != "prepare" && arg != "import-json" &&
          arg != "import-packs" && arg != "import-wikidata")
        throw Unsupported("Unknown facts command: " + arg);
      o.command = arg;
    } else if (o.command == "import-packs" && arg == "--packs")
      o.packs = value(arg);
    else if (o.command == "import-packs" && arg == "--pack")
      o.pack_names.push_back(value(arg));
    else if (o.command == "import-packs" && arg == "--titles")
      values(o.titles);
    else if (o.command == "import-wikidata" && arg == "--cache")
      o.cache = value(arg);
    else if (o.command == "import-wikidata" && arg == "--depth")
      o.depth = integer(value(arg));
    else if (o.command == "import-wikidata" && arg == "--max-entities")
      o.budget = integer(value(arg));
    else if (o.command == "import-wikidata" && arg == "--offline")
      o.fetch = false;
    else if (!arg.starts_with("--") && o.command == "import-json")
      o.files.push_back(arg);
    else if (!arg.starts_with("--") && o.command == "import-wikidata")
      o.qids.push_back(arg);
    else
      throw Unsupported("Unknown option or argument: " + arg);
  }
  if (o.command.empty() || (o.command == "import-json" && o.files.empty()) ||
      (o.command == "import-packs" && (o.titles.empty() || o.packs.empty())) ||
      (o.command == "import-wikidata" && o.qids.empty()))
    throw Unsupported("Missing required facts command arguments");
  if (o.depth < 0 || o.depth > 3 || o.budget < 1 || o.budget > 200)
    throw Unsupported("Import budget exceeded");
  return o;
}
} // namespace
int main(int argc, char **argv) {
  try {
    auto o = parse(argc, argv);
    FactStore store(o.database, o.root);
    Json result;
    if (o.command == "import-json") {
      result = Json::array();
      for (auto &file : o.files)
        result.push(store.import_document(Json::parse(read_file(file))));
    } else if (o.command == "import-packs")
      result = import_packs(store, o.packs, o.titles, o.pack_names);
    else if (o.command == "import-wikidata")
      result =
          import_wikidata(store, o.qids, o.cache, o.depth, o.budget, o.fetch);
    else if (o.command == "prepare") {
      NativeEngine engine(o.root, o.worker);
      store.begin_read();
      auto now = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
      auto facts = Json::array();
      for (auto &c : store.claims(now))
        facts.push(c.data());
      auto relations = store.relations(), complete = store.completeness();
      store.end_read();
      auto prepared =
          engine.snapshot(facts, relations, complete, after_ms(30000));
      auto id = engine.input_key(facts, relations);
      store.save_prepared(id, prepared);
      result = Json::object();
      result.set("snapshot", Json::string(id));
      result.set("tuples", Json::number(prepared["closure"].size()));
      result.set("status", Json::string("complete"));
    } else {
      result = store.status();
      result.set("database", Json::string(o.database.string()));
    }
    std::cout << result.dump() << '\n';
    return 0;
  } catch (const std::exception &e) {
    auto result = Json::object();
    result.set("status", Json::string("error"));
    result.set("text", Json::string(e.what()));
    std::cout << result.dump() << '\n';
    return 1;
  }
}
