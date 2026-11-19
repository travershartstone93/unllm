#include "unllm/symbolic/gui_target.h"
#include <cstdlib>
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
