#include "unllm/maintenance/ingest.h"
#include "unllm/symbolic/gui_target.h"
#include <iostream>
#include <map>
#include <memory>
#include <sqlite3.h>
#include <zip.h>
using namespace unllm::maintenance;
using namespace unllm::native;
namespace fs = std::filesystem;
namespace {
void check(bool expected, const std::string &why) {
  if (!expected)
    throw std::runtime_error(why);
}
std::string query(const Path &dbfile, const char *sql) {
  sqlite3 *raw = nullptr;
  auto rc =
      sqlite3_open_v2(dbfile.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
  check(rc == SQLITE_OK, "open shard");
  sqlite3_stmt *s = nullptr;
  rc = sqlite3_prepare_v2(raw, sql, -1, &s, nullptr);
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(
      s, sqlite3_finalize);
  check(rc == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW, "query shard");
  return reinterpret_cast<const char *>(sqlite3_column_text(s, 0));
}
void make_zip(const Path &path) {
  int error = 0;
  auto z = zip_open(path.c_str(), ZIP_CREATE | ZIP_EXCL, &error);
  check(z, "open fixture zip");
  std::map<std::string, std::string> files{
      {"nested/entries-a.json",
       R"({"arctic_fox":{"n":{"sense":[{"synset":"n1"}]}}})"},
      {"nested/syn.json",
       R"({"n1":{"definition":["a pale fox"],"example":[{"text":"It lives in snow"}],"members":["arctic_fox","polar_fox"]}})"}};
  for (auto &[n, s] : files) {
    auto b = zip_source_buffer(z, s.data(), s.size(), 0);
    check(b && zip_file_add(z, n.c_str(), b, 0) >= 0, "fixture member");
  }
  check(!zip_close(z), "finish fixture zip");
}
} // namespace
int main() {
  auto root =
      fresh_gui_directory(fs::temp_directory_path(), "unllm-ingest-fixture");
  try {
    Paths p{root / "data", root / "live-packs", root / "downloads"};
    auto folder = root / "Collection";
    fs::create_directories(folder);
    write_atomic(folder / "The Red Fox.txt",
                 "This little fox is running across the snow.\n\nToo "
                 "short.\n\nAnother longer paragraph about the forest.\n");
    auto manifest = stage_pack(p, folder);
    check(manifest["docs"].integer() == 1 &&
              manifest["passages"].integer() == 2,
          "Document counts");
    auto staged = p.data / "staging/packs";
    auto db = staged / "docs-collection/shard-000.sqlite";
    check(query(db, "PRAGMA application_id") == "1431194704" &&
              query(db, "SELECT title_key FROM docs") == "red fox",
          "Reference pack schema");
    check(query(db, "SELECT count(*) FROM passages_fts WHERE passages_fts "
                    "MATCH 'run'") == "1" &&
              query(db, "SELECT count(*) FROM titles_fts WHERE titles_fts "
                        "MATCH 'fox'") == "1",
          "FTS stemming");
    check(query(db,
                "SELECT group_concat(role, ',') FROM passages ORDER BY ord") ==
              "lead,body",
          "Document roles");
    auto dictionary = root / "kaikki-English.jsonl";
    write_atomic(
        dictionary,
        R"({"word":"foxes","pos":"noun","senses":[{"glosses":["**plural** of fox"],"form_of":[{"word":"fox"}]}]}
{"word":"other","lang_code":"fr","senses":[{"glosses":["skip"]}]}
{"word":"fox","pos":"noun","senses":[{"glosses":["a wild canine"]}]}
malformed
)");
    stage_pack(p, dictionary);
    db = staged / "wiktionary-en/shard-000.sqlite";
    check(query(db, "SELECT text FROM passages WHERE role='form'") ==
              "foxes (noun): plural of fox",
          "Inflection or cleaned gloss");
    check(query(db, "SELECT json_extract(extra,'$.form_of') FROM docs WHERE "
                    "title='foxes'") == "fox",
          "Inflection target");
    write_atomic(
        dictionary,
        R"({"word":"fox","pos":"noun","senses":[{"glosses":["<p>broken markup</p>"]}]})");
    bool rejected = false;
    try {
      stage_pack(p, dictionary);
    } catch (const Unsupported &) {
      rejected = true;
    }
    check(rejected && query(db, "SELECT COUNT(*) FROM docs") == "2",
          "Rejected build replaced usable pack");
    auto archive = root / "english-wordnet-2025-json.zip";
    make_zip(archive);
    manifest = stage_pack(p, archive);
    check(manifest["version"].text() == "2025" &&
              manifest["docs"].integer() == 1,
          "WordNet version or count");
    db = staged / "wordnet-en/shard-000.sqlite";
    check(query(db, "SELECT text FROM passages") ==
              "arctic fox (noun): a pale fox. Example: It lives in snow. Also: "
              "polar fox.",
          "WordNet content");
    write_atomic(folder / "The Red Fox.txt",
                 "A different text replaces the previous text safely.");
    check(stage_pack(p, folder)["passages"].integer() == 1,
          "Restaging did not replace old contents");
    check(!fs::exists(p.packs), "Ingestion modified active packs");
    fs::remove_all(root);
    std::cout << "Three source readers produced compatible SQLite shards, FTS "
                 "indexes and manifests. Failed restaging preserved previous "
                 "contents\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << " in " << root << '\n';
    return 1;
  }
}
