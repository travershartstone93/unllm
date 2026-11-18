#pragma once
#include "common.h"
#include <chrono>
#include <sys/types.h>
#include <vector>

namespace unllm::native {
using Deadline = std::chrono::steady_clock::time_point;
inline Deadline after_ms(int ms) {
  return std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
}
struct ProcessResult {
  int exit_code;
  std::string out, err;
};
class Process {
public:
  explicit Process(const std::vector<std::string> &argv,
                   const std::string &cwd = "",
                   std::size_t output_limit = 16 * 1024 * 1024);
  ~Process();
  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;
  void write(const std::string &data, Deadline deadline);
  std::string line(Deadline deadline);
  ProcessResult finish(Deadline deadline);
  void reset_output_budget();
  const std::string &errors() const { return errors_; }
  pid_t pid() const { return child_; }
  void stop() noexcept;

private:
  pid_t child_ = -1;
  int input_ = -1, output_ = -1, error_ = -1;
  std::size_t limit_, received_ = 0;
  std::string pending_, errors_;
  void pump(Deadline deadline, bool writable = false);
  void drain(int &fd, std::string &into);
};
} // namespace unllm::native
