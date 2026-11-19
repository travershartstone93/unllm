#include "unllm/maintenance/refresh.h"
#include "unllm/maintenance/evaluate.h"
#include "unllm/maintenance/ingest.h"
#include "unllm/maintenance/transaction.h"
#include "unllm/symbolic/process.h"
#include <algorithm>
#include <climits>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
namespace unllm::maintenance {
using namespace native;
namespace fs = std::filesystem;
namespace {
Path setting(const char *name, Path fallback) {
  auto value = std::getenv(name);
  return value && *value ? Path(value) : fallback;
}
std::string component(std::string name) {
  if (name.empty() || name.front() == '.' ||
      name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVW"
                             "XYZ0123456789_.-") != name.npos)
    throw Unsupported("Invalid index or pack component: " + name);
  return name;
}
ProcessResult execute(const std::vector<std::string> &args, int allowed = 0) {
  auto r = Process(args).finish(std::chrono::steady_clock::now() +
                                std::chrono::hours(24 * 7));
  if (r.exit_code < 0 || r.exit_code > allowed)
    throw Unsupported(args.front() + " exited " + std::to_string(r.exit_code) +
                      ": " + r.err);
  return r;
}
struct Entry {
  int no;
  std::string pack, file;
  int64_t indexed, actual;
};
std::vector<Entry> audit(const Path &embed, const Path &index,
                         const Path &packs) {
  auto report = execute({embed, "audit", index, packs}, 1);
  std::istringstream lines(report.out);
  std::string line;
  std::vector<Entry> result;
  std::set<int> ids;
  std::set<std::pair<std::string, std::string>> names;
  while (std::getline(lines, line)) {
    if (!line.starts_with("shard "))
      continue;
    std::istringstream fields(line);
    std::string label, index_label, pack_label;
    Entry s{};
    if (!(fields >> label >> s.no >> s.pack >> s.file >> index_label >>
          s.indexed >> pack_label >> s.actual) ||
        s.no < 0 || s.no == INT_MAX || index_label != "index" ||
        pack_label != "pack" || !ids.insert(s.no).second ||
        !names.emplace(s.pack, s.file).second)
      throw Unsupported("Invalid vector index shard table");
    component(s.pack);
    component(s.file);
    result.push_back(s);
  }
  if (result.empty())
    throw Unsupported("The vector index has no named shards");
  return result;
}
Path active_index(const Paths &p) {
  auto dir = p.data / "vectors";
  Path index = setting("UNLLM_MEANING", {});
  if (index.empty() && fs::is_directory(dir))
    for (auto &file : fs::directory_iterator(dir))
      if (file.path().extension() == ".unlpq" &&
          (index.empty() || file.path() < index))
        index = file.path();
  if (index.empty() || !fs::is_regular_file(index))
    throw Unsupported("No active vector index");
  component(index.filename());
  return fs::absolute(index).lexically_normal();
}
void download(const Json &item, const DownloadFile &transport) {
  Path file = item.at(1).text(), partial = file.string() + ".part";
  fs::create_directories(file.parent_path());
  if (transport)
    transport(item.at(0).text(), partial);
  else
    execute({"/usr/bin/curl", "--fail", "--silent", "--show-error",
             "--location", "--proto", "=https", "--proto-redir", "=https",
             "--retry", "8", "--retry-all-errors", "--continue-at", "-",
             "--output", partial, "--", item.at(0).text()});
  const auto size = item.at(2).integer();
  if (size > 0 && fs::file_size(partial) != static_cast<uintmax_t>(size))
    throw Unsupported("Incomplete download: " + partial.string());
  auto hash = item.at(3).text();
  if (!hash.empty() &&
      execute({"/usr/bin/sha256sum", "--", partial}).out.substr(0, 64) !=
          hash) {
    fs::rename(partial, file.string() + ".bad");
    throw Unsupported("Checksum mismatch: " + file.string());
  }
  fs::rename(partial, file);
}
int score(const Path &cli, const Path &packs, const Path &index,
          const Path &tsv) {
  auto answer = [&](const std::string &q) {
    return ask_candidate(cli, q,
                         {"UNLLM_PACKS=" + packs.string(),
                          "UNLLM_MEANING=" + index.string(),
                          "UNLLM_NO_SERVER=1"});
  };
  return evaluate_tsv(tsv, answer).right;
}
} // namespace
void refresh(const Paths &paths, const Path &cli, const Path &embed,
             const Json &rows, const Json &state, const RefreshOptions &opts,
             const DownloadFile &transport) {
  auto index = active_index(paths);
  auto table = audit(embed, index, paths.packs);
  auto stage = paths.data / "staging";
  std::map<std::string, Path> inputs;
  std::vector<Json> sources;
  uintmax_t bytes = 0;
  auto pending_file = paths.data / "update/pending-sources.json";
  auto pending = read_optional_json(pending_file);
  if (pending.null())
    pending = Json::object();
  std::map<std::string, Json> selected;
  if (!opts.repair)
    for (auto &[name, entry] : pending.members())
      selected[name] = entry;
  for (auto row : rows.items())
    if (row["fetch"].size() && (row["newer"].flag() || opts.full))
      selected[row["source"].text()] = row;
  for (auto &[source, row] : selected) {
    auto name = row["source"].text(), folder = row["folder"].text();
    if (!row["fetch"].size() ||
        !(opts.sources.empty() || opts.sources.contains(name) ||
          opts.sources.contains(folder)))
      continue;
    sources.push_back(row);
    for (auto item : row["fetch"].items()) {
      Path dest = item.at(1).text();
      if (dest.parent_path() != paths.downloads / component(folder))
        throw Unsupported("Unexpected download destination");
      component(dest.filename());
      bytes += item.at(2).integer();
      if (name.starts_with("stackexchange")) {
        auto site = dest.filename().string();
        inputs["se-" + component(site.substr(0, site.find('.')))] = dest;
      } else
        inputs[component(name)] =
            name.starts_with("wikipedia") ? dest.parent_path() : dest;
    }
  }
  std::vector<Entry> jobs;
  if (opts.repair)
    for (auto &e : table)
      if (e.actual >= 0 && e.actual != e.indexed)
        jobs.push_back(e);
  if (inputs.empty() && jobs.empty()) {
    std::cout << "No pack or index changes required\n";
    return;
  }
  for (auto &[name, input] : inputs)
    std::cout << "Build " << name << " from " << input << "\n";
  for (auto &e : jobs)
    std::cout << "Repair " << e.pack << "/" << e.file << "\n";
  if (opts.dry)
    return;
  const auto free_before = fs::space(paths.data).available;
  if (bytes * 3.5L > free_before)
    throw Unsupported("Insufficient space for refresh");
  if (!sources.empty()) {
    for (auto row : sources)
      pending.set(row["source"].text(), row);
    write_atomic(pending_file, canonical(pending) + "\n");
  }
  for (auto row : sources)
    for (auto item : row["fetch"].items())
      download(item, transport);
  // Only the files in this release belong to the rebuilt Wiki pack. Retired
  // downloads may still be present in the user's download folder.
  for (auto row : sources) {
    auto name = row["source"].text();
    if (!name.starts_with("wikipedia"))
      continue;
    auto folder = stage / "inputs" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    auto side = read_optional_json(inputs.at(name) / "source.json");
    if (side.null())
      side = Json::object();
    side.set("source", Json::string("HuggingFaceFW/finewiki"));
    write_atomic(folder / "source.json", canonical(side));
    for (auto item : row["fetch"].items()) {
      Path file = item.at(1).text();
      fs::create_symlink(fs::absolute(file), folder / file.filename());
    }
    inputs[name] = folder;
  }
  std::set<int> used;
  std::map<std::pair<std::string, std::string>, int> existing;
  for (auto &e : table) {
    used.insert(e.no);
    existing[{e.pack, e.file}] = e.no;
  }
  std::vector<std::string> names;
  for (auto &[name, input] : inputs) {
    auto manifest = stage_pack(paths, input, name);
    auto ranker = embed.parent_path() / "unllm_index";
    if (fs::is_regular_file(ranker))
      execute({ranker, stage / "packs" / name});
    else if (fs::exists(paths.packs / name / "tantivy") ||
             fs::exists(paths.packs / name / "ranked"))
      throw Unsupported("A ranked pack needs unllm_index before publication");
    names.push_back(name);
    for (auto shard : manifest["shards"].items()) {
      auto file = component(required_text(shard, "file"));
      auto it = existing.find({name, file});
      int no =
          it == existing.end() ? std::max(100, *used.rbegin()) + 1 : it->second;
      if (no == INT_MAX)
        throw Unsupported("Shard number exhausted");
      used.insert(no);
      jobs.push_back({no, name, file, 0, shard["passages"].integer()});
    }
  }
  auto candidate = stage / "candidate";
  fs::create_directories(stage / "codes");
  fs::remove_all(candidate);
  fs::create_directory(candidate);
  for (auto &e : fs::directory_iterator(paths.packs)) {
    auto name = e.path().filename().string();
    if (!inputs.contains(name) && !name.ends_with(".building") &&
        !name.ends_with(".previous"))
      fs::create_directory_symlink(fs::absolute(e.path()), candidate / name);
  }
  for (auto &name : names)
    fs::create_directory_symlink(fs::absolute(stage / "packs" / name),
                                 candidate / name);
  auto out = stage / index.filename();
  auto model = setting("UNLLM_EMBED", paths.data / "models/bge-small-en-v1.5");
  std::vector<std::string> patch{embed, "patch", index, out};
  std::set<int> refreshed;
  for (auto &j : jobs) {
    auto id = std::to_string(j.no);
    auto codes = stage / "codes" / (id + ".codes");
    execute({embed, "encode", index, model, candidate / j.pack / j.file, id,
             codes});
    patch.insert(patch.end(), {"--add", codes, id, j.pack, j.file});
    refreshed.insert(j.no);
  }
  for (auto &e : table)
    if (inputs.contains(e.pack) && !refreshed.contains(e.no))
      patch.insert(patch.end(), {"--drop", std::to_string(e.no)});
  execute(patch);
  auto missing = refreshed;
  for (auto &e : audit(embed, out, candidate)) {
    if (refreshed.contains(e.no) && (e.actual < 0 || e.actual != e.indexed))
      throw Unsupported("Candidate shard count mismatch");
    missing.erase(e.no);
  }
  if (!missing.empty())
    throw Unsupported("A refreshed shard is missing from the candidate index");
  auto tsv = setting("UNLLM_VERIFY_TSV", state["verify_tsv"].text());
  if (!tsv.empty()) {
    int before = score(cli, paths.packs, index, tsv),
        after = score(cli, candidate, out, tsv);
    std::cout << "Verification: live " << before << " right, candidate "
              << after << " right\n";
    if (after < before)
      throw Unsupported("Candidate score regressed");
  } else
    std::cout << "No verification TSV configured\n";
  publish_verified(paths, names, index.filename());
  for (auto row : sources) {
    auto dir = paths.downloads / row["folder"].text();
    auto side = read_optional_json(dir / "source.json");
    if (side.null())
      side = Json::object();
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char day[16];
    std::strftime(day, sizeof day, "%Y-%m-%d", &tm);
    side.set("fetched", Json::string(day));
    if (!side.contains("name"))
      side.set("name", row["source"]);
    if (row["source"].text().starts_with("wikipedia")) {
      std::string sums;
      for (auto item : row["fetch"].items())
        if (!item.at(3).text().empty())
          sums += item.at(3).text() + "  " +
                  Path(item.at(1).text()).filename().string() + "\n";
      write_atomic(dir / "SHA256SUMS", sums);
    }
    write_atomic(dir / "source.json", canonical(side) + "\n");
  }
  for (auto row : sources)
    pending.erase(row["source"].text());
  if (!sources.empty())
    write_atomic(pending_file, canonical(pending) + "\n");
  fs::remove_all(candidate);
  fs::remove_all(stage / "codes");
  if (free_before < (uintmax_t{80} << 30))
    prune_update(paths, false);
  std::cout << "Published " << jobs.size() << " refreshed shards\n";
}
} // namespace unllm::maintenance
