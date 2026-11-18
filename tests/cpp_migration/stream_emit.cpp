#include "unllm/symbolic/emit.h"
#include "unllm/symbolic/stream_contract.h"
#include "unllm/symbolic/target.h"
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace unllm::native;
int main(int argc, char **argv) {
  if (argc < 3 || argc > 4)
    return 2;
  std::string language = argv[2];
  auto root = std::filesystem::path(argv[1]);
  auto folder = root / ".state/cpp-migration" /
                ("stream-targets-" + language + "-" + std::to_string(getpid()));
  try {
    auto fixtures = Json::parse(
        read_file(root / "tests/cpp_migration/fixtures/stream_outputs.json"));
    std::filesystem::create_directories(folder);
    size_t checks = 0;
    for (auto fixture : fixtures.items()) {
      auto program = Program::from(fixture["spec"]);
      // Validate the new native oracle against the frozen independent Python
      // oracle before using it for specifications beyond these fixtures.
      for (auto sample : fixture["cases"].items()) {
        Json actual;
        bool rejected = false;
        try {
          actual = stream_reference(program, sample["input"].text());
        } catch (const Unsupported &) {
          rejected = true;
        }
        if (sample.contains("expected")) {
          if (rejected || canonical(actual) != canonical(sample["expected"]))
            throw std::runtime_error("Native contract oracle mismatch: " +
                                     fixture["id"].text());
        } else if (!rejected)
          throw std::runtime_error(
              "Native contract oracle accepted invalid input");
      }
      auto job = folder / fixture["id"].text();
      std::filesystem::create_directory(job);
      auto source = job / (language == "cpp" ? "program.cpp" : "program.py");
      {
        std::ofstream output(source);
        output << emit_stream(program, language, root);
        if (!output)
          throw std::runtime_error("Cannot write generated target");
      }
      for (int variant = 0; variant < (language == "cpp" ? 2 : 1); ++variant) {
        bool sanitized = variant == 1;
        std::string name = sanitized ? "program-sanitized" : "program";
        if (language == "cpp") {
          std::vector<std::string> command{"/usr/bin/clang++",
                                           "-std=c++20",
                                           "-O1",
                                           "-Wall",
                                           "-Wextra",
                                           source.string(),
                                           "-o",
                                           (job / name).string()};
          if (sanitized) {
            command.push_back("-fsanitize=address,undefined");
            command.push_back("-fno-omit-frame-pointer");
          }
          Process compiler(command);
          auto result = compiler.finish(after_ms(30000));
          if (result.exit_code != 0)
            throw std::runtime_error("Generated C++ compilation failed: " +
                                     result.err);
        }
        for (auto sample : fixture["cases"].items()) {
          auto command = language == "cpp"
                             ? std::vector<std::string>{"/job/" + name}
                             : std::vector<std::string>{
                                   argc == 4 ? argv[3] : "/usr/bin/python3",
                                   "/job/program.py"};
          auto result = run_stream_target(job, command, sample["input"].text(),
                                          after_ms(5000), sanitized);
          bool ok = sample.contains("expected")
                        ? result.exit_code == 0 &&
                              output_matches(sample["expected"], result.out)
                        : result.exit_code == 2 && result.out.empty() &&
                              result.err.starts_with("error: ");
          if (!ok)
            throw std::runtime_error(
                "Target mismatch: " + fixture["id"].text() + " " + language +
                (sanitized ? " sanitized" : "") +
                " input=" + canonical(sample["input"]) +
                " stdout=" + result.out + " stderr=" + result.err);
          ++checks;
        }
      }
    }
    // The checker must detect a reversed decrement and incorrect column order.
    if (output_matches(Json::parse("[[16]]"), "38\n") ||
        output_matches(Json::parse("[[\"count\",\"sum\",\"min\"]]"),
                       "count\tmin\tsum\n"))
      throw std::runtime_error("Oracle sensitivity control failed");
    std::cout << language << ": " << fixtures.size() << " generated programs, "
              << checks << " isolated executable checks passed; artifacts "
              << folder << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\nArtifacts: " << folder << '\n';
    return 1;
  }
}
