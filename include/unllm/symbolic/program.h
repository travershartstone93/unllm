#pragma once
#include "expression.h"
#include "inference.h"
#include <optional>
namespace unllm::native {
struct Step {
  std::string op;
  std::optional<Expr> expr;
  bool descending = false;
  Json data() const;
};
struct Program {
  std::string input, output = "values", invalid = "error",
                     name = "process_data";
  std::vector<Step> steps;
  std::vector<std::string> assumptions;
  static Program from(const Json &data);
  Json data() const;
};
struct Catalog {
  Json facts = Json::array(), relations = Json::array();
};
Catalog stream_catalog();
Json plan(const Program &program, NativeEngine &native, Deadline deadline);
} // namespace unllm::native
