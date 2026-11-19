#include "ir.h"
#include <charconv>
#include <set>
namespace unllm::algorithm {
using native::fields_only;
using native::Unsupported;
namespace {
void require(bool condition, const std::string &why) {
  if (!condition)
    throw Unsupported(why);
}
Type type(const Json &j) {
  if (j.text() == "int")
    return Type::integer;
  if (j.text() == "bool")
    return Type::boolean;
  if (j.text() == "ints")
    return Type::sequence;
  throw Unsupported("Expected int, bool or ints");
}
std::string name(const Json &j) {
  auto s = j.text();
  require(!s.empty() && s.size() <= 48 &&
              s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQR"
                                  "STUVWXYZ0123456789_") == s.npos,
          "Invalid identifier");
  return s;
}
// Integer literals and arguments travel as decimal strings. This prevents a
// JSON reader from saturating an out-of-range number before it can be checked.
int64_t integer(const Json &j) {
  auto s = j.text();
  int64_t n = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
  require(ec == std::errc{} && p == s.data() + s.size(),
          "Expected signed int64 decimal string");
  return n;
}
struct Decoder {
  Program program;
  std::map<std::string, int> helpers, scope;
  Function f{};
  int nodes = 0;
  void count() { require(++nodes <= 80, "80-node budget exceeded"); }
  int bind(const Json &j, Type t) {
    auto n = name(j);
    int slot = f.slots.size();
    require(scope.emplace(n, slot).second, "Duplicate variable " + n);
    f.slots.push_back(t);
    f.names.push_back(n);
    return slot;
  }
  int lookup(const Json &j) {
    auto n = name(j);
    require(scope.contains(n), "Unknown variable " + n);
    return scope.at(n);
  }
  Node expression(const Json &j) {
    count();
    require(j.is_array() && j.size() > 0, "Expected expression array");
    auto op = j.at(0).text();
    auto arity = [&](size_t n) {
      require(j.size() == n + 1, "Wrong arity for " + op);
    };
    if (op == "int") {
      arity(1);
      return {Op::literal, Type::integer, -1, integer(j.at(1)), {}, {}, {}};
    }
    if (op == "bool") {
      arity(1);
      require(j.at(1).is_boolean(), "Invalid boolean");
      return {Op::literal, Type::boolean, -1, j.at(1).flag(), {}, {}, {}};
    }
    if (op == "var") {
      arity(1);
      int slot = lookup(j.at(1));
      return {Op::read, f.slots[slot], slot, 0, {}, {}, {}};
    }
    if (op == "empty") {
      arity(0);
      return {Op::empty, Type::sequence, -1, 0, {}, {}, {}};
    }
    Node n{};
    if (op == "call") {
      require(j.size() >= 2, "Missing callee");
      auto h = name(j.at(1));
      require(helpers.contains(h),
              "Calls must target a previously defined helper");
      n.slot = helpers.at(h);
      auto &callee = program.functions[n.slot];
      arity(callee.parameters + 1);
      n.op = Op::call;
      n.type = callee.result;
      for (size_t i = 0; i < callee.parameters; ++i)
        n.operands.push_back(typed(j.at(i + 2), callee.slots[i]));
      return n;
    }
    static const std::map<std::string, Op> operators{
        {"add", Op::add},  {"sub", Op::sub},      {"mul", Op::mul},
        {"lt", Op::less},  {"eq", Op::equal},     {"len", Op::length},
        {"at", Op::index}, {"append", Op::append}};
    require(operators.contains(op), "Unsupported expression " + op);
    n.op = operators.at(op);
    arity(n.op == Op::length ? 1 : 2);
    bool seq = n.op == Op::length || n.op == Op::index || n.op == Op::append;
    n.operands.push_back(typed(j.at(1), seq ? Type::sequence : Type::integer));
    if (j.size() == 3)
      n.operands.push_back(typed(j.at(2), Type::integer));
    n.type = n.op == Op::less || n.op == Op::equal ? Type::boolean
             : n.op == Op::append                  ? Type::sequence
                                                   : Type::integer;
    return n;
  }
  Node typed(const Json &j, Type t) {
    auto n = expression(j);
    require(n.type == t, "Expression type mismatch");
    return n;
  }
  std::vector<Node> block(const Json &j, int depth) {
    require(j.is_array(), "Expected statement list");
    std::vector<Node> body;
    for (auto s : j.items()) {
      count();
      require(s.is_array() && s.size() >= 3, "Invalid statement");
      Node n{};
      auto op = s.at(0).text();
      if (op == "set") {
        require(s.size() == 3, "Invalid assignment");
        n.op = Op::assign;
        n.slot = lookup(s.at(1));
        n.operands.push_back(typed(s.at(2), f.slots[n.slot]));
      } else if (op == "if") {
        require(s.size() == 4, "Expected both branches");
        n.op = Op::branch;
        n.operands.push_back(typed(s.at(1), Type::boolean));
        n.body = block(s.at(2), depth);
        n.otherwise = block(s.at(3), depth);
      } else {
        require((op == "each" || op == "range") && s.size() == 4 && depth < 2,
                "Unknown statement or loop nesting exceeded");
        n.op = op == "each" ? Op::each : Op::range;
        n.operands.push_back(
            typed(s.at(2), n.op == Op::each ? Type::sequence : Type::integer));
        n.slot = bind(s.at(1), Type::integer);
        n.body = block(s.at(3), depth + 1);
        scope.erase(name(s.at(1)));
      }
      body.push_back(std::move(n));
    }
    return body;
  }
  void function(const Json &j) {
    fields_only(j, {"name", "params", "locals", "result", "body", "return"});
    scope.clear();
    f = Function{};
    f.name = name(j["name"]);
    f.result = type(j["result"]);
    for (auto field : {"params", "locals"}) {
      auto rows = j[field];
      bool params = std::string_view(field) == "params";
      require(rows.is_array() && rows.size() <= 8,
              "Expected up to eight bindings per group");
      for (auto row : rows.items()) {
        require(row.is_array() && row.size() == (params ? 2 : 3),
                "Invalid binding");
        auto t = type(row.at(1));
        Node n{};
        if (!params) {
          count();
          n.operands.push_back(typed(row.at(2), t));
        }
        n.slot = bind(row.at(0), t);
        if (params)
          ++f.parameters;
        else {
          n.op = Op::assign;
          f.body.push_back(std::move(n));
        }
      }
    }
    auto body = block(j["body"], 0);
    f.body.insert(f.body.end(), body.begin(), body.end());
    f.output = typed(j["return"], f.result);
    require(f.slots.size() - f.parameters <= 8, "Eight-local budget exceeded");
    require(helpers.emplace(f.name, program.functions.size()).second,
            "Duplicate function");
    program.functions.push_back(std::move(f));
  }
};
} // namespace
Program decode(const Json &spec) {
  fields_only(spec, {"version", "functions"});
  require(spec["version"].is_integer() && spec["version"].integer() == 1,
          "Unknown IR version");
  auto rows = spec["functions"];
  require(rows.is_array() && rows.size() >= 1 && rows.size() <= 5,
          "Expected an entry function and up to four helpers");
  Decoder d;
  for (auto row : rows.items())
    d.function(row);
  return std::move(d.program);
}
Value argument(const Json &j, Type t) {
  if (t == Type::integer)
    return integer(j);
  if (t == Type::boolean) {
    require(j.is_boolean(), "Expected bool");
    return j.flag();
  }
  require(j.is_array() && j.size() <= 64, "sequence bound");
  Sequence s;
  for (auto v : j.items())
    s.push_back(integer(v));
  return s;
}
Json json(const Value &v) {
  if (auto p = std::get_if<int64_t>(&v))
    return Json::string(std::to_string(*p));
  if (auto p = std::get_if<bool>(&v))
    return Json::boolean(*p);
  auto a = Json::array();
  for (auto n : std::get<Sequence>(v))
    a.push(Json::string(std::to_string(n)));
  return a;
}
} // namespace unllm::algorithm
