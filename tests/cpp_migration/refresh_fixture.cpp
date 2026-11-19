#include <cstring>
// Integration fixture for the installed embedder and CLI. Arguments name the
// fixture data root, the program directory and an installed embedding model.
#include "unllm/maintenance/refresh.h"
#include "unllm/maintenance/transaction.h"
#include "unllm/symbolic/process.h"
#include <fstream>
#include <iostream>
using namespace unllm::native;
using namespace unllm::maintenance;
namespace fs = std::filesystem;
namespace {
void check(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
}
void empty_index(const Path &path) {
  std::ofstream out(path, std::ios::binary);
  out.exceptions(std::ios::badbit | std::ios::failbit);
  out.write("UNLPQ1\0\0", 8);
  int32_t fields[] = {384, 1, 1, 8, 0, 0};
  out.write(reinterpret_cast<char *>(fields), sizeof fields);
  int64_t total = 0;
  out.write(reinterpret_cast<char *>(&total), 8);
  int32_t one = 1, no = 3;
  out.write(reinterpret_cast<char *>(&one), 4);
  out.write(reinterpret_cast<char *>(&no), 4);
  char names[128] = "wiktionary-en";
  std::memcpy(names + 64, "shard-000.sqlite", 16);
  out.write(names, 128);
  std::string tables(384 * 257 * 4 + 8, '\0');
  out.write(tables.data(), tables.size());
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 7 && std::string_view(argv[1]) == "--branch") {
    Path packs = getenv("UNLLM_PACKS");
    std::cout << (packs.filename() == "candidate" ? "4" : "5")
              << "\n[branch: calc]\n";
    return 0;
  }
  if (argc != 4)
    return 2;
  try {
    Path root = fs::absolute(argv[1]), bin = fs::absolute(argv[2]);
    check(fs::create_directory(root), "expected fresh fixture root");
    Paths paths{root / "data", root / "data/packs", root / "downloads"};
    fs::create_directories(paths.packs);
    fs::create_directories(paths.data / "vectors");
    setenv("UNLLM_DATA", paths.data.c_str(), 1);
    setenv("UNLLM_EMBED", argv[3], 1);
    setenv("UNLLM_MODEL", "off", 1);
    setenv("XDG_STATE_HOME", root.c_str(), 1);
    setenv("UNLLM_FACT_DB", (root / "facts.sqlite").c_str(), 1);
    auto index = paths.data / "vectors/fixture.unlpq";
    empty_index(index);
    auto original = sha256(read_file(index));
    auto dest = paths.downloads / "wiktionary/kaikki-English.jsonl";
    const std::string payload =
        R"({"word":"redstart","pos":"noun","senses":[{"glosses":["a small bird with a red tail"]}]})";
    auto rows = Json::parse(
        R"([{"source":"wiktionary-en","folder":"wiktionary","newer":true,"fetch":[]}])");
    auto item = Json::array();
    for (auto s : {"https://fixture.invalid/dictionary", dest.c_str()})
      item.push(Json::string(s));
    item.push(Json::number(payload.size()));
    item.push(Json::string(sha256(payload)));
    rows.at(0)["fetch"].push(item);
    auto tsv = root / "verify.tsv";
    write_atomic(tsv,
                 "id\tquestion\texpect\tcategory\n1\tWhat is 2 plus 3?\t5\t\n");
    setenv("UNLLM_VERIFY_TSV", tsv.c_str(), 1);
    auto transport = [&](const std::string &, const Path &out) {
      write_atomic(out, payload);
    };
    UpdateLock lock(paths);
    RefreshOptions opt;
    opt.dry = true;
    refresh(paths, bin / "unllm", bin / "unllm_embed", rows, {}, opt,
            transport);
    check(!fs::exists(dest) && sha256(read_file(index)) == original,
          "dry run changed data");
    opt.dry = false;
    try {
      refresh(paths, bin / "unllm", bin / "unllm_embed", rows, {}, opt,
              [&](const auto &, const Path &out) {
                write_atomic(out, std::string(payload.size(), 'x'));
              });
      throw std::runtime_error("accepted incorrect checksum");
    } catch (const Unsupported &e) {
      check(std::string(e.what()).find("Checksum mismatch") !=
                std::string::npos,
            "unexpected download error");
    }
    check(!fs::exists(dest) && !fs::exists(paths.packs / "wiktionary-en"),
          "bad download published");
    auto reject_cli = fs::canonical("/proc/self/exe");
    try {
      refresh(paths, reject_cli, bin / "unllm_embed", Json::array(), {}, opt,
              transport);
      throw std::runtime_error("A lower candidate score was published");
    } catch (const Unsupported &e) {
      check(std::string(e.what()).find("score regressed") != std::string::npos,
            "unexpected candidate failure");
    }
    check(sha256(read_file(index)) == original &&
              !fs::exists(paths.packs / "wiktionary-en"),
          "rejected candidate changed active data");
    refresh(paths, bin / "unllm", bin / "unllm_embed", Json::array(), {}, opt,
            transport);
    check(fs::is_regular_file(paths.packs / "wiktionary-en/manifest.json"),
          "missing manifest");
    auto audit = Process({bin / "unllm_embed", "audit", index, paths.packs})
                     .finish(after_ms(5000));
    check(!audit.exit_code &&
              audit.out.find("every shard complete") != audit.out.npos,
          "index audit");
    check(rollback_update(paths) &&
              !fs::exists(paths.packs / "wiktionary-en") &&
              sha256(read_file(index)) == original,
          "rollback failed");
    std::cout << "Full fixture publication and rollback passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
