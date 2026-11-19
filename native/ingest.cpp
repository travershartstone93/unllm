#include "unllm/maintenance/ingest.h"
#include "unllm/maintenance/transaction.h"
#include "unllm/symbolic/process.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace unllm::maintenance;
using namespace unllm::native;
namespace fs = std::filesystem;
namespace {
void valid_name(const std::string &name) {
  if (name.empty() || name.front() == '.' ||
      name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVW"
                             "XYZ0123456789_.-") != name.npos)
    throw Unsupported("Invalid pack name");
}
std::vector<Path> entries(const Path &dir) {
  std::vector<Path> out;
  if (fs::is_directory(dir))
    for (auto &e : fs::directory_iterator(dir))
      out.push_back(e.path());
  std::sort(out.begin(), out.end());
  return out;
}
void add(const Paths &p, const Path &source, const std::string &override,
         bool force, unsigned workers) {
  auto version = source_version(source, override), name = version["name"];
  valid_name(name.text());
  auto old = read_optional_json(p.packs / name.text() / "manifest.json");
  if (!force && old["ingest_version"].text() == "4-native-1" &&
      old["version"].text() >= version["version"].text()) {
    std::cout << name.text() << " already installed\n";
    return;
  }
  auto progress = p.data / "progress/ingest.json";
  auto report = [&](size_t done, size_t total) {
    auto r = Json::object();
    r.set("name", name);
    r.set("pid", Json::number(getpid()));
    r.set("finished_shards", Json::number(done));
    r.set("total_shards", Json::number(total));
    write_atomic(progress, canonical(r) + "\n");
  };
  auto m = stage_pack(p, source, name.text(), force, workers, report, true);
  auto ranker = fs::canonical("/proc/self/exe").parent_path() / "unllm_index";
  if (!fs::exists(ranker))
    ranker = ranker.parent_path().parent_path() / "unllm_index";
  if (fs::exists(ranker)) {
    auto r = Process({ranker, p.data / "staging/packs" / name.text()})
                 .finish(std::chrono::steady_clock::now() +
                         std::chrono::hours(24 * 7));
    if (r.exit_code)
      throw Unsupported("Pack ranking failed: " + r.err);
  } else if (fs::exists(p.packs / name.text() / "ranked") ||
             fs::exists(p.packs / name.text() / "tantivy"))
    throw Unsupported("A ranked pack requires unllm_index");
  install_staged_pack(p, name.text());
  fs::create_directories(p.data / "learned");
  std::ofstream log(p.data / "learned/new-packs.txt", std::ios::app);
  log << std::time(nullptr) << '\t' << name.text() << '\n';
  fs::remove(progress);
  std::cout << canonical(m) << '\n';
}
std::vector<Path> sources(const Path &path) {
  std::vector<Path> out;
  for (auto &entry : entries(path))
    if (entry.extension() == ".7z")
      out.push_back(entry);
  if (out.empty())
    out.push_back(path);
  return out;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc < 2)
      throw Unsupported(
          "Use stage, add, list, check, remove, progress or inbox");
    auto p = Paths::environment();
    std::string cmd = argv[1];
    if (cmd == "list" || cmd == "check") {
      if (argc > (cmd == "list" ? 2 : 3))
        throw Unsupported("Unexpected pack argument");
      for (auto &dir : entries(p.packs)) {
        if (argc == 3 && dir.filename() != argv[2])
          continue;
        auto m = read_optional_json(dir / "manifest.json");
        if (m.null())
          continue;
        if (cmd == "list")
          std::cout << m["name"].text() << '\t' << m["version"].text() << '\t'
                    << m["passages"].integer() << " passages\n";
        else
          std::cout << dir.filename() << '\t' << canonical(inspect_pack(dir))
                    << '\n';
      }
      return 0;
    }
    if (cmd == "progress") {
      bool watch = argc == 3 && std::string_view(argv[2]) == "--watch";
      if (argc != 2 && !watch)
        throw Unsupported("Use progress [--watch]");
      do {
        auto r = read_optional_json(p.data / "progress/ingest.json");
        if (r.null() || r["pid"].integer() <= 0 ||
            kill(r["pid"].integer(), 0) != 0)
          std::cout << "No active ingestion\n";
        else
          std::cout << r["name"].text() << ": "
                    << r["finished_shards"].integer() << "/"
                    << r["total_shards"].integer() << " shards\n";
        std::cout.flush();
        if (watch)
          std::this_thread::sleep_for(std::chrono::seconds(2));
      } while (watch);
      return 0;
    }
    UpdateLock lock(p);
    recover_update(p);
    if (cmd == "remove" && argc == 3) {
      valid_name(argv[2]);
      auto pack = p.packs / argv[2];
      if (fs::exists(pack))
        fs::remove_all(pack);
      return 0;
    }
    if (cmd == "stage" || cmd == "add") {
      std::vector<Path> input;
      std::string name;
      unsigned workers = cmd == "add" ? 2 : 1;
      bool force = false;
      for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--name" && i + 1 < argc) {
          name = argv[++i];
          valid_name(name);
        } else if (cmd == "add" && a == "--force")
          force = true;
        else if (cmd == "add" && a == "--workers" && i + 1 < argc) {
          std::string n = argv[++i];
          auto [end, ec] =
              std::from_chars(n.data(), n.data() + n.size(), workers);
          if (ec != std::errc{} || end != n.data() + n.size() || !workers)
            throw Unsupported("Invalid worker count");
        } else if (a.starts_with("--"))
          throw Unsupported("Unknown option " + a);
        else {
          auto listed = cmd == "add" ? sources(a) : std::vector<Path>{a};
          input.insert(input.end(), listed.begin(), listed.end());
        }
      }
      if (input.empty() || (cmd == "stage" && input.size() != 1))
        throw Unsupported("Expected pack source");
      if (cmd == "stage")
        std::cout << canonical(stage_pack(p, input[0], name)) << '\n';
      else
        for (auto &source : input)
          add(p, source, input.size() == 1 ? name : "", force, workers);
      return 0;
    }
    if (cmd == "inbox") {
      bool night = false;
      double hours = 6;
      for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--night-only")
          night = true;
        else if (a == "--max-hours" && i + 1 < argc) {
          std::string n = argv[++i];
          size_t used;
          hours = std::stod(n, &used);
          if (used != n.size() || !std::isfinite(hours) || hours < 0)
            throw Unsupported("Invalid inbox time budget");
        } else
          throw Unsupported("Invalid inbox option " + a);
      }
      auto dir = p.data / "inbox";
      fs::create_directories(dir);
      auto start = std::time(nullptr);
      for (auto &entry : entries(dir)) {
        if (entry.filename() == "done")
          continue;
        Path target = entry;
        if (entry.extension() == ".source") {
          auto text = read_file(entry);
          text.erase(text.find_last_not_of(" \t\r\n") + 1);
          target = text;
        }
        auto now = std::time(nullptr);
        if (std::difftime(now, start) > hours * 3600)
          break;
        if (night && (std::localtime(&now)->tm_hour < 1 ||
                      std::localtime(&now)->tm_hour >= 7)) {
          uintmax_t size = 0;
          if (fs::is_regular_file(target))
            size = fs::file_size(target);
          else
            for (auto &f : fs::recursive_directory_iterator(target))
              if (f.is_regular_file())
                size += f.file_size();
          if (size > 300 * 1024 * 1024)
            continue;
        }
        for (auto &s : sources(target))
          add(p, s, "", false, 2);
        char day[16];
        std::strftime(day, sizeof day, "%Y-%m-%d", std::localtime(&now));
        auto done = dir / "done" / day;
        fs::create_directories(done);
        fs::rename(entry, done / entry.filename());
      }
      return 0;
    }
    throw Unsupported("Unknown ingest command");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
