#pragma once
#include "inference.h"
#include <set>
namespace unllm::native {
struct CalculatorSpec {
  static CalculatorSpec from(const Json &input);
  Json data() const;
  std::vector<std::string> operations;
  bool memory = false, parentheses = true, backspace = true, keyboard = true;
  int precision = 12;
  std::string title = "Calculator";
};
Json plan_calculator(const CalculatorSpec &, const std::string &language,
                     NativeEngine &, Deadline,
                     const std::set<std::string> &disabled = {});
Json construct_calculator(const CalculatorSpec &, const Json &plan,
                          const std::filesystem::path &root, Deadline);
std::string emit_calculator(const Json &ir, const std::string &language,
                            const std::filesystem::path &root);
Json build_calculator(const CalculatorSpec &, const std::string &language,
                      const std::filesystem::path &root,
                      const std::filesystem::path &artifacts, NativeEngine &,
                      Deadline, const std::string &python, bool emit_only);
} // namespace unllm::native
