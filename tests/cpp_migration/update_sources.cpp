#include "unllm/maintenance/sources.h"
#include "unllm/symbolic/gui_target.h"
#include <iostream>
using namespace unllm::maintenance;
using namespace unllm::native;
namespace fs = std::filesystem;
namespace {
void check(bool ok, const std::string &s) {
  if (!ok)
    throw std::runtime_error(s);
}
} // namespace
int main() {
  auto dir =
      fresh_gui_directory(fs::temp_directory_path(), "unllm-source-fixture");
  try {
    Paths paths{dir / "data", dir / "packs", dir / "downloads"};
    write_atomic(paths.downloads / "finewiki-en/SHA256SUMS",
                 "abc  one.parquet\n");
    write_atomic(paths.packs / "wordnet-en/manifest.json",
                 R"({"version":"2025","shards":[]})");
    auto state = Json::object();
    bool failure = false, changed = false;
    int requests = 0;
    Fetch query = [&](const std::string &url, bool head) -> std::string {
      ++requests;
      if (failure)
        return "";
      if (url.ends_with("/enwiki") || url.ends_with("/simplewiki")) {
        std::string subset = url.ends_with("/enwiki") ? "enwiki" : "simplewiki";
        return "[{\"path\":\"data/" + subset +
               "/one.parquet\",\"size\":100,\"lfs\":{\"oid\":\"" +
               (changed ? "xyz" : "abc") + "\"}}]";
      }
      if (url.ends_with("/finewiki"))
        return R"({"lastModified":"2026-08-30"})";
      if (url.ends_with("/stackexchange"))
        return std::string(
                   "{\"files\":[{\"name\":\"askubuntu.com.7z\",\"size\":"
                   "\"150\",\"mtime\":\"1699982000\",\"md5\":\"") +
               (changed ? "new" : "old") + "\"}]}";
      if (url.ends_with(".jsonl") && head)
        return std::string("HTTP/2 200\r\nLast-Modified: Thu, 13 Nov 2025 "
                           "11:01:00 GMT\r\nContent-Length: ") +
               (changed ? "180" : "160") + "\r\n";
      if (url.ends_with("/latest"))
        return changed ? R"({"tag_name":"v2026","published_at":"2026-02-01"})"
                       : R"({"tag_name":"v2025","published_at":"2025-02-01"})";
      throw std::runtime_error("Unexpected request " + url);
    };
    auto rows = check_sources(paths, state, query);
    auto report = public_check(rows);
    check(check_result(report) == 0 && requests == 7 && rows.size() == 6,
          "Initial update decision");
    check(rows.at(0)["fetch"].at(0).at(3).text() == "abc" &&
              !report["sources"].at(0).contains("fetch"),
          "Checksum or report leak");
    check(state["wiktionary-en"]["modified"].text() == "2025-11-13" &&
              state["wiktionary-en"]["size"].integer() == 160,
          "Header parse");
    fs::remove(paths.downloads / "finewiki-en/SHA256SUMS");
    changed = true;
    rows = check_sources(paths, state, query);
    report = public_check(rows);
    check(check_result(report) == 1 && report["newer"].size() == 4 &&
              rows.at(0)["available"].text().find("1 changed") !=
                  std::string::npos,
          "Changed sources or cache");
    check(rows.at(2)["fetch"].size() == 1 &&
              rows.at(2)["fetch"].at(0).at(1).text() ==
                  (paths.downloads / "stackexchange/askubuntu.com.7z").string(),
          "StackExchange fetch plan");
    check(state["wiktionary-en"]["size"].integer() == 160,
          "Lost installed Wiktionary size");
    failure = true;
    report = public_check(check_sources(paths, state, query));
    check(check_result(report) == 2 && report["unreachable"].size() == 5 &&
              state["wordnet-en"]["tag"].text() == "v2026",
          "Unavailable observations replaced known source state");
    fs::remove_all(dir);
    std::cout << "Six fixture sources, changed/current/unreachable states and "
                 "cached checksums passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << " in " << dir << '\n';
    return 1;
  }
}
