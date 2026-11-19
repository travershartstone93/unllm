#include "unllm/symbolic/behaviour_job.h"
#include "unllm/symbolic/gui_target.h"
#include <fstream>
#include <iostream>
#include <random>
using namespace unllm::native;
namespace fs = std::filesystem;
namespace {
Json fresh_task() {
  auto task = Json::parse(R"SPEC({
 "id":"new_requirements","spec":{"schema":2,"title":"Fresh Requirements",
 "state":{"quantity":{"type":"number","initial":0},"text":{"type":"text","initial":""}},
 "views":[{"id":"quantity","kind":"number_input","state":"quantity"},{"id":"text","kind":"text_edit","state":"text"},{"id":"characters","kind":"label","value":"chars(text)"}],
 "actions":[{"id":"solve"},{"id":"transform","assign":{"text":"reverse_lines(upper(strip(text)))"}}]},
 "fresh":[{"initial":{},"events":[{"button":"solve"},{"input":"text","text":"  kiwi\nmango  "},{"button":"transform"}],"expect":{"state":{"text":"MANGO\nKIWI"}}}]})SPEC");
  std::random_device rng;
  int a = std::uniform_int_distribution<int>(2, 7)(rng),
      b = std::uniform_int_distribution<int>(3, 20)(rng),
      x = std::uniform_int_distribution<int>(11, 110)(rng);
  task["spec"].set("program",
                   Json::string("new_goal_" + std::to_string(rng())));
  task["spec"]["actions"].at(0).set(
      "ensure",
      Json::string(std::to_string(a) + " * next.quantity == prev.quantity + " +
                   std::to_string(b)));
  task["fresh"].at(0)["initial"].set("quantity", Json::number(x));
  task["fresh"].at(0)["expect"]["state"].set("quantity",
                                             Json::real(double(x + b) / a));
  return task;
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 4 && argc != 5)
    return 2;
  try {
    fs::path root = argv[1];
    std::string lang = argv[3], python = argc == 5 ? argv[4] : "";
    NativeEngine engine(root, argv[2]);
    auto base = fresh_gui_directory(root / ".state/cpp-migration",
                                    "compositions-" + lang);
    auto tasks =
        Json::parse(read_file(root / "tests/upgrade_behaviours.json"))["tasks"];
    tasks.push(fresh_task());
    std::ofstream(base / "tasks.json") << canonical(tasks) << '\n';
    int count = 0, extra = 0;
    for (auto task : tasks.items()) {
      auto spec = BehaviourSpec::from(task["spec"]);
      auto report = build_behaviour(spec, lang, root, base, engine,
                                    after_ms(180000), python, false);
      if (report["status"].text() != "validated")
        throw std::runtime_error(task["id"].text() + ": " +
                                 report["text"].text() + " " +
                                 report["validation"]["message"].text());
      fs::path job = report["job"].text();
      auto scenarios = task["fresh"];
      for (auto s : scenarios.items()) {
        auto output = fresh_gui_directory(job, "fresh-output");
        auto rows = Json::array();
        rows.push(s);
        auto input = Json::object();
        input.set("scenarios", rows);
        std::vector<std::string> cmd =
            lang == "cpp"
                ? std::vector<std::string>{"/job/gui_driver"}
                : std::vector<std::string>{python, "/job/gui_driver.py"};
        auto result =
            run_gui_target(job, output, cmd, input, false, after_ms(15000));
        if (result.exit_code)
          throw std::runtime_error(result.err);
        auto actual = Json::parse(result.out).at(0);
        check_behaviour_outcome(spec, s, actual, output);
        ++extra;
      }
      ++count;
      std::cout << task["id"].text() << " "
                << report["validation"]["cases"].integer()
                << " executable checks plus " << scenarios.size()
                << " independent cases" << std::endl;
    }
    // The same executing program must fail when its expected increment is
    // reversed.
    auto original = Json::parse(
        R"({"state":{"count":{"type":"number","initial":0}},"actions":[{"id":"adjust","ensure":"next.count == prev.count + 7"}]})");
    auto candidate =
        build_behaviour(BehaviourSpec::from(original), lang, root, base, engine,
                        after_ms(180000), python, true);
    if (candidate["status"].text() != "emitted")
      throw std::runtime_error(candidate["text"].text());
    original["actions"].at(0).set("ensure",
                                  Json::string("next.count == prev.count - 7"));
    auto fail = validate_behaviour(BehaviourSpec::from(original), root,
                                   candidate["job"].text(), lang,
                                   after_ms(180000), python);
    if (fail["valid"].flag() || fail["stage"].text() != "outcome" ||
        fail["message"].text() != "Transition postcondition failed")
      throw std::runtime_error("Reversed decrement control: " +
                               canonical(fail));
    if (count != 5 || extra < 5)
      throw std::runtime_error("Incomplete composition checks");
    std::cout << "Reversed decrement rejected. Artifacts " << base << std::endl;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
