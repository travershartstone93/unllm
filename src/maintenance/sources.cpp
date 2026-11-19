#include "unllm/maintenance/sources.h"
#include "../symbolic/gui_json.h"
#include <algorithm>
#include <charconv>
#include <ctime>
#include <curl/curl.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
namespace unllm::maintenance {
using namespace native;
using namespace native::gui;
namespace fs = std::filesystem;
namespace {
std::string env(const char *key, const std::string &fallback) {
  auto s = std::getenv(key);
  return s && *s ? s : fallback;
}
std::string day(time_t t, bool utc = false) {
  struct tm m{};
  if (utc)
    gmtime_r(&t, &m);
  else
    localtime_r(&t, &m);
  char buf[20];
  strftime(buf, sizeof buf, "%Y-%m-%d", &m);
  return buf;
}
std::string size_text(int64_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  double n = bytes;
  size_t u = 0;
  while (n >= 1000 && u < 4) {
    n /= 1000;
    ++u;
  }
  std::ostringstream out;
  out << std::fixed << std::setprecision(u ? 1 : 0) << n << " " << units[u];
  return out.str();
}
int64_t number(const Json &val) {
  if (val.is_integer() && val.integer() >= 0)
    return val.integer();
  auto s = val.text();
  int64_t n;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
  if (ec != std::errc{} || p != s.data() + s.size() || n < 0)
    throw Unsupported("Invalid source metadata number");
  return n;
}
std::string edition(const std::string &s) {
  std::string year;
  for (char c : s)
    if (c >= '0' && c <= '9' && year.size() < 4)
      year += c;
  return year;
}
bool basename(const std::string &s) {
  return !s.empty() && s != "." && s != ".." && fs::path(s).filename() == s &&
         s.find('\0') == s.npos;
}
Json installed(const Paths &p, const std::string &name) {
  auto m = read_optional_json(p.packs / name / "manifest.json");
  if (m.null())
    return {};
  time_t latest = 0;
  for (auto shard : m["shards"].items()) {
    auto f = shard["file"].text();
    if (!basename(f))
      continue;
    struct stat info{};
    if (stat((p.packs / name / f).c_str(), &info) == 0)
      latest = std::max(latest, info.st_mtime);
  }
  auto version = m["version"];
  return obj({{"version", version.is_string()
                              ? version
                              : str(version.null() ? "" : canonical(version))},
              {"built", str(latest ? day(latest) : "")}});
}
Json row(std::string source, std::string folder, std::string installed,
         std::string host) {
  return obj({{"source", str(source)},
              {"folder", str(folder)},
              {"installed", str(installed)},
              {"available", str("couldn't reach " + host)},
              {"newer", Json{}},
              {"note", str("")},
              {"fetch", Json::array()}});
}
std::string installed_text(const Json &pack, const std::string &fetched = {}) {
  if (pack.null())
    return "not installed";
  auto built = pack["built"].text();
  return pack["version"].text() + (built.empty() ? "" : ", built " + built) +
         (fetched.empty() ? "" : ", fetched " + fetched);
}
Json fetch_item(std::string url, Path dest, int64_t size,
                std::string hash = {}) {
  return arr({str(url), str(dest.string()), Json::number(size), str(hash)});
}
std::map<std::string, std::string> checksums(const Paths &p,
                                             const std::string &folder) {
  auto cache = p.data / "update" / (folder + ".sha256");
  for (const auto &file : {p.downloads / folder / "SHA256SUMS", cache}) {
    std::ifstream in(file);
    std::string line;
    std::map<std::string, std::string> result;
    while (std::getline(in, line)) {
      std::istringstream fields(line);
      std::string hash, name, extra;
      if (fields >> hash >> name && !(fields >> extra))
        result[name] = hash;
    }
    if (result.empty())
      continue;
    if (file != cache) {
      std::string text;
      for (auto &[name, hash] : result)
        text += hash + "  " + name + "\n";
      write_atomic(cache, text);
    }
    return result;
  }
  return {};
}
class Sources {
  const Paths &p;
  Json state;
  const Fetch &fetch;
  std::string today = day(time(nullptr));
  Json metadata(const std::string &url) {
    auto bytes = fetch(url, false);
    if (bytes.empty())
      return {};
    try {
      return Json::parse(bytes);
    } catch (const std::runtime_error &) {
      return {};
    }
  }
  std::string fetched(const std::string &folder) {
    return read_optional_json(p.downloads / folder / "source.json")["fetched"]
        .text();
  }

public:
  Sources(const Paths &p, Json s, const Fetch &f) : p(p), state(s), fetch(f) {}
  Json wiki(const std::string &subset, const std::string &folder,
            const std::string &source) {
    const std::string hub = "https://huggingface.co",
                      data = "HuggingFaceFW/finewiki",
                      api = hub + "/api/datasets/" + data;
    auto pack = installed(p, source);
    auto r = row(source, folder, installed_text(pack, fetched(folder)),
                 "huggingface.co");
    auto files = metadata(api + "/tree/main/data/" + subset);
    if (!files.is_array() || !files.size())
      return r;
    std::map<std::string, Json> remote;
    int64_t total = 0;
    for (auto x : files.items()) {
      auto path = x["path"].text(), name = fs::path(path).filename().string();
      if (!basename(name) || path != "data/" + subset + "/" + name ||
          remote.contains(name))
        return r;
      try {
        auto n = number(x["size"]);
        if (n > INT64_MAX - total)
          return r;
        total += n;
      } catch (const Unsupported &) {
        return r;
      }
      remote.emplace(name, x);
    }
    auto changed = metadata(api)["lastModified"].text().substr(0, 10);
    auto have = checksums(p, folder);
    int added = 0, differ = 0, removed = 0;
    auto hashes = Json::object();
    for (auto &[name, file] : remote) {
      auto hash = file["lfs"]["oid"].text();
      hashes.set(name, str(hash));
      if (!have.contains(name))
        ++added;
      else if (!hash.empty() && hash != have.at(name))
        ++differ;
      r["fetch"].push(fetch_item(
          hub + "/datasets/" + data + "/resolve/main/" + file["path"].text(),
          p.downloads / folder / name, number(file["size"]), hash));
    }
    for (auto &[name, _] : have)
      if (!remote.contains(name))
        ++removed;
    auto info = std::to_string(remote.size()) + " files, " + size_text(total);
    bool newer = false;
    if (have.empty()) {
      auto built = pack["built"].text();
      newer = !built.empty() && !changed.empty() && changed > built;
      if (!changed.empty())
        info += ", dataset last changed " + changed;
      r.set("note", str("no checksum list on this machine to compare file by "
                        "file; judged by the dataset's last-changed date"));
    } else {
      newer = added || differ || removed;
      if (!newer)
        info += ", all with the checksums you have";
      else {
        std::string changes;
        for (auto &[n, word] : std::vector<std::pair<int, std::string>>{
                 {differ, "changed"}, {added, "new"}, {removed, "gone"}})
          if (n) {
            if (!changes.empty())
              changes += ", ";
            changes += std::to_string(n) + " " + word;
          }
        info += ": " + changes;
      }
      if (!changed.empty())
        info += " (dataset last changed " + changed + ")";
    }
    r.set("available", str(info));
    r.set("newer", boolean(newer));
    state.set(source, obj({{"files", hashes}, {"seen", str(today)}}));
    return r;
  }
  Json stack() {
    std::istringstream sites(
        "cooking diy gardening travel outdoors bicycles pets money lifehacks "
        "health english ell physics chemistry biology astronomy history "
        "mechanics unix electronics math askubuntu superuser");
    std::set<std::string> all;
    std::string s, latest;
    int count = 0;
    while (sites >> s) {
      all.insert(s + (s == "askubuntu" || s == "superuser"
                          ? ".com.7z"
                          : ".stackexchange.com.7z"));
      auto pack = installed(p, "se-" + s);
      if (!pack.null()) {
        ++count;
        latest = std::max(latest, pack["built"].text());
      }
    }
    auto date = fetched("stackexchange");
    auto r = row("stackexchange (23 sites)", "stackexchange",
                 count ? std::to_string(count) + " of 23 packs, built " +
                             latest + (date.empty() ? "" : ", fetched " + date)
                       : "not installed",
                 "archive.org");
    auto files =
             metadata("https://archive.org/metadata/stackexchange")["files"],
         kept = Json::object(), downloads = Json::array();
    int64_t total = 0, newest = 0;
    for (auto f : files.items()) {
      auto name = f["name"].text();
      if (!all.contains(name))
        continue;
      if (kept.contains(name))
        return r;
      int64_t size, mtime;
      try {
        size = number(f["size"]);
        mtime = number(f["mtime"]);
      } catch (const Unsupported &) {
        return r;
      }
      if (size > INT64_MAX - total)
        return r;
      auto hash = f["md5"].text();
      auto last = state["stackexchange"]["files"][name]["md5"].text();
      auto local = p.downloads / "stackexchange" / name;
      if ((fs::is_regular_file(local) &&
           fs::file_size(local) != uint64_t(size)) ||
          (!last.empty() && last != hash) ||
          (!date.empty() && day(mtime) > date))
        downloads.push(fetch_item(
            "https://archive.org/download/stackexchange/" + name, local, size));
      total += size;
      newest = std::max(newest, mtime);
      kept.set(name, obj({{"size", Json::number(size)},
                          {"md5", str(hash)},
                          {"mtime", Json::number(mtime)}}));
    }
    if (!kept.size())
      return r;
    auto info = std::to_string(kept.size()) + " dumps, " + size_text(total) +
                ", newest dated " + day(newest) + "; " +
                (downloads.size()
                     ? std::to_string(downloads.size()) + " newer than yours"
                     : "none newer than yours");
    if (kept.size() < all.size())
      info += "; " + std::to_string(all.size() - kept.size()) +
              " no longer on the archive";
    r.set("fetch", downloads);
    r.set("available", str(info));
    r.set("newer", boolean(downloads.size()));
    state.set("stackexchange", obj({{"seen", str(today)}, {"files", kept}}));
    return r;
  }
  Json dictionary() {
    std::string name = "kaikki.org-dictionary-English.jsonl",
                url = "https://kaikki.org/dictionary/English/" + name;
    auto pack = installed(p, "wiktionary-en");
    auto date = fetched("wiktionary");
    auto r = row("wiktionary-en", "wiktionary", installed_text(pack, date),
                 "kaikki.org");
    std::istringstream lines(fetch(url, true));
    std::string line, modified;
    int64_t size = 0;
    while (std::getline(lines, line)) {
      if (line.starts_with("HTTP/")) {
        modified.clear();
        size = 0;
      }
      auto i = line.find(':');
      if (i == line.npos)
        continue;
      auto val = line.substr(i + 1);
      auto first = val.find_first_not_of(" \t");
      if (first == val.npos)
        continue;
      val = val.substr(first);
      val.erase(val.find_last_not_of("\r \t") + 1);
      auto k = key(line.substr(0, i));
      if (k == "last-modified") {
        auto t = curl_getdate(val.c_str(), nullptr);
        if (t >= 0)
          modified = day(t, true);
      }
      if (k == "content-length") {
        try {
          size = number(str(val));
        } catch (const Unsupported &) {
        }
      }
    }
    if (modified.empty() && !size)
      return r;
    auto local = p.downloads / "wiktionary" / name;
    int64_t have = fs::is_regular_file(local)
                       ? int64_t(fs::file_size(local))
                       : state["wiktionary-en"]["size"].integer();
    auto built = pack["built"].text();
    bool newer = (!date.empty() && modified > date) ||
                 (have && size && have != size) ||
                 (!built.empty() && modified > built);
    r.set("available", str("file dated " + modified + ", " + size_text(size)));
    r.set("newer", boolean(newer));
    r["fetch"].push(fetch_item(url, local, size));
    state.set("wiktionary-en",
              obj({{"modified", str(modified)},
                   {"size", Json::number(newer && have ? have : size)},
                   {"seen", str(today)}}));
    return r;
  }
  Json wordnet() {
    auto pack = installed(p, "wordnet-en");
    auto r = row("wordnet-en", "wordnet",
                 pack.null() ? "not installed"
                             : pack["version"].text() + " edition" +
                                   (pack["built"].text().empty()
                                        ? ""
                                        : ", built " + pack["built"].text()),
                 "github.com");
    auto meta = metadata("https://api.github.com/repos/globalwordnet/"
                         "english-wordnet/releases/latest");
    auto tag = meta["tag_name"].text(), year = edition(tag);
    if (tag.empty() || !basename(tag) || year.size() != 4)
      return r;
    auto name = "english-wordnet-" + year + "-json.zip";
    r.set("available", str(tag + ", published " +
                           meta["published_at"].text().substr(0, 10)));
    r.set("newer",
          boolean(!pack.null() && year > edition(pack["version"].text())));
    r["fetch"].push(fetch_item(
        "https://github.com/globalwordnet/english-wordnet/releases/download/" +
            tag + "/" + name,
        p.downloads / "wordnet" / name, 0));
    state.set("wordnet-en", obj({{"tag", str(tag)}, {"seen", str(today)}}));
    return r;
  }
};
} // namespace
Paths Paths::environment() {
  auto home = env("HOME", "");
  Path data = env("UNLLM_DATA",
                  env("XDG_DATA_HOME", home + "/.local/share") + "/unllm");
  return {data, env("UNLLM_PACKS", (data / "packs").string()),
          env("UNLLM_KNOWLEDGE", home + "/Downloads/unllm-knowledge")};
}
Json read_optional_json(const Path &file) {
  if (!fs::exists(file))
    return {};
  return Json::parse(read_file(file));
}
void write_atomic(const Path &file, const std::string &value) {
  fs::create_directories(file.parent_path());
  auto tmp = file.string() + ".tmp-XXXXXX";
  int fd = mkstemp(tmp.data());
  if (fd < 0)
    throw std::runtime_error("Cannot create temporary file: " + file.string());
  try {
    size_t done = 0;
    while (done < value.size()) {
      ssize_t n = ::write(fd, value.data() + done, value.size() - done);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error("Cannot write " + tmp);
      done += size_t(n);
    }
    if (fsync(fd) < 0)
      throw std::runtime_error("Cannot sync " + tmp);
    if (close(fd) < 0) {
      fd = -1;
      throw std::runtime_error("Cannot close " + tmp);
    }
    fd = -1;
    fs::rename(tmp, file);
    int dir =
        open(file.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0)
      throw std::runtime_error("Cannot open state directory");
    int rc = fsync(dir);
    close(dir);
    if (rc < 0)
      throw std::runtime_error("Cannot sync state directory");
  } catch (...) {
    if (fd >= 0)
      close(fd);
    unlink(tmp.c_str());
    throw;
  }
}
Json check_sources(const Paths &p, Json state, const Fetch &fetch) {
  if (!state.is_object())
    throw Unsupported("Update state must be an object");
  Sources s(p, state, fetch);
  auto rows = arr({s.wiki("enwiki", "finewiki-en", "wikipedia-en"),
                   s.wiki("simplewiki", "finewiki-simple", "wikipedia-simple"),
                   s.stack(), s.dictionary(), s.wordnet()});
  auto poetry = row("gutenberg-poetry", "gutenberg-poetry",
                    installed_text(installed(p, "gutenberg-poetry")), "");
  poetry.set("newer", boolean(false));
  poetry.set("available", str("a fixed file (v001); it does not change"));
  rows.push(poetry);
  state.set("checked", str(day(time(nullptr))));
  return rows;
}
Json public_check(const Json &rows) {
  auto sources = Json::array(), newer = Json::array(),
       unreachable = Json::array();
  for (auto item : rows.items()) {
    auto r = item.clone();
    r.erase("fetch");
    sources.push(r);
    if (r["newer"].null())
      unreachable.push(r["source"]);
    else if (r["newer"].flag())
      newer.push(r["source"]);
  }
  return obj(
      {{"sources", sources}, {"newer", newer}, {"unreachable", unreachable}});
}
int check_result(const Json &r) {
  return r["unreachable"].size() ? 2 : r["newer"].size() ? 1 : 0;
}
} // namespace unllm::maintenance
