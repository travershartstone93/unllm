#include "../../../src/symbolic/grounding_internal.h"
#include <SWI-Prolog.h>
#include <chrono>
#include <iostream>
extern "C" char *unllm_logic_request(const char *);
extern "C" void unllm_logic_free(char *);
using namespace unllm::native;
using namespace unllm::native::frames;
using namespace unllm::native::gui;
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}
std::string run(const Json &reading) {
  Json rows = Json::array();
  for (auto ep : reading.items()) {
    auto p = predicate(ep), value = ep["carg"].text();
    if (ep["literal_sign"].text() == "-" && !value.starts_with('-'))
      value = "-" + value;
    auto kind = p.find("_v_") != p.npos   ? "verb"
                : p.find("_n_") != p.npos ? "noun"
                                          : "other";
    rows.push(arr({str(word(ep)), str(arg(ep, "ARG0")), str(arg(ep, "ARG1")),
                   str(arg(ep, "ARG2")), str(kind), str(value)}));
  }
  auto frame = PL_open_foreign_frame();
  auto args = PL_new_term_refs(2);
  if (!PL_put_atom_chars(args, rows.dump().c_str()) ||
      !PL_call_predicate(nullptr, PL_Q_NODEBUG,
                         PL_predicate("evaluate", 2, "role_trial"), args))
    throw std::runtime_error("SWI role query failed");
  char *value = nullptr;
  if (!PL_get_atom_chars(args + 1, &value))
    throw std::runtime_error("Invalid trial result");
  std::string result(value);
  PL_discard_foreign_frame(frame);
  return result;
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  std::filesystem::path root = argv[1];
  setenv("UNLLM_NATIVE_RULE_PATH", (root / "reasoning/prolog/rules.pl").c_str(),
         1);
  setenv("SWI_HOME_DIR", (root / ".deps/native/usr/lib/swipl").c_str(), 1);
  auto reply = unllm_logic_request("{}");
  auto init = Json::parse(reply);
  unllm_logic_free(reply);
  if (init["status"].text() != "complete")
    throw std::runtime_error(init.dump());
  auto path = PL_new_term_ref();
  if (!PL_put_atom_chars(
          path,
          (root / "archive/interpretation_diagnostics/role_trial/roles.pl")
              .c_str()) ||
      !PL_call_predicate(nullptr, PL_Q_NODEBUG,
                         PL_predicate("consult", 1, "user"), path))
    return 1;
  const Terms evidence_words{"counter",
                             "start",
                             "at",
                             "decrease",
                             "by",
                             "card",
                             "basic_numbered_hour",
                             "appos",
                             "save",
                             "list",
                             "text",
                             "editor"};
  SemanticParser parser(root);
  for (auto q :
       {"Build a counter starting at 8.5 that decreases by 2.25.",
        "Build a counter that starts at 8.5 and decreases by 2.25.",
        "Build a counter starting at -2.25 that decreases by 8.5.",
        "Build an app with a text area and a to-do list and a save button.",
        "Build an app with a text area and a save button.",
        "Create a text editor and a list with save buttons.",
        "Create a list with a save button.",
        "Create a list with save buttons."}) {
    Meaning m;
    try {
      m = parser.parse(
          q, [](auto &, bool) { return std::string(); }, {}, after_ms(11500),
          true);
    } catch (const Unsupported &e) {
      std::cout
          << obj({{"question", str(q)}, {"parse_stop", str(e.what())}}).dump()
          << std::endl;
      continue;
    }
    auto start = Clock::now();
    std::string baseline = "accepted";
    try {
      ground_program(m, root);
    } catch (const std::exception &e) {
      baseline = e.what();
    }
    auto baseline_ms = ms(start);
    Json traces = Json::object(), witnesses = Json::object();
    double binding_ms = 0;
    for (auto reading : m.analyses.items()) {
      start = Clock::now();
      auto outcome = run(reading);
      binding_ms += ms(start);
      if (!witnesses.contains(outcome)) {
        Json evidence = Json::array();
        for (auto ep : reading.items())
          if (evidence_words.contains(word(ep)))
            evidence.push(ep);
        witnesses.set(outcome, evidence);
      }
      traces.set(outcome, Json::number(traces[outcome].integer() + 1));
    }
    std::cout << obj({{"question", str(q)},
                      {"baseline", str(baseline)},
                      {"baseline_ms", num(baseline_ms)},
                      {"binding_ms", num(binding_ms)},
                      {"reading_cap",
                       Json::number(m.exhausted_program_readings)},
                      {"reading_outcomes", traces},
                      {"first_witness", witnesses}})
                     .dump()
              << std::endl;
  }
}
