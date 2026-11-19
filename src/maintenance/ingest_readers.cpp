#include "../symbolic/gui_json.h"
#include "shard.h"
#include "unllm/symbolic/process.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <unicode/regex.h>
#include <zip.h>
namespace unllm::maintenance {
using namespace native;
using namespace native::gui;
namespace fs = std::filesystem;
namespace {
std::string replace(std::string text, const std::string &from,
                    const std::string &to) {
  for (size_t i = 0; (i = text.find(from, i)) != text.npos; i += to.size())
    text.replace(i, from.size(), to);
  return text;
}
std::string simplify(const std::string &text) {
  auto u = icu::UnicodeString::fromUTF8(text);
  icu::UnicodeString result;
  bool space = false;
  for (int32_t i = 0; i < u.length();) {
    auto cp = u.char32At(i);
    i += U16_LENGTH(cp);
    if (u_isUWhiteSpace(cp)) {
      space = !result.isEmpty();
      continue;
    }
    if (space)
      result += ' ';
    space = false;
    result.append(cp);
  }
  std::string out;
  result.toUTF8String(out);
  return out;
}
std::string clean(const std::string &src) {
  std::string value = src;
  for (auto seq : {"﻿", "​", "‌", "‍", "‎", "‏", "⁠", "­"})
    value = replace(value, seq, "");
  value = replace(value, " ", " ");
  const static std::vector<std::pair<std::regex, std::string>> rules{
      {std::regex(R"(\*\*([^*\n]+)\*\*)"), "$1"},
      {std::regex(R"(\[([^\]\n]+)\]\([^)\n]*\))"), "$1"},
      {std::regex(R"REG(`([^`\n]+)`)REG"), "$1"},
      {std::regex("[ \t]+"), " "},
      {std::regex(R"( ([,.;:!?)]))"), "$1"},
      {std::regex(R"(\(\s*[,;]?\s*\))"), ""}};
  for (auto &[rx, sub] : rules)
    value = std::regex_replace(value, rx, sub);
  auto u = icu::UnicodeString::fromUTF8(value);
  u.trim();
  value.clear();
  u.toUTF8String(value);
  return value;
}
std::vector<std::string> pieces(const std::string &p) {
  auto words = [](const std::string &s) {
    return s.empty() ? 0 : 1 + int(std::count(s.begin(), s.end(), ' '));
  };
  if (words(p) <= 180)
    return {p};
  std::vector<std::string> parts;
  std::string group;
  size_t start = 0;
  for (size_t i = 0; i <= p.size(); ++i) {
    if (i != p.size() &&
        !(i > 0 && i + 1 < p.size() && p[i] == ' ' &&
          std::string_view(".!?").find(p[i - 1]) != std::string_view::npos &&
          std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\"'(")
                  .find(p[i + 1]) != std::string_view::npos))
      continue;
    if (!group.empty())
      group += ' ';
    group += p.substr(start, i - start);
    start = i + 1;
    if (words(group) >= 150) {
      parts.push_back(std::move(group));
      group.clear();
    }
  }
  if (!group.empty()) {
    if (!parts.empty() && words(group) < 40)
      parts.back() += " " + group;
    else
      parts.push_back(group);
  }
  return parts;
}
std::string prefix(const std::string &s, int max) {
  auto u = icu::UnicodeString::fromUTF8(s);
  u.truncate(u.moveIndex32(0, max));
  std::string out;
  u.toUTF8String(out);
  return out;
}
class Zip {
  std::unique_ptr<zip_t, decltype(&zip_discard)> archive{nullptr, zip_discard};

public:
  explicit Zip(const Path &p) {
    int err = 0;
    archive.reset(zip_open(p.c_str(), ZIP_RDONLY, &err));
    if (!archive)
      throw Unsupported("Cannot open WordNet ZIP");
  }
  std::map<std::string, zip_uint64_t> entries() const {
    std::map<std::string, zip_uint64_t> out;
    auto n = zip_get_num_entries(archive.get(), 0);
    for (zip_int64_t i = 0; i < n; ++i) {
      auto *s = zip_get_name(archive.get(), i, 0);
      if (!s || !out.emplace(s, i).second)
        throw Unsupported("Invalid or duplicate ZIP member");
    }
    return out;
  }
  Json json(zip_uint64_t index) const {
    zip_stat_t info{};
    if (zip_stat_index(archive.get(), index, 0, &info) < 0 ||
        info.size > 256u * 1024 * 1024)
      throw Unsupported("Invalid WordNet member size");
    std::unique_ptr<zip_file_t, decltype(&zip_fclose)> file(
        zip_fopen_index(archive.get(), index, 0), zip_fclose);
    if (!file)
      throw Unsupported("Cannot open WordNet member");
    std::string buffer(info.size, '\0');
    size_t done = 0;
    while (done < buffer.size()) {
      auto n =
          zip_fread(file.get(), buffer.data() + done, buffer.size() - done);
      if (n <= 0)
        throw Unsupported("Truncated WordNet member");
      done += n;
    }
    auto result = Json::parse(buffer);
    if (!result.is_object())
      throw Unsupported("Expected a WordNet dictionary");
    return result;
  }
};
} // namespace
std::vector<std::string> split_passage(const std::string& text) {return pieces(text);}
void ingest_documents(const Path &folder, Shard &shard) {
  std::vector<Path> files;
  for (auto &item : fs::recursive_directory_iterator(folder))
    if (item.is_regular_file())
      files.push_back(item.path());
  std::sort(files.begin(), files.end());
  for (auto &p : files) {
    auto ext = key(p.extension().string());
    std::string text;
    if (ext == ".txt" || ext == ".text")
      text = read_file(p, 256 * 1024 * 1024);
    else {
      std::vector<std::string> cmd;
      if (ext == ".pdf")
        cmd = {"pdftotext", "-layout", p.string(), "-"};
      else if (ext == ".md" || ext == ".markdown" || ext == ".rst" ||
               ext == ".html" || ext == ".htm")
        cmd = {"pandoc", "-t", "plain", p.string()};
      else
        continue;
      auto r = Process(cmd, "", 256 * 1024 * 1024).finish(after_ms(300000));
      if (r.exit_code)
        throw Unsupported("Document conversion failed: " + p.string());
      text = std::move(r.out);
    }
    std::istringstream lines(text);
    std::string line, para;
    int64_t id = 0, order = 0;
    auto flush = [&] {
      auto normalized = simplify(para);
      para.clear();
      if (std::count(normalized.begin(), normalized.end(), ' ') < 4)
        return;
      if (!id)
        id = shard.document(p.stem().string(), "file://" + p.string(),
                            "local, private");
      for (auto &piece : pieces(normalized))
        shard.passage(id, order, order ? "body" : "lead", piece);
      ++order;
    };
    while (std::getline(lines, line)) {
      if (simplify(line).empty())
        flush();
      else {
        para += line;
        para += ' ';
      }
    }
    flush();
  }
}
void ingest_wiktionary(const Path &p, Shard &shard) {
  std::ifstream in(p);
  if (!in)
    throw Unsupported("Cannot open Wiktionary input");
  std::string line;
  while (std::getline(in, line)) {
    Json e;
    try {
      e = Json::parse(line);
    } catch (const std::runtime_error &) {
      continue;
    }
    auto word = e["word"].text(), pos = e["pos"].text();
    if (e["lang_code"].text("en") != "en" || word.empty())
      continue;
    std::vector<std::pair<std::string, bool>> senses;
    std::string base;
    for (auto s : e["senses"].items()) {
      auto g = s["glosses"];
      if (!g.size())
        g = s["raw_glosses"];
      if (!g.is_array() || !g.size())
        continue;
      auto links = s["form_of"];
      if (!links.size())
        links = s["alt_of"];
      bool form = links.size() > 0;
      for (auto t : s["tags"].items())
        form |= t.text() == "form-of" || t.text() == "alt-of";
      if (base.empty() && links.is_array() && links.size())
        base = links.at(0)["word"].text();
      senses.emplace_back(clean(g.at(g.size() - 1).text()), form);
      if (senses.size() == 3)
        break;
    }
    if (senses.empty())
      continue;
    auto extra =
        obj({{"etymology", str(prefix(e["etymology_text"].text(), 400))},
             {"form_of", str(base)}});
    auto id = shard.document(
        word, "https://en.wiktionary.org/wiki/" + replace(word, " ", "_"),
        "CC BY-SA 4.0", pos, canonical(extra));
    int order = 0;
    for (auto &[s, form] : senses)
      shard.passage(id, order++, form ? "form" : "definition",
                    word + " (" + pos + "): " + s);
  }
  if (in.bad())
    throw Unsupported("Wiktionary input read failed");
}
void ingest_wordnet(const Path &p, Shard &shard) {
  Zip file(p);
  auto names = file.entries();
  auto synsets = Json::object();
  for (auto &[path, i] : names) {
    auto f = fs::path(path).filename().string();
    if (f.ends_with(".json") && !f.starts_with("entries") && f != "frames.json")
      for (auto &[key, v] : file.json(i).members())
        synsets.set(key, v);
  }
  const std::map<std::string, std::string> labels{{"n", "noun"},
                                                  {"v", "verb"},
                                                  {"a", "adjective"},
                                                  {"s", "adjective"},
                                                  {"r", "adverb"}};
  for (auto &[path, i] : names) {
    if (!fs::path(path).filename().string().starts_with("entries"))
      continue;
    for (auto &[lemma, parts] : file.json(i).members())
      for (auto &[pos, entry] : parts.members()) {
        auto senses = entry["sense"];
        if (!senses.size())
          continue;
        auto word = replace(lemma, "_", " "),
             tag = labels.contains(pos) ? labels.at(pos) : pos;
        auto id = shard.document(word, "https://en-word.net/lemma/" + lemma,
                                 "CC BY 4.0", tag);
        for (size_t j = 0; j < std::min<size_t>(4, senses.size()); ++j) {
          auto syn = synsets[senses.at(j)["synset"].text()],
               defs = syn["definition"];
          if (!defs.size())
            continue;
          auto text = word + " (" + tag + "): " + defs.at(0).text();
          auto ex = syn["example"];
          if (ex.size()) {
            auto e = ex.at(0);
            text +=
                ". Example: " + (e.is_string() ? e.text() : e["text"].text());
          }
          int n = 0;
          for (auto m : syn["members"].items())
            if (m.text() != lemma) {
              text += (n ? ", " : ". Also: ") + replace(m.text(), "_", " ");
              if (++n == 4)
                break;
            }
          shard.passage(id, j, "definition", text + ".");
        }
      }
  }
}
} // namespace unllm::maintenance
