#pragma once
#include "ir.h"
namespace unllm::algorithm {
Json check(const Json &ir, const Json &contract,
           const std::filesystem::path &job, const std::string &python);
Json construct(const std::filesystem::path &session, const Json &request,
               bool revision, const std::string &python);
} // namespace unllm::algorithm
