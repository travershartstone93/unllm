#include <sqlite3.h>

#include "unllm/unllm.h"

namespace unllm {

namespace {
void exec(sqlite3* db, const char* sql) { sqlite3_exec(db, sql, nullptr, nullptr, nullptr); }
}  // namespace

Index::Index() : Index(":memory:") {}

Index::Index(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        db_ = nullptr;
        return;
    }
    exec(db_, "CREATE VIRTUAL TABLE IF NOT EXISTS docs USING fts5("
              "text, kind UNINDEXED, t UNINDEXED, doc_id UNINDEXED, tokenize='porter unicode61')");
}

Index::~Index() {
    if (db_) sqlite3_close(db_);
}

void Index::add(int64_t id, int64_t t, const std::string& kind, const std::string& text) {
    if (!db_) return;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO docs(text, kind, t, doc_id) VALUES(?,?,?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, t);
    sqlite3_bind_int64(st, 4, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::vector<Hit> Index::search(const std::vector<std::string>& terms,
                               int64_t from, int64_t to, int limit) const {
    std::vector<Hit> out;
    if (!db_ || terms.empty()) return out;
    // Each term is quoted so punctuation in a term cannot become FTS syntax.
    std::string match;
    for (const auto& t : terms) {
        std::string clean;
        for (char c : t)
            if (c != '"') clean += c;
        if (clean.empty()) continue;
        match += (match.empty() ? "" : " OR ") + ("\"" + clean + "\"");
    }
    if (match.empty()) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT doc_id, t, kind, text, bm25(docs) FROM docs "
                           "WHERE docs MATCH ? AND t >= ? AND t < ? ORDER BY bm25(docs) LIMIT ?",
                           -1, &st, nullptr) != SQLITE_OK)
        return out;
    sqlite3_bind_text(st, 1, match.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, from);
    sqlite3_bind_int64(st, 3, to);
    sqlite3_bind_int(st, 4, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Hit h;
        h.id = sqlite3_column_int64(st, 0);
        h.t = sqlite3_column_int64(st, 1);
        h.kind = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        h.text = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        h.score = -sqlite3_column_double(st, 4);  // bm25() is smaller-is-better
        out.push_back(std::move(h));
    }
    sqlite3_finalize(st);
    return out;
}

}  // namespace unllm
