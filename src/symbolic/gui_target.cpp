#include "unllm/symbolic/gui_target.h"
#include <cstdlib>
#include <sstream>
namespace unllm::native {
using Path = std::filesystem::path;
using Args = std::vector<std::string>;
Path fresh_gui_directory(const Path &parent, const std::string &prefix) {
  std::filesystem::create_directories(parent);
  auto pat =
      (std::filesystem::absolute(parent) / (prefix + "-XXXXXX")).string();
  std::vector<char> buf(pat.begin(), pat.end());
  buf.push_back(0);
  if (!mkdtemp(buf.data()))
    throw std::runtime_error("Cannot create job directory");
  return buf.data();
}
Args qt_compile_flags(bool driver, Deadline deadline) {
  Args args{"/usr/bin/pkg-config", "--cflags", "--libs", "Qt6Widgets"};
  if (driver)
    args.push_back("Qt6Test");
  auto p = Process(args).finish(deadline);
  if (p.exit_code)
    throw Unsupported("Cannot locate Qt compiler flags: " + p.err);
  std::istringstream input(p.out);
  Args flags;
  std::string flag;
  while (input >> flag)
    flags.push_back(flag);
  return flags;
}
ProcessResult run_gui_target(const Path &job, const Path &output,
                             const Args &cmd, const Json &input, bool san,
                             Deadline deadline,
                             const Path &antlr_target_package) {
  Args args{"/usr/bin/bwrap",
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
            "--perms",
            "0700",
            "--dir",
            "/tmp/runtime",
            "--ro-bind",
            std::filesystem::canonical(job).string(),
            "/job",
            "--bind",
            std::filesystem::canonical(output).string(),
            "/output",
            "--chdir",
            "/job",
            "--setenv",
            "PATH",
            "/usr/bin",
            "--setenv",
            "LANG",
            "C.UTF-8",
            "--setenv",
            "XDG_RUNTIME_DIR",
            "/tmp/runtime",
            "--setenv",
            "QT_QPA_PLATFORM",
            "offscreen",
            "--setenv",
            "ASAN_OPTIONS",
            "detect_leaks=0",
            "--setenv",
            "UBSAN_OPTIONS",
            "halt_on_error=1"};
  if (!antlr_target_package.empty()) {
    auto module = std::filesystem::canonical(antlr_target_package);
    if (!std::filesystem::is_regular_file(module / "__init__.py"))
      throw Unsupported(
          "Expected the optional target antlr4 package directory");
    args.insert(args.end(),
                {"--ro-bind", module.string(), "/target-packages/antlr4",
                 "--setenv", "PYTHONPATH", "/target-packages"});
  }
  if (std::filesystem::is_directory("/etc/fonts"))
    args.insert(args.end(), {"--ro-bind", "/etc/fonts", "/etc/fonts"});
  args.insert(args.end(), {"--", "/usr/bin/prlimit", "--cpu=15:15",
                           "--nofile=128:128", "--fsize=8388608:8388608"});
  if (!san)
    args.push_back("--as=1610612736:1610612736");
  args.push_back("--");
  args.insert(args.end(), cmd.begin(), cmd.end());
  Process child(args, "", 8 * 1024 * 1024);
  auto limit = std::min(deadline, after_ms(30000));
  child.write(canonical(input) + "\n", limit);
  return child.finish(limit);
}
} // namespace unllm::native
