#include "unllm/symbolic/stream_job.h"
#include "unllm/symbolic/emit.h"
#include "unllm/symbolic/stream_contract.h"
#include "unllm/symbolic/target.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unistd.h>
namespace unllm::native {
namespace {
void write(const std::filesystem::path &path, const std::string &value) {
  std::ofstream out(path, std::ios::binary);
  out << value;
  out.close();
  if (!out)
    throw std::runtime_error("Cannot write artifact: " + path.string());
}
Json strings(const std::vector<std::string> &values) {
  auto out = Json::array();
  for (auto &v : values)
    out.push(Json::string(v));
  return out;
}
Json failure(const std::string &stage, size_t checked,
             const Json &compilation) {
  auto out = Json::object();
  out.set("valid", Json::boolean(false));
  out.set("stage", Json::string(stage));
  out.set("cases", Json::number(checked));
  out.set("compilation", compilation);
  return out;
}
} // namespace
Json validate_stream(const Program &original, const std::filesystem::path &job,
                     const std::string &language, Deadline deadline,
                     const std::string &python_target) {
  if (language != "cpp" && language != "python")
    throw Unsupported("Unsupported target language");
  auto compilation = Json::array();
  std::vector<std::pair<std::vector<std::string>, bool>> commands;
  if (language == "python") {
    if (python_target.empty() ||
        !std::filesystem::is_regular_file(python_target))
      return failure("target_unavailable", 0, compilation);
    commands.push_back({{python_target, "/job/program.py"}, false});
  } else {
    for (bool sanitized : {false, true}) {
      auto name = sanitized ? "program.checked" : "program";
      std::vector<std::string> cmd{"/usr/bin/clang++", "-std=c++20", "-Wall",
                                   "-Wextra", sanitized ? "-O1" : "-O2"};
      if (sanitized) {
        cmd.insert(cmd.end(), {"-g", "-fsanitize=address,undefined",
                               "-fno-omit-frame-pointer"});
      }
      cmd.insert(cmd.end(),
                 {(job / "program.cpp").string(), "-o", (job / name).string()});
      try {
        Process compiler(cmd);
        auto result = compiler.finish(std::min(deadline, after_ms(30000)));
        auto step = Json::object();
        step.set("command", strings(cmd));
        step.set("returncode", Json::number(result.exit_code));
        step.set("diagnostics",
                 Json::string(result.err.substr(
                     result.err.size() > 4000 ? result.err.size() - 4000 : 0)));
        compilation.push(step);
        if (result.exit_code != 0)
          return failure("compile", 0, compilation);
      } catch (const ResourceLimit &) {
        return failure("compile_timeout", 0, compilation);
      }
      commands.push_back({{"/job/" + std::string(name)}, sanitized});
    }
  }
  size_t checked = 0;
  for (auto &input : stream_cases(original)) {
    Json expected;
    bool valid_input = true;
    try {
      expected = stream_reference(original, input);
    } catch (const Unsupported &) {
      valid_input = false;
      expected = Json::array();
    }
    for (auto &[cmd, sanitized] : commands) {
      if (std::chrono::steady_clock::now() >= deadline)
        return failure("budget", checked, compilation);
      ProcessResult result;
      try {
        result = run_stream_target(
            job, cmd, input, std::min(deadline, after_ms(5000)), sanitized);
      } catch (const ResourceLimit &) {
        auto out = failure("execution_timeout", checked, compilation);
        out.set("input", Json::string(input));
        return out;
      }
      ++checked;
      bool passed = valid_input ? result.exit_code == 0 &&
                                      output_matches(expected, result.out)
                                : result.exit_code == 2 && result.out.empty() &&
                                      result.err.starts_with("error: ");
      if (!passed) {
        auto out = failure("execution", checked, compilation);
        out.set("input", Json::string(input));
        out.set("expected", expected);
        out.set("expected_valid", Json::boolean(valid_input));
        out.set("returncode", Json::number(result.exit_code));
        out.set("stdout", Json::string(result.out));
        out.set("stderr", Json::string(result.err));
        return out;
      }
    }
  }
  auto out = Json::object();
  out.set("valid", Json::boolean(true));
  out.set("cases", Json::number(checked));
  out.set("compilation", compilation);
  out.set("method", Json::string("independent C++ contract interpreter, fixed "
                                 "edge cases and seeded generated inputs"));
  out.set(
      "sandbox",
      Json::string("bubblewrap; read-only job; no network; CPU/output limits"));
  out.set("sanitizers", Json::boolean(language == "cpp"));
  out.set(
      "proof",
      Json::string("tested executable behavior, not unrestricted correctness"));
  return out;
}
Json build_stream(const Program &specification, const std::string &language,
                  const std::filesystem::path &resources,
                  const std::filesystem::path &artifacts, NativeEngine &engine,
                  Deadline deadline, const std::string &python_target,
                  bool emission_only) {
  auto started = std::chrono::steady_clock::now();
  auto original = Program::from(specification.data());
  if (language != "cpp" && language != "python")
    throw Unsupported("Target language must be python or cpp");
  auto construction = plan(original, engine, deadline);
  auto solved = original;
  auto reports = Json::array();
  for (auto &step : solved.steps)
    if (step.expr) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                           deadline - std::chrono::steady_clock::now())
                           .count();
      if (remaining <= 0)
        throw ResourceLimit("Synthesis deadline exceeded");
      auto result = synthesize_expression(*step.expr, step.op == "filter",
                                          std::min<int64_t>(3000, remaining));
      step.expr = std::move(result.expression);
      reports.push(result.evidence);
    }
  std::filesystem::create_directories(artifacts);
  auto pattern = (std::filesystem::absolute(artifacts) / "job-XXXXXX").string();
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  if (!mkdtemp(buffer.data()))
    throw std::runtime_error("Cannot create synthesis artifact directory");
  std::filesystem::path job(buffer.data());
  auto source = job / (language == "cpp" ? "program.cpp" : "program.py");
  auto attempts = Json::array();
  bool validated = false;
  std::string status = "emitted";
  for (int attempt = 0; attempt < 2; ++attempt) {
    write(source, emit_stream(solved, language, resources));
    if (emission_only)
      break;
    auto validation =
        validate_stream(original, job, language, deadline, python_target);
    attempts.push(validation);
    validated = validation["valid"].flag();
    if (validated) {
      status = "validated";
      break;
    }
    if (validation["stage"].text() == "target_unavailable") {
      status = "emitted";
      break;
    }
    status = "failed";
    if (attempt == 0 && validation["stage"].text() == "execution" &&
        reports.size()) {
      solved = original;
      validation.set(
          "repair", Json::string("preserve specified scalar evaluation order"));
      continue;
    }
    break;
  }
  auto content = read_file(source);
  size_t lines = 0;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    auto begin = line.find_first_not_of(" \t\r");
    if (begin != std::string::npos &&
        !line.substr(begin).starts_with(language == "cpp" ? "//" : "#"))
      ++lines;
  }
  auto out = Json::object();
  out.set("status", Json::string(status));
  out.set("language", Json::string(language));
  out.set("source", Json::string(source.string()));
  out.set("job", Json::string(job.string()));
  out.set("specification", original.data());
  out.set("emitted_specification", solved.data());
  out.set("construction", construction);
  out.set("solver", reports);
  out.set("attempts", attempts);
  out.set("meaningful_lines", Json::number(lines));
  auto hashes = Json::object();
  hashes.set(source.filename().string(), Json::string(sha256(content)));
  out.set("source_sha256", hashes);
  out.set("elapsed_ms",
          Json::real(std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - started)
                         .count()));
  out.set("claim",
          Json::string(
              validated
                  ? "executable behavior validated on the reported cases; no "
                    "unrestricted correctness proof"
                  : "source emitted; executable validation has not passed"));
  out.set("text",
          Json::string(validated ? "Generated and validated " + language +
                                       " program: " + source.string()
                       : status == "failed"
                           ? "Generated candidate failed validation: " +
                                 source.string()
                           : "Emitted " + language +
                                 " source without executable validation: " +
                                 source.string()));
  write(job / "report.json", canonical(out) + "\n");
  return out;
}
} // namespace unllm::native
