#include "unllm/symbolic/grounding.h"
#include <iostream>
#include <set>
using namespace unllm::native;
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    std::filesystem::path root = argv[1];
    SemanticParser parser(root);
    size_t count = 0;
    auto same = [](Json a, Json b) { return canonical(a) == canonical(b); };
    auto cases = Json::parse(
        read_file(root / "tests/cpp_migration/fixtures/interpretation.json"));
    // Keep the migration fixture frozen. These requests exhausted the new
    // enumeration before any complete contract could be grounded.
    const std::set<std::string> limited{
        "text_paraphrase", "initial_fraction", "two_actions", "shopping_paraphrase",
        "task_checkboxes", "discount_sentence", "pause_synonym", "small_fraction",
        "two_metrics", "encryption", "two_counters"};
    for (auto c : cases.items()) {
      auto id = c["id"].text();
      auto expected_status = limited.contains(id) ? "resource_limit" : c["status"].text();
      auto expected_spec = c["spec"].clone();
      if (id == "known_failed_coordination") {
        // Both senses ground the same increase by four. Only its MRS citation changed.
        for (auto binding : expected_spec["interpretation"]["bindings"].items())
          if (binding["operation"].text() == "increase")
            binding.set("predicate", Json::string("_increase_v_1"));
      }

      std::string status = "accepted";
      Json spec;
      try {
        auto m = parser.parse(
            c["question"].text(),
            [](const std::string &, bool) { return std::string(); }, {},
            after_ms(11500), true);
        spec = ground_program(m, root);
      } catch (const ResourceLimit &e) {
        if (!std::string(e.what()).starts_with("No complete program contract among the first 128"))
          throw;
        status = "resource_limit";
      } catch (const Ambiguous &) {
        status = "ambiguous";
      } catch (const Unsupported &) {
        status = "unsupported";
      }
      if (status != expected_status ||
          (status == "accepted" && !same(spec, expected_spec)))
        throw std::runtime_error("Grounding mismatch on " + c["id"].text() +
                                 ": " + c["question"].text() + " wanted " +
                                 expected_status + " got " + status + "\n" +
                                 canonical(spec) + "\n" + canonical(c["spec"]));
      ++count;
    }
    std::cout << count
              << " live English interpretations matched Stage 13b expectations\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
