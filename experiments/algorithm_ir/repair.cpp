#include "repair.h"
namespace unllm::algorithm {
namespace {
using Visit = std::function<bool(const Json &, const std::string &)>;
Json replace(const Json &a, size_t position, const Json &value) {
  auto r = Json::array();
  size_t i = 0;
  for (auto x : a.items())
    r.push(i++ == position ? value : x);
  return r;
}
Json list(std::initializer_list<Json> nodes) {
  auto r = Json::array();
  for (auto n : nodes)
    r.push(n);
  return r;
}
bool walk(const Json &j, const std::string &path, const Visit &visit,
          std::string iterator = {}) {
  if (!j.is_array() || !j.size())
    return false;
  auto op = j.at(0).text();
  auto w = Json::string;
  if (j.size() == 3 &&
      (op == "add" || op == "sub" || op == "mul" || op == "lt" || op == "eq")) {
    bool predicate = op == "lt" || op == "eq";
    for (std::string other : {"add", "sub", "mul", "lt", "eq"}) {
      if (other != op && predicate == (other == "lt" || other == "eq") &&
          visit(replace(j, 0, w(other)), path + "/0"))
        return true;
    }
    if (visit(list({j.at(0), j.at(2), j.at(1)}), path))
      return true;
  }
  if (op == "int" && j.size() == 2 && j.at(1).is_string())
    for (std::string n : {"0", "1", "-1", "2"})
      if (n != j.at(1).text() && visit(replace(j, 1, w(n)), path + "/1"))
        return true;
  if (op == "at" && j.size() == 3) {
    auto index = j.at(2);
    if (index.is_array() && index.size() == 3 &&
        (index.at(0).text() == "sub" || index.at(0).text() == "add"))
      if (visit(replace(j, 2, index.at(1)), path + "/2"))
        return true;
    for (std::string change : {"sub", "add"})
      if (visit(
              replace(j, 2, list({w(change), index, list({w("int"), w("1")})})),
              path + "/2"))
        return true;
  }
  if (op == "append" && j.size() == 3 && visit(j.at(1), path))
    return true;
  if (op == "if" && j.size() == 4 &&
      visit(list({w("if"), j.at(1), j.at(3), j.at(2)}), path))
    return true;
  if (op == "set" && j.size() == 3 && !iterator.empty()) {
    auto zero = list({w("int"), w("0")}), item = list({w("var"), w(iterator)});
    for (auto cond : {list({w("lt"), zero, item}), list({w("lt"), item, zero}),
                      list({w("eq"), item, zero})})
      if (visit(list({w("if"), cond, list({j}), Json::array()}), path))
        return true;
  }
  for (size_t i = 0; i < j.size(); ++i) {
    auto child_iterator =
        (op == "each" || op == "range") && i == 3 ? j.at(1).text() : iterator;
    if (walk(
            j.at(i), path + "/" + std::to_string(i),
            [&](const Json &child, const std::string &site) {
              return visit(replace(j, i, child), site);
            },
            child_iterator))
      return true;
  }
  return false;
}
} // namespace
bool visit_edits(const Json &tree, const Visit &visit) {
  auto functions = tree["functions"];
  for (size_t i = 0; i < functions.size(); ++i) {
    auto f = functions.at(i);
    for (auto key : {"locals", "body", "return"}) {
      auto values = f[key];
      auto offer = [&](const Json &replacement, const std::string &site) {
        auto updated = f.clone();
        updated.set(key, replacement);
        auto program = tree.clone();
        program.set("functions", replace(functions, i, updated));
        return visit(program, site);
      };
      auto path = "/functions/" + std::to_string(i) + "/" + key;
      if (std::string_view(key) == "locals") {
        for (size_t j = 0; j < values.size(); ++j) {
          auto local = values.at(j);
          if (walk(local.at(2), path + "/" + std::to_string(j) + "/2",
                   [&](const Json &child, const std::string &site) {
                     return offer(replace(values, j, replace(local, 2, child)),
                                  site);
                   }))
            return true;
        }
      } else if (walk(values, path, offer))
        return true;
    }
  }
  return false;
}
} // namespace unllm::algorithm
