#pragma once
#include "behaviour_solver.h"
#include "inference.h"
#include <set>
namespace unllm::native {
Json plan_behaviour(const BehaviourSpec &spec, const std::string &language,
                    const std::filesystem::path &root, NativeEngine &engine,
                    Deadline deadline,
                    const std::set<std::string> &disabled = {});
struct BehaviourAction {
  std::string id, target;
  std::map<std::string, BehaviourValue> updates;
  std::optional<BehaviourValue> guard;
  Json parameters, steps;
};
struct BehaviourGraph {
  std::vector<BehaviourAction> actions;
  std::map<std::string, BehaviourExpression> labels;
  std::set<std::string> persistent;
  Json reports = Json::array();
};
BehaviourGraph construct_behaviour(const BehaviourSpec &spec, const Json &plan,
                                   Deadline deadline);
} // namespace unllm::native
