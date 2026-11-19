#pragma once
#include "unllm/symbolic/inference.h"
#include <map>
#include <set>
namespace unllm::native {
using Names = std::vector<std::string>;
struct Provider {
  std::string capability, target;
  Names needs, accepts, produces;
};
using ProviderCatalog = std::map<std::string, Provider>;
Json checked_provider_plan(
    const ProviderCatalog &providers, const Names &needed,
    const std::vector<std::pair<std::string, std::string>> &ports,
    const std::string &language, const std::set<std::string> &disabled,
    const std::string &prefix, const std::string &source, NativeEngine &engine,
    Deadline deadline);
} // namespace unllm::native
