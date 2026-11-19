#include "../../../src/symbolic/algorithm_goal.h"
#include "../../../src/symbolic/gui_json.h"
#include <iostream>

using namespace unllm::native;
using namespace unllm::native::gui;
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  SemanticParser parser(argv[1]);
  std::string prompt;
  while (std::getline(std::cin, prompt)) {
    auto row = obj({{"prompt", str(prompt)}, {"candidates", Json::array()}});
    try {
      auto m = parser.parse(
          prompt, [](auto &, bool) { return std::string(); }, {},
          after_ms(11500), true);
      row.set("readings", Json::number(m.analyses.size()));
      row.set("candidates", interval_candidates(m, prompt));
    } catch (const ResourceLimit &e) {
      row.set("parse_stop", str(e.what()));
    } catch (const Unsupported &e) {
      row.set("parse_stop", str(e.what()));
    }
    std::cout << row.dump() << std::endl;
  }
}
