// Resolve a Python target compiler's module symbol graph for one retrieved
// definition.
#include "unllm/symbolic/common.h"
#include <algorithm>
#include <iostream>
#include <set>
using unllm::native::Json;
namespace {
struct Closure {
  Json symbols;
  std::set<std::string> seen;
  std::vector<std::pair<int, std::string>> ordered;
  void visit(const std::string &name) {
    if (!seen.insert(name).second)
      return; // Recursive functions resolve at call time.
    if (seen.size() > 32)
      throw std::runtime_error("closure exceeds 32 names");
    if (!symbols.contains(name))
      throw std::runtime_error("unresolved: " + name);
    auto def = symbols[name];
    auto kind = def["kind"].text();
    if (kind == "import") {
      static const std::set<std::string> allowed{
          "math",       "itertools", "functools", "collections", "operator",
          "statistics", "bisect",    "heapq",     "re",          "string",
          "typing",     "fractions", "decimal"};
      if (!allowed.contains(def["module"].text()))
        throw std::runtime_error("unsupported import: " + name);
    } else if (kind != "function")
      throw std::runtime_error("unsupported or ambiguous: " + name);
    for (auto dep : def["refs"].items())
      visit(dep.text());
    ordered.emplace_back(def["line"].integer(), name);
  }
};
} // namespace
int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    auto result = Json::object();
    try {
      auto q = Json::parse(line);
      Closure closure{q["symbols"], {}, {}};
      closure.visit(q["root"].text());
      std::sort(closure.ordered.begin(), closure.ordered.end());
      auto names = Json::array();
      for (auto &[number, name] : closure.ordered)
        names.push(Json::string(name));
      result.set("names", names);
    } catch (const std::exception &e) {
      result.set("error", Json::string(e.what()));
    }
    std::cout << result.dump() << '\n';
  }
}
