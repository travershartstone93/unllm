#pragma once
#include "behaviour_emit.h"
namespace unllm::native {
Json behaviour_cases(const BehaviourSpec &, const std::filesystem::path &job);
void check_behaviour_outcome(const BehaviourSpec &, const Json &scenario,
                             const Json &observed,
                             const std::filesystem::path &output);
Json validate_behaviour(const BehaviourSpec &,
                        const std::filesystem::path &resources,
                        const std::filesystem::path &job,
                        const std::string &language, Deadline deadline,
                        const std::string &python_target = "");
Json build_behaviour(const BehaviourSpec &, const std::string &language,
                     const std::filesystem::path &resources,
                     const std::filesystem::path &artifacts, NativeEngine &,
                     Deadline deadline, const std::string &python_target = "",
                     bool emit_only = false);
} // namespace unllm::native
