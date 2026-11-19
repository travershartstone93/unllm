#include "unllm/maintenance/evaluate.h"
#include "unllm/symbolic/gui_target.h"
#include <atomic>
#include <iostream>
using namespace unllm::maintenance;
using namespace unllm::native;
namespace fs = std::filesystem;
namespace {
void check(bool condition, const std::string &why) {
  if (!condition)
    throw std::runtime_error(why);
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 7 && std::string(argv[1]) == "--branch" &&
      std::string(argv[5]) == "--") {
    std::cout << "The sample answer is blue.\n[branch: world:fixture]\n[brief] "
                 "irrelevant\n[passage] not part of answer\n";
    return 0;
  }
  auto dir = fresh_gui_directory(fs::temp_directory_path(), "unllm-eval-test");
  try {
    auto fixture = dir / "dev.tsv";
    auto raw =
        "# private test fixture\nid\tquestion\texpect\tcategory\r\n1\tWhich "
        "colour?\t\\bBLUE\\b\t\r\n2\tHedge?\tgreen\t\r\n3\tNobody?"
        "\t\tlimits\r\n4\t\"multi\nline?\t\"\"quote\"\"\"\tblue\t\r\n";
    write_atomic(fixture, raw);
    std::atomic<int> calls = 0;
    auto answer =
        [&](const std::string &q) -> std::pair<std::string, std::string> {
      ++calls;
      if (q == "Hedge?")
        return {"world", "The closest thing I know is green"};
      if (q == "Nobody?")
        return {"none", "No reference packs are installed"};
      return {"world", "blue"};
    };
    auto done = Json::object();
    auto scores = evaluate_tsv(
        fixture, answer, [&](const Json &r) { done.set(r["id"].text(), r); },
        2);
    check(scores.known == 3 && scores.right == 3 && scores.hedged == 1 &&
              scores.declined_unknown == 1,
          "Evaluation totals");
    check(done["4"]["question"].text() == "multi\nline?\t\"quote\"",
          "TSV quoting");
    calls = 0;
    scores = evaluate_tsv(fixture, answer, {}, 1, done);
    check(!calls && scores.right == 3, "Resume");
    auto hidden = dir / "synthetic-heldout-fixture.tsv";
    write_atomic(hidden, raw);
    bool leaked = false;
    scores =
        evaluate_tsv(hidden, answer, [&](const Json &) { leaked = true; }, 2);
    check(!leaked && scores.right == 3, "Aggregate-only output");
    auto [branch, text] = ask_candidate("/proc/self/exe", "--help");
    check(branch == "world:fixture" && text == "The sample answer is blue.",
          "Candidate framing");
    fs::remove_all(dir);
    std::cout << "TSV quoting, concurrent scoring, resume, aggregate-only "
                 "reporting and CLI framing passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
