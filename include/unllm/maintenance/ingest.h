#pragma once
#include "sources.h"
namespace unllm::maintenance {
// Builds under data/staging only; verified publication updates the active set.
Json stage_pack(const Paths &, const Path &input,
                const std::string &name_override = {}, bool force = false,
                unsigned workers = 1,
                const std::function<void(size_t, size_t)> &progress = {},
                bool resume = false);
Json inspect_pack(const Path &pack);
Json source_version(const Path &input, const std::string &name_override);
void install_staged_pack(const Paths &, const std::string &name);
} // namespace unllm::maintenance
