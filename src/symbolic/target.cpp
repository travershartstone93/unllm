#include "unllm/symbolic/target.h"
#include <algorithm>
#include <charconv>
namespace unllm::native {
ProcessResult run_stream_target(const std::filesystem::path &job,
                                const std::vector<std::string> &command,
                                const std::string &input, Deadline deadline,
                                bool sanitized) {
  if (command.empty())
    throw Unsupported("Missing target executable");
  std::vector<std::string> args{"/usr/bin/bwrap",
                                "--unshare-all",
                                "--die-with-parent",
                                "--clearenv",
                                "--ro-bind",
                                "/usr",
                                "/usr",
                                "--symlink",
                                "usr/lib",
                                "/lib",
                                "--symlink",
                                "usr/lib",
                                "/lib64",
                                "--proc",
                                "/proc",
                                "--dev",
                                "/dev",
                                "--tmpfs",
                                "/tmp",
                                "--ro-bind",
                                std::filesystem::canonical(job).string(),
                                "/job",
                                "--chdir",
                                "/job",
                                "--setenv",
                                "PATH",
                                "/usr/bin",
                                "--setenv",
                                "LANG",
                                "C.UTF-8",
                                "--setenv",
                                "ASAN_OPTIONS",
                                "detect_leaks=0",
                                "--setenv",
                                "UBSAN_OPTIONS",
                                "halt_on_error=1",
                                "--",
                                "/usr/bin/prlimit",
                                "--cpu=3:3",
                                "--fsize=2097152:2097152",
                                "--nofile=64:64"};
  if (!sanitized)
    args.push_back("--as=536870912:536870912");
  args.push_back("--");
  args.insert(args.end(), command.begin(), command.end());
  Process process(args, "", 2 * 1024 * 1024);
  process.write(input, deadline);
  return process.finish(deadline);
}
namespace {
std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    auto end = s.find(delim, start);
    out.push_back(
        s.substr(start, end == std::string::npos ? end : end - start));
    if (end == std::string::npos)
      return out;
    start = end + 1;
  }
}
} // namespace
bool output_matches(const Json &expected, const std::string &output) {
  if (!expected.is_array())
    return false;
  std::string text = output;
  if (text.ends_with('\n'))
    text.pop_back();
  auto rows = output.empty() ? std::vector<std::string>{} : split(text, '\n');
  if (rows.size() != expected.size())
    return false;
  for (size_t i = 0; i < rows.size(); ++i) {
    auto gold = expected.at(i);
    if (!gold.is_array())
      return false;
    if (gold.size() == 1 && gold.at(0).is_string()) {
      if (gold.at(0).text() != rows[i])
        return false;
      continue;
    }
    auto fields = split(rows[i], '\t');
    if (fields.size() != gold.size())
      return false;
    for (size_t j = 0; j < fields.size(); ++j) {
      auto wanted = gold.at(j);
      auto &actual = fields[j];
      if (wanted.is_integer()) {
        int64_t n;
        auto [end, error] =
            std::from_chars(actual.data(), actual.data() + actual.size(), n);
        if (error != std::errc{} || end != actual.data() + actual.size() ||
            n != wanted.integer())
          return false;
      } else if (wanted.is_number()) {
        double n;
        auto [end, error] =
            std::from_chars(actual.data(), actual.data() + actual.size(), n);
        if (error != std::errc{} || end != actual.data() + actual.size() ||
            !std::isfinite(n))
          return false;
        double want = wanted.real(), difference = std::abs(n - want);
        if (difference >
            std::max(1e-12, 1e-10 * std::max(std::abs(n), std::abs(want))))
          return false;
      } else if (!wanted.is_string() || wanted.text() != actual)
        return false;
    }
  }
  return true;
}
} // namespace unllm::native
