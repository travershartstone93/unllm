#include "calculator_cases.h"
#include "gui_json.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <map>
#include <random>
namespace unllm::native {
using namespace gui;
namespace {
const std::map<std::string, std::string> operators{
    {"add", "+"}, {"subtract", "-"}, {"multiply", "*"}, {"divide", "/"}};
std::string display(double v, int precision) {
  char buffer[64];
  auto res = std::to_chars(buffer, buffer + sizeof buffer, v == 0 ? 0. : v,
                           std::chars_format::general, precision);
  if (res.ec != std::errc{})
    throw Unsupported("Cannot format expected result");
  return {buffer, res.ptr};
}
double apply(char op, double a, double b) {
  switch (op) {
  case '+':
    return a + b;
  case '-':
    return a - b;
  case '*':
    return a * b;
  case '/':
    return a / b;
  }
  throw Unsupported("Unknown arithmetic operation");
}
struct Example {
  std::string text;
  Json value;
};
Json event(const std::string &type, const std::string &name) {
  return obj({{type, str(name)}});
}
Json buttons(std::initializer_list<std::string> ids) {
  auto out = Json::array();
  for (auto &id : ids)
    out.push(event("button", id));
  return out;
}
Json typed(const std::string &s) {
  return arr({event("text", s), event("key", "Return")});
}
} // namespace
CalculatorCases calculator_cases(const CalculatorSpec &spec) {
  CalculatorCases c;
  auto enabled = [&](const std::string &op) {
    return std::find(spec.operations.begin(), spec.operations.end(), op) !=
           spec.operations.end();
  };
  auto add = [&](std::string expr, Json v = {}) {
    c.inputs.push(str(expr));
    c.values.push(v);
  };
  for (auto &[s, v] :
       std::vector<std::pair<std::string, double>>{{"0", 0},
                                                   {"12", 12},
                                                   {"0005", 5},
                                                   {".5", .5},
                                                   {"2.", 2},
                                                   {"1e-500", 0},
                                                   {"1e15", 1e15},
                                                   {"-.25", -.25},
                                                   {"-+4", -4}})
    add(s, num(v));
  for (auto invalid : {"1e16", "1+", "", "1//2", "2**3", "1_000", "true",
                       "0x10", "1 2", ")(", "1..2", "one", "1e", "\"5\""})
    add(invalid);
  add(std::string(33, '(') + "1" + std::string(33, ')'));
  add(std::string(33, '-') + "1");
  add(std::string(1025, '9'));
  std::string long_chain = "1";
  for (int i = 0; i < 150; ++i)
    long_chain += "+1";
  add(long_chain);
  for (auto &[op, symbol] : operators) {
    add("12" + symbol + "3",
        enabled(op) ? num(apply(symbol[0], 12, 3)) : Json());
    add("(" + std::string("12") + symbol + "3)",
        enabled(op) && spec.parentheses ? num(apply(symbol[0], 12, 3))
                                        : Json());
  }
  add("2+3*4", enabled("add") && enabled("multiply") ? num(14) : Json());
  add("(2+3)*4", enabled("add") && enabled("multiply") && spec.parentheses
                     ? num(20)
                     : Json());
  add("1/0");
  add("1/(2-2)");
  add("1e15/0.01");
  // Generate the expression tree and its value together, independently of the
  // target parser.
  std::mt19937 random(1291);
  std::function<Example(int)> tree = [&](int depth) -> Example {
    if (!depth) {
      int n = int(random() % 501) - 250;
      return {display(n / 4., 10), num(n / 4.)};
    }
    auto left = tree(depth - 1), right = tree(depth - 1);
    auto op = spec.operations[random() % spec.operations.size()];
    char symbol = operators.at(op)[0];
    double v = left.value.null() || right.value.null()
                   ? NAN
                   : apply(symbol, left.value.real(), right.value.real());
    auto src = left.text + symbol + right.text;
    if (spec.parentheses)
      src = "(" + src + ")";
    return {src, std::isfinite(v) && std::abs(v) <= 1e15 ? num(v) : Json()};
  };
  for (int i = 0; i < 72; ++i) {
    auto e = tree(spec.parentheses ? 1 + i % 3 : 1);
    add(e.text, e.value);
  }
  auto scenario = [&](std::string name, Json events, std::string expected,
                      Json memory = {}) {
    c.scenarios.push(obj({{"name", str(name)},
                          {"events", events},
                          {"expected", str(expected)},
                          {"memory", memory}}));
  };
  auto op = spec.operations.front(), sym = operators.at(op);
  auto calc = [&](double a, double b) { return apply(sym[0], a, b); };
  auto shown = [&](double x) { return display(x, spec.precision); };
  scenario("buttons",
           buttons({"digit_1", "digit_2", op, "digit_3", "evaluate"}),
           shown(calc(12, 3)));
  scenario("clear", buttons({"digit_7", "clear"}), "0");
  scenario("decimal",
           buttons({"decimal", "digit_5", op, "digit_2", "evaluate"}),
           shown(calc(.5, 2)));
  if (spec.keyboard) {
    scenario("keyboard", typed("7" + sym + "2"), shown(calc(7, 2)));
    scenario("keyboard_clear",
             arr({event("text", "123"), event("key", "Escape")}), "0");
    auto edited = typed("2" + sym + "3");
    edited.push(event("text", "4"));
    scenario("fresh_after_equals", edited, "4");
    auto continued = typed("2" + sym + "3");
    continued.push(event("text", sym + "2"));
    continued.push(event("key", "Return"));
    scenario("continue_after_equals", continued, shown(calc(calc(2, 3), 2)));
    scenario("malformed", typed("1..2"), "Error: ");
    auto recovery = typed("1..2");
    recovery.push(event("text", "3"));
    scenario("recover", recovery, "3");
    scenario("leading_zero", typed("0005"), "5");
    scenario("budget_recovery",
             arr({event("text", std::string(1025, '9')), event("key", "Escape"),
                  event("text", "4")}),
             "4");
    if (spec.parentheses)
      scenario("parentheses", typed("(2" + sym + "3)" + sym + "4"),
               shown(calc(calc(2, 3), 4)));
    if (enabled("divide")) {
      scenario("zero_divisor", typed("8/0"), "Error: ");
      auto recovered = typed("8/0");
      recovered.push(event("text", "6"));
      scenario("recover_after_zero", recovered, "6");
    }
    for (auto &[other, symbol] : operators)
      if (!enabled(other))
        scenario("disabled_" + other, typed("2" + symbol + "3"), "Error: ");
  } else
    scenario("keyboard_disabled", typed("123"), "0");
  if (spec.backspace)
    scenario("backspace", buttons({"digit_1", "digit_2", "backspace"}), "1");
  if (spec.memory) {
    scenario("memory_accumulate",
             buttons({"digit_1", "digit_2", "memory_add", "clear", "digit_3",
                      "memory_subtract", "memory_recall"}),
             "9", num(9));
    scenario(
        "memory_clear",
        buttons({"digit_5", "memory_add", "memory_clear", "memory_recall"}),
        "0", num(0));
    if (spec.keyboard)
      scenario(
          "memory_overflow_is_atomic",
          arr({event("text", "1000000000000000"), event("button", "memory_add"),
               event("button", "memory_add"),
               event("button", "memory_recall")}),
          shown(1e15), num(1e15));
  }
  std::set<std::string> ids{"clear", "evaluate", "decimal"};
  for (int d = 0; d < 10; ++d)
    ids.insert("digit_" + std::to_string(d));
  ids.insert(spec.operations.begin(), spec.operations.end());
  if (spec.backspace)
    ids.insert("backspace");
  if (spec.parentheses)
    ids.insert({"lparen", "rparen"});
  if (spec.memory)
    ids.insert(
        {"memory_add", "memory_subtract", "memory_clear", "memory_recall"});
  for (auto &s : ids)
    c.buttons.push(str(s));
  return c;
}
void check_calculator_results(const CalculatorSpec &spec,
                              const CalculatorCases &c, const Json &observed) {
  auto values = observed["expressions"], gui = observed["gui"];
  if (!values.is_array() || values.size() != c.values.size() ||
      !gui.is_array() || gui.size() != c.scenarios.size())
    throw Unsupported("Incomplete calculator response");
  for (size_t i = 0; i < values.size(); ++i) {
    auto actual = values.at(i), w = c.values.at(i), v = actual["value"],
         err = actual["error"];
    bool ok = actual.contains("value") && err.is_string();
    if (w.null())
      ok = ok && v.null() && !err.text().empty();
    else
      ok = ok && v.is_number() && err.text().empty() &&
           std::abs(v.real() - w.real()) <=
               std::max(1e-12, 1e-11 * std::abs(w.real()));
    if (!ok)
      throw Unsupported("Arithmetic mismatch for " + canonical(c.inputs.at(i)) +
                        " expected " + canonical(w) + " received " +
                        canonical(actual));
  }
  for (size_t i = 0; i < gui.size(); ++i) {
    auto actual = gui.at(i), s = c.scenarios.at(i);
    auto shown = actual["display"].text(), expected = s["expected"].text();
    bool ok = (expected == "Error: " ? shown.starts_with(expected)
                                     : shown == expected) &&
              actual["title"].text() == spec.title &&
              canonical(actual["buttons"]) == canonical(c.buttons);
    if (!s["memory"].null()) {
      if (actual["memory"].is_number())
        ok = ok && actual["memory"].real() == s["memory"].real();
      else
        ok = ok && actual["memory_text"].text() ==
                       "Memory: " + display(s["memory"].real(), spec.precision);
    }
    if (!ok)
      throw Unsupported("GUI mismatch for " + s["name"].text() + " expected " +
                        expected + " received " + canonical(actual));
  }
}
} // namespace unllm::native
