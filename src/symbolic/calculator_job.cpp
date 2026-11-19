#include "calculator_cases.h"
#include "gui_json.h"
#include "unllm/symbolic/calculator.h"
#include "unllm/symbolic/gui_target.h"
#include <cstdlib>
namespace unllm::native {
using namespace gui;
namespace fs = std::filesystem;
namespace {
using Args = std::vector<std::string>;
Json validate(const CalculatorSpec &spec, const fs::path &root,
              const fs::path &job, const std::string &lang,
              const std::string &python, Deadline deadline) {
  auto compiles = Json::array(),
       result = obj({{"valid", boolean(false)}, {"compilations", compiles}});
  auto dir = root / "resources/targets/calculator", deps = root / ".deps/apps";
  auto cases = calculator_cases(spec);
  auto request = obj({{"expressions", cases.inputs},
                      {"gui", cases.scenarios},
                      {"preview", boolean(true)}});
  write(job / "validation-cases.json", canonical(request) + "\n");
  result.set("arithmetic_cases", Json::number(cases.inputs.size()));
  result.set("gui_scenarios", Json::number(cases.scenarios.size()));
  try {
    std::vector<std::pair<Args, bool>> targets;
    fs::path antlr_target;
    if (lang == "python") {
      result.set("stage", str("target_unavailable"));
      if (python.empty())
        throw Unsupported("Provide the optional generated-Python interpreter");
      auto exe = fs::canonical(python);
      if (!exe.string().starts_with("/usr/") || !fs::is_regular_file(exe))
        throw Unsupported("Expected a target interpreter under /usr");
      if (auto package = std::getenv("UNLLM_PYTHON_TARGET_ANTLR4");
          package && *package)
        antlr_target = package;
      fs::copy_file(dir / "python_driver.py", job / "gui_driver.py",
                    fs::copy_options::overwrite_existing);
      targets.push_back({{exe.string(), "/job/gui_driver.py"}, false});
    } else {
      result.set("stage", str("compile"));
      fs::copy_file(dir / "cpp_driver.cpp", job / "gui_driver.cpp",
                    fs::copy_options::overwrite_existing);
      for (std::string file : {"program", "gui_driver", "gui_driver.checked"}) {
        bool driver = file != "program", san = file == "gui_driver.checked";
        Args cmd{"/usr/bin/clang++", "-std=c++20",         "-Wall",
                 "-Wextra",          "-DANTLR4CPP_STATIC", san ? "-O1" : "-O2"};
        if (san)
          cmd.insert(cmd.end(), {"-fsanitize=address,undefined",
                                 "-fno-omit-frame-pointer"});
        cmd.insert(
            cmd.end(),
            {(job / (driver ? "gui_driver.cpp" : "program.cpp")).string(),
             "-I" + job.string(),
             "-I" + (deps / "native/include/antlr4-runtime").string(),
             (deps / "generated" /
              (san ? "expression.checked.a" : "expression.normal.a"))
                 .string(),
             (deps / "native/lib/libantlr4-runtime.a").string()});
        auto flags = qt_compile_flags(driver, deadline);
        cmd.insert(cmd.end(), flags.begin(), flags.end());
        cmd.insert(cmd.end(), {"-o", (job / file).string()});
        auto process = Process(cmd).finish(std::min(deadline, after_ms(50000)));
        auto command = Json::array();
        for (auto &arg : cmd)
          command.push(str(arg));
        compiles.push(obj({{"command", command},
                           {"returncode", Json::number(process.exit_code)},
                           {"diagnostics", str(process.err.substr(0, 6000))}}));
        if (process.exit_code)
          throw Unsupported("Failed calculator compilation: " + file);
        if (driver)
          targets.push_back({{"/job/" + file}, san});
      }
    }
    size_t checked = 0;
    for (auto &[cmd, san] : targets) {
      result.set("stage", str("execution"));
      auto output =
          fresh_gui_directory(job, san ? "checked-output" : "test-output");
      auto r = run_gui_target(job, output, cmd, request, san, deadline,
                              antlr_target);
      if (r.exit_code)
        throw Unsupported("Calculator target failed: " + r.err.substr(0, 4000));
      result.set("stage", str("outcome"));
      auto actual = Json::parse(r.out);
      write(output / "observations.json", canonical(actual) + "\n");
      check_calculator_results(spec, cases, actual);
      checked += cases.inputs.size() + cases.scenarios.size();
      result.set("preview", str((output / "preview.png").string()));
      result.set("cases", Json::number(checked));
    }
    result.set("sanitizers", boolean(lang == "cpp"));
    result.set("valid", boolean(true));
    result.set("stage", str("complete"));
  } catch (const std::exception &e) {
    result.set("message", str(e.what()));
  }
  return result;
}
} // namespace
Json build_calculator(const CalculatorSpec &spec, const std::string &lang,
                      const fs::path &root, const fs::path &artifacts,
                      NativeEngine &engine, Deadline deadline,
                      const std::string &python, bool emit_only) {
  auto job = fresh_gui_directory(artifacts, "job");
  auto report = obj({{"status", str("failed")},
                     {"kind", str("application")},
                     {"job", str(job.string())},
                     {"specification", spec.data()},
                     {"language", str(lang)}});
  try {
    auto construction = plan_calculator(spec, lang, engine, deadline);
    auto ir = construct_calculator(spec, construction, root, deadline);
    auto source = job / (lang == "cpp" ? "program.cpp" : "program.py");
    auto code = emit_calculator(ir, lang, root);
    write(source, code);
    report.set("construction", construction);
    report.set("solver", ir["solver"]);
    report.set("source", str(source.string()));
    report.set("source_sha256", str(sha256(code)));
    write(job / "application.ir.json", canonical(ir) + "\n");
    write(job / "specification.json", canonical(spec.data()) + "\n");
    auto deps = root / ".deps/apps", generated = deps / "generated";
    auto manifest = Json::parse(read_file(generated / "manifest.json"));
    auto grammar =
        read_file(root / "resources/targets/calculator/Arithmetic.g4");
    if (sha256(grammar) != required_text(manifest, "grammar_sha256"))
      throw Unsupported(
          "Calculator grammar identity differs from the staged parser");
    write(job / "Arithmetic.g4", grammar);
    fs::copy_file(deps / "antlr-source/LICENSE.txt", job / "ANTLR-LICENSE.txt");
    for (auto &[path, hash] : manifest["files"].members()) {
      fs::path file(path);
      if (file.parent_path() != fs::path(lang))
        continue;
      if (file.extension() != ".py" && file.extension() != ".h" &&
          file.extension() != ".cpp")
        continue;
      auto contents = read_file(generated / file);
      if (sha256(contents) != hash.text())
        throw Unsupported("Arithmetic parser identity mismatch: " + path);
      write(job / file.filename(), contents);
    }
    if (lang == "python")
      write(job / "requirements.txt",
            "PySide6-Essentials==6.12.0\nshiboken6==6.12.0\nantlr4-python3-"
            "runtime==4.13.2\n");
    if (emit_only)
      report.set("status", str("emitted"));
    else {
      auto validation = validate(spec, root, job, lang, python, deadline);
      report.set("validation", validation);
      if (validation["valid"].flag())
        report.set("status", str("validated"));
    }
    report.set("text", str(report["status"].text() + " " + lang +
                           " calculator: " + source.string()));
  } catch (const std::exception &e) {
    report.set("text", str(e.what()));
  }
  write(job / "report.json", canonical(report) + "\n");
  return report;
}
} // namespace unllm::native
