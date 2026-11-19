#pragma once
#include "unllm/symbolic/common.h"
#include <functional>
namespace unllm::maintenance {
using native::Json;
using Path = std::filesystem::path;
struct Paths {
  Path data, packs, downloads;
  static Paths environment();
};
using Fetch = std::function<std::string(const std::string &url, bool head)>;
Json check_sources(const Paths &, Json state, const Fetch &fetch);
Json public_check(const Json &rows);
int check_result(const Json &report);
Json read_optional_json(const Path &);
void write_atomic(const Path &, const std::string &);
} // namespace unllm::maintenance
