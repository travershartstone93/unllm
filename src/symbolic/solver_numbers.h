#pragma once
#include "unllm/symbolic/common.h"
namespace unllm::native {
// SMT real literals are exact decimal rationals. Expand scientific notation
// before passing a literal to cvc5 and round once when decoding its answer.
std::string solver_decimal(const Json &value);
double solver_binary64(const std::string &rational);
} // namespace unllm::native
