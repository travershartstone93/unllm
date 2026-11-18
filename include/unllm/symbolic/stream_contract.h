#pragma once
#include "program.h"
namespace unllm::native {
Json stream_reference(const Program &program, const std::string &input);
std::vector<std::string> stream_cases(const Program &program);
} // namespace unllm::native
