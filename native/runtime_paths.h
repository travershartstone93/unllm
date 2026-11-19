#pragma once
#include "unllm/symbolic/common.h"
#include <cstdlib>

namespace unllm::native {
inline std::filesystem::path runtime_state() {
  if (auto p = std::getenv("XDG_STATE_HOME"); p && *p)
    return std::filesystem::path(p) / "unllm-native";
  if (auto p = std::getenv("HOME"); p && *p)
    return std::filesystem::path(p) / ".local/state/unllm-native";
  throw Unsupported("Set HOME or XDG_STATE_HOME for native runtime state");
}
inline std::filesystem::path
runtime_database(const std::filesystem::path &state) {
  if (auto p = std::getenv("UNLLM_FACT_DB"); p && *p)
    return p;
  return state / "facts.sqlite";
}
} // namespace unllm::native
