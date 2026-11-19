#include "unllm/json.h"
#include "unllm/ranked_index.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/fs.h>
#include <map>
#include <queue>
#include <set>
#include <sqlite3.h>
#include <sstream>
#include <sys/syscall.h>
#include <unicode/locid.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unistd.h>
#include <vector>
#include <xapian.h>

namespace {
namespace fs = std::filesystem;
const std::set<std::string> stop = [] {
  std::istringstream in(
      "a about after all also am an and ani are as at be becaus been befor "
      "between but by can could did do doe dure each for from had has have he "
      "her him his how i if in into is it me more most my no not of on or "
      "other our s she should so some such than that the their them then there "
      "these they this those to under up us veri was we were what when where "
      "whi which while who whom whose will with would you your");
  std::set<std::string> out;
  for (std::string term; in >> term;)
    out.insert(term);
  return out;
}();
std::vector<std::string> tokens(std::string_view raw) {
  Xapian::Stem stem("english");
  auto text = icu::UnicodeString::fromUTF8(raw);
  std::vector<std::string> out;
  for (int32_t i = 0; i < text.length();) {
    auto start = i;
    while (i < text.length() && u_isalnum(text.char32At(i)))
      i += U16_LENGTH(text.char32At(i));
    if (i > start) {
      std::string token;
      text.tempSubStringBetween(start, i).toUTF8String(token);
      if (token.size() < 40) {
        auto lower = icu::UnicodeString::fromUTF8(token);
        lower.toLower(icu::Locale::getRoot());
        token.clear();
        lower.toUTF8String(token);
        out.push_back(stem(token));
      }
    }
    if (i < text.length())
      i += U16_LENGTH(text.char32At(i));
  }
  return out;
}
// Tantivy uses Lucene byte4 field lengths for BM25.
int norm(int length) {
  if (length < 40)
    return length;
  auto v = static_cast<unsigned>(length - 24);
  unsigned shift = std::bit_width(v) - 4;
  return 24 + ((v >> shift) << shift);
}
struct Record {
  int64_t pid, doc;
  uint32_t shard;
  int32_t length[3];
};
struct Search {
  Xapian::Database db;
  unsigned schema;
  std::array<float, 3> mean{};
  float title = 4, section = .5;
  explicit Search(const fs::path &path)
      : db(path.string()), schema(std::stoul(db.get_metadata("unllm-schema"))) {
    for (int f = 0; f < 3; ++f)
      mean[f] = std::stod(db.get_metadata("total" + std::to_string(f))) /
                std::max(1u, db.get_doccount());
  }
};
void index_shard(Xapian::WritableDatabase &index, const fs::path &file,
                 int shard, int schema, std::array<int64_t, 3> &totals) {
  sqlite3 *db = nullptr;
  int rc = sqlite3_open_v2(file.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> owner(db, sqlite3_close);
  if (rc != SQLITE_OK)
    throw std::runtime_error("Cannot open " + file.string());
  sqlite3_stmt *ptr = nullptr;
  if (sqlite3_prepare_v2(
          db,
          "SELECT p.pid,p.doc_id,p.role,p.text,d.title,p.section FROM passages "
          "p LEFT JOIN docs d ON d.doc_id=p.doc_id ORDER BY p.pid",
          -1, &ptr, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> rows(
      ptr, sqlite3_finalize);
  while ((rc = sqlite3_step(ptr)) == SQLITE_ROW) {
    Record rec{};
    rec.shard = shard;
    rec.pid = sqlite3_column_int64(ptr, 0);
    rec.doc = schema == 2 ? sqlite3_column_int64(ptr, 1) : 0;
    auto col = [&](int i) {
      auto s = sqlite3_column_text(ptr, i);
      return s ? std::string(reinterpret_cast<const char *>(s),
                             sqlite3_column_bytes(ptr, i))
               : std::string{};
    };
    Xapian::Document doc;
    auto role = col(2);
    doc.add_boolean_term("R" + role);
    doc.add_value(0, role);
    for (int f = 0; f < (schema == 2 ? 3 : 1); ++f) {
      auto terms = tokens(col(3 + f));
      totals[f] += terms.size();
      rec.length[f] = norm(terms.size());
      for (auto &term : terms)
        doc.add_term(char('A' + f) + term);
    }
    doc.set_data(
        std::string(reinterpret_cast<const char *>(&rec), sizeof(rec)));
    index.add_document(doc);
  }
  if (rc != SQLITE_DONE)
    throw std::runtime_error(sqlite3_errmsg(db));
}
float idf(float total, float df) {
  return std::log1p((total - df + .5f) / (df + .5f));
}
struct Hit {
  float score;
  uint32_t id;
  Record rec;
};
bool better(const Hit &a, const Hit &b) {
  return a.score != b.score ? a.score > b.score : a.id < b.id;
}
using Doc = std::pair<uint32_t, int64_t>;
std::vector<Hit> query(const Search &s, const char *text, const char *role,
                       const std::set<Doc> *filter, uint32_t limit) {
  auto terms = tokens(text);
  std::sort(terms.begin(), terms.end());
  terms.erase(std::unique(terms.begin(), terms.end()), terms.end());
  if (!limit || terms.empty() || (filter && filter->empty()))
    return {};
  int required = std::max<int>(1, terms.size() - 1);
  auto var = filter && getenv("UNLLM_INNER_MIN_MATCH") ? "UNLLM_INNER_MIN_MATCH"
                                                       : "UNLLM_MIN_MATCH";
  if (auto v = getenv(var); v && std::string_view(v) != "n-1") {
    char *end;
    double f = strtod(v, &end);
    required =
        *end || !std::isfinite(f)
            ? 1
            : std::max(1, int(std::ceil(std::clamp(f, 0., 1.) * terms.size())));
  }
  float title = s.title;
  if (filter)
    if (auto v = getenv("UNLLM_INNER_TITLE_BOOST")) {
      char *end;
      float k = strtof(v, &end);
      if (!*end && std::isfinite(k))
        title = k;
    }
  struct Cursor {
    Xapian::PostingIterator it;
    int field;
    size_t word;
    float weight;
  };
  std::vector<Cursor> cursors;
  auto total = s.db.get_doccount();
  for (size_t i = 0; i < terms.size(); ++i)
    for (int f = 0; f < (s.schema == 2 ? 3 : 1); ++f) {
      float boost = f == 0 ? 1 : f == 1 ? title : s.section;
      if (boost <= 0 || (f && stop.contains(terms[i])))
        continue;
      auto term = char('A' + f) + terms[i];
      auto it = s.db.postlist_begin(term);
      if (it != s.db.postlist_end(term))
        cursors.push_back(
            {it, f, i, boost * 2.2f * idf(total, s.db.get_termfreq(term))});
    }
  float role_score =
      role ? idf(total, s.db.get_termfreq("R" + std::string(role))) : 0;
  std::priority_queue<Hit, std::vector<Hit>, decltype(&better)> hits(&better);
  Xapian::PostingIterator end;
  while (true) {
    Xapian::docid id = 0;
    for (auto &c : cursors)
      if (c.it != end && (!id || *c.it < id))
        id = *c.it;
    if (!id)
      break;
    int matched = 0;
    size_t last = terms.size();
    for (auto &c : cursors)
      if (c.it != end && *c.it == id && c.word != last) {
        ++matched;
        last = c.word;
      }
    if (matched >= required) {
      auto d = s.db.get_document(id);
      auto data = d.get_data();
      Record rec;
      if (data.size() != sizeof(rec))
        throw std::runtime_error("Invalid ranked passage record");
      std::memcpy(&rec, data.data(), sizeof(rec));
      if ((!role || d.get_value(0) == role) &&
          (!filter || filter->contains({rec.shard, rec.doc}))) {
        float score = role_score;
        for (auto &c : cursors)
          if (c.it != end && *c.it == id) {
            float tf = c.it.get_wdf();
            score += c.weight * tf /
                     (tf + 1.2f * (.25f + .75f * rec.length[c.field] /
                                              s.mean[c.field]));
          }
        Hit hit{score, id, rec};
        if (hits.size() < limit)
          hits.push(hit);
        else if (better(hit, hits.top())) {
          hits.pop();
          hits.push(hit);
        }
      }
    }
    for (auto &c : cursors)
      if (c.it != end && *c.it == id)
        ++c.it;
  }
  std::vector<Hit> out(hits.size());
  for (auto it = out.rbegin(); it != out.rend(); ++it) {
    *it = hits.top();
    hits.pop();
  }
  return out;
}
} // namespace
namespace unllm {
void build_ranked_index(const fs::path &pack) {
  std::map<int, fs::path> shards;
  for (auto &e : fs::directory_iterator(pack)) {
    auto name = e.path().filename().string();
    if (name.size() == 16 && name.starts_with("shard-") &&
        name.ends_with(".sqlite") &&
        name.substr(6, 3).find_first_not_of("0123456789") == name.npos)
      shards.emplace(std::stoi(name.substr(6, 3)), e.path());
  }
  if (shards.empty())
    throw std::runtime_error("No SQLite shards in " + pack.string());
  auto legacy = pack / "tantivy/meta.json";
  int schema = 2;
  if (fs::exists(legacy)) {
    std::ifstream in(legacy);
    std::ostringstream text;
    text << in.rdbuf();
    auto fields = json::Value::parse(text.str())["schema"];
    if (!fields.is_array())
      throw std::runtime_error("Invalid Tantivy schema");
    schema = 1;
    for (auto field : fields.items())
      if (field["name"].text() == "doc_key")
        schema = 2;
  }
  auto dest = pack / "ranked";
  auto temp = (pack / ".ranked-XXXXXX").string();
  if (!mkdtemp(temp.data()))
    throw std::runtime_error("Cannot stage ranked index");
  try {
    std::array<int64_t, 3> totals{};
    Xapian::WritableDatabase db(temp, Xapian::DB_CREATE_OR_OVERWRITE);
    for (auto &[number, path] : shards)
      index_shard(db, path, number, schema, totals);
    db.set_metadata("unllm-schema", std::to_string(schema));
    for (int f = 0; f < 3; ++f)
      db.set_metadata("total" + std::to_string(f), std::to_string(totals[f]));
    db.commit();
    db.close();
    if (fs::exists(dest)) {
      if (syscall(SYS_renameat2, AT_FDCWD, temp.c_str(), AT_FDCWD, dest.c_str(),
                  RENAME_EXCHANGE) < 0)
        throw std::runtime_error("Cannot exchange ranked index");
      fs::remove_all(temp);
    } else
      fs::rename(temp, dest);
  } catch (...) {
    fs::remove_all(temp);
    throw;
  }
}
} // namespace unllm
extern "C" {
void *us_open(const char *path) {
  if (!path)
    return nullptr;
  auto dir = fs::path(path).parent_path() / "ranked";
  if (!fs::exists(dir)) {
    if (fs::is_regular_file(fs::path(path) / "meta.json"))
      std::fprintf(stderr,
                   "Ranked index requires migration. Run unllm_index on %s\n",
                   dir.parent_path().c_str());
    return nullptr;
  }
  try {
    return new Search(dir);
  } catch (const Xapian::Error &e) {
    std::fprintf(stderr, "Cannot open ranked index: %s\n",
                 e.get_description().c_str());
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Invalid ranked index: %s\n", e.what());
  }
  return nullptr;
}
uint32_t us_version() { return 2; }
uint32_t us_schema(const Search *s) { return s ? s->schema : 0; }
void us_set_boosts(Search *s, float title, float section) {
  if (s) {
    s->title = std::max(0.f, title);
    s->section = std::max(0.f, section);
  }
}
int us_search2(const Search *s, const char *words, const char *role,
               const uint32_t *shards, const int64_t *docs, uint32_t n,
               uint32_t k, uint32_t *out_shards, int64_t *pids,
               int64_t *doc_ids, float *scores) {
  if (!s || !words || !out_shards || !pids || !scores || (shards && n && !docs))
    return 0;
  if (shards && s->schema == 1)
    return -1;
  std::set<Doc> filter;
  if (shards)
    for (unsigned i = 0; i < n; ++i)
      filter.emplace(shards[i], docs[i]);
  auto hits = query(*s, words, role, shards ? &filter : nullptr, k);
  for (size_t i = 0; i < hits.size(); ++i) {
    auto &h = hits[i];
    out_shards[i] = h.rec.shard;
    pids[i] = h.rec.pid;
    if (doc_ids)
      doc_ids[i] = h.rec.doc;
    scores[i] = h.score;
  }
  return hits.size();
}
int us_search(const Search *s, const char *words, const char *role, uint32_t k,
              uint32_t *shards, int64_t *pids, float *scores) {
  return us_search2(s, words, role, nullptr, nullptr, 0, k, shards, pids,
                    nullptr, scores);
}
void us_close(Search *s) { delete s; }
}
