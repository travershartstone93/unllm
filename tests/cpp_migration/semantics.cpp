#include "unllm/symbolic/semantics.h"
#include <algorithm>
#include <iostream>
using namespace unllm::native;
static std::string comparison(Json meaning) {
  meaning = meaning.clone();
  // Scope denotes reachable handle membership, not traversal order. Preserve
  // every label and duplicate while ignoring dictionary iteration order.
  for (auto ep : meaning["semantics"].items())
    if (ep.contains("scope")) {
      std::vector<std::string> labels;
      for (auto v : ep["scope"].items())
        labels.push_back(v.text());
      std::sort(labels.begin(), labels.end());
      auto values = Json::array();
      for (auto &label : labels)
        values.push(Json::string(label));
      ep.set("scope", values);
    }
  return canonical(meaning);
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    auto vectors =
        Json::parse(read_file(std::filesystem::path(argv[1]) /
                              "tests/cpp_migration/fixtures/semantics.json"));
    size_t accepted = 0, rejected = 0;
    for (auto v : vectors.items()) {
      auto mrs = decode_mrs(v["mrs"].text());
      std::vector<std::string> focus;
      for (auto f : v["focus"].items())
        focus.push_back(f.text());
      std::string error;
      std::optional<Meaning> result;
      try {
        result = translate(
            mrs, v["sentence"].text(),
            [](const std::string &label, bool) {
              return "entity:" + key(label);
            },
            focus);
      } catch (const Unsupported &e) {
        error = e.what();
      } catch (const Ambiguous &e) {
        error = e.what();
      }
      if (v.contains("expected")) {
        if (!result)
          throw std::runtime_error("Rejected reference-supported analysis of " +
                                   v["sentence"].text() + ": " + error);
        if (comparison(result->data()) != comparison(v["expected"]) ||
            result->signature() != v["signature"].text()) {
          std::cerr << "Expected: " << canonical(v["expected"])
                    << "\nActual: " << canonical(result->data()) << '\n';
          throw std::runtime_error("Meaning mismatch: " + v["sentence"].text());
        }
        ++accepted;
      } else {
        if (result)
          throw std::runtime_error("Accepted reference-rejected analysis: " +
                                   v["sentence"].text());
        ++rejected;
      }
    }
    SemanticParser parser(argv[1]);
    auto resolve = [](const std::string &label, bool) {
      return "entity:" + key(label);
    };
    auto assertion = parser.parse("Paris is in France.", resolve);
    auto query = parser.parse("Is Paris in France?", resolve);
    if (assertion.act != "assert" || query.act != "query" ||
        query.goals.size() != 1 || query.goals[0].predicate != "located_in")
      throw std::runtime_error("Live semantic parsing");
    auto explanation = parser.parse("Why?", resolve);
    if (explanation.act != "explain")
      throw std::runtime_error("Fragment parsing");
    std::cout << "Semantic reference parity: " << accepted << " accepted, "
              << rejected
              << " rejected; live resident/fragment parsing passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
