#include "search.h"
#include <iostream>
using namespace unllm::algorithm;
int main() {
  try {
    auto contract = Json::parse(
        R"({"input":"ints","output":"ints","examples":[{"input":[],"output":[]},{"input":["2","3","-8"],"output":["2","5","-3"]}]})");
    auto r = synthesize(contract);
    if (r["status"].text() != "candidate" || !r["counterexamples"].size())
      throw std::runtime_error("Construction failed");
    if (interpret(decode(r["ir"]), {Sequence{9, -2, 3}}) !=
        Value(Sequence{9, 7, 10}))
      throw std::runtime_error("Constructed scan failed on new input");
    auto before = r["ir"].clone();
    contract["examples"].push(Json::parse(R"({"input":[],"output":["0"]})"));
    if (synthesize(contract, before)["status"].text() != "inconsistent")
      throw std::runtime_error("Contradiction accepted");
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
