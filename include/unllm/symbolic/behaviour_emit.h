#pragma once
#include "behaviour_plan.h"
namespace unllm::native {
std::string emit_behaviour(const BehaviourSpec &spec,
                           const BehaviourGraph &graph,
                           const std::string &language,
                           const std::filesystem::path &root);
}
