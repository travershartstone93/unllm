#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <dirent.h>
#include <future>
#include <map>

#include "unllm/knowledge.h"

namespace unllm {

namespace {

std::string meta_value(sqlite3* db, const char* key) {
    sqlite3_stmt* st = nullptr;
    std::string out;
    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key = ?", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0))
            out = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n != "." && n != "..") out.push_back(n);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string col(sqlite3_stmt* st, int i) {
    const unsigned char* t = sqlite3_column_text(st, i);
    return t ? reinterpret_cast<const char*>(t) : "";
}

// FTS5 query syntax from plain words: each word quoted, so punctuation and
// keywords like NOT never reach the parser.
std::string fts_terms(const std::vector<std::string>& words, const char* joiner) {
    std::string q;
    for (const auto& w : words) {
        std::string clean;
        for (char c : w)
            if (std::isalnum(static_cast<unsigned char>(c))) clean += c;
        if (clean.size() < 2) continue;
        q += (q.empty() ? "" : joiner) + std::string("\"") + clean + "\"";
    }
    return q;
}

const char* kPassageSelect =
    "SELECT p.pid, p.doc_id, p.ord, p.section, p.role, p.text, d.title, d.url, d.author, d.license";

void fill(Passage& p, sqlite3_stmt* st) {
    p.pid = sqlite3_column_int64(st, 0);
    p.doc_id = sqlite3_column_int64(st, 1);
    p.ord = sqlite3_column_int(st, 2);
    p.section = col(st, 3);
    p.role = col(st, 4);
    p.text = col(st, 5);
    p.title = col(st, 6);
    p.url = col(st, 7);
    p.author = col(st, 8);
    p.license = col(st, 9);
}

}  // namespace

std::string title_key(const std::string& title) {
    std::string t;
    bool space = false;
    for (char c : title) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) {
            if (space && !t.empty()) t += ' ';
            space = false;
            t += char(std::tolower(u));
        } else {
            space = true;
        }
    }
    for (const char* a : {"the ", "a ", "an "}) {
        std::string art = a;
        if (t.compare(0, art.size(), art) == 0) {
            t = t.substr(art.size());
            break;
        }
    }
    return t;
}

#ifdef UNLLM_HAVE_TANTIVY
// The Rust helper (search/src/lib.rs).
extern "C" {
void* us_open(const char* path);
int us_search(const void* s, const char* words, const char* role, uint32_t k, uint32_t* shards, int64_t* pids,
              float* scores);
void us_close(void* s);
// Interface version 2 (search/src/lib.rs us_version).
uint32_t us_version();
uint32_t us_schema(const void* s);
void us_set_boosts(void* s, float title, float section);
int us_search2(const void* s, const char* words, const char* role, const uint32_t* doc_shards,
               const int64_t* doc_ids, uint32_t n_docs, uint32_t k, uint32_t* shards, int64_t* pids,
               int64_t* out_doc_ids, float* scores);
}
#endif

Library::~Library() {
    for (auto& s : shards_)
        if (s.db) sqlite3_close(s.db);
#ifdef UNLLM_HAVE_TANTIVY
    for (void* e : engines_)
        if (e) us_close(e);
#endif
}

bool Library::has_ranked() const {
    for (void* e : engines_)
        if (e) return true;
    return false;
}

#ifdef UNLLM_HAVE_TANTIVY
// Turns one pack's hits into passages: each pid is read from its shard,
// with one prepared statement per shard for the whole batch (k can be 200).
#endif

