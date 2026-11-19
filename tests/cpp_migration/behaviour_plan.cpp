#include "unllm/symbolic/behaviour_plan.h"
#include <iostream>
using namespace unllm::native;
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  try {
    NativeEngine engine(argv[1], argv[2]);
    auto cases = Json::parse(
        read_file(std::filesystem::path(argv[1]) /
                  "tests/cpp_migration/fixtures/behaviour_plans.json"));
    for (auto item : cases.items()) {
      auto spec = BehaviourSpec::from(item["spec"]);
      auto p = plan_behaviour(spec, item["language"].text(), argv[1], engine,
                              after_ms(5000));
      for (auto &[property, expected] : item["expected"].members())
        if (canonical(p[property]) != canonical(expected))
          throw std::runtime_error("Plan parity: " + item["id"].text() + " " +
                                   property + "\n" + canonical(p[property]) +
                                   "\n" + canonical(expected));
      if (!p["derivation"]["valid"].flag() ||
          !p["wire_derivation"]["valid"].flag())
        throw std::runtime_error("Missing independent proof");
      auto graph = construct_behaviour(spec, p, after_ms(10000));
      if (graph.actions.size() != spec.actions().size())
        throw std::runtime_error("Missing handler");
    }
    auto raw = cases.at(0)["spec"].clone();
    raw.set("program", Json::string("unknown_to_the_catalog"));
    auto spec = BehaviourSpec::from(raw);
    auto p = plan_behaviour(spec, "cpp", argv[1], engine, after_ms(5000));
    if (canonical(p["action_plans"]) !=
        canonical(cases.at(0)["expected"]["action_plans"]))
      throw std::runtime_error("Label changed planning");
    bool failed = false;
    try {
      plan_behaviour(spec, "cpp", argv[1], engine, after_ms(5000),
                     {"behaviour:write_atomic_utf8"});
    } catch (const Unsupported &) {
      failed = true;
    }
    if (!failed)
      throw std::runtime_error("Missing persistence capability accepted");
    std::cout << "Eight reference plans matched; arbitrary name and disabled "
                 "provider checked\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
