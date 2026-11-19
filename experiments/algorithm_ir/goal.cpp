#include "../../src/symbolic/algorithm_goal.h"
#include "../../src/symbolic/gui_json.h"
#include "search.h"
#include "contract.h"
#include "unllm/symbolic/gui_target.h"
#include "validate.h"
#include <limits>

namespace unllm::native {
using namespace gui;
namespace {
struct IntervalGoal {
  int64_t first, last;
  bool square, sum;
};
IntervalGoal read_goal(const Json &spec) {
  fields_only(spec, {"program", "interval", "map", "reduce"});
  auto bounds = spec["interval"];
  if (!bounds.is_array() || bounds.size() != 2)
    throw Unsupported("Supply two inclusive integer bounds");
  for (auto x : bounds.items())
    if (!x.is_integer() || x.integer() < INT32_MIN || x.integer() > INT32_MAX)
      throw Unsupported("Interval endpoints must be signed 32-bit integers");
  auto a = bounds.at(0).integer(), b = bounds.at(1).integer();
  if (b < a || b - a >= 64)
    throw Unsupported("Supply an ascending interval of 1 to 64 integers");
  auto transform = spec["map"].text(), reduce = spec["reduce"].text();
  if ((transform != "identity" && transform != "square") ||
      (reduce != "collect" && reduce != "sum"))
    throw Unsupported("Unknown interval transformation or reduction");
  return {a, b, transform == "square", reduce == "sum"};
}
algorithm::Value expected(const IntervalGoal &g) {
  algorithm::Sequence values;
  int64_t total = 0;
  for (auto x = g.first; x <= g.last; ++x) {
    auto y = g.square ? x * x : x;
    if (g.sum && __builtin_add_overflow(total, y, &total))
      throw Unsupported("Sum exceeds signed 64-bit result");
    values.push_back(y);
  }
  if (g.sum)
    return total;
  return values;
}
Json integer(int64_t n) { return arr({str("int"), str(std::to_string(n))}); }
Json variable(const char *v) { return arr({str("var"), str(v)}); }
Json loop(int64_t first, int64_t count, bool offset, bool square, bool sum) {
  auto index = variable("index"), out = variable("out");
  auto term = offset ? arr({str("add"), integer(first), index}) : index;
  if (square)
    term = arr({str("mul"), term, term});
  auto update = arr(
      {str("set"), str("out"), arr({str(sum ? "add" : "append"), out, term})});
  auto body =
      arr({arr({str("range"), str("index"), integer(count), arr({update})})});
  auto type = str(sum ? "int" : "ints");
  auto function = obj(
      {{"name", str("compute")},
       {"params", Json::array()},
       {"locals",
        arr({arr({str("out"), type, sum ? integer(0) : arr({str("empty")})})})},
       {"body", body},
       {"result", type},
       {"return", out}});
  return obj({{"version", Json::number(1)}, {"functions", arr({function})}});
}
Json emit_function(const Json &spec, const Json &ir,
                   const algorithm::Program &program,
                   const std::vector<Json> &cases, const std::string &language,
                   const std::filesystem::path &artifacts,
                   const std::string &python, bool emit_only) {
  bool py = language == "python";
  auto source = algorithm::emit(program, py);
  auto job = fresh_gui_directory(artifacts, "job");
  auto file = job / (py ? "algorithm.py" : "algorithm.cpp");
  write(file, source);
  if (!emit_only)
    algorithm::check_emission(job, source, cases, py ? python : "");
  auto status = emit_only ? "emitted" : "validated";
  return obj({{"status", str(status)},
              {"kind", str("algorithm")},
              {"specification", spec},
              {"ir", ir},
              {"text", str(std::string(status) + " " + language +
                           " function: " + file.string())},
              {"job", str(job.string())},
              {"source", str(file.string())},
              {"source_sha256", str(sha256(source))},
              {"language", str(language)}});
}
} // namespace
Json build_function_contract(const Json &spec, const std::string &language,
                             const std::filesystem::path &artifacts,
                             const std::string &python, bool emit_only) {
  auto contract = construct_reduction(spec);
  auto ir = contract["ir"];
  auto result = emit_function(spec, ir, algorithm::decode(ir), {contract["smoke"]},
                              language, artifacts, python, emit_only);
  result.set("validation_basis", str(emit_only
      ? "cvc5 base, transition and return equivalence"
      : "cvc5 base, transition and return equivalence; empty-input emission check"));
  result.set("transition_candidates", contract["attempts"]);
  return result;
}
Json build_example_function(const Json &spec, const std::string &language,
                            const std::filesystem::path &artifacts,
                            const std::string &python, bool emit_only) {
  auto search = algorithm::synthesize(spec);
  if (search["status"].text() != "candidate") {
    search.set("text", search["reason"]);
    return search;
  }
  auto ir = search["ir"];
  auto program = algorithm::decode(ir);
  std::vector<Json> cases;
  for (const auto &example : spec["examples"].items())
    cases.push_back(obj(
        {{"args", arr({example["input"]})}, {"expected", example["output"]}}));
  auto result = emit_function(spec, ir, program, cases, language, artifacts,
                              python, emit_only);
  result.set("unique_candidates", search["unique_candidates"]);
  result.set("validation_basis", str("supplied examples"));
  return result;
}
Json build_algorithm_goal(const Json &spec, const std::string &language,
                          const std::filesystem::path &artifacts,
                          const std::string &python, bool emit_only) {
  auto goal = read_goal(spec);
  auto oracle = expected(goal);
  int attempts = 0;
  for (bool offset : {false, true})
    for (bool square : {false, true}) {
      ++attempts;
      // Also check the loop shape on a non-degenerate interval so that an
      // incidental equality at 0 or 1 cannot decide the transformation.
      IntervalGoal witness{-2, 3, goal.square, goal.sum};
      auto trial = algorithm::decode(loop(-2, 6, offset, square, goal.sum));
      if (algorithm::interpret(trial, {}) != expected(witness))
        continue;
      auto ir = loop(goal.first, goal.last - goal.first + 1, offset, square,
                     goal.sum);
      auto program = algorithm::decode(ir);
      if (algorithm::interpret(program, {}) != oracle)
        continue;
      auto result =
          emit_function(spec, ir, program,
                        {obj({{"args", Json::array()},
                              {"expected", algorithm::json(oracle)}})},
                        language, artifacts, python, emit_only);
      result.set("search_attempts", Json::number(attempts));
      return result;
    }
  throw ResourceLimit("Interval loop search exhausted");
}
} // namespace unllm::native
