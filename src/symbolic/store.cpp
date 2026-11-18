#include "unllm/symbolic/store.h"
#include <chrono>
#include <map>
#include <memory>

namespace unllm::native {
namespace {
class Statement {
public:
  sqlite3_stmt *p = nullptr;
  sqlite3 *db;
  Statement(sqlite3 *d, const std::string &sql,
            const std::vector<Json> &bindings)
      : db(d) {
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &p, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
    for (size_t i = 0; i < bindings.size(); ++i) {
      auto &v = bindings[i];
      int rc;
      if (v.null())
        rc = sqlite3_bind_null(p, i + 1);
      else if (v.is_integer())
        rc = sqlite3_bind_int64(p, i + 1, v.integer());
      else if (v.is_number())
        rc = sqlite3_bind_double(p, i + 1, v.real());
      else if (v.is_boolean())
        rc = sqlite3_bind_int(p, i + 1, v.flag());
      else if (v.is_string()) {
        auto s = v.text();
        rc = sqlite3_bind_text(p, i + 1, s.data(), s.size(), SQLITE_TRANSIENT);
      } else {
        sqlite3_finalize(p);
        p = nullptr;
        throw Unsupported("Invalid SQL binding");
      }
      if (rc != SQLITE_OK) {
        sqlite3_finalize(p);
        p = nullptr;
        throw std::runtime_error(sqlite3_errmsg(db));
      }
    }
  }
  ~Statement() { sqlite3_finalize(p); }
  int step() {
    int rc = sqlite3_step(p);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE)
      throw std::runtime_error(sqlite3_errmsg(db));
    return rc;
  }
};
Json strings(std::initializer_list<std::string> values) {
  auto a = Json::array();
  for (auto &s : values)
    a.push(Json::string(s));
  return a;
}
} // namespace
void FactStore::exec(const std::string &sql) const {
  char *error = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    std::string s = error ? error : "SQLite error";
    sqlite3_free(error);
    throw std::runtime_error(s);
  }
}
void FactStore::execute(const std::string &sql,
                        const std::vector<Json> &values) const {
  Statement statement(db_, sql, values);
  if (statement.step() != SQLITE_DONE)
    throw std::runtime_error("Unexpected SQL rows");
}
Json FactStore::rows(const std::string &sql,
                     const std::vector<Json> &values) const {
  Statement s(db_, sql, values);
  auto out = Json::array();
  while (s.step() == SQLITE_ROW) {
    auto row = Json::object();
    for (int i = 0; i < sqlite3_column_count(s.p); ++i) {
      Json v;
      switch (sqlite3_column_type(s.p, i)) {
      case SQLITE_INTEGER:
        v = Json::number(sqlite3_column_int64(s.p, i));
        break;
      case SQLITE_FLOAT:
        v = Json::real(sqlite3_column_double(s.p, i));
        break;
      case SQLITE_TEXT:
        v = Json::string(std::string(
            reinterpret_cast<const char *>(sqlite3_column_text(s.p, i)),
            sqlite3_column_bytes(s.p, i)));
        break;
      case SQLITE_NULL:
        break;
      default:
        throw Unsupported("Unexpected binary database column");
      }
      row.set(sqlite3_column_name(s.p, i), v);
    }
    out.push(row);
  }
  return out;
}
FactStore::FactStore(const std::filesystem::path &path,
                     const std::filesystem::path &resources) {
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
  if (sqlite3_open_v2(path.c_str(), &db_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_NOMUTEX,
                      nullptr) != SQLITE_OK) {
    std::string error = sqlite3_errmsg(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error(error);
  }
  try {
    sqlite3_busy_timeout(db_, 10000);
    exec(R"SQL(PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;
 CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
 CREATE TABLE IF NOT EXISTS relations(name TEXT PRIMARY KEY,data TEXT NOT NULL);
 CREATE TABLE IF NOT EXISTS sources(id TEXT PRIMARY KEY,data TEXT NOT NULL);
 CREATE TABLE IF NOT EXISTS entities(id TEXT PRIMARY KEY,name TEXT NOT NULL,type TEXT NOT NULL);
 CREATE TABLE IF NOT EXISTS aliases(alias TEXT NOT NULL,entity TEXT NOT NULL REFERENCES entities(id),PRIMARY KEY(alias,entity));
 CREATE TABLE IF NOT EXISTS claims(id TEXT PRIMARY KEY,predicate TEXT NOT NULL,subject TEXT NOT NULL,object TEXT NOT NULL,negative INTEGER NOT NULL,valid_from INTEGER,valid_to INTEGER,source TEXT NOT NULL REFERENCES sources(id),origin TEXT NOT NULL,qualifiers TEXT NOT NULL);
 CREATE INDEX IF NOT EXISTS claim_lookup ON claims(predicate,subject,object);
 CREATE INDEX IF NOT EXISTS claim_source ON claims(source);
 CREATE TABLE IF NOT EXISTS completeness(predicate TEXT NOT NULL,subject TEXT NOT NULL,source TEXT NOT NULL REFERENCES sources(id),PRIMARY KEY(predicate,subject,source));
 CREATE TABLE IF NOT EXISTS prepared(id TEXT PRIMARY KEY,data TEXT NOT NULL,checksum TEXT NOT NULL,created REAL NOT NULL);)SQL");
    auto version = rows("SELECT value FROM metadata WHERE key='schema'");
    if (version.size() && version.at(0)["value"].text() != "1")
      throw Unsupported("Unsupported fact-store schema");
    exec("BEGIN IMMEDIATE");
    try {
      exec("INSERT OR IGNORE INTO metadata VALUES('schema','1'); INSERT OR "
           "IGNORE INTO metadata VALUES('revision','0')");
      for (auto r :
           Json::parse(read_file(resources / "data/reasoning_relations.json"))
               .items()) {
        r = relation(r);
        execute("INSERT INTO relations VALUES(?,?) ON CONFLICT(name) DO UPDATE "
                "SET data=excluded.data",
                {r["name"], Json::string(canonical(r))});
      }
      exec("COMMIT");
    } catch (...) {
      exec("ROLLBACK");
      throw;
    }
    if (!rows("SELECT id FROM sources WHERE id='unllm:ontology:v1'").size())
      import_document(
          Json::parse(read_file(resources / "data/reasoning_ontology.json")));
  } catch (...) {
    sqlite3_close(db_);
    db_ = nullptr;
    throw;
  }
}
FactStore::~FactStore() {
  if (db_)
    sqlite3_close(db_);
}
Json FactStore::status() const {
  auto result = rows("SELECT (SELECT COUNT(*) FROM sources) AS sources, "
                     "(SELECT COUNT(*) FROM claims) AS claims, "
                     "(SELECT COUNT(*) FROM entities) AS entities, "
                     "CAST((SELECT value FROM metadata WHERE key='revision') "
                     "AS INTEGER) AS revision").at(0);
  return result;
}
int64_t FactStore::revision() const {
  return std::stoll(rows("SELECT value FROM metadata WHERE key='revision'")
                        .at(0)["value"]
                        .text());
}
Json FactStore::relations() const {
  auto out = Json::array();
  for (auto row : rows("SELECT data FROM relations ORDER BY name").items())
    out.push(Json::parse(row["data"].text()));
  return out;
}
Json FactStore::entities() const {
  auto out = Json::object();
  for (auto row : rows("SELECT * FROM entities").items())
    out.set(row["id"].text(), row);
  return out;
}
Json FactStore::resolve(const std::string &label) const {
  auto direct =
      rows("SELECT * FROM entities WHERE id=?", {Json::string(label)});
  if (direct.size())
    return direct;
  return rows("SELECT e.* FROM entities e JOIN aliases a ON a.entity=e.id "
              "WHERE a.alias=? ORDER BY e.id",
              {Json::string(key(label))});
}
Json FactStore::source(const std::string &id) const {
  auto result = rows("SELECT data FROM sources WHERE id=?", {Json::string(id)});
  if (result.size())
    return Json::parse(result.at(0)["data"].text());
  auto out = Json::object();
  out.set("id", Json::string(id));
  out.set("title", Json::string(id));
  return out;
}
std::vector<Claim> FactStore::claims(int64_t now) const {
  std::vector<Claim> result;
  for (auto row :
       rows("SELECT * FROM claims WHERE (valid_from IS NULL OR valid_from<=?) "
            "AND (valid_to IS NULL OR ?<valid_to) ORDER BY id",
            {Json::number(now), Json::number(now)})
           .items()) {
    row.set("negative", Json::boolean(row["negative"].integer() != 0));
    row.set("qualifiers", Json::parse(row["qualifiers"].text()));
    result.push_back(Claim::from(row));
  }
  return result;
}
Json FactStore::completeness() const {
  return rows("SELECT predicate,subject,source FROM completeness ORDER BY "
              "predicate,subject,source");
}
std::vector<std::string> FactStore::aliases() const {
  std::vector<std::string> out;
  for (auto row : rows("SELECT a.alias FROM aliases a JOIN entities e ON "
                       "e.id=a.entity WHERE e.type!='type'")
                      .items())
    out.push_back(row["alias"].text());
  return out;
}
Json FactStore::prepared(const std::string &id) const {
  auto result =
      rows("SELECT data,checksum FROM prepared WHERE id=?", {Json::string(id)});
  if (!result.size())
    return {};
  try {
    auto value = Json::parse(result.at(0)["data"].text());
    return digest(value) == result.at(0)["checksum"].text() ? value : Json{};
  } catch (const std::exception &) {
    return {};
  }
}
void FactStore::save_prepared(const std::string &id, const Json &snapshot) {
  if (snapshot["status"].text() != "complete")
    throw Unsupported("Cannot publish incomplete materialization");
  double now = std::chrono::duration<double>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
  exec("BEGIN IMMEDIATE");
  try {
    execute("INSERT OR REPLACE INTO prepared VALUES(?,?,?,?)",
            {Json::string(id), Json::string(canonical(snapshot)),
             Json::string(digest(snapshot)), Json::real(now)});
    exec("DELETE FROM prepared WHERE id NOT IN (SELECT id FROM prepared ORDER "
         "BY created DESC LIMIT 3)");
    exec("COMMIT");
  } catch (...) {
    exec("ROLLBACK");
    throw;
  }
}
void FactStore::begin_read() {
  if (!reading_) {
    exec("BEGIN");
    reading_ = true;
  }
}
void FactStore::end_read() {
  if (reading_) {
    exec("COMMIT");
    reading_ = false;
  }
}
Json FactStore::import_document(const Json &doc) {
  if (reading_)
    throw std::runtime_error("Import cannot mutate an active read snapshot");
  if (!doc.is_object())
    throw Unsupported("Import must be an object");
  for (auto name : {"entities", "relations", "claims", "complete"})
    if (doc.contains(name) && !doc[name].is_array())
      throw Unsupported(std::string(name) + " must be an array");
  if (doc.contains("schema") &&
      (!doc["schema"].is_integer() || doc["schema"].integer() != 1))
    throw Unsupported("Unsupported import schema");
  auto src = doc["source"];
  auto sid = required_text(src, "id");
  auto incoming = doc.contains("entities") ? doc["entities"] : Json::array();
  if (!incoming.is_array())
    throw Unsupported("Entities must be an array");
  auto existing = entities();
  std::map<std::string, Json> definitions;
  for (auto r : relations().items())
    definitions[r["name"].text()] = r;
  for (auto raw : doc["relations"].items()) {
    auto r = relation(raw);
    auto name = r["name"].text();
    if (definitions.contains(name) &&
        canonical(r) != canonical(definitions.at(name)))
      throw Unsupported(
          "Changing relation definition requires a schema migration");
    definitions[name] = r;
  }
  for (auto e : incoming.items()) {
    auto id = required_text(e, "id");
    required_text(e, "name");
    auto type = e.contains("type") ? required_text(e, "type") : "entity";
    if (existing[id]["type"].text() == "type" && type != "type")
      throw Unsupported("Entity ID collides with a declared type");
    e = e.clone();
    e.set("type", Json::string(type));
    existing.set(id, e);
  }
  std::vector<Claim> records;
  for (auto e : incoming.items()) {
    auto id = e["id"].text(), type = existing[id]["type"].text();
    auto c = Json::object();
    c.set("id", Json::string("type:" + digest(strings({sid, id, type}))));
    c.set("predicate", Json::string("type"));
    c.set("subject", Json::string(id));
    c.set("object", Json::string(type));
    c.set("source", Json::string(sid));
    c.set("origin", Json::string("metadata"));
    records.push_back(Claim::from(c));
  }
  for (auto raw : doc["claims"].items()) {
    raw = raw.clone();
    raw.set("source", Json::string(sid));
    if (!raw.contains("origin"))
      raw.set("origin", Json::string("source"));
    if (!raw.contains("id")) {
      auto data = Json::array();
      data.push(Json::string(sid));
      data.push(raw.clone());
      raw.set("id", Json::string("claim:" + digest(data)));
    }
    auto c = Claim::from(raw);
    if (!definitions.contains(c.predicate))
      throw Unsupported("Undeclared relation: " + c.predicate);
    if (!existing.contains(c.subject))
      throw Unsupported("Unknown subject: " + c.subject);
    auto range = definitions[c.predicate]["range"].text();
    if (range == "bool" && c.object != "true")
      throw Unsupported("Boolean predicates require object=true");
    if (range == "number" || range == "quantity") {
      size_t end = 0;
      double v;
      try {
        v = std::stod(c.object, &end);
      } catch (...) {
        throw Unsupported("Invalid numeric fact");
      }
      if (end != c.object.size() || !std::isfinite(v) || std::abs(v) > 1e15)
        throw Unsupported("Numeric fact out of domain");
    }
    if (range != "bool" && range != "type" && range != "number" &&
        range != "quantity" && !existing.contains(c.object))
      throw Unsupported("Unknown object: " + c.object);
    records.push_back(c);
  }
  for (auto item : doc["complete"].items())
    if (!definitions.contains(required_text(item, "predicate")) ||
        !existing.contains(required_text(item, "subject")))
      throw Unsupported("Invalid completeness boundary");
  exec("BEGIN IMMEDIATE");
  try {
    execute("INSERT OR REPLACE INTO sources VALUES(?,?)",
            {Json::string(sid), Json::string(canonical(src))});
    for (auto &[name, r] : definitions)
      execute("INSERT OR IGNORE INTO relations VALUES(?,?)",
              {Json::string(name), Json::string(canonical(r))});
    for (auto e : incoming.items()) {
      auto id = e["id"];
      execute("INSERT INTO entities VALUES(?,?,?) ON CONFLICT(id) DO UPDATE "
              "SET name=excluded.name,type=excluded.type",
              {id, e["name"], existing[id.text()]["type"]});
      auto aliases =
          e.contains("aliases") ? e["aliases"].clone() : Json::array();
      if (!aliases.is_array())
        throw Unsupported("Aliases must be an array");
      aliases.push(e["name"]);
      for (auto alias : aliases.items()) {
        if (!alias.is_string())
          throw Unsupported("Alias must be a string");
        execute("INSERT OR IGNORE INTO aliases VALUES(?,?)",
                {Json::string(key(alias.text())), id});
      }
    }
    execute("DELETE FROM claims WHERE source=?", {Json::string(sid)});
    execute("DELETE FROM completeness WHERE source=?", {Json::string(sid)});
    for (auto &c : records) {
      auto v = c.data();
      execute("INSERT INTO claims VALUES(?,?,?,?,?,?,?,?,?,?)",
              {v["id"], v["predicate"], v["subject"], v["object"],
               Json::number(c.negative), v["valid_from"], v["valid_to"],
               v["source"], v["origin"],
               Json::string(canonical(c.qualifiers))});
    }
    for (auto item : doc["complete"].items())
      execute("INSERT INTO completeness VALUES(?,?,?)",
              {item["predicate"], item["subject"], Json::string(sid)});
    exec("UPDATE metadata SET value=CAST(value AS INTEGER)+1 WHERE "
         "key='revision'; COMMIT");
  } catch (...) {
    exec("ROLLBACK");
    throw;
  }
  auto out = Json::object(), snap = Json::array();
  for (auto &c : records)
    snap.push(c.data());
  out.set("source", Json::string(sid));
  out.set("claims", Json::number(records.size()));
  out.set("entities", Json::number(incoming.size()));
  out.set("revision", Json::number(revision()));
  out.set("snapshot", Json::string(digest(snap)));
  return out;
}
} // namespace unllm::native
