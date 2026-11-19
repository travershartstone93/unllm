#include "unllm/symbolic/mrs.h"
#include "../../experiments/interpretation_diagnostics/identity.h"
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
    auto reading = decode_mrs(R"([ TOP: h0 INDEX: e1 RELS: <
      [ named<0:1> LBL: h2 ARG0: x3 CARG: "h0" ]
      [ named<0:1> LBL: h4 ARG0: x5 CARG: "Alice" ]
      [ _see_v_1<2:5> LBL: h6 ARG0: e1 ARG1: x3 ARG2: x5 ] >
      HCONS: < h0 qeq h6 > ])");
    auto reordered = decode_mrs(R"([ TOP: h7 INDEX: e8 RELS: <
      [ _see_v_1<2:5> LBL: h9 ARG0: e8 ARG1: x10 ARG2: x11 ]
      [ named<0:1> LBL: h12 ARG0: x11 CARG: "Alice" ]
      [ named<0:1> LBL: h13 ARG0: x10 CARG: "h0" ] >
      HCONS: < h7 qeq h9 > ])");
    auto original_id = diagnostics::identity(reading);
    check(original_id == diagnostics::identity(reordered),
          "MRS identity changed with variable names or predication order");
    auto &roles = reordered.rels.front().args;
    std::swap(roles.at("ARG1"), roles.at("ARG2"));
    check(original_id != diagnostics::identity(reordered), "MRS identity lost roles");
    reading.hcons.at(0).set("lo", Json::string("h2"));
    check(original_id != diagnostics::identity(reading), "MRS identity lost scope");
    auto partial = decode_ace_response(
        R"((:results . ()) (:readings . 0) (:error . "timeout, ran out of time while unpacking"))");
    check(partial.error.starts_with("timeout") && partial.analyses.empty(),
          "ACE protocol timeout lost");
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
