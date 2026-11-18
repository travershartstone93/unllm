#pragma once
#include "process.h"
namespace unllm::native {
// Run a generated stream target with no network, read-only job/tools, bounded
// output and OS resource limits. Python, when requested, is a target
// executable.
ProcessResult run_stream_target(const std::filesystem::path &job,
                                const std::vector<std::string> &command,
                                const std::string &input, Deadline deadline,
                                bool sanitized = false);
bool output_matches(const Json &expected, const std::string &output);
} // namespace unllm::native
