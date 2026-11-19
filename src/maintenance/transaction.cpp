#include "unllm/maintenance/transaction.h"
#include "../symbolic/gui_json.h"
#include <chrono>
#include <fcntl.h>
#include <set>
#include <sys/file.h>
#include <unistd.h>
namespace unllm::maintenance {
using namespace native;
using namespace native::gui;
namespace fs = std::filesystem;
namespace {
Path txn(const Paths &p) { return p.data / "update/transaction"; }
void syncdir(const Path &p) {
  int fd = open(p.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    throw std::runtime_error("Cannot open update directory: " + p.string());
  int result = fsync(fd);
  close(fd);
  if (result < 0)
    throw std::runtime_error("Cannot sync update directory: " + p.string());
}
void name(const std::string &s) {
  if (s.empty() || s.front() == '.' ||
      s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                          "0123456789_.-") != s.npos)
    throw Unsupported("Invalid pack or index filename");
}
Path configured_index(const Paths &p, const std::string &index) {
  Path path = p.data / "vectors" / index;
  if (auto value = std::getenv("UNLLM_MEANING"); value && *value)
    path = value;
  if (path.filename() != index)
    throw Unsupported("Configured vector index differs from the transaction");
  return fs::absolute(path).lexically_normal();
}
void verify_location(const Paths &p, const Json &record,
                     const std::string &index) {
  Path recorded =
      record.contains("location")
          ? Path(required_text(record, "location"))
          : fs::absolute(p.data / "vectors" / index).lexically_normal();
  if (configured_index(p, index) != recorded)
    throw Unsupported(
        "Restore the original vector index setting before recovery");
}
bool exists_entry(const Path &path) {
  return fs::exists(fs::symlink_status(path));
}
void check_kind(const Path &path, bool dir) {
  auto s = fs::symlink_status(path);
  if (!(dir ? fs::is_directory(s) : fs::is_regular_file(s)))
    throw Unsupported("Missing or invalid update artifact: " + path.string());
}
struct Move {
  Path from, to;
};
std::vector<Move> moves(const Paths &p, const Json &journal) {
  auto work = txn(p), prev = p.data / "previous", stage = p.data / "staging";
  auto mode = required_text(journal, "operation");
  bool publish = mode == "publish";
  if (!publish && mode != "rollback")
    throw Unsupported("Unknown transaction");
  auto index = required_text(journal, "index");
  name(index);
  verify_location(p, journal, index);
  auto liveindex = configured_index(p, index);
  auto packs = journal["packs"];
  if (!packs.is_array() || packs.size() > 1024)
    throw Unsupported("Invalid pack transaction");
  std::vector<Move> m;
  std::set<std::string> seen;
  if (publish && journal["saved_previous"].flag())
    m.push_back({prev, work / "prior"});
  for (auto item : packs.items()) {
    auto n = required_text(item, "name");
    name(n);
    if (!seen.insert(n).second || !item["old"].is_boolean() ||
        !item["live"].is_boolean())
      throw Unsupported("Invalid pack journal");
    auto live = p.packs / n;
    bool old = item["old"].flag();
    if (publish) {
      if (old)
        m.push_back({live, work / "retiring/packs" / n});
      m.push_back({stage / "packs" / n, live});
    } else {
      if (item["live"].flag())
        m.push_back({live, work / "discard/packs" / n});
      if (old)
        m.push_back({prev / "packs" / n, live});
    }
  }
  if (publish) {
    if (journal["old_index"].flag())
      m.push_back({liveindex, work / "retiring" / index});
    m.push_back({stage / index, liveindex});
    m.push_back({work / "retiring", prev});
  } else {
    if (journal["live_index"].flag())
      m.push_back({liveindex, work / "discard" / index});
    if (journal["old_index"].flag())
      m.push_back({prev / index, liveindex});
    m.push_back({prev, work / "retired"});
  }
  return m;
}
void move(const Move &m) {
  if (!exists_entry(m.from) || exists_entry(m.to))
    throw Unsupported("Unexpected transaction paths: " + m.from.string() +
                      " -> " + m.to.string());
  fs::rename(m.from, m.to);
  syncdir(m.from.parent_path());
  if (m.from.parent_path() != m.to.parent_path())
    syncdir(m.to.parent_path());
}
void journal(const Paths &p, const Json &j) {
  write_atomic(txn(p) / "journal.json", canonical(j) + "\n");
}
void cleanup(const Paths &p) {
  // Move journals out of the active location before removing any journal data.
  auto work = txn(p), garbage = p.data / "update/finished-transaction";
  if (exists_entry(garbage))
    fs::remove_all(garbage);
  fs::rename(work, garbage);
  syncdir(work.parent_path());
  fs::remove_all(garbage);
  syncdir(work.parent_path());
}
void rollback_partial(const Paths &p, Json j) {
  auto ops = moves(p, j);
  auto n = j["issued"].integer(-1);
  if (n < 0 || uint64_t(n) > ops.size())
    throw Unsupported("Invalid transaction cursor");
  while (n > 0) {
    auto &m = ops[n - 1];
    bool from = exists_entry(m.from), to = exists_entry(m.to);
    if (from == to)
      throw Unsupported("Cannot reconcile transaction paths for " +
                        m.from.string());
    if (to)
      move({m.to, m.from});
    j.set("issued", Json::number(--n));
    journal(p, j);
  }
  cleanup(p);
}
void finish(const Paths &p) {
  auto path = p.data / "update/state.json";
  auto state = read_optional_json(path);
  if (state.null())
    state = Json::object();
  state.erase("baseline");
  write_atomic(path, canonical(state) + "\n");
  cleanup(p);
}
void execute(const Paths &p, Json j, Progress after_move) {
  auto operations = moves(p, j);
  j.set("issued", Json::number(0));
  j.set("committed", boolean(false));
  journal(p, j);
  try {
    for (size_t i = 0; i < operations.size(); ++i) {
      if (!exists_entry(operations[i].from) || exists_entry(operations[i].to))
        throw Unsupported("Update artifact changed before publication: " +
                          operations[i].from.string());
      j.set("issued", Json::number(i + 1));
      journal(p, j);
      move(operations[i]);
      if (after_move)
        after_move(i + 1);
    }
    j.set("committed", boolean(true));
    journal(p, j);
  } catch (...) {
    recover_update(p);
    throw;
  }
  finish(p);
}
Json prepare(const Paths &p, const std::string &op, const std::string &index) {
  name(index);
  if (fs::path(index).extension() != ".unlpq")
    throw Unsupported("Expected a vector index name");
  recover_update(p);
  fs::create_directories(p.packs);
  fs::create_directories(p.data / "vectors");
  if (!fs::create_directory(txn(p)))
    throw Unsupported("Concurrent update transaction");
  return obj({{"operation", str(op)},
              {"index", str(index)},
              {"location", str(configured_index(p, index).string())},
              {"packs", Json::array()}});
}
} // namespace
UpdateLock::UpdateLock(const Paths &p) {
  auto dir = p.data / "update";
  fs::create_directories(dir);
  fd_ = open((dir / "lock").c_str(), O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_RDWR,
             0600);
  if (fd_ < 0)
    throw std::runtime_error("Cannot open update lock");
  while (flock(fd_, LOCK_EX) < 0)
    if (errno != EINTR) {
      close(fd_);
      throw std::runtime_error("Cannot lock update state");
    }
}
UpdateLock::~UpdateLock() { close(fd_); }
void recover_update(const Paths &p) {
  auto dir = p.data / "update";
  fs::create_directories(dir);
  auto finished = dir / "finished-transaction";
  if (exists_entry(finished))
    fs::remove_all(finished);
  auto work = txn(p);
  if (!exists_entry(work))
    return;
  check_kind(work, true);
  auto j = read_optional_json(work / "journal.json");
  if (j.null()) {
    cleanup(p);
    return;
  } // preparation finished no live rename before first journal
  if (!j["committed"].is_boolean())
    throw Unsupported("Unrecognized update journal");
  if (j["committed"].flag())
    finish(p);
  else
    rollback_partial(p, j);
}
void publish_verified(const Paths &p, const std::vector<std::string> &packs,
                      const std::string &index, Progress progress) {
  auto j = prepare(p, "publish", index);
  auto work = txn(p), stage = p.data / "staging", prev = p.data / "previous";
  fs::create_directories(work / "retiring/packs");
  auto names = Json::array(), old = Json::array();
  for (auto &n : packs) {
    name(n);
    check_kind(stage / "packs" / n, true);
    check_kind(stage / "packs" / n / "manifest.json", false);
    bool exists = exists_entry(p.packs / n);
    if (exists) {
      check_kind(p.packs / n, true);
      old.push(str(n));
    }
    j["packs"].push(obj({{"name", str(n)},
                         {"live", boolean(exists)},
                         {"old", boolean(exists)}}));
    names.push(str(n));
  }
  check_kind(stage / index, false);
  bool old_index = exists_entry(configured_index(p, index));
  if (old_index)
    check_kind(configured_index(p, index), false);
  if (exists_entry(prev))
    check_kind(prev, true);
  j.set("saved_previous", boolean(exists_entry(prev)));
  j.set("old_index", boolean(old_index));
  auto manifest = obj({{"retired", Json::number(std::time(nullptr))},
                       {"packs", names},
                       {"old_packs", old},
                       {"index", str(index)},
                       {"location", str(configured_index(p, index).string())},
                       {"old_index", boolean(old_index)}});
  write_atomic(work / "retiring/retired.json", canonical(manifest) + "\n");
  syncdir(work);
  syncdir(work.parent_path());
  execute(p, j, progress);
}
bool rollback_update(const Paths &p, Progress progress) {
  recover_update(p);
  auto prev = p.data / "previous";
  auto saved = read_optional_json(prev / "retired.json");
  if (saved.null())
    return false;
  auto index = required_text(saved, "index");
  verify_location(p, saved, index);
  auto j = prepare(p, "rollback", index);
  fs::create_directories(txn(p) / "discard/packs");
  auto names = saved["packs"];
  if (!names.is_array())
    throw Unsupported("Invalid retired pack list");
  std::set<std::string> old_names;
  for (auto n : saved["old_packs"].items())
    old_names.insert(n.text());
  for (auto item : names.items()) {
    auto n = item.text();
    name(n);
    bool exists = exists_entry(p.packs / n);
    if (exists)
      check_kind(p.packs / n, true);
    // Older retirement manifests recorded only the list of touched packs.
    bool old = exists_entry(prev / "packs" / n);
    if (saved.contains("old_packs") && old != old_names.contains(n))
      throw Unsupported("Retired pack is incomplete: " + n);
    if (!saved.contains("old_packs") && !old)
      continue;
    if (old)
      check_kind(prev / "packs" / n, true);
    j["packs"].push(obj(
        {{"name", str(n)}, {"live", boolean(exists)}, {"old", boolean(old)}}));
  }
  bool has_old = exists_entry(prev / index),
       live = exists_entry(configured_index(p, index));
  if (saved.contains("old_index") && has_old != saved["old_index"].flag())
    throw Unsupported("Retired index is incomplete");
  if (has_old)
    check_kind(prev / index, false);
  if (live)
    check_kind(configured_index(p, index), false);
  j.set("old_index", boolean(has_old));
  j.set("live_index",
        boolean(live && (saved.contains("old_index") || has_old)));
  execute(p, j, progress);
  return true;
}
bool prune_update(const Paths &p, bool expired) {
  recover_update(p);
  auto prev = p.data / "previous";
  if (!exists_entry(prev))
    return false;
  check_kind(prev, true);
  if (expired) {
    auto retired = read_optional_json(prev / "retired.json")["retired"];
    if (!retired.is_number() || retired.real() <= 0 ||
        std::time(nullptr) - retired.real() < 7 * 86400)
      return false;
  }
  auto trash = p.data / "update/finished-transaction";
  move({prev, trash});
  fs::remove_all(trash);
  syncdir(trash.parent_path());
  return true;
}
} // namespace unllm::maintenance
