#include "unllm/maintenance/transaction.h"
#include "unllm/symbolic/gui_target.h"
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
using namespace unllm::maintenance;
using namespace unllm::native;
namespace fs = std::filesystem;
namespace {
void check(bool condition, const std::string &why) {
  if (!condition)
    throw std::runtime_error(why);
}
const std::string index = "sample.unlpq";
void pack(const Path &root, const std::string &name, const std::string &value) {
  write_atomic(root / name / "manifest.json", "{}");
  write_atomic(root / name / "contents", value);
}
void seed(const Paths &p) {
  pack(p.packs, "atlas", "before");
  pack(p.packs, "untouched", "stable");
  pack(p.data / "staging/packs", "atlas", "after");
  pack(p.data / "staging/packs", "cedar", "new");
  write_atomic(p.data / "vectors" / index, "old-index");
  write_atomic(p.data / "staging" / index, "new-index");
  write_atomic(p.data / "previous/old-marker", "unrelated retirement");
  write_atomic(p.data / "update/state.json",
               R"({"baseline":{"right":9},"other":19})");
}
void before(const Paths &p) {
  check(read_file(p.packs / "atlas/contents") == "before" &&
            !fs::exists(p.packs / "cedar") &&
            read_file(p.packs / "untouched/contents") == "stable",
        "Live pack set changed");
  check(read_file(p.data / "vectors" / index) == "old-index",
        "Live index changed");
}
void after(const Paths &p) {
  check(read_file(p.packs / "atlas/contents") == "after" &&
            read_file(p.packs / "cedar/contents") == "new" &&
            read_file(p.packs / "untouched/contents") == "stable",
        "Wrong candidate pack set");
  check(read_file(p.data / "vectors" / index) == "new-index" &&
            read_file(p.data / "previous" / index) == "old-index",
        "Wrong candidate or retired index");
}
void publish(const Paths &p, Progress f = {}) {
  publish_verified(p, {"atlas", "cedar"}, index, f);
}
template <class F> void crash(F f, int step) {
  pid_t pid = fork();
  if (pid < 0)
    throw std::runtime_error("fork failed");
  if (pid == 0) {
    try {
      f([&](size_t n) {
        if (n == size_t(step))
          _exit(73);
      });
      _exit(0);
    } catch (...) {
      _exit(1);
    }
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 73)
    throw std::runtime_error("Did not reach transaction crash point " +
                             std::to_string(step));
}
} // namespace
int main() {
  auto base = fresh_gui_directory(fs::temp_directory_path(), "unllm-txn-test");
  try {
    for (bool rollback : {false, true})
      for (int step = 1; step <= (rollback ? 6 : 7); ++step) {
        auto data = base / ((rollback ? "rollback-" : "publish-") +
                            std::to_string(step));
        Paths p{data, data / "packs", data / "downloads"};
        seed(p);
        if (rollback)
          publish(p);
        crash(
            [&](Progress f) {
              UpdateLock guard(p);
              if (rollback)
                rollback_update(p, f);
              else
                publish(p, f);
            },
            step);
        UpdateLock guard(p);
        recover_update(p);
        recover_update(p);
        if (rollback) {
          after(p);
          check(rollback_update(p), "Rollback after recovery failed");
          before(p);
        } else {
          before(p);
          check(read_file(data / "previous/old-marker") ==
                    "unrelated retirement",
                "Lost earlier retirement");
          check(read_file(data / "staging/packs/atlas/contents") == "after",
                "Lost staged pack");
          publish(p);
          after(p);
        }
        check(
            read_optional_json(data / "update/state.json")["other"].integer() ==
                19,
            "Unrelated state lost");
      }
    Paths p{base / "ordinary", base / "ordinary/packs", base / "downloads"};
    seed(p);
    UpdateLock guard(p);
    bool invalid = false;
    try {
      publish_verified(p, {"atlas", "../unsafe"}, index);
    } catch (const Unsupported &) {
      invalid = true;
    }
    check(invalid, "Invalid pack name accepted");
    recover_update(p);
    before(p);
    publish(p);
    after(p);
    check(
        !read_optional_json(p.data / "update/state.json").contains("baseline"),
        "Stale score cache");
    check(!prune_update(p, true), "New rollback set pruned");
    auto old = read_optional_json(p.data / "previous/retired.json");
    old.set("retired", Json::number(1));
    write_atomic(p.data / "previous/retired.json", canonical(old));
    check(prune_update(p, true) && !prune_update(p, false) &&
              !rollback_update(p),
          "Prune replay");
    Paths external{base / "external", base / "external/packs",
                   base / "downloads"};
    seed(external);
    auto target = base / index;
    fs::rename(external.data / "vectors" / index, target);
    setenv("UNLLM_MEANING", target.c_str(), 1);
    publish(external);
    check(read_file(target) == "new-index",
          "External index publication failed");
    unsetenv("UNLLM_MEANING");
    bool refused = false;
    try {
      rollback_update(external);
    } catch (const Unsupported &) {
      refused = true;
    }
    check(refused && read_file(target) == "new-index",
          "Wrong index rollback accepted");
    setenv("UNLLM_MEANING", target.c_str(), 1);
    check(rollback_update(external) && read_file(target) == "old-index",
          "External index rollback failed");
    fs::remove_all(base);
    std::cout << "13 forced process exits across publication and rollback "
                 "recovered. Retry, expiry and invalid metadata passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << " in " << base << '\n';
    return 1;
  }
}
