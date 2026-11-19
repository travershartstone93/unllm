#pragma once
#include "ir.h"
namespace unllm::algorithm {
void check_emission(const std::filesystem::path &job, const std::string &source,
                    const std::vector<Json> &cases, const std::string &python);
}
