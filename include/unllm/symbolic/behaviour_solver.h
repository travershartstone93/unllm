#pragma once
#include "behaviour.h"
#include <map>
namespace unllm::native {
struct TransitionSolution {
  std::map<std::string, BehaviourValue> updates;
  Json evidence;
};
TransitionSolution synthesize_transition(const BehaviourSpec &spec,
                                         const std::string &action_id,
                                         int timeout_ms = 3000);
} // namespace unllm::native
