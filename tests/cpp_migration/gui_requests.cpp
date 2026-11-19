#include "unllm/symbolic/gui_target.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>
using namespace unllm::native;
namespace fs = std::filesystem;
int main(int argc, char **argv) {
  if (argc != 5 && argc != 6)
    return 2;
  try {
    fs::path root = argv[1], cli = argv[2], worker = argv[3];
    std::string lang = argv[4], python = argc == 6 ? argv[5] : "";
    auto base = root / ".state/cpp-migration" /
                ("gui-requests-" + lang + "-" + std::to_string(getpid()));
    fs::create_directories(base);
    auto specs = Json::parse(
        read_file(root / "tests/cpp_migration/fixtures/request_specs.json"));
    auto workflows = Json::parse(
        read_file(root / "tests/upgrade_request_interactions.json"));
    std::map<std::string, std::string> questions;
    auto requests =
        Json::parse(read_file(root / "tests/upgrade_requests.json"));
    for (auto request : requests.items())
      questions.emplace(request["id"].text(), request["question"].text());
    const char *product = std::getenv("UNLLM_TEST_PRODUCT_CLI");
    if (product) {
      setenv("UNLLM_SYMBOLIC_RUNTIME", "native", 1);
      setenv("UNLLM_MODEL", "off", 1);
      setenv("UNLLM_NO_SERVER", "1", 1);
      setenv("UNLLM_FACT_DB", (base / "facts.sqlite").c_str(), 1);
      setenv("UNLLM_ARTIFACTS", base.c_str(), 1);
      setenv("UNLLM_PYTHON_TARGET", python.c_str(), 1);
    }
    int assertions = 0, runs = 0;
    auto log = Json::array();
    for (auto fixture : specs.items()) {
      auto id = fixture["id"].text();
      auto specfile = base / (id + ".json");
      std::ofstream(specfile) << fixture["spec"].dump();
      std::vector<std::string> command{
          cli.string(),      root.string(), worker.string(),
          specfile.string(), lang,          base.string()};
      if (lang == "python")
        command.insert(command.end(), {"--python-target", python});
      if (product)
        command = {product, "--json", "--code", lang, questions.at(id)};
      Process generator(command);
      auto generated = generator.finish(after_ms(190000));
      auto report = Json::parse(generated.out);
      if (generated.exit_code || report["status"].text() != "validated")
        throw std::runtime_error(id + " generation: " + report["text"].text() +
                                 " " + report["validation"]["message"].text() +
                                 " " + report["validation"]["stderr"].text());
      auto actual = report["specification"].clone();
      if (product && actual["interpretation"].contains("requirements"))
        actual["interpretation"].erase("requirements");
      if (canonical(actual) != canonical(fixture["spec"]))
        throw std::runtime_error(id + " produced a different specification");
      fs::path job = report["job"].text(), output = job / "independent-output";
      fs::create_directory(output);
      if (sha256(read_file(report["source"].text())) !=
          report["source_sha256"].text())
        throw std::runtime_error("Generated source hash changed");
      std::vector<std::string> probe;
      if (lang == "python") {
        fs::copy_file(root / "tools/probes/request_python.py",
                      job / "independent_probe.py");
        probe = {python, "/job/independent_probe.py"};
      } else {
        fs::copy_file(root / "tools/probes/request_cpp.cpp",
                      job / "independent_probe.cpp");
        std::vector<std::string> compile{
            "/usr/bin/clang++", "-std=c++20", "-O0",
            (job / "independent_probe.cpp").string()};
        auto flags = qt_compile_flags(true, after_ms(3000));
        compile.insert(compile.end(), flags.begin(), flags.end());
        compile.insert(compile.end(),
                       {"-o", (job / "independent_probe").string()});
        auto r = Process(compile).finish(after_ms(60000));
        if (r.exit_code)
          throw std::runtime_error(r.err);
        probe = {"/job/independent_probe"};
      }
      auto r = run_gui_target(job, output, probe, workflows[id], false,
                              after_ms(30000));
      if (r.exit_code)
        throw std::runtime_error(id + " independent probe: " + r.err);
      auto result = Json::parse(r.out);
      result.set("job", report["job"]);
      result.set("id", Json::string(id));
      log.push(result);
      std::ofstream(base / "workflows.json") << canonical(log) << "\n";
      if (!result["passed"].flag() || !result["checks"].size())
        throw std::runtime_error(id + ": " + r.out);
      assertions += result["checks"].size();
      ++runs;
      std::cout << id << " passed " << result["checks"].size()
                << " independent checks" << std::endl;
    }
    if (runs != 6 || assertions != 39)
      throw std::runtime_error("Incomplete independent workflow suite");
    std::cout << lang << ": six native-generated applications, " << assertions
              << " independent assertions. Artifacts: " << base << std::endl;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
