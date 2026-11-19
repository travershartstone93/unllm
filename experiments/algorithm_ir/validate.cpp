#include "validate.h"
#include "unllm/symbolic/target.h"
#include <fstream>
#include <sstream>
namespace unllm::algorithm {
using namespace unllm::native;
namespace {
std::string literal(const Json &x, bool py) {
  if (x.is_boolean())
    return x.flag() ? (py ? "True" : "true") : (py ? "False" : "false");
  if (x.is_string()) {
    auto n = std::get<int64_t>(argument(x, Type::integer));
    return py ? std::to_string(n)
              : "I(std::stoll(" + Json::string(std::to_string(n)).dump() + "))";
  }
  if (!x.is_array())
    throw Unsupported("Unexpected fixture type");
  std::string s = py ? "[" : "Seq{";
  size_t i = 0;
  for (auto item : x.items()) {
    if (i++)
      s += ", ";
    s += literal(item, py);
  }
  return s + (py ? "]" : "}");
}
std::string driver(const std::vector<Json> &rows, bool py) {
  std::ostringstream s;
  if (!py)
    s << "\n#include <string>\nint main(){\n";
  for (auto row : rows) {
    std::string args;
    int i = 0;
    if (!py)
      s << "{\n";
    for (auto arg : row["args"].items()) {
      auto n = "a" + std::to_string(i++);
      s << (py ? "" : "auto ") << n << " = " << literal(arg, py)
        << (py ? "\n" : ";\n");
      if (args.size())
        args += ", ";
      args += n;
    }
    auto call = "algorithm(" + args + ")";
    if (row.contains("error")) {
      auto error = row["error"].dump();
      if (py)
        s << "try:\n    " << call
          << "\nexcept Exception as e:\n    assert str(e) == " << error
          << "\nelse:\n    raise AssertionError('missing expected "
             "exception')\n";
      else
        s << "try { " << call
          << "; return 1; } catch(const std::exception &e){ "
             "if(std::string(e.what())!="
          << error << ")return 2; }\n";
    } else {
      auto match = call + " == " + literal(row["expected"], py);
      if (py)
        s << "assert " << match << "\n";
      else
        s << "if(!(" << match << "))return 3;\n";
    }
    i = 0;
    for (auto arg : row["args"].items()) {
      if (arg.is_array()) {
        auto match = "a" + std::to_string(i) + " == " + literal(arg, py);
        if (py)
          s << "assert " << match << "\n";
        else
          s << "if(!(" << match << "))return 4;\n";
      }
      ++i;
    }
    if (!py)
      s << "}\n";
  }
  if (!py)
    s << "}\n";
  return s.str();
}

} // namespace
void check_emission(const std::filesystem::path &job, const std::string &source,
                    const std::vector<Json> &cases, const std::string &python) {
  bool py = !python.empty();
  auto file = job / (py ? "test.py" : "test.cpp");
  {
    std::ofstream stream(file);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << source << driver(cases, py);
  }
  std::vector<std::string> command;
  if (py)
    command = {python, "-I", "/job/test.py"};
  else {
    auto compiled =
        Process({"/usr/bin/clang++", "-std=c++20",
                 "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                 file, "-o", job / "program"})
            .finish(after_ms(30000));
    if (compiled.exit_code)
      throw Unsupported("C++ compilation failed: " + compiled.err);
    command = {"/job/program"};
  }
  auto run = run_stream_target(job, command, "", after_ms(10000), !py);
  if (run.exit_code)
    throw Unsupported("Executable failed checks: " +
                      std::to_string(run.exit_code) + " " + run.err);
}
} // namespace unllm::algorithm
