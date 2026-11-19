#pragma once
#include "process.h"
namespace unllm::native {
std::filesystem::path fresh_gui_directory(const std::filesystem::path &parent,
                                          const std::string &prefix);
std::vector<std::string> qt_compile_flags(bool test_driver, Deadline deadline);
ProcessResult run_gui_target(
    const std::filesystem::path &job, const std::filesystem::path &output,
    const std::vector<std::string> &command, const Json &input, bool sanitized,
    Deadline deadline, const std::filesystem::path &antlr_target_package = {});
} // namespace unllm::native
