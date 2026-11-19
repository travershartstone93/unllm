#include "gui_json.h"
#include "unllm/symbolic/behaviour_solver.h"
#include "unllm/symbolic/calculator.h"
#include "unllm/symbolic/expression.h"
namespace unllm::native {
using namespace gui;
namespace {
Json method_binding(const std::string &method, Json args = Json::array()) {
  return obj({{"method", str(method)}, {"args", args}});
}
Json variable(const std::string &name) {
  return arr({str("local"), str(name)});
}
Json memory_term(const BehaviourValue &v) {
  if (v.kind == "literal")
    return v.value;
  if (v.kind == "state")
    return v.name == "operand" ? variable("operand")
                               : arr({str("state"), str("memory")});
  auto out = arr({str(v.name)});
  for (auto &a : v.args)
    out.push(memory_term(a));
  return out;
}
Json scalar_term(const Expr &v) {
  if (v.op == "variable")
    return variable("right");
  if (v.op == "constant")
    return v.value;
  auto out = arr({str(v.op)});
  for (auto &a : v.args)
    out.push(scalar_term(a));
  return out;
}
int budget(Deadline deadline) {
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
                .count();
  if (ms <= 0)
    throw ResourceLimit("Calculator synthesis timed out");
  return std::min<int64_t>(2000, ms);
}
const std::map<std::string, std::string> symbols{
    {"add", "+"}, {"subtract", "-"}, {"multiply", "*"}, {"divide", "/"}};
} // namespace
Json construct_calculator(const CalculatorSpec &spec, const Json &plan,
                          const std::filesystem::path &root,
                          Deadline deadline) {
  auto selected = plan["providers"], functions = Json::object(),
       solver = Json::array(), widgets = Json::array(), keys = Json::object();
  if (!selected.contains("app:interactive_expression"))
    throw Unsupported("Missing interactive expression provider");
  auto methods = Json::parse(read_file(root / "data/calculator_methods.json"));
  for (auto &[name, method] : methods.members())
    if (!method.contains("requires") ||
        selected.contains(method["requires"].text()))
      functions.set(name, method);
  auto state = obj({{"entry", str("")},
                    {"after_result", boolean(false)},
                    {"error", str("")},
                    {"last_value", num(0)}});
  if (spec.memory) {
    state.set("memory", num(0));
    auto schema = Json::parse(
        R"({"state":{"memory":{"type":"number","initial":0},"operand":{"type":"number","initial":0}},"actions":[]})");
    for (std::string op : {"add", "subtract"}) {
      schema["actions"].push(
          obj({{"id", str(op)},
               {"ensure", str("next.memory == prev.memory " + symbols.at(op) +
                              " prev.operand")}}));
    }
    auto transition = BehaviourSpec::from(schema);
    for (std::string op : {"add", "subtract"}) {
      auto solved = synthesize_transition(transition, op, budget(deadline));
      auto body = Json::parse(
          R"([["let","operand",["invoke","current_value"]],["set","memory",["checked_number"]],["set","error",""]])");
      body.at(1).at(2).push(memory_term(solved.updates.at("memory")));
      auto recover = Json::parse(
          R"([["set","error",["local","problem"]],["set","after_result",false]])");
      functions.set("memory_" + op,
                    obj({{"result", str("void")},
                         {"parameters", Json::array()},
                         {"body", arr({arr({str("try"), body, recover})})}}));
      solver.push(solved.evidence);
    }
  }
  auto rules = Json::array();
  for (auto &op : spec.operations) {
    if (!selected.contains("arithmetic:" + op))
      throw Unsupported("Unplanned arithmetic operation");
    auto steps = Json::array();
    if (op == "divide") {
      auto solved = synthesize_expression(expression(str("not (x == 0)")), true,
                                          budget(deadline));
      solver.push(solved.evidence);
      steps.push(
          arr({str("if"), arr({str("not"), scalar_term(solved.expression)}),
               arr({arr({str("raise"), str("Division by zero")})})}));
    }
    steps.push(arr({str("return"),
                    arr({str("checked_number"), arr({str(op), variable("left"),
                                                     variable("right")})})}));
    rules.push(arr(
        {str("if"),
         arr({str("eq"), variable("operation"), str(symbols.at(op))}), steps}));
  }
  rules.push(arr({str("raise"), str("Operation is not enabled")}));
  auto operation = obj(
      {{"result", str("number")},
       {"parameters",
        Json::parse(
            R"([["operation","text"],["left","number"],["right","number"]])")},
       {"body", rules}});
  int row = 0;
  auto widget = [&](std::string kind, std::string id, std::string label, int r,
                    int c, const Json &binding, int span = 1) {
    widgets.push(obj({{"kind", str(kind)},
                      {"id", str(id)},
                      {"label", str(label)},
                      {"row", Json::number(r)},
                      {"column", Json::number(c)},
                      {"span", Json::number(span)},
                      {"binding", binding}}));
  };
  auto button = [&](const std::string &id, const std::string &label, int r,
                    int c, Json binding) {
    widget("button", id, label, r, c, binding);
  };
  auto append = [](const std::string &s, bool fresh) {
    return method_binding("append", arr({str(s), boolean(fresh)}));
  };
  widget("display", "display", "", row++, 0, method_binding("render"), 4);
  if (spec.memory) {
    widget("label", "memory_display", "", row++, 0, method_binding("memory_display"), 4);
    int col = 0;
    for (auto &[name, label] :
         std::vector<std::pair<std::string, std::string>>{{"clear", "MC"},
                                                          {"recall", "MR"},
                                                          {"add", "M+"},
                                                          {"subtract", "M-"}})
      button("memory_" + name, label, row, col++, method_binding("memory_" + name));
    ++row;
  }
  button("clear", "C", row, 0, method_binding("clear"));
  if (spec.backspace)
    button("backspace", "⌫", row, 1, method_binding("backspace"));
  if (spec.parentheses) {
    button("lparen", "(", row, 2, append("(", true));
    button("rparen", ")", row, 3, append(")", false));
  }
  ++row;
  for (int i = 0; i < 10; ++i) {
    auto digit = std::to_string(i);
    button("digit_" + digit, digit, row + (i ? 2 - (i - 1) / 3 : 3),
           i ? (i - 1) % 3 : 0, append(digit, true));
    if (spec.keyboard)
      keys.set(digit, append(digit, true));
  }
  button("decimal", ".", row + 3, 1, append(".", true));
  button("evaluate", "=", row + 3, 2, method_binding("evaluate"));
  const std::map<std::string, std::pair<int, std::string>> layout{
      {"divide", {0, "÷"}},
      {"multiply", {1, "×"}},
      {"subtract", {2, "−"}},
      {"add", {3, "+"}}};
  for (auto &op : spec.operations) {
    auto &[offset, label] = layout.at(op);
    button(op, label, row + offset, 3, append(symbols.at(op), false));
  }
  if (spec.keyboard) {
    for (auto &[op, symbol] : symbols)
      keys.set(symbol, append(symbol, false));
    keys.set(".", append(".", true));
    if (spec.parentheses) {
      keys.set("(", append("(", true));
      keys.set(")", append(")", false));
    }
    for (auto k : {"Return", "Enter", "="})
      keys.set(k, method_binding("evaluate"));
    for (auto k : {"Escape", "Delete", "c", "C"})
      keys.set(k, method_binding("clear"));
    if (spec.backspace)
      keys.set("Backspace", method_binding("backspace"));
  }
  return obj({{"state", state},
              {"methods", functions},
              {"operator", operation},
              {"widgets", widgets},
              {"keys", keys},
              {"spec", spec.data()},
              {"solver", solver}});
}
} // namespace unllm::native
