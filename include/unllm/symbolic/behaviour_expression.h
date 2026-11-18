#pragma once
#include "common.h"
#include <vector>
namespace unllm::native {
// Shared typed expression representation, independent of any application label.
struct BehaviourValue {
  std::string kind, name;
  Json value;
  std::vector<BehaviourValue> args;
  Json data() const;
};
struct BehaviourExpression {
  BehaviourValue value;
  std::string type;
};
BehaviourExpression behaviour_expression(const Json &source, const Json &state,
                                         bool transition = false);
} // namespace unllm::native
