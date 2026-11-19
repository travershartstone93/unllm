#include "formula.h"
#include "shard.h"
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <libxml/HTMLparser.h>
#include <md4c-html.h>
#include <md4c.h>
#include <parquet/arrow/reader.h>
#include <set>
#include <sstream>
#include <unicode/uchar.h>
#include <unicode/utf8.h>

namespace unllm::maintenance {
using namespace native;
namespace {
template <class T> T unpack(arrow::Result<T> r) {
  if (!r.ok())
    throw Unsupported(r.status().ToString());
  return std::move(r).ValueUnsafe();
}
const std::set<std::string> dropped = {"references",
                                       "external links",
                                       "see also",
                                       "notes",
                                       "further reading",
                                       "bibliography",
                                       "sources",
                                       "related pages",
                                       "other websites",
                                       "footnotes",
                                       "citations",
                                       "works cited",
                                       "notes and references",
                                       "references and notes",
                                       "explanatory notes",
                                       "external sources",
                                       "sources and further reading",
                                       "references and further reading",
                                       "further reading and external links",
                                       "notes and sources",
                                       "general references",
                                       "cited sources",
                                       "selected bibliography",
                                       "literature",
                                       "gallery",
                                       "other pages",
                                       "links",
                                       "web links",
                                       "online sources",
                                       "primary sources",
                                       "secondary sources",
                                       "citations and notes",
                                       "endnotes"};
std::string content(xmlNode *n) {
  auto s = xmlNodeGetContent(n);
  if (!s)
    return {};
  std::string result(reinterpret_cast<char *>(s));
  xmlFree(s);
  return result;
}
std::string words(const std::string &s) {
  std::istringstream in(s);
  std::string out;
  for (std::string w; in >> w;) {
    if (!out.empty())
      out += ' ';
    out += w;
  }
  return out;
}
struct Passage {
  std::string role, text, section;
};
struct Article {
  std::vector<Passage> parts;
  bool first = true;
  int skip = 0;
  std::string section, buffer, label;
  bool colon_label = false;
  void markdown_paragraph(const std::string &raw) {
    static const std::set<std::string> verbs = {
        "is", "are", "was", "were", "has", "have", "had", "be", "been"};
    std::istringstream in(raw);
    for (std::string line; std::getline(in, line);) {
      auto text = words(display_formulas(line));
      bool letters = false, verb = false;
      for (int32_t i = 0; i < static_cast<int32_t>(text.size());) {
        UChar32 c;
        U8_NEXT(text, i, static_cast<int32_t>(text.size()), c);
        letters |= bool(u_isalpha(c));
      }
      if (!letters || skip)
        continue;
      std::istringstream tokens(text);
      size_t count = 0;
      for (std::string w; tokens >> w;) {
        ++count;
        verb |= verbs.contains(key(w));
      }
      bool colon = text.ends_with(':') && count <= 30;
      bool bare =
          count <= 4 && !verb &&
          std::string_view(".!?;,").find(text.back()) == std::string_view::npos;
      if (colon || bare) {
        emit();
        label = text;
        if (colon)
          label.pop_back();
        colon_label = colon;
      } else {
        paragraph(!label.empty() && colon_label ? label + ": " + text : text);
        label.clear();
      }
    }
  }
  void emit() {
    if (!buffer.empty()) {
      for (auto &piece : split_passage(buffer)) {
        parts.push_back({first ? "lead" : "body", piece, section});
        first = false;
      }
      buffer.clear();
    }
  }
  void paragraph(const std::string &text) {
    auto p = words(text);
    if (skip || p.empty())
      return;
    if (!buffer.empty())
      buffer += ' ';
    buffer += p;
    if (std::count(buffer.begin(), buffer.end(), ' ') >= 59)
      emit();
  }
  void heading(int level, const std::string &s) {
    emit();
    label.clear();
    if (level == 1 || (skip && level > skip))
      return;
    skip = dropped.contains(key(s)) ? level : 0;
    section = s;
  }
  void markup(xmlNode *node) {
    for (auto n = node; n; n = n->next) {
      if (n->type != XML_ELEMENT_NODE)
        continue;
      std::string tag(reinterpret_cast<const char *>(n->name));
      if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6')
        heading(tag[1] - '0', words(content(n)));
      else if (tag == "p")
        markdown_paragraph(content(n));
      else if (tag == "pre" || tag == "ul" || tag == "ol" || tag == "table") {
        emit();
        if (skip)
          continue;
        std::string out;
        if (tag == "pre") {
          out = content(n);
          while (out.ends_with('\n'))
            out.pop_back();
        } else if (tag == "table") {
          auto rows = [&](auto &&self, xmlNode *row) -> void {
            for (; row; row = row->next) {
              if (row->type != XML_ELEMENT_NODE)
                continue;
              if (xmlStrEqual(row->name, BAD_CAST "tr")) {
                std::string line;
                for (auto c = row->children; c; c = c->next)
                  if (c->type == XML_ELEMENT_NODE) {
                    if (!line.empty())
                      line += ", ";
                    line += words(content(c));
                  }
                if (!out.empty())
                  out += '\n';
                out += line;
              } else
                self(self, row->children);
            }
          };
          rows(rows, n->children);
          out = display_formulas(out);
          if (!label.empty())
            out = label + ":\n" + out;
        } else {
          auto list = [&](auto &&self, xmlNode *node,
                          int depth) -> std::string {
            std::string result;
            for (auto item = node->children; item; item = item->next) {
              if (item->type != XML_ELEMENT_NODE)
                continue;
              std::string line;
              for (auto child = item->children; child; child = child->next) {
                if (xmlStrEqual(child->name, BAD_CAST "ul") ||
                    xmlStrEqual(child->name, BAD_CAST "ol")) {
                  if (depth < 64)
                    line += " " + self(self, child, depth + 1);
                } else
                  line += content(child);
              }
              line = words(display_formulas(line));
              line.erase(line.find_last_not_of(".;, ") + 1);
              if (line.empty())
                continue;
              if (!result.empty())
                result += depth ? ", " : "; ";
              result += line;
            }
            return result;
          };
          out = list(list, n, 0);
          auto prefix = label.empty() ? section : label;
          if (!out.empty())
            out = (prefix.empty() ? "" : prefix + ": ") + out + ".";
        }
        label.clear();
        if (!out.empty())
          parts.push_back({tag == "pre"     ? "code"
                           : tag == "table" ? "table"
                                            : "list",
                           out, section});
      } else if (tag != "script" && tag != "style")
        markup(n->children);
    }
  }
  Article(const std::string &text, bool markdown) {
    if (markdown) {
      std::string html;
      if (md_html(
              text.data(), text.size(),
              [](const char *s, MD_SIZE size, void *ctx) {
                static_cast<std::string *>(ctx)->append(s, size);
              },
              &html, MD_FLAG_TABLES | MD_FLAG_NOHTML, 0) != 0)
        throw Unsupported("Invalid article markup");
      std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc(
          htmlReadMemory(html.data(), html.size(), nullptr, "UTF-8",
                         HTML_PARSE_NONET | HTML_PARSE_NOERROR |
                             HTML_PARSE_NOWARNING),
          xmlFreeDoc);
      if (doc)
        markup(xmlDocGetRootElement(doc.get()));
    } else {
      size_t start = 0;
      while (start < text.size()) {
        auto end = text.find("\n\n", start);
        auto block = text.substr(start, end == text.npos ? end : end - start);
        auto s = words(block);
        if (!s.empty()) {
          if (block.find('\n') == block.npos && s.size() < 80 &&
              std::string_view(".!?:;").find(s.back()) == s.npos)
            heading(2, s);
          else if (auto head = words(block.substr(0, block.find('\n')));
                   dropped.contains(key(head)))
            heading(2, head);
          else
            paragraph(s);
        }
        if (end == text.npos)
          break;
        start = end + 2;
      }
    }
    emit();
  }
};
auto open_reader(const Path &path) {
  return unpack(parquet::arrow::OpenFile(
      unpack(arrow::io::ReadableFile::Open(path.string())),
      arrow::default_memory_pool()));
}
} // namespace
bool finewiki(const Path &path) {
  auto reader = open_reader(path);
  std::shared_ptr<arrow::Schema> schema;
  auto status = reader->GetSchema(&schema);
  if (!status.ok())
    throw Unsupported(status.ToString());
  return schema->GetFieldIndex("wikiname") >= 0;
}
void ingest_wiki(const Path &path, Shard &shard) {
  auto reader = open_reader(path);
  reader->set_batch_size(500);
  auto batches = unpack(reader->GetRecordBatchReader());
  auto schema = batches->schema();
  if (schema->GetFieldIndex("title") < 0 || schema->GetFieldIndex("text") < 0)
    throw Unsupported("Wikipedia table needs title and text columns");
  auto side = read_optional_json(path.parent_path() / "source.json");
  bool markdown = schema->GetFieldIndex("wikiname") >= 0 ||
                  side["source"].text() == "HuggingFaceFW/finewiki";
  while (auto batch = unpack(batches->Next())) {
    for (int64_t i = 0; i < batch->num_rows(); ++i) {
      auto get = [&](const char *field) {
        auto col = batch->GetColumnByName(field);
        if (!col || col->IsNull(i))
          return std::string{};
        return unpack(col->GetScalar(i))->ToString();
      };
      auto title = get("title");
      if (title.empty())
        continue;
      Article article(get("text"), markdown);
      auto boxes = Json::array();
      auto raw = get("infoboxes");
      if (!raw.empty())
        try {
          boxes = Json::parse(raw);
        } catch (const std::runtime_error &) {
        }
      if (article.parts.empty() && !boxes.size())
        continue;
      auto extra = Json::object();
      extra.set("modified", Json::string(get("date_modified")));
      extra.set("wikidata", Json::string(get("wikidata_id")));
      auto doc = shard.document(title, get("url"), "CC BY-SA 4.0", {},
                                markdown ? extra.dump() : "");
      int ord = 0;
      for (auto &p : article.parts)
        shard.passage(doc, ord++, p.role, p.text, p.section);
      for (auto box : boxes.items())
        for (auto &[k, v] : box["data"].members())
          shard.fact(doc, box["title"].text(), k,
                     v.is_string() ? v.text() : v.dump());
    }
  }
}
} // namespace unllm::maintenance
