#include "delimited.h"
#include "shard.h"
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <zlib.h>
namespace unllm::maintenance {
using namespace native;
namespace {
bool english(const std::string &text) {
  static const std::set<std::string> words = [] {
    std::istringstream in(
        "the and of to a in i is that it his he with my for her as was you me "
        "on be all not but by their they at from this so she we no or our thy "
        "thou thee shall what when where who like are have had there then will "
        "an which one its into upon now more them than him your o oh ye");
    std::set<std::string> out;
    for (std::string s; in >> s;)
      out.insert(s);
    return out;
  }();
  static const std::regex token("[a-z']+");
  auto lower = text;
  for (auto &c : lower)
    if (c >= 'A' && c <= 'Z')
      c += 32;
  size_t n = 0, found = 0;
  for (std::sregex_iterator i(lower.begin(), lower.end(), token), end; i != end;
       ++i) {
    ++n;
    found += words.contains(i->str());
  }
  return n && found >= .12 * n;
}
} // namespace
void ingest_poetry(const Path &file, Shard &shard) {
  std::map<std::string, std::pair<std::string, std::string>> books;
  auto catalog = file.parent_path() / "pg_catalog.csv";
  if (std::filesystem::exists(catalog)) {
    auto rows =
        parse_delimited(read_file(catalog, 256 * 1024 * 1024), ',', false);
    if (rows.empty())
      throw Unsupported("Empty Gutenberg catalogue");
    std::map<std::string, size_t> cols;
    for (size_t i = 0; i < rows.front().size(); ++i)
      cols[rows.front()[i]] = i;
    for (size_t n = 1; n < rows.size(); ++n) {
      auto get = [&](auto field) {
        auto i = cols.find(field);
        return i != cols.end() && i->second < rows[n].size()
                   ? rows[n][i->second]
                   : std::string();
      };
      auto title = get("Title"), author = get("Authors");
      title = title.substr(0, title.find_first_of("\r\n"));
      author = author.substr(0, author.find(';'));
      static const std::regex dates(R"(,\s*\d{3,4}\??-\d{0,4}\??)");
      books[get("Text#")] = {title, std::regex_replace(author, dates, "")};
    }
  }
  std::unique_ptr<gzFile_s, decltype(&gzclose)> in(gzopen(file.c_str(), "rb"),
                                                   gzclose);
  if (!in)
    throw Unsupported("Cannot open poetry file");
  int64_t doc = 0, ord = 0;
  int count = 0;
  std::string previous, verse, line;
  auto flush = [&] {
    if (count && english(verse))
      shard.passage(doc, ord++, "verse", verse);
    verse.clear();
    count = 0;
  };
  auto consume = [&] {
    Json obj;
    try {
      obj = Json::parse(line);
    } catch (const std::runtime_error &) {
      return;
    }
    auto gid = obj["gid"].text(), s = obj["s"].text();
    if (gid.empty() || !obj["s"].is_string())
      return;
    if (!doc || gid != previous) {
      flush();
      previous = gid;
      ord = 0;
      auto it = books.find(gid);
      doc = shard.document(it == books.end() ? "Project Gutenberg book " + gid
                                             : it->second.first,
                           "https://www.gutenberg.org/ebooks/" + gid,
                           "Public domain (US)", {}, {},
                           it == books.end() ? "" : it->second.second);
    }
    if (count++)
      verse += '\n';
    verse += s;
    auto last = s.find_last_not_of(" \r\n\t");
    if (count >= 16 || (count >= 8 && last != s.npos &&
                        std::string_view(".!?").find(s[last]) != s.npos))
      flush();
  };
  char buf[8192];
  while (gzgets(in.get(), buf, sizeof(buf))) {
    line += buf;
    if (line.ends_with('\n')) {
      consume();
      line.clear();
    }
  }
  if (!gzeof(in.get()) || gzclose(in.release()) != Z_OK)
    throw Unsupported("Incomplete poetry archive");
  if (!line.empty())
    consume();
  flush();
}
} // namespace unllm::maintenance
