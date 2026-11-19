#include "unllm/maintenance/ingest.h"
#include "unllm/symbolic/gui_target.h"
#include "unllm/symbolic/process.h"
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <iostream>
#include <parquet/arrow/writer.h>
#include <zlib.h>
using namespace unllm::native;
using namespace unllm::maintenance;
namespace fs = std::filesystem;
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
void parquet_fixture(const Path &path) {
  std::vector<std::shared_ptr<arrow::Field>> fields;
  std::vector<std::shared_ptr<arrow::Array>> columns;
  for (auto &[field, value] : std::map<std::string, std::string>{
           {"title", "Northport"},
           {"text", "# Northport\n\nNorthport is a town in the northern "
                    "valley.\n\n## Geology\n\nThe land sits on ancient "
                    "sedimentary rock. The equation is {\\displaystyle "
                    "h=\\frac{1}{2}gt^{2}}.\n\nBoardroom\n- Ann Smith\n- Bob "
                    "Jones\n\n## References\n\nOmitted citation."},
           {"wikiname", "enwiki"},
           {"infoboxes",
            R"([{"title":"Place","data":{"population":"2345"}}])"}}) {
    arrow::StringBuilder builder;
    check(builder.Append(value).ok(), "append value");
    columns.push_back(builder.Finish().ValueOrDie());
    fields.push_back(arrow::field(field, arrow::utf8()));
  }
  auto out = arrow::io::FileOutputStream::Open(path.string()).ValueOrDie();
  auto table = arrow::Table::Make(arrow::schema(fields), columns);
  check(
      parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), out, 32)
              .ok() &&
          out->Close().ok(),
      "write Parquet");
}
int main() {
  auto root =
      fresh_gui_directory(fs::temp_directory_path(), "unllm-corpus-fixture");
  try {
    Paths p{root / "data", root / "packs", root / "downloads"};
    auto wiki = root / "wikipedia-simple";
    fs::create_directories(wiki);
    parquet_fixture(wiki / "a.parquet");
    parquet_fixture(wiki / "b.parquet");
    bool interrupted = false;
    try {
      stage_pack(
          p, wiki, "", false, 1,
          [](size_t done, size_t) {
            if (done == 1)
              throw std::runtime_error("interrupt fixture");
          },
          true);
    } catch (const std::runtime_error &e) {
      interrupted = std::string(e.what()) == "interrupt fixture";
    }
    auto checkpoint =
        p.data / "staging/ingest-checkpoints/wikipedia-simple/shard-000.sqlite";
    check(interrupted && fs::exists(checkpoint), "checkpoint");
    auto retained = root / "saved.sqlite";
    fs::create_hard_link(checkpoint, retained);
    auto m = stage_pack(p, wiki, "", false, 2, {}, true);
    check(fs::equivalent(
              retained,
              p.data / "staging/packs/wikipedia-simple/shard-000.sqlite") &&
              !fs::exists(checkpoint),
          "shard resumed");
    check(m["shards"].size() == 2 && m["docs"].integer() == 2 &&
              m["passages"].integer() == 6,
          "wiki counts");
    auto query = [&](const std::string &pack, const std::string &sql) {
      auto r =
          Process(
              {"sqlite3",
               (p.data / "staging/packs" / pack / "shard-000.sqlite").string(),
               sql})
              .finish(after_ms(5000));
      check(!r.exit_code, "query shard");
      return r.out;
    };
    check(query("wikipedia-simple",
                "SELECT section||'|'||role FROM passages ORDER BY ord") ==
              "|lead\nGeology|body\nGeology|list\n",
          "article structure");
    check(query("wikipedia-simple",
                "SELECT text FROM passages WHERE role='list'") ==
              "Boardroom: Ann Smith; Bob Jones.\n",
          "list introduction");
    check(
        query("wikipedia-simple", "SELECT text FROM passages WHERE role='body'")
                .find("h=(1/2)gt²") != std::string::npos,
        "display formula");
    check(query("wikipedia-simple",
                "SELECT value FROM infobox WHERE key='population'") == "2345\n",
          "infobox values");
    auto poetry = root / "gutenberg-poetry-v001.ndjson.gz";
    auto gz = gzopen(poetry.c_str(), "wb");
    check(gz, "open gzip");
    for (int i = 0; i < 18; ++i)
      gzputs(gz, "{\"gid\":\"2\",\"s\":\"The wind is over the sea.\"}\n");
    check(gzclose(gz) == Z_OK, "finish gzip");
    write_atomic(root / "pg_catalog.csv",
                 "Text#,Title,Authors\r\n2,\"Songs, at Sea\",\"Brown, Anne, "
                 "1800-1880\"\r\n");
    m = stage_pack(p, poetry);
    check(m["passages"].integer() == 3, "verse grouping");
    check(query("gutenberg-poetry", "SELECT title||'|'||author FROM docs") ==
              "Songs, at Sea|Brown, Anne\n",
          "catalogue");
    write_atomic(
        root / "Posts.xml",
        R"(<posts><row Id="1" PostTypeId="1" Title="How do stars form?" Score="6" AcceptedAnswerId="3" OwnerUserId="10" Body="What makes a new star?" Tags="|space|" /><row Id="2" ParentId="1" PostTypeId="2" Score="7" Body="Other answer."/><row Id="3" ParentId="1" PostTypeId="2" Score="1" OwnerUserId="11" ContentLicense="CC BY-SA 4.0" Body="&lt;p&gt;Gravity draws gas together with energy $E=mc^2$.&lt;/p&gt;&lt;pre&gt;x=1&lt;/pre&gt;"/></posts>)");
    write_atomic(
        root / "Users.xml",
        R"(<users><row Id="10" DisplayName="Pat"/><row Id="11" DisplayName="Lin"/></users>)");
    auto archive = root / "physics.stackexchange.com.7z";
    auto r = Process({"7z", "a", archive.string(), "Posts.xml", "Users.xml"},
                     root.string())
                 .finish(after_ms(10000));
    check(!r.exit_code, "create archive");
    m = stage_pack(p, archive);
    check(m["docs"].integer() == 1 && m["passages"].integer() == 3,
          "archive counts");
    check(query("se-physics",
                "SELECT author||'|'||score||'|'||json_extract(extra,'$.asker') "
                "FROM docs") == "Lin|6|Pat\n",
          "post attribution");
    check(query("se-physics",
                "SELECT text FROM passages WHERE role='accepted'") ==
              "Gravity draws gas together with energy E=mc².\n",
          "accepted answer selection");
    fs::remove_all(root);
  } catch (const std::exception &e) {
    std::cerr << root << " " << e.what() << "\n";
    return 1;
  }
}
