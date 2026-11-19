#include "../symbolic/gui_json.h"
#include "shard.h"
#include "unllm/maintenance/transaction.h"
#include "unllm/symbolic/gui_target.h"
#include <charconv>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <future>
#include <linux/fs.h>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sys/syscall.h>
#include <unicode/unistr.h>
#include <unistd.h>
namespace unllm::maintenance {
using namespace native;
using namespace native::gui;
namespace fs = std::filesystem;
namespace {
std::string date(time_t t, const char *format) {
  char buf[64];
  tm parts{};
  localtime_r(&t, &parts);
  strftime(buf, sizeof buf, format, &parts);
  return buf;
}
struct Input {
  std::string name, kind, source, version, license, attribution;
  void (*read)(const Path &, Shard &);
};
Input detect(const Path &p) {
  auto base = p.filename().string();
  auto folder = fs::is_directory(p) ? p : p.parent_path();
  auto side = read_optional_json(folder / "source.json");
  Path parquet;
  if (p.extension() == ".parquet")
    parquet = p;
  else if (fs::is_directory(p))
    for (auto &e : fs::directory_iterator(p))
      if (e.path().extension() == ".parquet") {
        parquet = e.path();
        break;
      }
  if (!parquet.empty()) {
    bool simple = (side["version"].text() + folder.string()).find("simple") !=
                  std::string::npos;
    return {side["name"].text(simple ? "wikipedia-simple" : "wikipedia-en"),
            "encyclopedia",
            side["source"].text(finewiki(parquet) ? "HuggingFaceFW/finewiki"
                                                  : "wikimedia/wikipedia"),
            side["version"].text(folder.filename().string()),
            side["license"].text("CC BY-SA 4.0"),
            "From Wikipedia, \"{title}\"",
            ingest_wiki};
  }
  if (fs::is_directory(p)) {
    for (auto &f : fs::directory_iterator(p))
      if (f.path().extension() == ".parquet" || f.path().extension() == ".7z")
        throw Unsupported(
            "A specialized corpus adapter is required for this folder");
    auto name = pack_title_key(base);
    std::replace(name.begin(), name.end(), ' ', '-');
    return {"docs-" + name,   "documents",
            p.string(),       date(time(nullptr), "%Y%m%d%H%M"),
            "local, private", "From your file \"{title}\"",
            ingest_documents};
  }
  if (!fs::is_regular_file(p))
    throw Unsupported("Expected a pack source");
  if (base.ends_with(".7z") &&
      (base.find("stackexchange") != base.npos || base == "askubuntu.com.7z" ||
       base == "superuser.com.7z")) {
    auto site = p.stem().string();
    auto mtime = std::chrono::file_clock::to_sys(fs::last_write_time(p));
    return {"se-" + site.substr(0, site.find('.')),
            "qa",
            "archive.org/stackexchange",
            side["version"].text(
                date(std::chrono::system_clock::to_time_t(mtime), "%Y-%m")),
            "CC BY-SA (per post)",
            "From " + site + ", \"{title}\"",
            ingest_stackexchange};
  }
  if (base.starts_with("gutenberg-poetry") && base.ends_with(".ndjson.gz"))
    return {"gutenberg-poetry",
            "poems",
            "gutenberg-poetry-corpus",
            side["version"].text("v001"),
            "Public domain (US)",
            "From {title} by {author} (Project Gutenberg)",
            ingest_poetry};
  if (base.starts_with("english-wordnet") && base.ends_with(".zip")) {
    std::string year;
    for (char c : base)
      if (c >= '0' && c <= '9' && year.size() < 4)
        year += c;
    return {"wordnet-en",
            "definitions",
            "globalwordnet/english-wordnet",
            year.empty() ? side["version"].text("1") : year,
            "CC BY 4.0",
            "From WordNet",
            ingest_wordnet};
  }
  if (base.starts_with("kaikki") && base.ends_with(".jsonl")) {
    auto mtime = std::chrono::file_clock::to_sys(fs::last_write_time(p));
    return {"wiktionary-en",
            "definitions",
            "kaikki.org",
            side["version"].text(
                date(std::chrono::system_clock::to_time_t(mtime), "%Y-%m")),
            "CC BY-SA 4.0 and GFDL",
            "From Wiktionary",
            ingest_wiktionary};
  }
  throw Unsupported("Unsupported native corpus input: " + base);
}
Json quality(const Path &file, int64_t count, bool private_docs) {
  sqlite3 *raw = nullptr;
  auto rc = sqlite3_open_v2(
      file.c_str(), &raw, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
  if (rc != SQLITE_OK)
    throw Unsupported("Cannot inspect built shard");
  sqlite3_stmt *s = nullptr;
  if (sqlite3_prepare_v2(raw, "SELECT text,role FROM passages WHERE pid=?", -1,
                         &s, nullptr) != SQLITE_OK)
    throw Unsupported("Cannot inspect passages");
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(
      s, sqlite3_finalize);
  // Floyd's algorithm samples without replacement in bounded memory.
  std::mt19937_64 random(7);
  std::set<int64_t> ids;
  for (auto i = std::max<int64_t>(1, count - 5999); i <= count; ++i) {
    auto selected = std::uniform_int_distribution<int64_t>(1, i)(random);
    if (!ids.insert(selected).second)
      ids.insert(i);
  }
  const std::map<std::string, std::regex> defects{
      {"number holes",
       std::regex(
           R"(\b(?:about|around|nearly|over|under|approximately|roughly|almost|than|at|is|was|by)  +[a-z(])")},
      {"empty brackets", std::regex(R"(\(\s*[,;]?\s*\))")},
      {"leftover markup",
       std::regex(
           R"(</?(?:p|div|span|a|br|code|pre|strong|em|img|li|ul|ol|table|td|tr)\b[^>]*>|&(?:amp|lt|gt|quot|nbsp|#\d+);|\{\{|\}\}|\[\[|\]\]|\|thumb\||<ref\b)")},
      {"broken characters", std::regex("Ã(?:[\\xC2][\\x80-\\xBF])|â€|�")},
      {"raw formulas",
       std::regex(
           R"(\\(?:frac|displaystyle|mathrm|sqrt|cdot|times|left|right|alpha|sum|int)\b)")}};
  int tested = 0;
  std::map<std::string, int> counts;
  counts["tiny passages"] = 0;
  for (auto &[k, _] : defects)
    counts[k] = 0;
  for (auto id : ids) {
    sqlite3_reset(s);
    sqlite3_bind_int64(s, 1, id);
    if (sqlite3_step(s) != SQLITE_ROW)
      throw Unsupported("Built shard has a missing passage");
    auto get = [&](int col) {
      return std::string(
          reinterpret_cast<const char *>(sqlite3_column_text(s, col)),
          sqlite3_column_bytes(s, col));
    };
    if (get(1) == "code")
      continue;
    ++tested;
    auto text = get(0);
    for (auto &[k, rx] : defects)
      counts[k] += std::regex_search(text, rx);
    counts["tiny passages"] +=
        icu::UnicodeString::fromUTF8(text).countChar32() <= 14;
  }
  auto rates = Json::object();
  const std::map<std::string, double> limits{{"number holes", 3},
                                             {"empty brackets", 3},
                                             {"leftover markup", 2},
                                             {"broken characters", 1}};
  for (auto &[name, n] : counts) {
    auto rate = tested ? std::round(10000. * n / tested) / 100. : 0.;
    rates.set(name, num(rate));
    if (!private_docs && limits.contains(name) && rate > limits.at(name))
      throw Unsupported("Pack quality limit exceeded for " + name);
  }
  return obj(
      {{"passages_checked", Json::number(tested)}, {"defect_percent", rates}});
}
void check_pack_name(const std::string &name) {
  if (name.empty() || name.front() == '.' ||
      name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRS"
                             "TUVWXYZ0123456789_.-") != name.npos)
    throw Unsupported("Invalid pack name");
}
} // namespace
Json stage_pack(const Paths &paths, const Path &source,
                const std::string &override, bool force, unsigned workers,
                const std::function<void(size_t, size_t)> &progress,
                bool resume) {
  if (!workers)
    throw Unsupported("Expected at least one ingestion worker");
  auto input = fs::canonical(source);
  auto def = detect(input);
  const bool compact_fts =
      def.read == ingest_wiki &&
      (def.version + input.string()).find("simple") == std::string::npos;
  if (!override.empty())
    def.name = override;
  check_pack_name(def.name);
  auto parent = paths.data / "staging/packs";
  fs::create_directories(parent);
  Path work, final = parent / def.name;
  if (resume) {
    // The public add command holds UpdateLock for the entire build. Completed
    // shards survive in the checkpoint directory when this scratch tree resets.
    work = paths.data / "staging/ingest-work" / def.name;
    fs::remove_all(work);
    fs::create_directories(work);
  } else
    work = fresh_gui_directory(parent, ".building-" + def.name);
  auto meta = obj({{"name", str(def.name)},
                   {"kind", str(def.kind)},
                   {"source", str(def.source)},
                   {"version", str(def.version)},
                   {"license", str(def.license)},
                   {"attribution", str(def.attribution)}});
  const auto checkpoints = paths.data / "staging/ingest-checkpoints" / def.name;
  try {
    auto now = std::chrono::steady_clock::now();
    std::vector<Path> inputs{input};
    if (def.read == ingest_wiki) {
      inputs.clear();
      auto folder = fs::is_directory(input) ? input : input.parent_path();
      for (auto &file : fs::directory_iterator(folder))
        if (file.path().extension() == ".parquet")
          inputs.push_back(file.path());
      std::sort(inputs.begin(), inputs.end());
    }
    resume = resume && inputs.size() > 1;
    if (resume)
      fs::create_directories(checkpoints);
    auto shards = Json::array(), rates = Json::object();
    int64_t total_docs = 0, total_passages = 0, checked = 0;
    std::map<std::string, double> defects;
    struct Built {
      Json row, quality;
    };
    auto build = [&](const Path &part, size_t index, const Json &metadata) {
      std::ostringstream filename;
      filename << "shard-" << std::setw(3) << std::setfill('0') << index
               << ".sqlite";
      auto file = work / filename.str();
      auto cached = checkpoints / filename.str();
      auto stamp = cached.string() + ".json";
      std::string identity;
      if (resume) {
        auto record = metadata.clone();
        record.set("reader", str("4-native-1"));
        record.set("file", str(part.string()));
        record.set("size", str(std::to_string(fs::file_size(part))));
        record.set("modified",
                   str(std::to_string(
                       fs::last_write_time(part).time_since_epoch().count())));
        record.set("force", Json::boolean(force));
        identity = sha256(canonical(record));
        auto saved = read_optional_json(stamp);
        if (saved["identity"].text() == identity &&
            fs::is_regular_file(fs::symlink_status(cached)) &&
            saved["bytes"].integer() ==
                static_cast<int64_t>(fs::file_size(cached))) {
          fs::create_hard_link(cached, file);
          return Built{saved["row"], saved["quality"]};
        }
      }
      int64_t docs, passages;
      {
        Shard shard(file, metadata, compact_fts);
        def.read(part, shard);
        shard.finish();
        docs = shard.docs;
        passages = shard.passages;
      }
      auto row = obj({{"file", str(filename.str())},
                      {"input", str(part.filename().string())},
                      {"docs", Json::number(docs)},
                      {"passages", Json::number(passages)},
                      {"status", str("done")}});
      auto report = quality(file, passages, force || def.kind == "documents");
      if (resume) {
        fs::remove(stamp);
        fs::remove(cached);
        fs::create_hard_link(file, cached);
        write_atomic(
            stamp,
            canonical(obj({{"identity", str(identity)},
                           {"row", row},
                           {"quality", report},
                           {"bytes", Json::number(fs::file_size(file))}})));
      }
      return Built{row, report};
    };
    if (inputs.size() > 1000)
      throw Unsupported("Pack exceeds three-digit shard numbering");
    std::deque<std::future<Built>> futures;
    auto collect = [&] {
      auto result = futures.front().get();
      futures.pop_front();
      auto q = result.quality;
      auto n = q["passages_checked"].integer();
      checked += n;
      for (auto &[k, v] : q["defect_percent"].members())
        defects[k] += v.real() * n;
      shards.push(result.row);
      total_docs += result.row["docs"].integer();
      total_passages += result.row["passages"].integer();
      if (progress)
        progress(shards.size(), inputs.size());
    };
    if (progress)
      progress(0, inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
      futures.push_back(
          std::async(workers > 1 ? std::launch::async : std::launch::deferred,
                     build, inputs[i], i, meta.clone()));
      if (futures.size() >= workers)
        collect();
    }
    while (!futures.empty())
      collect();
    if (!total_docs)
      throw Unsupported("No documents could be ingested");
    for (auto &[k, sum] : defects)
      rates.set(k, num(checked ? sum / checked : 0));
    auto manifest = meta.clone();
    manifest.set("format", str("unllm-pack/1"));
    manifest.set("ingest_version", str("4-native-1"));
    manifest.set("shards", shards);
    manifest.set("docs", Json::number(total_docs));
    manifest.set("passages", Json::number(total_passages));
    manifest.set("quality", obj({{"passages_checked", Json::number(checked)},
                                 {"defect_percent", rates}}));
    manifest.set("built", str(date(time(nullptr), "%Y-%m-%dT%H:%M:%S")));
    manifest.set("built_seconds",
                 Json::number(std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::steady_clock::now() - now)
                                  .count()));
    write_atomic(work / "manifest.json", canonical(manifest) + "\n");
    if (fs::exists(fs::symlink_status(final))) {
      if (!fs::is_directory(fs::symlink_status(final)))
        throw Unsupported("Invalid staged pack path");
      if (syscall(SYS_renameat2, AT_FDCWD, work.c_str(), AT_FDCWD,
                  final.c_str(), RENAME_EXCHANGE) < 0)
        throw std::runtime_error("Cannot replace staged pack: " +
                                 std::string(strerror(errno)));
      fs::remove_all(work);
    } else
      fs::rename(work, final);
    if (resume)
      fs::remove_all(checkpoints);
    return manifest;
  } catch (...) {
    std::error_code ec;
    fs::remove_all(work, ec);
    throw;
  }
}
Json source_version(const Path &input, const std::string &override) {
  auto def = detect(fs::canonical(input));
  return obj({{"name", str(override.empty() ? def.name : override)},
              {"version", str(def.version)}});
}
Json inspect_pack(const Path &pack) {
  auto manifest = read_optional_json(pack / "manifest.json"),
       result = Json::array();
  for (auto row : manifest["shards"].items()) {
    auto filename = required_text(row, "file");
    if (fs::path(filename).filename() != filename)
      throw Unsupported("Invalid shard filename");
    sqlite3 *raw = nullptr;
    auto rc = sqlite3_open_v2((pack / filename).c_str(), &raw,
                              SQLITE_OPEN_READONLY, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
    if (rc != SQLITE_OK)
      throw Unsupported("Cannot inspect shard");
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(raw, "SELECT count(*) FROM passages", -1, &st,
                           nullptr) != SQLITE_OK)
      throw Unsupported("Cannot count passages");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(
        st, sqlite3_finalize);
    if (sqlite3_step(st) != SQLITE_ROW)
      throw Unsupported("Cannot count passages");
    auto q = quality(pack / filename, sqlite3_column_int64(st, 0), true);
    q.set("file", str(filename));
    result.push(q);
  }
  return result;
}
void install_staged_pack(const Paths &p, const std::string &name) {
  check_pack_name(name);
  auto stage = p.data / "staging/packs" / name, final = p.packs / name;
  fs::create_directories(p.packs);
  if (fs::exists(fs::symlink_status(final))) {
    if (!fs::is_directory(fs::symlink_status(final)))
      throw Unsupported("Invalid installed pack path");
    if (syscall(SYS_renameat2, AT_FDCWD, stage.c_str(), AT_FDCWD, final.c_str(),
                RENAME_EXCHANGE) < 0)
      throw Unsupported("Cannot exchange installed pack: " +
                        std::string(strerror(errno)));
    fs::remove_all(stage);
  } else
    fs::rename(stage, final);
}
} // namespace unllm::maintenance
