#include "formula.h"
#include "shard.h"
#include <archive.h>
#include <archive_entry.h>
#include <charconv>
#include <functional>
#include <libxml/HTMLparser.h>
#include <libxml/xmlreader.h>
#include <regex>
#include <sstream>

namespace unllm::maintenance {
using namespace native;
namespace {
using Reader = std::unique_ptr<xmlTextReader, decltype(&xmlFreeTextReader)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
int read_member(void *archive, char *buffer, int length) {
  return static_cast<int>(archive_read_data(
      static_cast<struct archive *>(archive), buffer, length));
}
std::string attribute(xmlTextReaderPtr reader, const char *key) {
  auto ptr = xmlTextReaderGetAttribute(reader, BAD_CAST key);
  if (!ptr)
    return {};
  std::string value(reinterpret_cast<const char *>(ptr));
  xmlFree(ptr);
  return value;
}
int64_t number(const std::string &s) {
  int64_t n = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
  return ec == std::errc{} && p == s.data() + s.size() ? n : 0;
}
void insert_row(sqlite3_stmt *st, const std::vector<std::string> &row) {
  sqlite3_reset(st);
  for (size_t i = 0; i < row.size(); ++i)
    if (sqlite3_bind_text(st, i + 1, row[i].data(), row[i].size(),
                          SQLITE_TRANSIENT) != SQLITE_OK)
      throw Unsupported("Cannot bind archive row");
  if (sqlite3_step(st) != SQLITE_DONE)
    throw Unsupported("Cannot stage archive row");
}
struct Blocks {
  std::vector<std::pair<bool, std::string>> rows;
  enum class Kind { text, item, code, items };
  using Block = std::pair<Kind, std::string>;
  std::vector<Block> raw;
  bool item = false;
  std::string text;
  void flush() {
    std::istringstream in(text);
    std::string normal;
    for (std::string s; in >> s;) {
      if (!normal.empty())
        normal += ' ';
      normal += s;
    }
    if (!normal.empty())
      raw.emplace_back(item ? Kind::item : Kind::text, std::move(normal));
    text.clear();
  }
  void visit(xmlNode *node) {
    for (auto n = node; n; n = n->next) {
      if (n->type == XML_TEXT_NODE) {
        text += reinterpret_cast<const char *>(n->content);
        continue;
      }
      if (n->type != XML_ELEMENT_NODE)
        continue;
      std::string tag(reinterpret_cast<const char *>(n->name));
      if (tag == "script" || tag == "style")
        continue;
      if (tag == "pre") {
        flush();
        auto content = xmlNodeGetContent(n);
        if (content && *content)
          raw.emplace_back(Kind::code, reinterpret_cast<const char *>(content));
        xmlFree(content);
      } else {
        bool block = tag == "p" || tag == "li" || tag == "br" || tag == "div" ||
                     tag == "blockquote" || tag == "tr" || tag == "hr" ||
                     (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' &&
                      tag[1] <= '6');
        if (block)
          flush();
        auto previous = item;
        item = item || tag == "li";
        visit(n->children);
        if (block)
          flush();
        item = previous;
      }
    }
  }
  void tidy(bool maths) {
    static const std::regex filler(
        R"(^(edit(ed)?( [0-9]+)?|update(d)?( [0-9]+)?|hope (this|that|it) helps|good luck|cheers|thanks|thank you|hth|regards|enjoy|hope this answers your question)[.!:]*$)",
        std::regex::icase);
    std::vector<Block> clean;
    std::string carry;
    auto intro = [&] {
      return !clean.empty() && clean.back().first == Kind::text &&
             clean.back().second.ends_with(':') &&
             clean.back().second.size() <= 300;
    };
    for (auto &[kind, value] : raw) {
      if (kind == Kind::code) {
        auto last = value.find_last_not_of("\r\n");
        if (last == value.npos)
          continue;
        value.resize(last + 1);
      } else {
        if (maths)
          value = inline_formulas(value);
        if (value.empty() || std::regex_match(value, filler))
          continue;
        if (kind == Kind::item && value.size() < 40) {
          value.erase(value.find_last_not_of(".;, ") + 1);
          if (!clean.empty() && clean.back().first == Kind::items &&
              carry.empty())
            clean.back().second += "; " + value;
          else if (intro() && carry.empty()) {
            clean.back().first = Kind::items;
            clean.back().second += " " + value;
          } else {
            clean.emplace_back(Kind::items,
                               carry.empty() ? value : carry + " " + value);
            carry.clear();
          }
          continue;
        }
        kind = Kind::text;
        if (value.size() < 25) {
          if (std::string_view(".!?:").find(value.back()) ==
              std::string_view::npos)
            value += ':';
          if (value.ends_with(':') || clean.empty() ||
              clean.back().first == Kind::code)
            carry = carry.empty() ? value : carry + " " + value;
          else {
            clean.back().first = Kind::text;
            clean.back().second += " " + value;
          }
          continue;
        }
      }
      if (kind == Kind::code && carry.empty() && intro()) {
        clean.back().first = Kind::code;
        clean.back().second += "\n" + value;
        continue;
      }
      if (!carry.empty()) {
        value = carry + (kind == Kind::code ? "\n" : " ") + value;
        carry.clear();
      }
      clean.emplace_back(kind, value);
    }
    if (!carry.empty() && !carry.ends_with(':')) {
      if (clean.empty())
        clean.emplace_back(Kind::text, carry);
      else
        clean.back().second += " " + carry;
    }
    for (auto &[kind, value] : clean) {
      if (kind == Kind::items && !value.empty() &&
          std::string_view(".!?:").find(value.back()) == std::string_view::npos)
        value += '.';
      rows.emplace_back(kind == Kind::code, std::move(value));
    }
  }
  Blocks(const std::string &html, bool maths) {
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc(
        htmlReadMemory(html.data(), html.size(), nullptr, "UTF-8",
                       HTML_PARSE_NONET | HTML_PARSE_NOERROR |
                           HTML_PARSE_NOWARNING),
        xmlFreeDoc);
    if (doc)
      visit(xmlDocGetRootElement(doc.get()));
    flush();
    tidy(maths);
  }
};
} // namespace
void ingest_stackexchange(const Path &path, Shard &shard) {
  sqlite3 *db = nullptr;
  auto rc = sqlite3_open("", &db);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(db,
                                                                sqlite3_close);
  if (rc != SQLITE_OK)
    throw Unsupported("Cannot open archive staging database");
  auto sql = [&](const char *query) {
    if (sqlite3_exec(db, query, nullptr, nullptr, nullptr) != SQLITE_OK)
      throw Unsupported(sqlite3_errmsg(db));
  };
  auto prepare = [&](const char *query) {
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, query, -1, &st, nullptr) != SQLITE_OK)
      throw Unsupported(sqlite3_errmsg(db));
    return Statement(st, sqlite3_finalize);
  };
  sql("BEGIN; CREATE TABLE posts(id INTEGER PRIMARY KEY,type INTEGER,parent "
      "INTEGER,accepted INTEGER,title TEXT,body TEXT,tags TEXT,score "
      "INTEGER,owner INTEGER,oname TEXT,license TEXT);CREATE TABLE users(id "
      "INTEGER PRIMARY KEY,name TEXT);");
  auto post = prepare("INSERT INTO posts VALUES(?,?,?,?,?,?,?,?,?,?,?)"),
       user = prepare("INSERT OR IGNORE INTO users VALUES(?,?)");
  std::unique_ptr<struct archive, decltype(&archive_read_free)> archive(
      archive_read_new(), archive_read_free);
  archive_read_support_format_7zip(archive.get());
  if (archive_read_open_filename(archive.get(), path.c_str(), 128 * 1024) !=
      ARCHIVE_OK)
    throw Unsupported("Cannot open Stack Exchange archive");
  struct archive_entry *entry = nullptr;
  bool posts = false;
  while ((rc = archive_read_next_header(archive.get(), &entry)) == ARCHIVE_OK) {
    auto file = Path(archive_entry_pathname(entry)).filename();
    if (file != "Posts.xml" && file != "Users.xml")
      continue;
    posts |= file == "Posts.xml";
    Reader reader(xmlReaderForIO(read_member, nullptr, archive.get(), nullptr,
                                 "UTF-8", XML_PARSE_NONET | XML_PARSE_NO_XXE),
                  xmlFreeTextReader);
    if (!reader)
      throw Unsupported("Cannot open archive XML");
    int result = 0;
    while ((result = xmlTextReaderRead(reader.get())) == 1) {
      if (xmlTextReaderNodeType(reader.get()) != XML_READER_TYPE_ELEMENT ||
          !xmlStrEqual(xmlTextReaderConstLocalName(reader.get()),
                       BAD_CAST "row"))
        continue;
      auto attr = [&](const char *key) { return attribute(reader.get(), key); };
      if (file == "Users.xml") {
        insert_row(user.get(), {attr("Id"), attr("DisplayName")});
        continue;
      }
      auto type = attr("PostTypeId"), score = attr("Score");
      if ((type != "1" && type != "2") || number(score) < 1)
        continue;
      auto lic = attr("ContentLicense"), date = attr("CreationDate");
      if (lic.empty())
        lic = date < "2011-04-08"   ? "CC BY-SA 2.5"
              : date < "2018-05-02" ? "CC BY-SA 3.0"
                                    : "CC BY-SA 4.0";
      insert_row(post.get(),
                 {attr("Id"), type, attr("ParentId"), attr("AcceptedAnswerId"),
                  attr("Title"), attr("Body"), attr("Tags"), score,
                  attr("OwnerUserId"), attr("OwnerDisplayName"), lic});
    }
    if (result < 0)
      throw Unsupported("Invalid archive XML");
  }
  if (rc != ARCHIVE_EOF || !posts)
    throw Unsupported("Incomplete Stack Exchange archive");
  sql("COMMIT; CREATE INDEX by_parent ON posts(parent,type,score DESC);");
  auto joined = prepare(
      R"(SELECT q.id,q.title,q.body,q.tags,q.score,COALESCE(u.name,q.oname),a.body,COALESCE(v.name,a.oname),a.license,(a.id=q.accepted)
   FROM posts q JOIN posts a ON a.id=COALESCE((SELECT id FROM posts WHERE type=2 AND id=q.accepted AND parent=q.id),
    (SELECT id FROM posts WHERE type=2 AND parent=q.id AND score>=2 ORDER BY score DESC,id LIMIT 1))
   LEFT JOIN users u ON u.id=q.owner LEFT JOIN users v ON v.id=a.owner WHERE q.type=1 ORDER BY q.id)");
  auto site = path.stem().string();
  while ((rc = sqlite3_step(joined.get())) == SQLITE_ROW) {
    auto field = [&](int i) {
      auto s = sqlite3_column_text(joined.get(), i);
      return s ? std::string(reinterpret_cast<const char *>(s)) : std::string();
    };
    bool accepted = sqlite3_column_int(joined.get(), 9);
    auto extra = Json::object();
    extra.set("asker", Json::string(field(5)));
    extra.set("accepted", Json::boolean(accepted));
    auto tags = field(3);
    for (auto &c : tags)
      if (c == '<' || c == '>' || c == '|')
        c = ' ';
    std::istringstream tagstream(tags);
    tags.clear();
    for (std::string tag; tagstream >> tag;) {
      if (!tags.empty())
        tags += ' ';
      tags += tag;
    }
    auto title = math_site(site) ? inline_formulas(field(1)) : field(1);
    auto doc = shard.document(title, "https://" + site + "/q/" + field(0),
                              field(8), tags, extra.dump(), field(7),
                              sqlite3_column_int(joined.get(), 4));
    std::string question = title + ".";
    int words = 0;
    auto body = Blocks(field(2), math_site(site));
    for (auto &[code, text] : body.rows) {
      std::istringstream in(text);
      std::string word;
      while (words < 120 && in >> word) {
        question += " " + word;
        ++words;
      }
    }
    shard.passage(doc, 0, "question", question);
    int ord = 0;
    auto blocks = Blocks(field(6), math_site(site));
    for (size_t i = 0; i < std::min<size_t>(12, blocks.rows.size()); ++i) {
      auto &[code, text] = blocks.rows[i];
      shard.passage(doc, ++ord,
                    code       ? "code"
                    : accepted ? "accepted"
                               : "answer",
                    text);
    }
  }
  if (rc != SQLITE_DONE)
    throw Unsupported("Archive selection failed");
}
} // namespace unllm::maintenance
