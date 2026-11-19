#include "unllm/maintenance/evaluate.h"
#include <charconv>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace fs = std::filesystem;
using namespace unllm::maintenance;
using namespace unllm::native;
namespace {
std::string timestamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char text[32];
  strftime(text, sizeof text, "%Y-%m-%d-%H%M%S", &tm);
  return text;
}
struct ReportLock {
  int fd;
  explicit ReportLock(const Path &p) {
    fd = open(p.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
      throw Unsupported("Cannot open report lock");
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
      close(fd);
      throw Unsupported("Evaluation already writing this report");
    }
  }
  ~ReportLock() { close(fd); }
};
std::string identity(const Path &p) {
  struct stat st{};
  if (stat(p.c_str(), &st) < 0)
    throw Unsupported("Cannot identify " + p.string());
  return fs::absolute(p).string() + " " + std::to_string(st.st_dev) + " " +
         std::to_string(st.st_ino) + " " + std::to_string(st.st_size) + " " +
         std::to_string(st.st_mtim.tv_sec) + " " +
         std::to_string(st.st_mtim.tv_nsec);
}
Json resume(const Path &file) {
  if (!fs::exists(file))
    return Json::object();
  std::ifstream in(file, std::ios::binary);
  std::string line;
  uintmax_t bytes = 0;
  auto rows = Json::object();
  while (std::getline(in, line)) {
    if (in.eof()) {
      fs::resize_file(file, bytes);
      break;
    }
    auto r = Json::parse(line);
    rows.set(required_text(r, "id"), r);
    bytes += line.size() + 1;
  }
  return rows;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc < 3 || std::string(argv[1]) != "tsv")
      throw Unsupported("Use tsv FILE... [--unllm PATH] [--out DIR] [--workers "
                        "N] [--layers L] [--label NAME]");
    auto bin = fs::canonical("/proc/self/exe").parent_path() / "unllm";
    Path output = Path(getenv("HOME") ? getenv("HOME") : ".") /
                  "Downloads/unllm-corpus-sources/reports/eval";
    std::vector<Path> inputs;
    unsigned workers = 1;
    std::string layers, label;
    for (int i = 2; i < argc; ++i) {
      std::string arg = argv[i];
      auto take = [&] {
        if (++i >= argc)
          throw Unsupported("Missing option value for " + arg);
        return std::string(argv[i]);
      };
      if (arg == "--out")
        output = take();
      else if (arg == "--unllm")
        bin = take();
      else if (arg == "--layers") {
        layers = take();
        setenv("UNLLM_LAYERS", layers.c_str(), 1);
      } else if (arg == "--workers") {
        auto n = take();
        auto [p, err] = std::from_chars(n.data(), n.data() + n.size(), workers);
        if (err != std::errc{} || p != n.data() + n.size() || workers < 1 ||
            workers > 128)
          throw Unsupported("Invalid worker count");
      } else if (arg == "--label")
        label = take();
      else if (arg.starts_with("-"))
        throw Unsupported("Unknown option: " + arg);
      else
        inputs.emplace_back(arg);
    }
    if (inputs.empty() ||
        (!label.empty() && (fs::path(label).filename() != label ||
                            label == "." || label == "..")))
      throw Unsupported("Expected question files and a valid label");
    fs::create_directories(output);
    auto stamp = timestamp();
    auto prefix = label.empty() ? "" : label + "-";
    std::string summary = "unllm evaluation " + stamp +
                          (layers.empty() ? "" : " layers=" + layers) +
                          (label.empty() ? "" : " (" + label + ")") + "\n";
    for (const auto &file : inputs) {
      auto name = file.stem().string(), tag = prefix + name;
      bool hidden =
          name.find("sealed") != name.npos || name.find("heldout") != name.npos;
      auto ask = [&](const std::string &q) { return ask_candidate(bin, q); };
      Evaluation score;
      if (hidden)
        score = evaluate_tsv(file, ask, {}, workers);
      else {
        auto checkpoint = output / (tag + ".resume.json"),
             results = output / (tag + ".jsonl");
        ReportLock lock(output / (tag + ".lock"));
        auto metadata = Json::object();
        metadata.set("cli", Json::string(identity(bin)));
        metadata.set("tsv", Json::string(sha256(read_file(file))));
        metadata.set("layers", Json::string(layers));
        for (auto var :
             {"UNLLM_PACKS", "UNLLM_MEANING", "UNLLM_ENGINE", "UNLLM_MODEL",
              "UNLLM_SYMBOLIC_RUNTIME", "UNLLM_FACT_DB", "UNLLM_LAYERS"})
          if (auto v = getenv(var))
            metadata.set(var, Json::string(v));
        auto existing = read_optional_json(checkpoint);
        auto done = Json::object();
        if (canonical(metadata) == canonical(existing))
          done = resume(results);
        else {
          write_atomic(results, "");
          write_atomic(checkpoint, canonical(metadata) + "\n");
        }
        std::ofstream out(results, std::ios::app);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        score = evaluate_tsv(
            file, ask,
            [&](const Json &r) { out << canonical(r) << '\n'
                                     << std::flush; },
            workers, done);
        out.close();
        fs::remove(checkpoint);
      }
      summary += score.summary(name);
    }
    write_atomic(output / (prefix + stamp + "-summary.txt"), summary);
    std::cout << summary;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
