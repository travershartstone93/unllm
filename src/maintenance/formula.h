#pragma once
#include <string>
namespace unllm::maintenance {
std::string display_formulas(const std::string &text);
std::string inline_formulas(const std::string &text);
bool math_site(const std::string &host);
} // namespace unllm::maintenance
