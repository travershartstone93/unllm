#pragma once
#include "unllm/json.h"
#include <filesystem>
#include <stdexcept>
#include <string>

namespace unllm::native {
using Json = json::Value;
struct Unsupported : std::runtime_error {
  using runtime_error::runtime_error;
};
struct Ambiguous : std::runtime_error {
  using runtime_error::runtime_error;
};
struct ResourceLimit : std::runtime_error {
  using runtime_error::runtime_error;
};
std::string read_file(const std::filesystem::path &path,
                      std::size_t limit = 16 * 1024 * 1024);
std::string key(const std::string &text);
std::string canonical(const Json &value);
std::string sha256(const std::string &value);
std::string digest(const Json &value);
std::string required_text(const Json &value, const std::string &field);
void fields_only(const Json &value, std::initializer_list<std::string> allowed);
} // namespace unllm::native
