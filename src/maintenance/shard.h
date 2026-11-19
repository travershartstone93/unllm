#pragma once
#include "unllm/maintenance/ingest.h"
#include <memory>
#include <sqlite3.h>
namespace unllm::maintenance {
class Shard {
  using Stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db{nullptr, sqlite3_close};
  Stmt doc{nullptr, sqlite3_finalize}, pass{nullptr, sqlite3_finalize},
      box{nullptr, sqlite3_finalize}, meta{nullptr, sqlite3_finalize};
  bool compact_fts_;
  void exec(const std::string &sql);
  Stmt prepare(const std::string &sql);
  void insert(sqlite3_stmt *, const std::vector<Json> &values);

public:
  int64_t docs = 0, passages = 0;
  explicit Shard(const Path &file, const Json &metadata,
                 bool compact_fts = false);
  int64_t document(const std::string &title, const std::string &url,
                   const std::string &license, const std::string &tags = {},
                   const std::string &extra = {},
                   const std::string &author = {}, int score = 0);
  void passage(int64_t id, int64_t order, const std::string &role,
               const std::string &text, const std::string &section = {});
  void fact(int64_t doc, const std::string &box, const std::string &key,
            const std::string &value);
  void finish();
};
std::string pack_title_key(const std::string &text);
bool finewiki(const Path &);
std::vector<std::string> split_passage(const std::string &);
void ingest_wiki(const Path &, Shard &);
void ingest_stackexchange(const Path &, Shard &);
void ingest_poetry(const Path &, Shard &);
void ingest_documents(const Path &, Shard &);
void ingest_wiktionary(const Path &, Shard &);
void ingest_wordnet(const Path &, Shard &);
} // namespace unllm::maintenance
