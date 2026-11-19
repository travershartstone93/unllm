#include "contract.h"
#include "../../src/symbolic/algorithm_goal.h"
#include "../../src/symbolic/gui_json.h"
#include <cvc5/cvc5.h>
#include <map>

namespace unllm::native {
using namespace gui;
namespace {
struct Expression {
  Json ir;
  cvc5::Term term;
};
struct Signature {
  std::string sequence, threshold;
  bool sequence_first;
};
Signature signature(const Json &spec) {
  auto params = spec["params"];
  if (!params.is_array() || params.size() != 2 ||
      spec["result"].text() != "int")
    throw Unsupported(
        "Expected an ints parameter, an int parameter and an int result");
  Signature s{};
  for (auto param : params.items()) {
    if (!param.is_array() || param.size() != 2 || !param.at(0).is_string())
      throw Unsupported("Expected a named typed parameter");
    auto name = param.at(0).text(), type = param.at(1).text();
    if (type == "ints")
      s.sequence = name;
    else if (type == "int")
      s.threshold = name;
    else
      throw Unsupported("Unknown reduction parameter type");
  }
  if (s.sequence.empty() || s.threshold.empty() || s.sequence == s.threshold)
    throw Unsupported("Expected distinct sequence and threshold bindings");
  s.sequence_first = params.at(0).at(0).text() == s.sequence;
  return s;
}
} // namespace
Json reduction_specification(const Json &req) {
  fields_only(req, {"status", "aggregate", "collection", "comparison",
                    "compare_to", "required_structure"});
  auto threshold = req["compare_to"];
  fields_only(threshold, {"input", "literal"});
  if (threshold.size() != 1)
    throw Unsupported("Expected exactly one threshold input or literal");
  auto where = obj({{"relation", req["comparison"]}});
  if (threshold.contains("input"))
    where.set("parameter", threshold["input"]);
  else
    where.set("literal", threshold["literal"]);
  return obj({{"program", str("function_contract")},
              {"params", arr({arr({str("values"), str("ints")}),
                              arr({str("cutoff"), str("int")})})},
              {"result", str("int")},
              {"behavior", obj({{"over", req["collection"]},
                                {"aggregate", req["aggregate"]},
                                {"where", where}})},
              {"structure", req["required_structure"]}});
}

Json construct_reduction(const Json &spec) {
  using namespace cvc5;
  fields_only(spec, {"program", "params", "result", "behavior", "structure"});
  auto s = signature(spec);
  auto structure = spec["structure"];
  if (!structure.is_array() || structure.size() > 2 ||
      (structure.size() >= 1 && structure.at(0).text() != "for") ||
      (structure.size() == 2 && structure.at(1).text() != "if"))
    throw Unsupported(
        "Expected [], [for], or [for, if] as the requested structure");
  auto behavior = spec["behavior"], where = behavior["where"];
  fields_only(behavior, {"aggregate", "over", "where"});
  fields_only(where, {"relation", "parameter", "literal"});
  if (where.contains("parameter") == where.contains("literal"))
    throw Unsupported("Expected one predicate parameter or literal");
  auto bound = where["literal"];
  if (where.contains("literal") &&
      (!bound.is_integer() || bound.dump() != std::to_string(bound.integer())))
    throw Unsupported("Expected a signed 64-bit literal");
  if (required_text(behavior, "over") != s.sequence ||
      (where.contains("parameter") &&
       required_text(where, "parameter") != s.threshold))
    throw Unsupported("Predicate or reduction refers to the wrong parameter");
  auto aggregate = required_text(behavior, "aggregate");
  static const std::map<std::string, Kind> relations{{"lt", Kind::LT},
                                                     {"le", Kind::LEQ},
                                                     {"eq", Kind::EQUAL},
                                                     {"gt", Kind::GT},
                                                     {"ge", Kind::GEQ}};
  auto relation = required_text(where, "relation");
  if ((aggregate != "sum" && aggregate != "count") ||
      !relations.contains(relation))
    throw Unsupported("Unsupported aggregation or relation");

  TermManager tm;
  Solver solver(tm);
  solver.setLogic("QF_LIA");
  solver.setOption("tlimit-per", "250");
  auto equivalent = [&](Term left, Term right) {
    auto verdict = solver.checkSatAssuming(left.eqTerm(right).notTerm());
    if (verdict.isUnknown())
      throw ResourceLimit("Reduction equivalence check inconclusive");
    return verdict.isUnsat();
  };
  auto input = [&](std::string name) {
    return Expression{arr({str("var"), str(name)}),
                      tm.mkConst(tm.getIntegerSort(), name)};
  };
  auto literal = [&](int64_t n) {
    return Expression{arr({str("int"), str(std::to_string(n))}),
                      tm.mkInteger(std::to_string(n))};
  };
  auto fresh = [&](std::string name) {
    while (name == s.sequence || name == s.threshold)
      name += "_";
    return name;
  };
  auto item_name = fresh("item"), acc_name = fresh("total");
  auto item = input(item_name), acc = input(acc_name),
       cutoff = where.contains("literal") ? literal(bound.integer())
                                          : input(s.threshold);
  auto zero = literal(0), one = literal(1);
  auto target_guard =
      tm.mkTerm(relations.at(relation), {item.term, cutoff.term});
  auto contribution = aggregate == "sum" ? item.term : one.term;
  auto step = [&](Term guard, Term delta) {
    return tm.mkTerm(
        Kind::ITE, {guard, tm.mkTerm(Kind::ADD, {acc.term, delta}), acc.term});
  };
  auto target = step(target_guard, contribution);
  auto compare = [&](Kind kind, const Expression &left,
                     const Expression &right) {
    return Expression{
        arr({str(kind == Kind::LT ? "lt" : "eq"), left.ir, right.ir}),
        tm.mkTerm(kind, {left.term, right.term})};
  };
  int attempts = 0;
  for (auto seed : {one, zero}) {
    if (!equivalent(seed.term, zero.term))
      continue;
    for (auto output : {cutoff, acc}) {
      if (!equivalent(output.term, acc.term))
        continue;
      for (auto guard :
           {compare(Kind::LT, item, cutoff), compare(Kind::LT, cutoff, item),
            compare(Kind::EQUAL, item, cutoff)}) {
        for (bool invert : {false, true}) {
          for (auto delta : {cutoff, one, item}) {
            ++attempts;
            if (!equivalent(step(invert ? guard.term.notTerm() : guard.term,
                                 delta.term),
                            target))
              continue;
            auto update = arr({arr({str("set"), str(acc_name),
                                    arr({str("add"), acc.ir, delta.ir})})});
            auto empty = Json::array();
            auto branch = arr({str("if"), guard.ir, invert ? empty : update,
                               invert ? update : empty});
            auto loop =
                arr({str("each"), str(item_name),
                     arr({str("var"), str(s.sequence)}), arr({branch})});
            auto function = obj(
                {{"name", str("compute")},
                 {"params", spec["params"]},
                 {"result", str("int")},
                 {"locals", arr({arr({str(acc_name), str("int"), seed.ir})})},
                 {"body", arr({loop})},
                 {"return", output.ir}});
            auto args = s.sequence_first ? arr({empty, str("0")})
                                         : arr({str("0"), empty});
            return obj(
                {{"ir", obj({{"version", Json::number(1)},
                             {"functions", arr({function})}})},
                 {"attempts", Json::number(attempts)},
                 {"smoke", obj({{"args", args}, {"expected", str("0")}})}});
          }
        }
      }
    }
  }
  throw Unsupported("No equivalent filtered reduction in the bounded grammar");
}
} // namespace unllm::native
