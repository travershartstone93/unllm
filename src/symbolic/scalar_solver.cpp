#include "unllm/symbolic/expression.h"
#include <chrono>
#include <cvc5/cvc5.h>
#include <functional>
#include "solver_numbers.h"
#include <map>
#include <set>
namespace unllm::native {
ExpressionSolution synthesize_expression(const Expr &target, bool predicate,
                                         int timeout_ms) {
  using namespace cvc5;
  auto started = std::chrono::steady_clock::now();
  if (target.type() != (predicate ? ExprType::Boolean : ExprType::Number))
    throw Unsupported("Synthesis result type differs from target");
  if (timeout_ms < 1 || timeout_ms > 180000)
    throw Unsupported("Invalid scalar solver time budget");
  TermManager tm;
  Solver solver(tm);
  solver.setOption("sygus", "true");
  solver.setOption("incremental", "false");
  solver.setOption("tlimit", std::to_string(timeout_ms));
  solver.setLogic("ALL");
  auto real = tm.getRealSort(), boolean = tm.getBooleanSort();
  auto x = tm.mkVar(real, "x"), number = tm.mkVar(real, "Number"),
       condition = tm.mkVar(boolean, "Condition"),
       constant = tm.mkVar(real, "Constant");
  auto first = predicate ? condition : number;
  std::vector<Term> nonterminals{first};
  for (auto t : {number, condition, constant})
    if (t != first)
      nonterminals.push_back(t);
  auto grammar = solver.mkGrammar({x}, nonterminals);
  std::set<double> values{0.0, 1.0, -1.0};
  for (auto n : target.constants())
    values.insert(n);
  std::vector<Term> literals;
  for (auto n : values)
    literals.push_back(tm.mkReal(solver_decimal(Json::real(n))));
  grammar.addRules(constant, literals);
  grammar.addRules(number, {x, constant, tm.mkTerm(Kind::ADD, {number, number}),
                            tm.mkTerm(Kind::SUB, {number, number}),
                            tm.mkTerm(Kind::MULT, {constant, number})});
  std::vector<Term> tests;
  for (auto k : {Kind::GT, Kind::GEQ, Kind::LT, Kind::LEQ, Kind::EQUAL})
    tests.push_back(tm.mkTerm(k, {number, number}));
  for (auto k : {Kind::AND, Kind::OR})
    tests.push_back(tm.mkTerm(k, {condition, condition}));
  tests.push_back(tm.mkTerm(Kind::NOT, {condition}));
  grammar.addRules(condition, tests);
  if (target.contains("if"))
    grammar.addRule(number, tm.mkTerm(Kind::ITE, {condition, number, number}));
  const std::map<std::string, Kind> kinds{
      {"add", Kind::ADD},    {"subtract", Kind::SUB}, {"multiply", Kind::MULT},
      {"negate", Kind::NEG}, {"gt", Kind::GT},        {"ge", Kind::GEQ},
      {"lt", Kind::LT},      {"le", Kind::LEQ},       {"eq", Kind::EQUAL},
      {"and", Kind::AND},    {"or", Kind::OR},        {"not", Kind::NOT},
      {"if", Kind::ITE}};
  auto input = solver.declareSygusVar("value", real);
  std::function<Term(const Expr &)> encode = [&](const Expr &e) -> Term {
    if (e.op == "variable")
      return input;
    if (e.op == "constant")
      return e.value.is_boolean() ? tm.mkBoolean(e.value.flag())
                                  : tm.mkReal(solver_decimal(e.value));
    std::vector<Term> args;
    for (auto &a : e.args)
      args.push_back(encode(a));
    return tm.mkTerm(kinds.at(e.op), args);
  };
  auto function = solver.synthFun(predicate ? "predicate" : "transform", {x},
                                  predicate ? boolean : real, grammar);
  solver.addSygusConstraint(
      tm.mkTerm(Kind::EQUAL, {tm.mkTerm(Kind::APPLY_UF, {function, input}),
                              encode(target)}));
  auto result = solver.checkSynth();
  if (!result.hasSolution())
    throw Unsupported(
        "The scalar synthesis problem exceeded its grammar/time budget: " +
        result.toString());
  auto solution = solver.getSynthSolution(function);
  std::function<Expr(const Term &, unsigned)> decode =
      [&](const Term &t, unsigned depth) -> Expr {
    if (depth > 16)
      throw Unsupported("Synthesized expression exceeds depth budget");
    auto k = t.getKind();
    if (k == Kind::VARIABLE) {
      if (t != x)
        throw Unsupported("Unexpected solver variable");
      return {"variable", {}, {}};
    }
    if (t.isRealValue()) {
      auto rational = t.getRealValue();
      double value = solver_binary64(rational);
      return {"constant", {}, Json::real(value)};
    }
    if (t.isBooleanValue())
      return {"constant", {}, Json::boolean(t.getBooleanValue())};
    std::string op;
    for (auto &[name, kind] : kinds)
      if (kind == k) {
        op = name;
        break;
      }
    if (op.empty())
      throw Unsupported("Unsupported solver expression node");
    std::vector<Expr> args;
    for (auto child : t)
      args.push_back(decode(child, depth + 1));
    if (args.size() > 2 &&
        (op == "add" || op == "multiply" || op == "and" || op == "or")) {
      Expr e{op, {args[0], args[1]}, {}};
      for (size_t i = 2; i < args.size(); ++i)
        e = {op, {e, args[i]}, {}};
      return e;
    }
    return {op, args, {}};
  };
  auto expression = decode(solution[1], 0);
  expression.type();
  auto evidence = Json::object();
  evidence.set("engine", Json::string("cvc5"));
  evidence.set("version", Json::string(solver.getVersion()));
  evidence.set("method", Json::string("SyGuS"));
  evidence.set("solution", Json::string(solution.toString()));
  evidence.set("elapsed_ms",
               Json::real(std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count()));
  evidence.set("formal_domain",
               Json::string("real arithmetic; executable floating-point "
                            "behavior checked separately"));
  return {expression, evidence};
}
} // namespace unllm::native
