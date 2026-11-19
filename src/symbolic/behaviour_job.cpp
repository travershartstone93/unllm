#include "unllm/symbolic/behaviour_job.h"
#include "gui_json.h"
#include "unllm/symbolic/gui_target.h"
#include <unistd.h>
namespace unllm::native {
using namespace gui;
namespace {
using Path = std::filesystem::path;
using Args = std::vector<std::string>;
Json strings(const Args &values) {
  auto out = Json::array();
  for (auto &v : values)
    out.push(str(v));
  return out;
}
std::string header(const Json &fields) {
  std::string inspect = "QJsonObject observe_state(ApplicationWindow &window) "
                        "{\n  QJsonObject result;\n",
              init = "void initialize_state(ApplicationWindow &window,const "
                     "QJsonObject &initial) {\n";
  const std::map<std::string, std::string> methods{{"text", "toString"},
                                                   {"number", "toDouble"},
                                                   {"bool", "toBool"},
                                                   {"records", "toArray"}};
  for (auto &[name, f] : fields.members()) {
    auto key = canonical(str(name)), member = "window.state_" + name,
         kind = f["type"].text();
    auto value = "initial[" + key + "]." + methods.at(kind) + "()";
    inspect += "  result[" + key +
               "] = " + (kind == "text" ? "qtext(" + member + ")" : member) +
               ";\n";
    init += "  if(initial.contains(" + key + ")) " + member + " = " +
            (kind == "text" ? "bytes(" + value + ")" : value) + ";\n";
  }
  return inspect + "  return result;\n}\n" + init + "}\n";
}
} // namespace
Json validate_behaviour(const BehaviourSpec &spec, const Path &root,
                        const Path &job, const std::string &lang,
                        Deadline deadline, const std::string &python) {
  auto compilations = Json::array();
  auto result =
      obj({{"valid", boolean(false)}, {"compilations", compilations}});
  auto stage = [&](const std::string &name) { result.set("stage", str(name)); };
  size_t checked = 0;
  try {
    auto resources = root / "resources/targets/behaviour";
    std::vector<std::pair<Args, bool>> targets;
    stage("target_unavailable");
    if (lang == "python") {
      if (python.empty())
        throw Unsupported("Provide a Python target with PySide6");
      auto exe = std::filesystem::canonical(python);
      if (!exe.string().starts_with("/usr/") ||
          !std::filesystem::is_regular_file(exe))
        throw Unsupported("This target sandbox requires a Python executable "
                          "installed under /usr");
      std::filesystem::copy_file(
          resources / "gui_driver.py", job / "gui_driver.py",
          std::filesystem::copy_options::overwrite_existing);
      targets.push_back({{exe.string(), "/job/gui_driver.py"}, false});
    } else if (lang == "cpp") {
      stage("compile");
      std::filesystem::copy_file(
          resources / "gui_driver.cpp", job / "gui_driver.cpp",
          std::filesystem::copy_options::overwrite_existing);
      write(job / "observation.h", header(spec.state()));
      for (std::string name : {"program", "gui_driver", "gui_driver.checked"}) {
        bool driver = name != "program", san = name == "gui_driver.checked";
        auto flags = qt_compile_flags(driver, deadline);
        Args cmd{"/usr/bin/clang++",     "-std=c++20",       "-Wall", "-Wextra",
                 "-Wno-unused-function", san ? "-O1" : "-O2"};
        if (san)
          cmd.insert(cmd.end(), {"-fsanitize=address,undefined",
                                 "-fno-omit-frame-pointer"});
        cmd.push_back(
            (job / (driver ? "gui_driver.cpp" : "program.cpp")).string());
        cmd.insert(cmd.end(), flags.begin(), flags.end());
        cmd.insert(cmd.end(), {"-o", (job / name).string()});
        auto r = Process(cmd).finish(std::min(deadline, after_ms(60000)));
        compilations.push(
            obj({{"command", strings(cmd)},
                 {"returncode", Json::number(r.exit_code)},
                 {"diagnostics",
                  str(r.err.substr(r.err.size() > 6000 ? r.err.size() - 6000
                                                       : 0))}}));
        if (r.exit_code)
          throw Unsupported("Qt compilation failed: " + name);
        if (driver)
          targets.push_back({{"/job/" + name}, san});
      }
    } else
      throw Unsupported("Unknown GUI language");
    stage("contract");
    auto scenarios = behaviour_cases(spec, job);
    write(job / "validation-cases.json", canonical(scenarios) + "\n");
    result.set("gui_scenarios", Json::number(scenarios.size()));
    // Keep scenarios that observe files in their own writable directory.
    std::vector<Json> groups;
    auto bulk = Json::array();
    for (auto s : scenarios.items()) {
      if (!s.is_object() || !s["events"].is_array())
        throw Unsupported("Invalid GUI scenario");
      if (s["expect"]["files"].size() || s["dialogs"].size())
        groups.push_back(arr({s}));
      else
        bulk.push(s);
    }
    if (bulk.size())
      groups.insert(groups.begin(), bulk);
    for (auto &[cmd, san] : targets)
      for (auto group : groups) {
        stage("execution");
        auto output =
            fresh_gui_directory(job, san ? "checked-output" : "test-output");
        auto run = run_gui_target(job, output, cmd, obj({{"scenarios", group}}),
                                  san, deadline);
        if (run.exit_code) {
          result.set("returncode", Json::number(run.exit_code));
          result.set("stderr",
                     str(run.err.substr(
                         run.err.size() > 4000 ? run.err.size() - 4000 : 0)));
          throw Unsupported("Qt driver failed");
        }
        auto observed = Json::parse(run.out);
        if (!observed.is_array() || observed.size() != group.size())
          throw Unsupported("Incomplete GUI response");
        stage("outcome");
        for (size_t i = 0; i < group.size(); ++i) {
          try {
            check_behaviour_outcome(spec, group.at(i), observed.at(i), output);
          } catch (...) {
            result.set("scenario", group.at(i));
            result.set("actual", observed.at(i));
            throw;
          }
          ++checked;
        }
      }
    result.set("valid", boolean(true));
    stage("complete");
    result.set("sanitizers", boolean(lang == "cpp"));
  } catch (const std::exception &e) {
    result.set("message", str(e.what()));
  }
  result.set("cases", Json::number(checked));
  return result;
}
Json build_behaviour(const BehaviourSpec &spec, const std::string &lang,
                     const Path &resources, const Path &artifacts,
                     NativeEngine &engine, Deadline deadline,
                     const std::string &python, bool emit_only) {
  auto job = fresh_gui_directory(artifacts, "job");
  auto report = obj({{"status", str("failed")},
                     {"kind", str("behaviour_program")},
                     {"language", str(lang)},
                     {"job", str(job.string())},
                     {"specification", spec.data()}});
  try {
    write(job / "specification.json", canonical(spec.data()) + "\n");
    auto plan = plan_behaviour(spec, lang, resources, engine, deadline);
    auto graph = construct_behaviour(spec, plan, deadline);
    report.set("construction", plan);
    report.set("solver", graph.reports);
    auto source = job / (lang == "cpp" ? "program.cpp" : "program.py");
    auto code = emit_behaviour(spec, graph, lang, resources);
    write(source, code);
    report.set("source", str(source.string()));
    report.set("source_sha256", str(sha256(code)));
    if (emit_only)
      report.set("status", str("emitted"));
    else {
      auto validation =
          validate_behaviour(spec, resources, job, lang, deadline, python);
      report.set("validation", validation);
      if (validation["valid"].flag())
        report.set("status", str("validated"));
    }
    report.set("text", str(report["status"].text() + " " + lang +
                           " program: " + source.string()));
  } catch (const std::exception &e) {
    report.set("text", str(e.what()));
  }
  write(job / "report.json", canonical(report) + "\n");
  return report;
}
} // namespace unllm::native
