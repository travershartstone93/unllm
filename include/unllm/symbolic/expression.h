#pragma once
#include "common.h"
namespace unllm::native {
enum class ExprType { Number, Boolean };
struct Expr {
  std::string op;
  std::vector<Expr> args;
  Json value;
  Json data() const;
  ExprType type(unsigned depth = 0) const;
  bool contains(const std::string &operation) const;
  std::vector<double> constants() const;
};
Expr expression(const Json &value, unsigned depth = 0);
struct ExpressionSolution {
  Expr expression;
  Json evidence;
};
ExpressionSolution synthesize_expression(const Expr &target,
                                         bool predicate = false,
                                         int timeout_ms = 3000);
} // namespace unllm::native
