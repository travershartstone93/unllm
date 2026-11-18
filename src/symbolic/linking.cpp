#include "unllm/symbolic/linking.h"
#include "unllm/symbolic/semantics.h"
#include <unicode/uchar.h>
#include <unicode/utf8.h>
namespace unllm::native {
std::vector<Word> words(const std::string &text) {
  if (text.size() > 16 * 1024 * 1024)
    throw ResourceLimit("Text tokenization limit exceeded");
  struct Point {
    UChar32 cp;
    size_t begin, end;
  };
  std::vector<Point> points;
  for (int32_t i = 0; i < static_cast<int32_t>(text.size());) {
    auto start = i;
    UChar32 cp;
    U8_NEXT(text.data(), i, static_cast<int32_t>(text.size()), cp);
    if (cp < 0)
      throw Unsupported("Invalid UTF-8");
    points.push_back({cp, static_cast<size_t>(start), static_cast<size_t>(i)});
  }
  auto alnum = [](UChar32 cp) {
    auto type = u_charType(cp);
    return (type >= U_UPPERCASE_LETTER && type <= U_OTHER_LETTER) ||
           (type >= U_DECIMAL_DIGIT_NUMBER && type <= U_OTHER_NUMBER);
  };
  std::vector<Word> result;
  for (size_t i = 0; i < points.size();) {
    if (!alnum(points[i].cp)) {
      ++i;
      continue;
    }
    auto start = i++;
    while (i < points.size() &&
           (alnum(points[i].cp) || points[i].cp == '\'' || points[i].cp == '-'))
      ++i;
    auto begin = points[start].begin, end = points[i - 1].end;
    result.push_back({key(text.substr(begin, end - begin)), begin, end});
  }
  return result;
}
LinkedText link_entities(const std::string &text,
                         const std::vector<std::string> &aliases) {
  struct Node {
    std::map<std::string, size_t> edges;
    bool terminal = false;
  };
  std::vector<Node> trie(1);
  for (auto &alias : aliases) {
    auto ts = words(alias);
    if (ts.empty() ||
        (ts.size() == 1 && type_words().contains(ts[0].normalized)))
      continue;
    size_t node = 0;
    for (auto &t : ts) {
      auto it = trie[node].edges.find(t.normalized);
      if (it == trie[node].edges.end()) {
        size_t next = trie.size();
        trie[node].edges[t.normalized] = next;
        trie.emplace_back();
        node = next;
      } else
        node = it->second;
    }
    trie[node].terminal = true;
  }
  auto tokens = words(text);
  LinkedText out;
  size_t cursor = 0;
  for (size_t i = 0; i < tokens.size();) {
    size_t node = 0, end = i, best = i;
    while (end < tokens.size() &&
           trie[node].edges.contains(tokens[end].normalized)) {
      if (end > i && !key(text.substr(tokens[end - 1].end,
                                      tokens[end].begin - tokens[end - 1].end))
                          .empty())
        break;
      node = trie[node].edges.at(tokens[end].normalized);
      ++end;
      if (trie[node].terminal)
        best = end;
    }
    if (best == i) {
      ++i;
      continue;
    }
    auto start = tokens[i].begin, finish = tokens[best - 1].end;
    int64_t n = out.names.size();
    std::string suffix;
    do {
      suffix = char('a' + n % 26) + suffix;
      n = n / 26 - 1;
    } while (n >= 0);
    auto placeholder = "Zzunllmname" + suffix;
    out.names[key(placeholder)] = text.substr(start, finish - start);
    out.text += text.substr(cursor, start - cursor) + placeholder;
    cursor = finish;
    i = best;
  }
  out.text += text.substr(cursor);
  return out;
}
} // namespace unllm::native
