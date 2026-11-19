#pragma once
#include "gui_json.h"
#include "unllm/symbolic/grounding.h"
#include <set>
namespace unllm::native::frames {
using Terms = std::set<std::string>;
inline bool any(const Terms &terms,
                std::initializer_list<std::string> choices) {
  for (auto &x : choices)
    if (terms.contains(x))
      return true;
  return false;
}
inline std::string predicate(const Json &e) { return e["predicate"].text(); }
inline std::string arg(const Json &e, const std::string &n) {
  return e["args"][n].text();
}
inline std::string word(const Json &e) {
  if (e.contains("lexical_concept"))
    return e["lexical_concept"].text();
  if (predicate(e) == "named")
    return key(e["carg"].text());
  auto w = lemma(predicate(e));
  return w.substr(0, w.find('/'));
}
inline bool in(const Json &a, const std::string &s) {
  for (auto v : a.items())
    if (v.text() == s)
      return true;
  return false;
}
struct Graph {
  const Json eps;
  Terms words;
  std::map<std::string, Terms> nouns, edges;
  explicit Graph(const Json &analysis) : eps(analysis) {
    for (auto e : eps.items()) {
      auto p = predicate(e), w = word(e), var = arg(e, "ARG0");
      words.insert(w);
      if (p.find("_n_") != p.npos || p == "named")
        nouns[var].insert(w);
      auto edge = [&](const std::string &a, const std::string &b) {
        if (!a.empty() && !b.empty() && a != b)
          edges[a].insert(b);
      };
      if (p == "compound")
        edge(arg(e, "ARG1"), arg(e, "ARG2"));
      if (p == "_and_c" || p == "implicit_conj" || p == "appos")
        for (auto k : {"ARG1", "ARG2"})
          edge(p == "appos" ? arg(e, "ARG1") : var, arg(e, k));
    }
  }
  Terms terms(const std::string &var) const {
    Terms seen, out;
    std::vector<std::string> todo{var};
    while (!todo.empty()) {
      auto n = todo.back();
      todo.pop_back();
      if (!seen.insert(n).second)
        continue;
      if (nouns.contains(n))
        out.insert(nouns.at(n).begin(), nouns.at(n).end());
      if (edges.contains(n))
        todo.insert(todo.end(), edges.at(n).begin(), edges.at(n).end());
    }
    return out;
  }
};
Json stream_spec(const Json &eps, const Json &vocab);
Json calculator_spec(const Json &eps, const Json &vocab);
} // namespace unllm::native::frames
