#include "unllm/symbolic/behaviour_expression.h"
#include <iostream>
#include <optional>
using namespace unllm::native;
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    auto fixtures = Json::parse(
        read_file(std::filesystem::path(argv[1]) /
                  "tests/cpp_migration/fixtures/behaviour_expressions.json"));
    int accepted = 0, rejected = 0;
    for (auto f : fixtures.items()) {
      std::optional<BehaviourExpression> result;
      std::string error;
      try {
        result = behaviour_expression(f["source"], f["state"],
                                      f["transition"].flag());
      } catch (const std::exception &e) {
        error = e.what();
      }
      if (f.contains("expected")) {
        if (!result || result->type != f["type"].text() ||
            canonical(result->value.data()) != canonical(f["expected"]))
          throw std::runtime_error(
              "Expression parity: " + f["source"].text() + "\n" + error +
              (result ? "\n" + canonical(result->value.data()) : ""));
        ++accepted;
      } else {
        if (result)
          throw std::runtime_error("Unexpected acceptance: " +
                                   f["source"].text());
        ++rejected;
      }
    }
    auto state = fixtures.at(0)["state"];
    for (auto input :
         {std::string(2049, 'x'), std::string(1, char(0xff)),
          std::string(1, '\0'),
          std::string(129, '(') + "x" + std::string(129, ')'),
          std::string("\"\\ud800\""), std::string("\"\\U00110000\"")}) {
      bool failed = false;
      try {
        behaviour_expression(Json::string(input), state);
      } catch (const std::exception &) {
        failed = true;
      }
      if (!failed)
        throw std::runtime_error(
            "Invalid Unicode/over-budget expression accepted");
    }
    std::cout << "Behaviour expression parity: " << accepted << " accepted, "
              << rejected << " rejected; Unicode and resource limits passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
