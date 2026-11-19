#pragma once
#include "unllm/symbolic/mrs.h"
#include <algorithm>
#include <functional>

namespace unllm::native::diagnostics {
// Group variables only by label-independent evidence. Usually their roles and
// spans distinguish them all. Minimize over ties so input order cannot select
// a different identity. The complete relabeled graph is what gets hashed.
inline std::string identity(const Mrs &m) {
  std::map<std::string, std::vector<std::string>> occurrences;
  for (const auto &ep : m.rels) {
    const auto anchor = ep.span.dump() + "/" + ep.predicate + "/";
    occurrences[ep.label].push_back(anchor + "LBL");
    for (const auto &[role, v] : ep.args)
      if (role != "CARG") occurrences[v].push_back(anchor + role);
  }
  if (!m.top.empty()) occurrences[m.top].push_back("TOP");
  if (!m.index.empty()) occurrences[m.index].push_back("INDEX");
  std::map<std::vector<std::string>, std::vector<std::string>> classes;
  for (const auto &[v, properties] : m.variables) {
    auto evidence = occurrences[v];
    std::sort(evidence.begin(), evidence.end());
    evidence.push_back(v.substr(0, 1));
    for (const auto &[key, value] : properties) {
      evidence.push_back(key);
      evidence.push_back(value);
    }
    classes[evidence].push_back(v);
  }
  std::map<std::string, std::string> names;
  std::vector<std::vector<std::string>> ties;
  size_t next = 0;
  for (const auto &[_, group] : classes) {
    for (const auto &v : group) names[v] = v.substr(0, 1) + std::to_string(next++);
    if (group.size() > 1) ties.push_back(group);
  }
  auto representation = [&]() {
    Mrs g = m;
    if (!g.top.empty()) g.top = names.at(g.top);
    if (!g.index.empty()) g.index = names.at(g.index);
    g.variables.clear();
    for (auto &[v, props] : m.variables) g.variables[names.at(v)] = props;
    for (auto &ep : g.rels) {
      ep.label = names.at(ep.label);
      for (auto &[role, v] : ep.args)
        if (role != "CARG") v = names.at(v);
    }
    auto constraints = [&](const Json &items) {
      auto result = items.clone();
      for (auto item : result.items())
        for (auto &[role, v] : item.members())
          if (role != "relation") item.set(role, Json::string(names.at(v.text())));
      return result;
    };
    g.hcons = constraints(m.hcons);
    g.icons = constraints(m.icons);
    auto graph = g.data();
    for (auto key : {"rels", "hcons", "icons"}) {
      std::vector<std::string> sorted;
      for (auto item : graph[key].items()) sorted.push_back(canonical(item));
      std::sort(sorted.begin(), sorted.end());
      auto rows = Json::array();
      for (const auto &row : sorted) rows.push(Json::string(row));
      graph.set(key, rows);
    }
    return canonical(graph);
  };
  size_t tried = 0;
  std::string best;
  std::function<void(size_t)> visit = [&](size_t i) {
    if (i == ties.size()) {
      if (++tried > 1024) throw ResourceLimit("MRS diagnostic identity permutation limit");
      auto value = representation();
      if (best.empty() || value < best) best = std::move(value);
      return;
    }
    std::vector<std::string> ids;
    for (const auto &v : ties[i]) ids.push_back(names.at(v));
    std::sort(ids.begin(), ids.end());
    do {
      for (size_t j = 0; j < ids.size(); ++j) names[ties[i][j]] = ids[j];
      visit(i + 1);
    } while (std::next_permutation(ids.begin(), ids.end()));
  };
  visit(0);
  return sha256(best);
}
} // namespace unllm::native::diagnostics
