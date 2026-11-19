#include "shard.h"
#include <unicode/locid.h>
#include <unicode/unistr.h>
namespace unllm::maintenance {
using namespace native;
std::string pack_title_key(const std::string &text) {
  auto unicode = icu::UnicodeString::fromUTF8(text);
  unicode.toLower(icu::Locale::getRoot());
  std::string lower;
  unicode.toUTF8String(lower);
  std::string out;
  bool space = false;
  for (unsigned char c : lower) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      if (space && !out.empty())
        out += ' ';
      out += char(c);
      space = false;
    } else
      space = true;
  }
  for (std::string article : {"the ", "a ", "an "})
    if (out.starts_with(article)) {
      out.erase(0, article.size());
      break;
    }
  return out;
}
void Shard::exec(const std::string &sql) {
  if (sqlite3_exec(db.get(), sql.c_str(), nullptr, nullptr, nullptr) !=
      SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db.get()));
}
Shard::Stmt Shard::prepare(const std::string &sql) {
  sqlite3_stmt *s = nullptr;
  if (sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &s, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db.get()));
  return {s, sqlite3_finalize};
}
void Shard::insert(sqlite3_stmt *s, const std::vector<Json> &values) {
  sqlite3_reset(s);
  sqlite3_clear_bindings(s);
  int col = 0;
  for (auto &val : values) {
    ++col;
    int rc;
    if (val.is_integer())
      rc = sqlite3_bind_int64(s, col, val.integer());
    else {
      auto text = val.text();
      rc =
          sqlite3_bind_text(s, col, text.data(), text.size(), SQLITE_TRANSIENT);
    }
    if (rc != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db.get()));
  }
  if (sqlite3_step(s) != SQLITE_DONE)
    throw std::runtime_error(sqlite3_errmsg(db.get()));
  sqlite3_reset(s);
}
Shard::Shard(const Path &path, const Json &fields, bool compact_fts)
    : compact_fts_(compact_fts) {
  sqlite3 *handle = nullptr;
  auto rc = sqlite3_open_v2(path.c_str(), &handle,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                SQLITE_OPEN_NOMUTEX,
                            nullptr);
  db.reset(handle);
  if (rc != SQLITE_OK)
    throw Unsupported("Cannot create SQLite shard");
  exec(
      R"(PRAGMA application_id=1431194704; PRAGMA user_version=1; PRAGMA cache_size=-65536;
 BEGIN;
 CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
 CREATE TABLE docs(doc_id INTEGER PRIMARY KEY, title TEXT NOT NULL, title_key TEXT NOT NULL,url TEXT,author TEXT,license TEXT,tags TEXT,score INTEGER,extra TEXT);
 CREATE TABLE passages(pid INTEGER PRIMARY KEY,doc_id INTEGER NOT NULL,ord INTEGER NOT NULL,section TEXT,role TEXT NOT NULL,text TEXT NOT NULL);
 CREATE TABLE infobox(doc_id INTEGER NOT NULL,box TEXT,key TEXT NOT NULL,value TEXT NOT NULL);)");
  doc = prepare("INSERT INTO docs VALUES(?,?,?,?,?,?,?,?,?)");
  box = prepare("INSERT INTO infobox VALUES(?,?,?,?)");
  pass = prepare("INSERT INTO passages VALUES(?,?,?,?,?,?)");
  meta = prepare("INSERT OR REPLACE INTO meta VALUES(?,?)");
  for (auto &[name, value] : fields.members())
    insert(meta.get(), {Json::string(name), value});
}
int64_t Shard::document(const std::string &t, const std::string &url,
                        const std::string &license, const std::string &tags,
                        const std::string &extra, const std::string &author,
                        int score) {
  insert(doc.get(),
         {Json::number(++docs), Json::string(t),
          Json::string(pack_title_key(t)), Json::string(url),
          Json::string(author), Json::string(license), Json::string(tags),
          Json::number(score), Json::string(extra)});
  return docs;
}
void Shard::passage(int64_t d, int64_t ord, const std::string &role,
                    const std::string &text, const std::string &section) {
  if (text.empty())
    return;
  insert(pass.get(),
         {Json::number(++passages), Json::number(d), Json::number(ord),
          Json::string(section), Json::string(role), Json::string(text)});
  if (passages % 20000 == 0)
    exec("COMMIT; BEGIN;");
}
void Shard::fact(int64_t doc, const std::string &group, const std::string &key,
                 const std::string &value) {
  insert(box.get(), {Json::number(doc), Json::string(group), Json::string(key),
                     Json::string(value)});
}
void Shard::finish() {
  exec(std::string(R"(CREATE INDEX docs_title_key ON docs(title_key);
 CREATE INDEX passages_doc ON passages(doc_id,ord);
 CREATE INDEX infobox_doc ON infobox(doc_id);
 CREATE VIRTUAL TABLE passages_fts USING fts5(text, content='passages', content_rowid='pid',tokenize='porter unicode61 remove_diacritics 2',detail=')") +
       (compact_fts_ ? "column" : "full") + R"(');
 CREATE VIRTUAL TABLE titles_fts USING fts5(title,content='docs',content_rowid='doc_id',tokenize='porter unicode61 remove_diacritics 2');
 INSERT INTO passages_fts(passages_fts) VALUES('rebuild');INSERT INTO titles_fts(titles_fts) VALUES('rebuild');
 INSERT INTO passages_fts(passages_fts) VALUES('optimize');
 CREATE VIRTUAL TABLE fts_vocab USING fts5vocab(passages_fts,'row');
 CREATE TABLE vocab(term TEXT PRIMARY KEY,df INTEGER) WITHOUT ROWID;
 INSERT INTO vocab SELECT term,doc FROM fts_vocab;DROP TABLE fts_vocab;)");
  insert(meta.get(),
         {Json::string("docs"), Json::string(std::to_string(docs))});
  insert(meta.get(),
         {Json::string("passages"), Json::string(std::to_string(passages))});
  exec("COMMIT;VACUUM;");
}
} // namespace unllm::maintenance
