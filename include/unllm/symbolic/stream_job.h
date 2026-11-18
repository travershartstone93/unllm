#pragma once
#include "program.h"
namespace unllm::native {
Json validate_stream(const Program &original, const std::filesystem::path &job,
                     const std::string &language, Deadline deadline,
                     const std::string &python_target = "");
Json build_stream(const Program &original, const std::string &language,
                  const std::filesystem::path &resources,
                  const std::filesystem::path &artifacts, NativeEngine &engine,
                  Deadline deadline, const std::string &python_target = "",
                  bool emission_only = false);
} // namespace unllm::native
