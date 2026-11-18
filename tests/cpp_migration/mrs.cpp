#include "unllm/symbolic/mrs.h"
#include <iostream>
using namespace unllm::native;
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected repository root");
    std::filesystem::path root = argv[1];
    auto fixtures =
        Json::parse(read_file(root / "tests/cpp_migration/fixtures/mrs.json"));
    int index = 0;
    for (auto row : fixtures.items()) {
      auto actual = decode_mrs(row["text"].text()).data();
      if (canonical(actual) != canonical(row["expected"])) {
        std::cerr << "fixture " << index << "\nactual=" << canonical(actual)
                  << "\nexpected=" << canonical(row["expected"]) << "\n";
        throw std::runtime_error("Reference MRS graph differs");
      }
      ++index;
    }
    bool failed = false;
    try {
      decode_mrs("[ TOP: h0 RELS: < [ _cat_n_1 LBL: h1 ARG0: x1 ] >");
    } catch (const Unsupported &) {
      failed = true;
    }
    check(failed, "Malformed MRS accepted");
    failed = false;
    try {
      decode_mrs("[ UNKNOWN: x1 ]");
    } catch (const Unsupported &) {
      failed = true;
    }
    check(failed, "Unknown MRS field accepted");
    AceClient ace(root, 4, 64);
    auto first = ace.parse("Paris is in France.");
    check(!first.analyses.empty(), "No live ACE results");
    auto second = ace.parse("Is Paris in France?");
    check(!second.analyses.empty(), "Resident ACE second response missing");
    bool question = false;
    for (auto &m : second.analyses) {
      auto props = m.variables.at(m.index);
      question |= props.contains("SF") && props.at("SF") == "ques";
    }
    check(question, "Question force lost");
    std::cout
        << index
        << " recorded MRS graphs matched; two live resident requests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
