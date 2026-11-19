#include "unllm/maintenance/refresh.h"
#include "unllm/maintenance/transaction.h"
#include "unllm/symbolic/process.h"
#include <fcntl.h>
#include <iostream>
#include <sys/file.h>
#include <unistd.h>
using namespace unllm::maintenance;
using namespace unllm::native;
namespace {
std::string fetch(const std::string &url, bool head) {
  std::vector<std::string> cmd{"/usr/bin/curl", "--ipv4",     "--silent",
                               "--show-error",  "--fail",     "--location",
                               "--proto",       "=https",     "--proto-redir",
                               "=https",        "--max-time", "25",
                               "--retry",       "2"};
  if (head)
    cmd.push_back("--head");
  cmd.insert(cmd.end(), {"--", url});
  try {
    auto p = Process(cmd).finish(after_ms(35000));
    return p.exit_code ? "" : p.out;
  } catch (const ResourceLimit &) {
    return "";
  }
}
} // namespace
int main(int argc, char **argv) {
  std::string verb = argc > 1 ? argv[1] : "";
  if (!(verb == "run" ||
        (verb == "check" &&
         (argc == 2 || (argc == 3 && std::string(argv[2]) == "--json"))) ||
        (argc == 2 && (verb == "prune" || verb == "rollback")))) {
    std::cerr
        << "Usage: unllm_update_native run [--dry-run] [--full] [--repair] "
           "[--source NAME] | check [--json] | prune | rollback\n";
    return 2;
  }
  try {
    auto paths = Paths::environment();
    auto dir = paths.data / "update";
    UpdateLock lock(paths);
    recover_update(paths);
    if (verb == "prune") {
      std::cout << (prune_update(paths, false) ? "Previous set removed"
                                               : "No previous set")
                << '\n';
      return 0;
    }
    if (verb == "rollback") {
      bool done = rollback_update(paths);
      std::cout << (done ? "Previous set restored" : "No previous set") << '\n';
      return done ? 0 : 1;
    }
    auto state = read_optional_json(dir / "state.json");
    if (state.null())
      state = Json::object();
    if (verb == "run") {
      RefreshOptions opts;
      for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dry-run")
          opts.dry = true;
        else if (arg == "--repair")
          opts.repair = true;
        else if (arg == "--full")
          opts.full = true;
        else if (arg == "--source" && i + 1 < argc)
          opts.sources.insert(argv[++i]);
        else
          throw Unsupported("Invalid update option: " + arg);
      }
      auto bin = std::filesystem::canonical("/proc/self/exe").parent_path();
      auto tools = std::filesystem::exists(bin / "unllm_embed")
                       ? bin
                       : bin.parent_path();
      auto rows =
          opts.repair ? Json::array() : check_sources(paths, state, fetch);
      refresh(paths, tools / "unllm", tools / "unllm_embed", rows, state, opts);
      if (!opts.dry) {
        state.erase("baseline");
        write_atomic(dir / "state.json", canonical(state) + "\n");
        prune_update(paths, true);
      }
      return 0;
    }
    auto rows = check_sources(paths, state, fetch), report = public_check(rows);
    write_atomic(dir / "state.json", canonical(state) + "\n");
    prune_update(paths, true);
    if (argc == 3)
      std::cout << canonical(report) << '\n';
    else
      for (auto row : report["sources"].items()) {
        std::cout << (row["newer"].null()   ? "?"
                      : row["newer"].flag() ? "NEWER"
                                            : "ok")
                  << "  " << row["source"].text()
                  << "  installed: " << row["installed"].text()
                  << "\n    available: " << row["available"].text() << "\n";
        if (!row["note"].text().empty())
          std::cout << "    " << row["note"].text() << "\n";
      }
    return check_result(report);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
}
