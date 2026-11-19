#include "unllm/symbolic/gui_target.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
using namespace unllm::native;
namespace {
void check(bool good, const std::string &why) {
  if (!good)
    throw std::runtime_error(why);
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  try {
    fs::path root = argv[1];
    auto base =
        fresh_gui_directory(root / ".state/cpp-migration", "worker-protocol");
    setenv("UNLLM_ARTIFACTS", base.c_str(), 1);
    unsetenv("UNLLM_PYTHON_TARGET");
    std::vector<std::string> args{argv[3], root.string(),
                                  (base / "facts.sqlite").string(), argv[2],
                                  "--events"};
    Process worker(args);
    auto submit = [&](const Json &request, bool generating = true) {
      auto deadline = after_ms(10000);
      worker.write(canonical(request) + "\n", deadline);
      auto reply = Json::parse(worker.line(deadline));
      if (generating) {
        check(reply["event"].text() == "synthesis_started",
              "Missing progress event");
        reply = Json::parse(worker.line(deadline));
      }
      return reply;
    };
    auto command = [](const std::string &text, std::string context = "a") {
      auto v = Json::object();
      v.set("question", Json::string(text));
      v.set("context", Json::string(context));
      v.set("emit_only", Json::boolean(true));
      return v;
    };
    check(submit(command(":code cpp"))["status"].text() == "unknown",
          "Empty generation history");
    auto example = submit(command(
        R"({"input":"ints","output":"ints","examples":[{"input":[],"output":[]},{"input":["3","-4"],"output":["4","-3"]}]})",
        "example"));
    check(example["status"].text() == "emitted" &&
              example["kind"].text() == "algorithm" &&
              example["validation_basis"].text() == "supplied examples",
          "Structured example contract did not reach algorithm construction");
    std::ifstream contract_file(
        root / "experiments/algorithm_ir/contract_trial/primary.json");
    std::string contract_text;
    check(bool(std::getline(contract_file, contract_text)), "Missing typed contract");
    auto reduction = submit(command(contract_text, "reduction"));
    check(reduction["status"].text() == "emitted" &&
              reduction["transition_candidates"].integer() > 0 &&
              reduction["ir"]["functions"].at(0)["body"].at(0).at(0).text() == "each",
          "Typed contract did not reach reduction search");
    auto requirements = Json::parse(R"({
      "status":"ok","aggregate":"count","collection":"values","comparison":"lt",
      "compare_to":{"literal":-3},"required_structure":["for"]
    })");
    auto interpreted = submit(command(requirements.dump(), "interpreted"));
    check(interpreted["status"].text() == "emitted" &&
              interpreted["specification"]["behavior"]["where"]["literal"].integer() == -3,
          "Lost literal threshold or for-only requirement");
    requirements["compare_to"].set("input", Json::string("cutoff"));
    auto invalid_threshold = submit(command(requirements.dump(), "interpreted"));
    check(invalid_threshold["status"].text() == "unsupported" &&
              !invalid_threshold.contains("source"),
          "Accepted both a literal and input reference");
    auto clarification = submit(command(
        R"({"status":"clarify","question":"Above or below the cutoff?"})", "interpreted"));
    check(clarification["status"].text() == "clarify" &&
              clarification["text"].text() == "Above or below the cutoff?" &&
              !clarification.contains("source") &&
              canonical(submit(command(":code cpp", "interpreted"))["ir"]) ==
                  canonical(interpreted["ir"]),
          "Clarification changed the last successful function");
    auto conflicting = submit(command(
        R"(:revise {"examples":[{"input":[],"output":[]},{"input":[],"output":["0"]}]})",
        "example"));
    check(conflicting["status"].text() == "inconsistent" &&
              canonical(submit(command(":code cpp", "example"))["ir"]) ==
                  canonical(example["ir"]),
          "Conflicting examples replaced the last successful program");
    auto first = submit(command("Make a standard calculator app."));
    check(first["status"].text() == "emitted" &&
              first["language"].text() == "python",
          "English calculator without a target");
    check(submit(command(":code cpp", "b"))["status"].text() == "unknown",
          "Context leaked");
    auto second = submit(command(":code cpp"));
    check(second["language"].text() == "cpp" &&
              canonical(first["specification"]) ==
                  canonical(second["specification"]),
          "Replay changed specification");
    auto revised =
        submit(command(":revise {\"memory\":true,\"title\":\"Δ Pocket\"}"));
    check(revised["status"].text() == "emitted" &&
              revised["language"].text() == "cpp" &&
              revised["specification"]["memory"].flag(),
          "Revision lost language or feature");
    auto invalid = submit(command(":revise {\"precision\":2}"));
    check(invalid["status"].text() == "unsupported",
          "Invalid revision accepted");
    auto replay = submit(command(":code python"));
    check(canonical(replay["specification"]) ==
              canonical(revised["specification"]),
          "Failed revision poisoned history");
    auto sequence = submit(command("Make an array from 1-10", "sequence"));
    auto bounds = sequence["specification"]["interval"];
    check(sequence["kind"].text() == "algorithm" &&
              sequence["goal_candidates"].size() == 1 &&
              bounds.at(0).integer() == 1 && bounds.at(1).integer() == 10,
          "Expected an interval goal on the English path");
    auto unspecified =
        submit(command("Make a number from 1 to 10.", "unspecified"));
    check(unspecified["status"].text() != "emitted",
          "A bare number became a sequence");
    auto too_long =
        submit(command(R"(:revise {"interval":[0,64]})", "sequence"));
    check(too_long["status"].text() == "unsupported" &&
              canonical(
                  submit(command(":code cpp", "sequence"))["specification"]) ==
                  canonical(sequence["specification"]),
          "Rejected interval revision changed the last successful goal");
    auto assertion = submit(command("Paris is in France."), false);
    auto query = submit(command("Is Paris in France?"), false);
    check(assertion["truth"].text() == "asserted" &&
              query["truth"].text() == "supported",
          "Factual conversation");
    auto reset = submit(command(":reset"), false);
    check(reset["status"].text() == "complete", "Reset failed");
    check(submit(command(":code python"))["status"].text() == "unknown",
          "Reset retained generation history");
    worker.write("oops\n", after_ms(1000));
    check(Json::parse(worker.line(after_ms(1000)))["status"].text() == "error",
          "Malformed message");
    check(submit(command(":code cpp"))["status"].text() == "unknown",
          "Worker did not recover after malformed input");
    check(worker.finish(after_ms(5000)).exit_code == 0, "Worker EOF");
    Process partial(args);
    partial.write("{}", after_ms(2000));
    check(partial.finish(after_ms(5000)).exit_code == 5,
          "Truncated request accepted");
    Process oversized(args);
    oversized.write(std::string(65537, 'x'), after_ms(3000));
    check(oversized.finish(after_ms(5000)).exit_code == 3,
          "Oversized request accepted");
    std::cout << "Context, replay, failed revision, factual reuse, framing, "
                 "size limit and EOF passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
