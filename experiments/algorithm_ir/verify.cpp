#include "validate.h"
// Independent expected values are stored in cases.json. All three
// implementations are exercised through the experiment's CLI or an emitted
// function's public API.
#include "unllm/symbolic/gui_target.h"
#include "unllm/symbolic/target.h"
#include <fstream>
#include <iostream>
#include <map>
using namespace unllm::native;
namespace fs = std::filesystem;
namespace {
ProcessResult command(const std::vector<std::string> &args) {
  return Process(args).finish(after_ms(30000));
}
std::string success(const std::vector<std::string> &args) {
  auto r = command(args);
  if (r.exit_code)
    throw Unsupported(args[0] + ": " + r.err);
  return r.out;
}
void check_host(const fs::path &cli, const fs::path &spec, const Json &row) {
  auto r = command({cli, "run", spec, row["args"].dump()});
  if (row.contains("error")) {
    if (r.exit_code == 1 && r.err == row["error"].text() + "\n")
      return;
  } else if (!r.exit_code &&
             canonical(Json::parse(r.out)) == canonical(row["expected"]))
    return;
  throw Unsupported("Interpreter mismatch: " + row.dump() + " " + r.out +
                    r.err);
}
} // namespace
int main(int argc, char **argv) {
  if (argc < 3 || argc > 4)
    return 2;
  bool py = argc == 4;
  auto dir =
      fresh_gui_directory(fs::temp_directory_path(), "unllm-algorithm-check");
  try {
    fs::path cli = argv[1], fixtures = argv[2];
    auto rows = Json::parse(read_file(fixtures / "cases.json"));
    std::map<std::string, std::vector<Json>> groups;
    for (auto row : rows.items())
      groups[row["spec"].text()].push_back(row);
    for (auto &[name, cases] : groups) {
      auto spec = fixtures / (name + ".json");
      for (auto &row : cases)
        check_host(cli, spec, row);
      auto src = success({cli, py ? "python" : "cpp", spec});
      unllm::algorithm::check_emission(dir, src, cases, py ? argv[3] : "");
    }
    // Exercise structural rejection through the same decoder used by both
    // emitters.
    for (auto src :
         {"[\"call\",\"entry\"]", "[\"int\",\"9223372036854775808\"]",
          "[\"var\",\"unknown\"]", "[\"bool\",true]",
          "[\"add\",[\"int\",\"0\"],[\"empty\"]]"}) {
      auto spec = Json::parse(
          R"({"version":1,"functions":[{"name":"entry","params":[],"locals":[],"result":"int","body":[]}]})");
      spec["functions"].at(0).set("return", Json::parse(src));
      std::ofstream(dir / "invalid.json") << spec.dump();
      if (command({cli, py ? "python" : "cpp", dir / "invalid.json"})
              .exit_code != 1)
        throw Unsupported("Decoder accepted invalid program");
    }
    std::cout << groups.size() << " supplied programs, " << rows.size()
              << " oracle cases matched interpreter and "
              << (py ? "Python" : "sanitized C++") << " output.\n";
    fs::remove_all(dir);
  } catch (const std::exception &e) {
    std::cerr << dir << ": " << e.what() << '\n';
    return 1;
  }
}
