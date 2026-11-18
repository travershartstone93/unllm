#include "unllm/symbolic/stream_job.h"
#include "unllm/symbolic/emit.h"
#include "unllm/symbolic/target.h"
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace unllm::native;
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  try {
    auto root = std::filesystem::path(argv[1]);
    auto artifacts = root / ".state/cpp-migration" /
                     ("job-controller-" + std::to_string(getpid()));
    NativeEngine native(root, argv[2]);
    auto specification = Program::from(Json::parse(
        R"({"input":"numbers","steps":[{"op":"map","expr":"x-11"}],"output":"values","name":"subtract_offset"})"));
    auto built = build_stream(specification, "cpp", root, artifacts, native,
                              after_ms(60000));
    check(built["status"].text() == "validated",
          "Native construction job did not validate");
    auto job = std::filesystem::path(built["job"].text());
    auto run =
        run_stream_target(job, {"/job/program"}, "27\n-3\n", after_ms(5000));
    check(run.exit_code == 0 && run.out == "16\n-14\n",
          "Independent decrement behavior");
    check(built["source_sha256"]["program.cpp"].text() ==
              sha256(read_file(job / "program.cpp")),
          "Artifact integrity");
    // Deliberately emit the opposite behavior and require the independent job
    // validator to reject it against the original requested contract.
    auto wrong = specification;
    wrong.steps[0].expr = expression(Json::string("x+11"));
    auto negative_job = artifacts / "negative-control";
    std::filesystem::create_directory(negative_job);
    {
      std::ofstream output(negative_job / "program.cpp");
      output << emit_stream(wrong, "cpp", root);
    }
    auto rejected =
        validate_stream(specification, negative_job, "cpp", after_ms(60000));
    check(!rejected["valid"].flag() && rejected["stage"].text() == "execution",
          "Reversed decrement escaped validation");
    auto python = build_stream(specification, "python", root, artifacts, native,
                               after_ms(10000), "", true);
    check(python["status"].text() == "emitted" &&
              python["attempts"].size() == 0,
          "Python emission must work without a target interpreter");
    check(read_file(python["source"].text())
                  .find("def subtract_offset(stream):") != std::string::npos,
          "Native Python emitter entry point");
    std::cout << "Native job synthesis, compilation, independent execution, "
                 "reversed-decrement rejection and Python emission passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
