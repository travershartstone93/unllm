#include "unllm/symbolic/program.h"
#include <iostream>
using namespace unllm::native;
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  try {
    auto fixtures =
        Json::parse(read_file(std::filesystem::path(argv[1]) /
                              "tests/cpp_migration/fixtures/programs.json"));
    auto catalog = stream_catalog();
    if (canonical(catalog.facts) != canonical(fixtures["facts"]) ||
        canonical(catalog.relations) != canonical(fixtures["relations"]))
      throw std::runtime_error("Component catalog identity mismatch");
    NativeEngine native(argv[1], argv[2]);
    size_t accepted = 0, rejected = 0;
    for (auto fixture : fixtures["programs"].items()) {
      std::optional<Program> p;
      try {
        p = Program::from(fixture["input"]);
      } catch (const Unsupported &) {
        if (fixture.contains("expected"))
          throw;
      }
      if (fixture.contains("expected")) {
        if (!p || canonical(p->data()) != canonical(fixture["expected"]))
          throw std::runtime_error("Program specification mismatch");
        auto constructed = plan(*p, native, after_ms(5000));
        if (constructed["components"].size() != p->steps.size() + 2 ||
            !constructed["derivation"]["valid"].flag())
          throw std::runtime_error(
              "Contract construction did not discharge obligations");
        ++accepted;
      } else {
        if (p)
          throw std::runtime_error("Accepted invalid program specification");
        ++rejected;
      }
    }
    std::cout << "Native program schemas: " << accepted << " accepted, "
              << rejected
              << " rejected; all valid plans independently proof checked\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
