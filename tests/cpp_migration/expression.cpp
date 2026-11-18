#include "unllm/symbolic/expression.h"
#include <iostream>
#include <optional>
using namespace unllm::native;
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
static double value(const Expr &e, double x) {
  if (e.op == "variable")
    return x;
  if (e.op == "constant")
    return e.value.is_boolean() ? e.value.flag() : e.value.real();
  auto a = value(e.args[0], x);
  if (e.op == "negate")
    return -a;
  if (e.op == "not")
    return !a;
  if (e.op == "if")
    return value(e.args[a ? 1 : 2], x);
  auto b = value(e.args[1], x);
  if (e.op == "add")
    return a + b;
  if (e.op == "subtract")
    return a - b;
  if (e.op == "multiply")
    return a * b;
  if (e.op == "gt")
    return a > b;
  if (e.op == "ge")
    return a >= b;
  if (e.op == "lt")
    return a < b;
  if (e.op == "le")
    return a <= b;
  if (e.op == "eq")
    return a == b;
  if (e.op == "and")
    return a && b;
  if (e.op == "or")
    return a || b;
  throw std::runtime_error("Unknown expression");
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    auto fixtures =
        Json::parse(read_file(std::filesystem::path(argv[1]) /
                              "tests/cpp_migration/fixtures/expressions.json"));
    for (auto fixture : fixtures.items()) {
      bool rejected = false;
      std::optional<Expr> result;
      try {
        result = expression(fixture["input"]);
      } catch (const std::exception &) {
        rejected = true;
      }
      if (fixture.contains("expected"))
        check(!rejected &&
                  canonical(result->data()) == canonical(fixture["expected"]),
              "Frozen expression-tree parity");
      else
        check(rejected, "Unsupported expression must be rejected");
    }
    for (auto source :
         {"x - 11", "x * 1.8 + 32", "x >= 3 and x < 9", "-x if x < 0 else x"}) {
      auto target = expression(Json::string(source));
      auto solved = synthesize_expression(
          target, target.type() == ExprType::Boolean, 3000);
      for (double x : {-25.0, -1.0, 0.0, 2.0, 3.0, 8.0, 9.0, 11.0, 100.0})
        check(std::abs(value(target, x) - value(solved.expression, x)) < 1e-10,
              "Solved expression disagrees on independent inputs");
      if (std::string(source) == "x - 11") {
        check(value(solved.expression, 27) == 16,
              "Decrement direction and operand");
        check(value(solved.expression, -3) == -14, "Negative input decrement");
      }
      if (std::string(source) == "x * 1.8 + 32")
        check(std::abs(value(solved.expression, 100) - 212) < 1e-10,
              "Fractional conversion");
    }
    std::cout << "Native expression parsing/type parity and scalar SyGuS "
                 "checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