void Library::read_hits(std::size_t pi, int n, const uint32_t* shard_nums, const int64_t* pids,
                        const float* scores, std::vector<Passage>& out) const {
    std::map<int, sqlite3_stmt*> stmts;
    const std::string sql =
        std::string(kPassageSelect) + " FROM passages p JOIN docs d ON d.doc_id = p.doc_id WHERE p.pid = ?";
    for (int h = 0; h < n; ++h) {
        int si = -1;
        for (std::size_t s = 0; s < shards_.size(); ++s)
            if (shards_[s].pack == pi && shards_[s].number == int(shard_nums[h])) si = int(s);
        if (si < 0) continue;
        sqlite3_stmt*& st = stmts[si];
        if (!st && sqlite3_prepare_v2(shards_[std::size_t(si)].db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
            st = nullptr;
            continue;
        }
        sqlite3_reset(st);
        sqlite3_bind_int64(st, 1, pids[h]);
        if (sqlite3_step(st) == SQLITE_ROW) {
            Passage p;
            fill(p, st);
            p.pack = packs_[pi].name;
            p.kind = packs_[pi].kind;
            p.attribution = packs_[pi].attribution;
            p.shard = si;
            p.bm25 = scores[h];
            out.push_back(std::move(p));
        }
    }
    for (auto& [si, st] : stmts)
        if (st) sqlite3_finalize(st);
}


int Library::doc_size(int shard, int64_t doc_id) const {
    if (shard < 0 || shard >= int(shards_.size())) return 0;
    sqlite3_stmt* st = nullptr;
    int n = 0;
    if (sqlite3_prepare_v2(shards_[std::size_t(shard)].db, "SELECT COUNT(*) FROM passages WHERE doc_id = ?", -1, &st,
                           nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, doc_id);
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return n;
}

std::vector<Passage> Library::by_place(const std::vector<Place>& places) const {
    std::vector<Passage> out;
    for (const auto& pl : places) {
        for (std::size_t pi = 0; pi < packs_.size(); ++pi) {
            if (packs_[pi].name != pl.pack) continue;
            const uint32_t no = uint32_t(pl.shard_no);
            read_hits(pi, 1, &no, &pl.pid, &pl.score, out);
            break;
        }
    }
    return out;
}

std::vector<Passage> Library::ranked(const std::string& words, const std::set<std::string>& kinds, int k,
                                     const std::string& role) const {
    std::vector<Passage> out;
#ifdef UNLLM_HAVE_TANTIVY
    if (k <= 0) return out;
    std::vector<uint32_t> shard_nums(static_cast<std::size_t>(k));
    std::vector<int64_t> pids(static_cast<std::size_t>(k));
    std::vector<float> scores(static_cast<std::size_t>(k));
    for (std::size_t pi = 0; pi < packs_.size() && pi < engines_.size(); ++pi) {
        if (!engines_[pi] || (!kinds.empty() && !kinds.count(packs_[pi].kind))) continue;
        const int n = us_search(engines_[pi], words.c_str(), role.empty() ? nullptr : role.c_str(), uint32_t(k),
                                shard_nums.data(), pids.data(), scores.data());
        read_hits(pi, n, shard_nums.data(), pids.data(), scores.data(), out);
    }
    std::stable_sort(out.begin(), out.end(), [](const Passage& a, const Passage& b) { return a.bm25 > b.bm25; });
    if (int(out.size()) > k) out.resize(std::size_t(k));
#else
    (void)words;
    (void)kinds;
    (void)role;
#endif
    return out;
}

std::vector<Passage> Library::ranked_in(const std::string& words, const std::vector<DocRef>& docs, int k,
                                        const std::string& role) const {
    std::vector<Passage> out;
#ifdef UNLLM_HAVE_TANTIVY
    if (k <= 0 || docs.empty()) return out;
    // The articles, per pack, as (shard number, doc_id).
    std::map<std::size_t, std::pair<std::vector<uint32_t>, std::vector<int64_t>>> by_pack;
    for (const auto& d : docs) {
        if (d.shard < 0 || std::size_t(d.shard) >= shards_.size()) continue;
        const Shard& sh = shards_[std::size_t(d.shard)];
        auto& [nums, ids] = by_pack[sh.pack];
        nums.push_back(uint32_t(sh.number));
        ids.push_back(d.doc_id);
    }
    std::vector<uint32_t> shard_nums(static_cast<std::size_t>(k));
    std::vector<int64_t> pids(static_cast<std::size_t>(k));
    std::vector<float> scores(static_cast<std::size_t>(k));
    for (const auto& [pi, want] : by_pack) {
        if (pi >= engines_.size() || !engines_[pi]) continue;
        const char* r = role.empty() ? nullptr : role.c_str();
        int n = us_search2(engines_[pi], words.c_str(), r, want.first.data(), want.second.data(),
                           uint32_t(want.first.size()), uint32_t(k), shard_nums.data(), pids.data(), nullptr,
                           scores.data());
        if (n >= 0) {
            read_hits(pi, n, shard_nums.data(), pids.data(), scores.data(), out);
            continue;
        }
        // An index built before schema 2 cannot filter: take a wide plain
        // search and keep the passages of the wanted articles.
        const uint32_t wide = 200;
        std::vector<uint32_t> wn(wide);
        std::vector<int64_t> wp(wide);
        std::vector<float> ws(wide);
        n = us_search(engines_[pi], words.c_str(), r, wide, wn.data(), wp.data(), ws.data());
        std::vector<Passage> got;
        read_hits(pi, n, wn.data(), wp.data(), ws.data(), got);
        for (auto& p : got)
            for (std::size_t i = 0; i < want.first.size(); ++i)
                if (shards_[std::size_t(p.shard)].number == int(want.first[i]) && p.doc_id == want.second[i]) {
                    out.push_back(std::move(p));
                    break;
                }
    }
    std::stable_sort(out.begin(), out.end(), [](const Passage& a, const Passage& b) { return a.bm25 > b.bm25; });
    if (int(out.size()) > k) out.resize(std::size_t(k));
#else
    (void)words;
    (void)docs;
    (void)role;
#endif
    return out;
}

void Library::set_ranked_boosts(float title, float section) {
#ifdef UNLLM_HAVE_TANTIVY
    for (void* e : engines_)
        if (e) us_set_boosts(e, title, section);
#else
    (void)title;
    (void)section;
#endif
}

int Library::ranked_schema() const {
    int lowest = 0;
#ifdef UNLLM_HAVE_TANTIVY
    for (void* e : engines_)
        if (e) {
            const int v = int(us_schema(e));
            if (lowest == 0 || v < lowest) lowest = v;
        }
#endif
    return lowest;
}

bool Library::open(const std::string& packs_dir) {
    for (const auto& name : list_dir(packs_dir)) {
        if (name.find('.') != std::string::npos) continue;  // .building, .previous
        const std::string dir = packs_dir + "/" + name;
        bool first = true;
        for (const auto& f : list_dir(dir)) {
            // Exactly shard-NNN.sqlite: a stray file must never pose as a shard.
            if (f.size() != 16 || f.rfind("shard-", 0) != 0 || f.substr(9) != ".sqlite" ||
                !std::all_of(f.begin() + 6, f.begin() + 9, [](char c) { return c >= '0' && c <= '9'; }))
                continue;
            sqlite3* db = nullptr;
            const std::string uri = "file:" + dir + "/" + f + "?mode=ro";
            if (sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI | SQLITE_OPEN_NOMUTEX,
                                nullptr) != SQLITE_OK) {
                if (db) sqlite3_close(db);
                continue;
            }
            sqlite3_exec(db, "PRAGMA query_only=1; PRAGMA mmap_size=268435456; PRAGMA cache_size=-4000;", nullptr,
                         nullptr, nullptr);
            if (first) {
                PackInfo info;
                info.dir = dir;
                info.name = meta_value(db, "name");
                if (info.name.empty()) info.name = name;
                info.kind = meta_value(db, "kind");
                info.source = meta_value(db, "source");
                info.version = meta_value(db, "version");
                info.license = meta_value(db, "license");
                info.attribution = meta_value(db, "attribution");
                packs_.push_back(info);
                first = false;
            }
            passages_ += std::atoll(meta_value(db, "passages").c_str());
            shards_.push_back({db, packs_.size() - 1, std::atoi(f.substr(6).c_str())});
        }
        if (!first) {
#ifdef UNLLM_HAVE_TANTIVY
            engines_.push_back(us_open((dir + "/tantivy").c_str()));
#else
            engines_.push_back(nullptr);
#endif
        }
    }
    return !shards_.empty();
}

std::vector<Passage> Library::by_title(const std::string& subject, const std::set<std::string>& kinds, int k) const {
    std::vector<Passage> out;
    const std::string key = title_key(subject);
    if (key.empty()) return out;
    for (std::size_t si = 0; si < shards_.size(); ++si) {
        const auto& sh = shards_[si];
        const PackInfo& pk = packs_[sh.pack];
        if (!kinds.empty() && !kinds.count(pk.kind)) continue;
        sqlite3_stmt* st = nullptr;
        std::string sql = std::string(kPassageSelect) +
                          " FROM docs d JOIN passages p ON p.doc_id = d.doc_id WHERE d.title_key = ? "
                          "ORDER BY d.doc_id, p.ord LIMIT ?";
        if (sqlite3_prepare_v2(sh.db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) continue;
        sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 2, k);
        while (sqlite3_step(st) == SQLITE_ROW) {
            Passage p;
            fill(p, st);
            p.pack = pk.name;
            p.kind = pk.kind;
            p.attribution = pk.attribution;
            p.shard = int(si);
            p.title_score = 1.0;
            out.push_back(std::move(p));
        }
        sqlite3_finalize(st);
    }
    return out;
}

std::vector<Passage> Library::neighbors(const Passage& p, int count) const {
    std::vector<Passage> out;
    if (p.shard < 0 || p.shard >= int(shards_.size())) return out;
    const auto& sh = shards_[p.shard];
    sqlite3_stmt* st = nullptr;
    std::string sql = std::string(kPassageSelect) +
                      " FROM passages p JOIN docs d ON d.doc_id = p.doc_id WHERE p.doc_id = ? AND p.ord > ? "
                      "ORDER BY p.ord LIMIT ?";
    if (sqlite3_prepare_v2(sh.db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int64(st, 1, p.doc_id);
    sqlite3_bind_int(st, 2, p.ord);
    sqlite3_bind_int(st, 3, count);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Passage n;
        fill(n, st);
        n.pack = p.pack;
        n.kind = p.kind;
        n.attribution = p.attribution;
        n.shard = p.shard;
        out.push_back(std::move(n));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<DocRef> Library::titled(const std::string& name, const std::set<std::string>& kinds, int k) const {
    std::vector<DocRef> out;
    const std::string key = title_key(name);
    if (key.empty()) return out;
    std::string match;
    std::size_t at = 0;
    while (at < key.size()) {
        std::size_t sp = key.find(' ', at);
        if (sp == std::string::npos) sp = key.size();
        match += (match.empty() ? "\"" : " AND \"") + key.substr(at, sp - at) + "\"";
        at = sp + 1;
    }
    // The exact title, plus the first few hundred titles holding every
    // word, each with its size.
    const char* sql =
        "SELECT d.doc_id, d.title, d.title_key, (SELECT COUNT(*) FROM passages p WHERE p.doc_id = d.doc_id) "
        "FROM docs d WHERE d.title_key = ?1 OR d.doc_id IN "
        "(SELECT rowid FROM titles_fts WHERE titles_fts MATCH ?2 LIMIT 300)";
    for (std::size_t si = 0; si < shards_.size(); ++si) {
        const auto& sh = shards_[si];
        if (!kinds.empty() && !kinds.count(packs_[sh.pack].kind)) continue;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(sh.db, sql, -1, &st, nullptr) != SQLITE_OK) continue;
        sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, match.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            DocRef d;
            d.shard = int(si);
            d.doc_id = sqlite3_column_int64(st, 0);
            d.title = col(st, 1);
            d.exact = col(st, 2) == key;
            d.passages = sqlite3_column_int(st, 3);
            out.push_back(std::move(d));
        }
        sqlite3_finalize(st);
    }
    std::stable_sort(out.begin(), out.end(), [](const DocRef& a, const DocRef& b) {
        if (a.exact != b.exact) return a.exact;
        return a.passages > b.passages;
    });
    if (int(out.size()) > k) out.resize(std::size_t(k));
    return out;
}

std::vector<std::pair<std::string, std::string>> Library::fact_box(int shard, int64_t doc_id) const {
    std::vector<std::pair<std::string, std::string>> out;
    if (shard < 0 || shard >= int(shards_.size())) return out;
    sqlite3_stmt* st = nullptr;
    // Packs built before fact boxes were kept have no such table.
    if (sqlite3_prepare_v2(shards_[shard].db, "SELECT key, value FROM infobox WHERE doc_id = ? ORDER BY rowid", -1,
                           &st, nullptr) != SQLITE_OK)
        return out;
    sqlite3_bind_int64(st, 1, doc_id);
    static const std::string bullet = "\xe2\x80\xa2";  // "•"
    std::string group;
    while (sqlite3_step(st) == SQLITE_ROW) {
        std::string key = col(st, 0), value = col(st, 1);
        if (key.rfind(bullet, 0) == 0) {
            std::size_t a = key.find_first_not_of(' ', bullet.size());
            std::string sub = a == std::string::npos ? "" : key.substr(a);
            key = group.empty() ? sub : group + " " + sub;
        } else {
            group = key;
        }
        out.emplace_back(std::move(key), std::move(value));
    }
    sqlite3_finalize(st);
    return out;
}

namespace {

// SQLite calls this every few thousand steps; a non-zero return stops the
// statement (it then reports SQLITE_INTERRUPT), so one huge match list
// cannot hold an answer up past its deadline.
int past_deadline(void* when) {
    return std::chrono::steady_clock::now() > *static_cast<const std::chrono::steady_clock::time_point*>(when) ? 1 : 0;
}

}  // namespace

std::vector<std::pair<Passage, int>> Library::leads_with(const std::vector<std::string>& phrases,
                                                         const std::vector<std::vector<std::string>>& need,
                                                         const std::set<std::string>& kinds, int per_shard,
                                                         bool leads_only, int deadline_ms) const {
    using clock = std::chrono::steady_clock;
    const clock::time_point deadline = clock::now() + std::chrono::milliseconds(deadline_ms);
    // Large packs index words without their positions, so the search asks
    // for all the words and the phrase itself is checked in the text.
    const std::string sql = std::string(kPassageSelect) +
                            " FROM passages_fts f JOIN passages p ON p.pid = f.rowid JOIN docs d ON d.doc_id = p.doc_id"
                            " WHERE passages_fts MATCH ?" + (leads_only ? std::string(" AND p.role = 'lead'") : "") +
                            " ORDER BY f.rank LIMIT ?";
    std::string required;
    for (const auto& group : need) {
        std::string any;
        for (const auto& w : group)
            if (!w.empty() && w.find('"') == std::string::npos) any += (any.empty() ? "(\"" : " OR \"") + w + "\"";
        if (!any.empty()) required += " AND " + any + ")";
    }
    auto search_shard = [&](std::size_t si, std::vector<std::pair<Passage, int>>& found) {
        const auto& sh = shards_[si];
        const PackInfo& pk = packs_[sh.pack];
        if (!kinds.empty() && !kinds.count(pk.kind)) return;
        std::set<int64_t> seen;
        sqlite3_progress_handler(sh.db, 2000, past_deadline, const_cast<clock::time_point*>(&deadline));
        for (const auto& phrase : phrases) {
            if (clock::now() > deadline) break;
            std::string match;
            for (std::size_t a = 0; a < phrase.size();) {
                std::size_t sp = phrase.find(' ', a);
                if (sp == std::string::npos) sp = phrase.size();
                if (sp > a) match += (match.empty() ? "\"" : " AND \"") + phrase.substr(a, sp - a) + "\"";
                a = sp + 1;
            }
            if (match.empty()) continue;
            match += required;
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(sh.db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) continue;
            sqlite3_bind_text(st, 1, match.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, per_shard);
            while (sqlite3_step(st) == SQLITE_ROW) {
                Passage p;
                fill(p, st);
                std::string low = p.text;
                for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
                bool holds = false;
                for (const auto& ph : phrases) holds |= low.find(ph) != std::string::npos;
                if (!holds || !seen.insert(p.doc_id).second) continue;
                p.pack = pk.name;
                p.kind = pk.kind;
                p.attribution = pk.attribution;
                p.shard = int(si);
                int size = 0;
                sqlite3_stmt* cnt = nullptr;
                if (sqlite3_prepare_v2(sh.db, "SELECT COUNT(*) FROM passages WHERE doc_id = ?", -1, &cnt, nullptr) ==
                    SQLITE_OK) {
                    sqlite3_bind_int64(cnt, 1, p.doc_id);
                    if (sqlite3_step(cnt) == SQLITE_ROW) size = sqlite3_column_int(cnt, 0);
                    sqlite3_finalize(cnt);
                }
                found.emplace_back(std::move(p), size);
            }
            sqlite3_finalize(st);
        }
        sqlite3_progress_handler(sh.db, 0, nullptr, nullptr);
    };
    const std::size_t threads = std::min<std::size_t>(4, std::max<std::size_t>(1, shards_.size()));
    std::vector<std::future<std::vector<std::pair<Passage, int>>>> fs;
    for (std::size_t t = 0; t < threads; ++t)
        fs.push_back(std::async(std::launch::async, [&, t] {
            std::vector<std::pair<Passage, int>> found;
            for (std::size_t si = t; si < shards_.size(); si += threads) search_shard(si, found);
            return found;
        }));
    std::vector<std::pair<Passage, int>> out;
    for (auto& f : fs) {
        auto part = f.get();
        out.insert(out.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
    }
    return out;
}

Passage Library::lead_of(int shard, int64_t doc_id) const {
    if (shard < 0 || shard >= int(shards_.size())) return {};
    const PackInfo& pk = packs_[shards_[shard].pack];
    Passage start;
    start.shard = shard;
    start.doc_id = doc_id;
    start.ord = -1;
    start.pack = pk.name;
    start.kind = pk.kind;
    start.attribution = pk.attribution;
    auto first = neighbors(start, 1);
    return first.empty() ? Passage{} : first[0];
}

std::vector<Passage> Library::candidates(const Query& q) const {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(q.deadline_ms);
    std::vector<Passage> out = by_title(q.subject, q.kinds, 3);
    const std::string all = fts_terms(q.words, " AND ");
    const std::string any = fts_terms(q.words, " OR ");
    if (all.empty()) return out;

    // One shard's best passages for a MATCH expression.
    auto search = [&](std::size_t si, const std::string& match, int limit) {
        std::vector<Passage> got;
        const auto& sh = shards_[si];
        const PackInfo& pk = packs_[sh.pack];
        if (!q.kinds.empty() && !q.kinds.count(pk.kind)) return got;
        if (clock::now() > deadline) return got;
        sqlite3_stmt* st = nullptr;
        std::string sql = std::string(kPassageSelect) +
                          ", bm25(passages_fts) FROM passages_fts JOIN passages p ON p.pid = passages_fts.rowid "
                          "JOIN docs d ON d.doc_id = p.doc_id WHERE passages_fts MATCH ? "
                          "ORDER BY bm25(passages_fts) LIMIT ?";
        if (sqlite3_prepare_v2(sh.db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return got;
        sqlite3_bind_text(st, 1, match.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 2, limit);
        while (sqlite3_step(st) == SQLITE_ROW) {
            Passage p;
            fill(p, st);
            p.bm25 = -sqlite3_column_double(st, 10);
            p.pack = pk.name;
            p.kind = pk.kind;
            p.attribution = pk.attribution;
            p.shard = int(si);
            got.push_back(std::move(p));
            if (clock::now() > deadline) break;
        }
        sqlite3_finalize(st);
        return got;
    };
    // Shards in parallel, a handful of threads, each taking every Nth shard.
    auto run = [&](const std::string& match, int limit) {
        const std::size_t threads = std::min<std::size_t>(4, std::max<std::size_t>(1, shards_.size()));
        std::vector<std::future<std::vector<Passage>>> fs;
        for (std::size_t t = 0; t < threads; ++t)
            fs.push_back(std::async(std::launch::async, [&, t] {
                std::vector<Passage> mine;
                for (std::size_t si = t; si < shards_.size(); si += threads) {
                    auto got = search(si, match, limit);
                    mine.insert(mine.end(), std::make_move_iterator(got.begin()), std::make_move_iterator(got.end()));
                }
                return mine;
            }));
        std::vector<Passage> merged;
        for (auto& f : fs) {
            auto got = f.get();
            merged.insert(merged.end(), std::make_move_iterator(got.begin()), std::make_move_iterator(got.end()));
        }
        return merged;
    };
    auto strict = run(all, q.per_shard);
    out.insert(out.end(), strict.begin(), strict.end());
    if (int(strict.size()) < 20 && q.words.size() > 1) {
        auto loose = run(any, std::max(5, q.per_shard / 2));
        out.insert(out.end(), loose.begin(), loose.end());
    }
    // One entry per passage, best bm25 first, capped.
    std::map<std::pair<int, int64_t>, std::size_t> seen;
    std::vector<Passage> unique;
    for (auto& p : out) {
        auto key = std::make_pair(p.shard, p.pid);
        auto it = seen.find(key);
        if (it == seen.end()) {
            seen[key] = unique.size();
            unique.push_back(std::move(p));
        } else if (p.title_score > unique[it->second].title_score) {
            unique[it->second].title_score = p.title_score;
        }
    }
    std::stable_sort(unique.begin(), unique.end(), [](const Passage& a, const Passage& b) {
        if (a.title_score != b.title_score) return a.title_score > b.title_score;
        return a.bm25 > b.bm25;
    });
    if (int(unique.size()) > q.total) unique.resize(std::size_t(q.total));
    return unique;
}

}  // namespace unllm
