#pragma once
#include "unllm/symbolic/semantics.h"

namespace unllm::native {
// Each entry records a distinct goal and its first supporting reading.
Json interval_candidates(const Meaning &meaning, const std::string &source);
Json build_example_function(const Json &spec, const std::string &language,
                            const std::filesystem::path &artifacts,
                            const std::string &python, bool emit_only);
// Convert the interpreter format with its declared (values, cutoff) signature.
Json reduction_specification(const Json &requirements);
Json build_function_contract(const Json &goal, const std::string &language,
                          const std::filesystem::path &artifacts,
                          const std::string &python, bool emit_only);
Json build_algorithm_goal(const Json &goal, const std::string &language,
                          const std::filesystem::path &artifacts,
                          const std::string &python, bool emit_only);
} // namespace unllm::native
