#pragma once
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <vector>
namespace bricks::sql {
using Strings = std::vector<std::string>;
using Db = std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)>;
inline Db open(const char *path, int flags = SQLITE_OPEN_READONLY) {
  sqlite3 *raw = nullptr;
  int rc = sqlite3_open_v2(path, &raw, flags, nullptr);
  Db db(raw, sqlite3_close_v2);
  if (rc != SQLITE_OK)
    throw std::runtime_error("SQLite open failed");
  return db;
}
using Stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
inline Stmt prepare(sqlite3 *db, const char *sql) {
  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));
  return Stmt(raw, sqlite3_finalize);
}
inline void bind(sqlite3_stmt *s, const std::string &v) {
  if (sqlite3_bind_text64(s, 1, v.data(), v.size(), SQLITE_TRANSIENT,
                          SQLITE_UTF8) != SQLITE_OK)
    throw std::runtime_error("SQLite bind failed");
}
inline Strings query(const char *path, const char *statement,
                     const std::string *value = nullptr) {
  auto db = open(path);
  auto s = prepare(db.get(), statement);
  if (value)
    bind(s.get(), *value);
  Strings out;
  int rc;
  while ((rc = sqlite3_step(s.get())) == SQLITE_ROW) {
    if (sqlite3_column_type(s.get(), 0) != SQLITE_TEXT)
      throw std::runtime_error("Expected text");
    auto text = sqlite3_column_text(s.get(), 0);
    if (!text)
      throw std::bad_alloc();
    out.emplace_back(reinterpret_cast<const char *>(text),
                     sqlite3_column_bytes(s.get(), 0));
  }
  if (rc != SQLITE_DONE)
    throw std::runtime_error(sqlite3_errmsg(db.get()));
  return out;
}
inline Strings tables(const char *p) {
  return query(p, "SELECT name FROM sqlite_schema WHERE type='table' AND name "
                  "NOT GLOB 'sqlite_*' ORDER BY name");
}
inline Strings views(const char *p) {
  return query(
      p, "SELECT name FROM sqlite_schema WHERE type='view' ORDER BY name");
}
inline Strings columns(const char *p, const std::string &table) {
  return query(p, "SELECT name FROM pragma_table_info(?) ORDER BY cid", &table);
}
inline Strings primary_keys(const char *p, const std::string &table) {
  return query(p,
               "SELECT name FROM pragma_table_info(?) WHERE pk>0 ORDER BY pk",
               &table);
}
inline Strings foreign_tables(const char *p, const std::string &table) {
  return query(
      p, "SELECT DISTINCT \"table\" FROM pragma_foreign_key_list(?) ORDER BY 1",
      &table);
}
inline Strings integrity(const char *p) {
  return query(p, "PRAGMA integrity_check");
}
inline std::string identifier(const std::string &name) {
  if (name.find('\0') != name.npos)
    throw std::invalid_argument("Invalid identifier");
  std::string out = "\"";
  for (char c : name) {
    out += c;
    if (c == '"')
      out += c;
  }
  return out + '"';
}
inline std::vector<std::int64_t> count(const char *p,
                                       const std::string &table) {
  auto db = open(p);
  auto s =
      prepare(db.get(), ("SELECT COUNT(*) FROM " + identifier(table)).c_str());
  if (sqlite3_step(s.get()) != SQLITE_ROW)
    throw std::runtime_error(sqlite3_errmsg(db.get()));
  return {sqlite3_column_int64(s.get(), 0)};
}
inline Strings values(const char *p, const std::string &table,
                      const std::string &column) {
  auto statement = "SELECT " + identifier(table) + "." + identifier(column) +
                   " FROM " + identifier(table) + " ORDER BY rowid";
  return query(p, statement.c_str());
}
template <class F> void publish(const char *out, F fill) {
  QTemporaryFile tmp(
      QFileInfo(QString::fromUtf8(out)).dir().filePath(".unllm-XXXXXX"));
  if (!tmp.open())
    throw std::runtime_error("Cannot create database");
  tmp.close();
  {
    auto db = open(tmp.fileName().toUtf8().constData(), SQLITE_OPEN_READWRITE);
    fill(db.get());
  }
  if (!tmp.rename(QString::fromUtf8(out)))
    throw std::runtime_error("Cannot publish fresh database");
  tmp.setAutoRemove(false);
}
inline void snapshot(const char *input, const char *out) {
  auto source = open(input);
  publish(out, [&](sqlite3 *destination) {
    auto backup =
        sqlite3_backup_init(destination, "main", source.get(), "main");
    if (!backup)
      throw std::runtime_error("Backup initialization failed");
    auto rc = sqlite3_backup_step(backup, -1);
    auto done = sqlite3_backup_finish(backup);
    if (rc != SQLITE_DONE || done != SQLITE_OK)
      throw std::runtime_error("Backup failed");
  });
}
inline void store_lines(const Strings &lines, const char *out) {
  publish(out, [&](sqlite3 *db) {
    if (sqlite3_exec(db, "BEGIN; CREATE TABLE lines(value TEXT NOT NULL)",
                     nullptr, nullptr, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
    auto s = prepare(db, "INSERT INTO lines VALUES (?)");
    for (auto &line : lines) {
      bind(s.get(), line);
      if (sqlite3_step(s.get()) != SQLITE_DONE ||
          sqlite3_reset(s.get()) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
  });
}
} // namespace bricks::sql
