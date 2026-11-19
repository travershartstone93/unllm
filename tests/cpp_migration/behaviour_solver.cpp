#include "unllm/symbolic/behaviour_solver.h"
#include <iostream>
using namespace unllm::native;
int main() {
  try {
    auto raw = Json::parse(
        R"JSON({"state":{"multiply":{"type":"number","initial":0},"right":{"type":"number","initial":1},"flag":{"type":"bool","initial":false}},"actions":[
      {"id":"exchange","ensure":"next.multiply == prev.right and next.right == prev.multiply"},
      {"id":"affine","ensure":"2 * next.multiply == prev.multiply + 6 and next.right == prev.right + 1"},
      {"id":"square","ensure":"next.right == prev.multiply * prev.multiply"},
      {"id":"floorzero","ensure":"next.multiply == (0 if prev.multiply < 0 else prev.multiply)"},
      {"id":"guard","requires":"multiply >= 0","ensure":"next.right == prev.multiply + 3"},
      {"id":"flagguard","requires":"flag","ensure":"next.right == prev.multiply - 3"},
      {"id":"impossible","ensure":"next.multiply > prev.multiply and next.multiply < prev.multiply"}
    ]})JSON");
    auto spec = BehaviourSpec::from(raw);
    size_t probes = 0;
    for (auto action : spec.actions().items()) {
      auto name = action["id"].text();
      if (name == "impossible")
        continue;
      auto solved = synthesize_transition(spec, name, 7000);
      if (solved.evidence["precondition_encoded"].flag() != (name == "guard"))
        throw std::runtime_error("Guard encoding: " + name);
      for (double x : {-7., 0., 2.5, 29.})
        for (double y : {-2., 3.}) {
          if (name == "guard" && x < 0)
            continue;
          auto before = Json::object();
          before.set("multiply", Json::real(x));
          before.set("right", Json::real(y));
          before.set("flag", Json::boolean(true));
          auto after = before.clone();
          for (auto &[key, expression] : solved.updates)
            after.set(key, evaluate_behaviour(expression, before));
          auto left = after["multiply"].real(), right = after["right"].real();
          if ((name == "exchange" && (left != y || right != x)) ||
              (name == "affine" && (left != (x + 6) / 2 || right != y + 1)) ||
              (name == "square" && (left != x || right != x * x)) ||
              (name == "floorzero" &&
               (left != std::max(0., x) || right != y)) ||
              (name == "guard" && (left != x || right != x + 3)) ||
              (name == "flagguard" && (left != x || right != x - 3)))
            throw std::runtime_error("Incorrect synthesized update: " + name);
          ++probes;
        }
    }
    bool failed = false;
    try {
      synthesize_transition(spec, "impossible", 1000);
    } catch (const Unsupported &) {
      failed = true;
    }
    if (!failed)
      throw std::runtime_error("Contradictory transition accepted");
    std::cout << "Six synthesized transitions passed " << probes
              << " independent checks. Contradiction rejected.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
