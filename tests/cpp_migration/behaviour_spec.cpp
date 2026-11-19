#include "unllm/symbolic/behaviour.h"
#include <iostream>
using namespace unllm::native;
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    auto cases = Json::parse(
        read_file(std::filesystem::path(argv[1]) /
                  "tests/cpp_migration/fixtures/behaviour_specs.json"));
    for (auto item : cases.items()) {
      auto raw = item["input"];
      auto before = canonical(raw);
      Json result;
      try {
        auto spec = BehaviourSpec::from(raw);
        result = spec.data();
        auto copy = spec.state();
        copy.set("changed", Json::boolean(true));
        if (spec.state().contains("changed"))
          throw std::logic_error("Mutable checked state");
      } catch (const Unsupported &) {
      }
      if (canonical(result) != canonical(item["expected"]) ||
          before != canonical(raw))
        throw std::runtime_error("Spec parity: " + item["id"].text());
    }
    std::cout << cases.size()
              << " behaviour specifications matched the reference\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
