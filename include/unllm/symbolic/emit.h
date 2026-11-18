#pragma once
#include "program.h"
namespace unllm::native {
std::string emit_expression(const Expr &expression,
                            const std::string &language);
std::string emit_stream(const Program &program, const std::string &language,
                        const std::filesystem::path &resources);
} // namespace unllm::native
