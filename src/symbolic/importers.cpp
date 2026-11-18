#include "unllm/symbolic/importers.h"
#include <algorithm>
#include <cerrno>
#include <curl/curl.h>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>
#include <unistd.h>
namespace unllm::native {
namespace {
Json obj(std::initializer_list<std::pair<std::string, Json>> fields) {
  auto value = Json::object();
  for (auto &[k, v] : fields)
    value.set(k, v);
  return value;
}
Json str(const std::string &value) { return Json::string(value); }
Json strings(const std::vector<std::string> &values) {
  auto out = Json::array();
  for (auto &value : values)
    out.push(str(value));
  return out;
}
bool alnum(UChar32 cp) {
  auto type = u_charType(cp);
  return (type >= U_UPPERCASE_LETTER && type <= U_OTHER_LETTER) ||
         (type >= U_DECIMAL_DIGIT_NUMBER && type <= U_OTHER_NUMBER);
}
bool qid_valid(const std::string &q) {
  return q.size() > 1 && q[0] == 'Q' &&
         std::all_of(q.begin() + 1, q.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}
std::string trim(std::string s) {
  auto begin = s.find_first_not_of(" \t\n\r\v\f");
  if (begin == std::string::npos)
    return "";
  return s.substr(begin, s.find_last_not_of(" \t\n\r\v\f") - begin + 1);
}
class PackReader {
  sqlite3 *db_ = nullptr;

public:
  explicit PackReader(const std::filesystem::path &path) {
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                        nullptr) != SQLITE_OK) {
      auto error = std::string(sqlite3_errmsg(db_));
      sqlite3_close(db_);
      db_ = nullptr;
      throw std::runtime_error(error);
    }
  }
  ~PackReader() { sqlite3_close(db_); }
  Json rows(const std::string &sql, const std::vector<Json> &params) {
    sqlite3_stmt *raw = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db_));
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(
        raw, sqlite3_finalize);
    for (size_t i = 0; i < params.size(); ++i) {
      int rc;
      if (params[i].is_integer())
        rc = sqlite3_bind_int64(raw, i + 1, params[i].integer());
      else {
        auto value = params[i].text();
        rc = sqlite3_bind_text(raw, i + 1, value.data(), value.size(),
                               SQLITE_TRANSIENT);
      }
      if (rc != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    auto result = Json::array();
    int rc;
    while ((rc = sqlite3_step(raw)) == SQLITE_ROW) {
      auto row = Json::object();
      for (int i = 0; i < sqlite3_column_count(raw); ++i) {
        Json value;
        auto type = sqlite3_column_type(raw, i);
        if (type == SQLITE_INTEGER)
          value = Json::number(sqlite3_column_int64(raw, i));
        else if (type == SQLITE_FLOAT)
          value = Json::real(sqlite3_column_double(raw, i));
        else if (type == SQLITE_TEXT)
          value = str(std::string(
              reinterpret_cast<const char *>(sqlite3_column_text(raw, i)),
              sqlite3_column_bytes(raw, i)));
        else if (type != SQLITE_NULL)
          throw Unsupported("Unsupported binary pack field");
        row.set(sqlite3_column_name(raw, i), value);
      }
      result.push(row);
    }
    if (rc != SQLITE_DONE)
      throw std::runtime_error(sqlite3_errmsg(db_));
    return result;
  }
};
struct Download {
  std::string text;
  bool limit = false;
};
size_t downloaded(char *data, size_t size, size_t count, void *state) {
  auto &out = *static_cast<Download *>(state);
  if (size && count > 8 * 1024 * 1024 / size) {
    out.limit = true;
    return 0;
  }
  size_t bytes = size * count;
  if (bytes > 8 * 1024 * 1024 - out.text.size()) {
    out.limit = true;
    return 0;
  }
  try {
    out.text.append(data, bytes);
    return bytes;
  } catch (...) {
    return 0;
  }
}
std::string download(const std::string &q) {
  static const int initialized = [] {
    return curl_global_init(CURL_GLOBAL_DEFAULT);
  }();
  if (initialized != CURLE_OK)
    throw std::runtime_error("Cannot initialize HTTP client");
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
                                                           curl_easy_cleanup);
  if (!curl)
    throw std::runtime_error("Cannot create HTTP client");
  auto url = "https://www.wikidata.org/wiki/Special:EntityData/" + q + ".json";
  Download result;
  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_USERAGENT,
                   "unllm-upgrade/0.1 (local symbolic knowledge importer)");
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 3L);
  curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, downloaded);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &result);
  curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
  auto rc = curl_easy_perform(curl.get());
  if (result.limit)
    throw ResourceLimit("Wikidata entity exceeds input budget");
  if (rc != CURLE_OK)
    throw std::runtime_error(std::string("Wikidata fetch failed: ") +
                             curl_easy_strerror(rc));
  return result.text;
}
void cache_write(const std::filesystem::path &target, const std::string &data) {
  auto pattern = (target.parent_path() / "download-XXXXXX").string();
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back('\0');
  int fd = mkstemp(name.data());
  if (fd < 0)
    throw std::runtime_error("Cannot create cache staging file");
  try {
    size_t offset = 0;
    while (offset < data.size()) {
      auto n = ::write(fd, data.data() + offset, data.size() - offset);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error("Cannot write cache data");
      offset += n;
    }
    if (fsync(fd) != 0)
      throw std::runtime_error("Cannot flush cache data");
    close(fd);
    fd = -1;
    std::filesystem::rename(name.data(), target);
  } catch (...) {
    if (fd >= 0)
      close(fd);
    unlink(name.data());
    throw;
  }
}
} // namespace
std::string title_key(const std::string &text) {
  std::string out;
  bool space = false;
  for (int32_t i = 0; i < static_cast<int32_t>(text.size());) {
    UChar32 cp;
    U8_NEXT(text.data(), i, static_cast<int32_t>(text.size()), cp);
    if (cp < 0)
      throw Unsupported("Invalid UTF-8 title");
    if (!alnum(cp)) {
      space = !out.empty();
      continue;
    }
    if (space) {
      out += ' ';
      space = false;
    }
    icu::UnicodeString word(cp);
    word.foldCase();
    word.toUTF8String(out);
  }
  if (out == "the" || out == "a" || out == "an")
    return "";
  for (auto article : {"the ", "a ", "an "})
    if (out.starts_with(article)) {
      out.erase(0, std::string(article).size());
      break;
    }
  return out;
}
Json find_documents(const std::filesystem::path &packs,
                    const std::vector<std::string> &titles,
                    const std::vector<std::string> &pack_names) {
  std::set<std::string> wanted;
  for (auto &title : titles)
    wanted.insert(title_key(title));
  auto found = Json::array();
  if (wanted.empty() || !std::filesystem::exists(packs))
    return found;
  std::vector<std::filesystem::path> folders;
  for (auto &entry : std::filesystem::directory_iterator(packs))
    if (entry.is_directory())
      folders.push_back(entry.path());
  std::sort(folders.begin(), folders.end());
  std::set<std::pair<std::string, std::string>> seen;
  for (auto &directory : folders) {
    auto name = directory.filename().string();
    auto manifest = directory / "manifest.json";
    if (!std::filesystem::exists(manifest) ||
        (!pack_names.empty() && std::find(pack_names.begin(), pack_names.end(),
                                          name) == pack_names.end()))
      continue;
    auto info = Json::parse(read_file(manifest));
    if (info["kind"].text() != "encyclopedia")
      continue;
    std::vector<std::filesystem::path> shards;
    for (auto &entry : std::filesystem::directory_iterator(directory))
      if (entry.path().filename().string().starts_with("shard-") &&
          entry.path().extension() == ".sqlite")
        shards.push_back(entry.path());
    std::sort(shards.begin(), shards.end());
    for (auto &shard : shards) {
      PackReader db(shard);
      std::string placeholders;
      std::vector<Json> params;
      for (auto &title : wanted) {
        if (!placeholders.empty())
          placeholders += ',';
        placeholders += '?';
        params.push_back(str(title));
      }
      for (auto row : db.rows("SELECT * FROM docs WHERE title_key IN (" +
                                  placeholders + ")",
                              params)
                          .items()) {
        auto fields =
            db.rows("SELECT box,key,value FROM infobox WHERE doc_id=?",
                    {row["doc_id"]});
        auto extra = Json::parse(
            row["extra"].text().empty() ? "{}" : row["extra"].text());
        auto qid = extra["wikidata"].text();
        auto identity =
            qid.empty()
                ? "pack:" + digest(strings({name, row["title_key"].text()}))
                                .substr(0, 24)
                : "wd:" + qid;
        if (!seen.insert({name, identity}).second)
          continue;
        found.push(obj(
            {{"id", str(identity)},
             {"qid", str(qid)},
             {"name", row["title"]},
             {"row", row},
             {"fields", fields},
             {"pack", str(name)},
             {"version", info.contains("version") ? info["version"] : str("")},
             {"license", info.contains("license") ? info["license"] : str("")},
             {"shard", str(shard.filename().string())}}));
      }
    }
  }
  return found;
}
Json import_packs(FactStore &store, const std::filesystem::path &packs,
                  const std::vector<std::string> &titles,
                  const std::vector<std::string> &pack_names) {
  const std::map<std::string, std::string> fields{
      {"country", "country"},         {"constituent country", "country"},
      {"sovereign state", "country"}, {"author", "author"},
      {"authors", "author"},          {"birth place", "birthplace"},
      {"birthplace", "birthplace"},   {"capital", "capital"},
      {"location", "located_in"}};
  auto documents = find_documents(packs, titles, pack_names),
       catalog = Json::array(), skipped = Json::array(),
       results = Json::array();
  std::map<std::string, std::set<std::string>> aliases;
  std::set<std::string> qids;
  for (auto d : documents.items()) {
    aliases[key(d["name"].text())].insert(d["id"].text());
    catalog.push(
        obj({{"id", d["id"]}, {"name", d["name"]}, {"type", str("entity")}}));
    if (!d["qid"].text().empty())
      qids.insert(d["qid"].text());
  }
  if (catalog.size())
    store.import_document(
        obj({{"source",
              obj({{"id", str("pack-catalog:" + digest(catalog))},
                   {"title",
                    str("Entity identities from installed reference packs")}})},
             {"entities", catalog}}));
  int64_t count = 0;
  for (auto d : documents.items()) {
    auto claims = Json::array();
    for (auto field : d["fields"].items()) {
      auto normalized = field["key"].text();
      std::replace(normalized.begin(), normalized.end(), '_', ' ');
      normalized = key(normalized);
      if (!fields.contains(normalized))
        continue;
      auto value = trim(field["value"].text());
      std::set<std::string> ids;
      auto it = aliases.find(key(value));
      if (it != aliases.end())
        ids = it->second;
      else
        for (auto match : store.resolve(value).items())
          ids.insert(match["id"].text());
      if (ids.size() != 1) {
        skipped.push(
            obj({{"title", d["name"]},
                 {"field", field["key"]},
                 {"reason", str("not one resolved structured entity")}}));
        continue;
      }
      auto location = obj({{"shard", d["shard"]},
                           {"document", d["row"]["doc_id"]},
                           {"box", field["box"]},
                           {"field", field["key"]}});
      claims.push(obj({{"predicate", str(fields.at(normalized))},
                       {"subject", d["id"]},
                       {"object", str(*ids.begin())},
                       {"qualifiers", obj({{"source_location", location}})}}));
    }
    auto source =
        obj({{"id", str("pack:" + d["pack"].text() + ":" + d["id"].text())},
             {"title", d["name"]},
             {"url", d["row"]["url"]},
             {"version", d["version"]},
             {"license", d["license"]},
             {"adapter", str("exact-infobox-entity-v1")}});
    auto imported =
        store.import_document(obj({{"source", source}, {"claims", claims}}));
    count += imported["claims"].integer();
    results.push(imported);
  }
  return obj(
      {{"documents", Json::number(documents.size())},
       {"claims", Json::number(count)},
       {"sources", results},
       {"skipped", skipped},
       {"qids", strings(std::vector<std::string>(qids.begin(), qids.end()))}});
}
Json import_wikidata(FactStore &store, const std::vector<std::string> &qids,
                     const std::filesystem::path &cache, int depth,
                     int max_entities, bool fetch) {
  if (depth < 0 || depth > 3 || max_entities < 1 || max_entities > 200)
    throw Unsupported("Import budget exceeded");
  const std::vector<std::pair<std::string, std::string>> properties{
      {"P17", "country"},     {"P19", "birthplace"},  {"P50", "author"},
      {"P36", "capital"},     {"P131", "located_in"}, {"P31", "type"},
      {"P279", "subclass_of"}};
  const std::vector<std::pair<std::string, std::string>> classes{
      {"Q5", "person"},           {"Q515", "city"}, {"Q6256", "country"},
      {"Q43229", "organization"}, {"Q571", "work"}, {"Q8261", "work"},
      {"Q11424", "work"}};
  std::filesystem::create_directories(cache);
  std::deque<std::pair<std::string, int>> todo;
  for (auto &q : qids)
    todo.push_back({q, 0});
  std::set<std::string> seen;
  auto imported = Json::array(), skipped = Json::array(),
       entities = Json::object();
  struct Raw {
    std::string qid;
    Json raw, claims, document;
  };
  std::vector<Raw> raw_documents;
  while (!todo.empty() && seen.size() < static_cast<size_t>(max_entities)) {
    auto [qid, level] = todo.front();
    todo.pop_front();
    if (seen.contains(qid))
      continue;
    if (!qid_valid(qid))
      throw Unsupported("Invalid Wikidata ID");
    seen.insert(qid);
    auto path = cache / (qid + ".json");
    if (!std::filesystem::exists(path)) {
      if (!fetch) {
        skipped.push(obj(
            {{"entity", str(qid)}, {"reason", str("not in offline cache")}}));
        continue;
      }
      auto data = download(qid);
      auto parsed = Json::parse(data);
      if (!parsed["entities"].contains(qid))
        throw Unsupported("Missing requested entity");
      cache_write(path, data);
    }
    auto document = Json::parse(read_file(path, 8 * 1024 * 1024)),
         raw = document["entities"][qid];
    if (!raw.is_object())
      throw Unsupported("Missing requested entity");
    auto name = raw["labels"]["en"]["value"].text(qid);
    auto aliases = Json::array();
    for (auto a : raw["aliases"]["en"].items())
      aliases.push(str(required_text(a, "value")));
    entities.set("wd:" + qid, obj({{"id", str("wd:" + qid)},
                                   {"name", str(name)},
                                   {"type", str("entity")},
                                   {"aliases", aliases}}));
    auto claims = Json::array();
    for (auto &[property, predicate] : properties)
      for (auto record : raw["claims"][property].items()) {
        if (record["rank"].text() == "deprecated")
          continue;
        auto snak = record["mainsnak"];
        if (snak["snaktype"].text() != "value" ||
            snak["datatype"].text() != "wikibase-item")
          continue;
        auto target = snak["datavalue"]["value"]["id"].text();
        if (target.empty())
          continue;
        if (!qid_valid(target))
          throw Unsupported("Invalid referenced Wikidata ID");
        if (record["qualifiers"].size()) {
          skipped.push(obj(
              {{"entity", str(qid)},
               {"property", str(property)},
               {"claim", record["id"]},
               {"reason",
                str("qualified assertion needs a supported scope adapter")}}));
          continue;
        }
        auto target_id = "wd:" + target;
        if (!entities.contains(target_id))
          entities.set(target_id, obj({{"id", str(target_id)},
                                       {"name", str(target)},
                                       {"type", str("entity")}}));
        auto id = required_text(record, "id");
        auto location = obj(
            {{"property", str(property)},
             {"statement", str(id)},
             {"rank", record["rank"]},
             {"references", record.contains("references") ? record["references"]
                                                          : Json::array()}});
        claims.push(
            obj({{"id", str("wikidata:" + id)},
                 {"predicate", str(predicate)},
                 {"subject", str("wd:" + qid)},
                 {"object", str(target_id)},
                 {"qualifiers", obj({{"source_location", location}})}}));
        if (level < depth && property != "P31" && property != "P279")
          todo.push_back({target, level + 1});
      }
    raw_documents.push_back({qid, raw, claims, document});
  }
  for (auto &[qid, type] : classes)
    if (!entities.contains("wd:" + qid))
      entities.set("wd:" + qid, obj({{"id", str("wd:" + qid)},
                                     {"name", str(qid)},
                                     {"type", str("type")}}));
  auto existing = store.entities(), catalog = Json::array();
  for (auto &[id, entity] : entities.members()) {
    if (qid_valid(entity["name"].text()) && existing.contains(id))
      entity.set("name", existing[id]["name"]);
    catalog.push(entity);
  }
  store.import_document(
      obj({{"source", obj({{"id", str("wikidata:catalog:" +
                                      digest(strings(std::vector<std::string>(
                                          seen.begin(), seen.end()))))},
                           {"title", str("Wikidata entity labels and aliases")},
                           {"version", str("cached")}})},
           {"entities", catalog}}));
  auto mappings = Json::array();
  for (auto &[q, type] : classes)
    mappings.push(obj({{"predicate", str("subclass_of")},
                       {"subject", str("wd:" + q)},
                       {"object", str(type)}}));
  store.import_document(obj(
      {{"source",
        obj({{"id", str("unllm:wikidata-type-mapping:v1")},
             {"title",
              str("Declared mapping of Wikidata classes to unllm types")}})},
       {"claims", mappings}}));
  int64_t count = 0;
  for (auto &d : raw_documents) {
    auto rev = d.raw["lastrevid"];
    auto version = rev.null()        ? ""
                   : rev.is_string() ? rev.text()
                                     : canonical(rev);
    auto source = obj({{"id", str("wikidata:" + d.qid)},
                       {"title", entities["wd:" + d.qid]["name"]},
                       {"url", str("https://www.wikidata.org/wiki/" + d.qid)},
                       {"version", str(version)},
                       {"license", str("CC0")},
                       {"cache_sha256", str(digest(d.document))},
                       {"adapter", str("wikidata-unqualified-items-v1")}});
    auto result =
        store.import_document(obj({{"source", source}, {"claims", d.claims}}));
    count += result["claims"].integer();
    imported.push(result);
  }
  return obj({{"documents", Json::number(imported.size())},
              {"claims", Json::number(count)},
              {"sources", imported},
              {"skipped", skipped},
              {"queued_beyond_budget", Json::number(todo.size())}});
}
} // namespace unllm::native
