#pragma once
#include "behaviour_expression.h"
namespace unllm::native {
class BehaviourSpec {
  Json value_;
  explicit BehaviourSpec(Json value) : value_(std::move(value)) {}

public:
  static BehaviourSpec from(const Json &value);
  Json data() const { return value_.clone(); }
  Json state() const { return value_["state"].clone(); }
  Json actions() const { return value_["actions"].clone(); }
  Json views() const { return value_["views"].clone(); }
};
} // namespace unllm::native
