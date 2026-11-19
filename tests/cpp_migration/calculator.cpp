#include "unllm/symbolic/calculator.h"
#include "unllm/symbolic/gui_target.h"
#include <algorithm>
#include <iostream>
using namespace unllm::native;
namespace fs = std::filesystem;
namespace {
template <class F> void rejects(F attempt) {
  try {
    attempt();
  } catch (const Unsupported &) {
    return;
  }
  throw std::runtime_error("Invalid calculator contract accepted");
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 4 && argc != 5)
    return 2;
  try {
    fs::path root = argv[1];
    std::string language = argv[3], python = argc == 5 ? argv[4] : "";
    auto base =
        fresh_gui_directory(root / ".state/cpp-migration", "calculator-suite");
    NativeEngine engine(root, argv[2]);
    auto from = [](const std::string &extra) {
      return CalculatorSpec::from(
          Json::parse("{\"program\":\"calculator\"," + extra + "}"));
    };
    for (auto bad :
         {R"("title":"")", R"("title":"a\nb")", R"("extra":1)",
          R"("operations":["add","add"])", R"("operations":[])",
          R"("operations":["AND"])", R"("schema":true)", R"("memory":1)",
          R"("keyboard":"yes")", R"("precision":10.0)", R"("precision":16)"})
      rejects([&] { from(bad); });
    auto reordered = from(R"("operations":["divide","subtract"])");
    if (reordered.operations != std::vector<std::string>{"subtract", "divide"})
      throw std::runtime_error("Unstable normalized operation order");
    rejects([&] {
      plan_calculator(reordered, language, engine, after_ms(4000),
                      {"app-component:antlr4"});
    });
    auto tasks =
        Json::parse(read_file(root / "tests/upgrade_apps.json"))["tasks"];
    size_t validated = 0, total = 0;
    for (auto t : tasks.items()) {
      auto spec = CalculatorSpec::from(t["spec"]);
      auto report = build_calculator(spec, language, root, base, engine,
                                     after_ms(180000), python, false);
      if (report["status"].text() != "validated")
        throw std::runtime_error(t["id"].text() + ": " + report["text"].text() +
                                 " " + report["validation"]["message"].text());
      if (report["construction"]["wires"].size() != 4 ||
          report["solver"].size() !=
              size_t(spec.memory ? 2 : 0) +
                  size_t(std::find(spec.operations.begin(),
                                   spec.operations.end(),
                                   "divide") != spec.operations.end()))
        throw std::runtime_error("Missing planner/solver evidence");
      auto checks = report["validation"]["cases"].integer();
      total += checks;
      ++validated;
      std::cout << t["id"].text() << " " << checks << " checks" << std::endl;
    }
    if (validated != 4)
      throw std::runtime_error("Incomplete calculator suite");
    std::cout << total << " checks. Artifacts " << base << std::endl;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
