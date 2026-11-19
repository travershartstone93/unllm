#include "unllm/symbolic/behaviour_solver.h"
#include "solver_numbers.h"
#include <algorithm>
#include <chrono>
#include <cvc5/cvc5.h>
#include <set>
namespace unllm::native {
namespace {
using namespace cvc5;
using Bindings = std::map<std::string, Term>;
const std::map<std::string, Kind> operations{
    {"add", Kind::ADD},       {"subtract", Kind::SUB},
    {"multiply", Kind::MULT}, {"divide", Kind::DIVISION},
    {"eq", Kind::EQUAL},      {"ne", Kind::DISTINCT},
    {"lt", Kind::LT},         {"le", Kind::LEQ},
    {"gt", Kind::GT},         {"ge", Kind::GEQ},
    {"and", Kind::AND},       {"or", Kind::OR},
    {"not", Kind::NOT},       {"neg", Kind::NEG}};
void walk(const BehaviourValue &v, std::vector<const BehaviourValue *> &nodes) {
  nodes.push_back(&v);
  for (const auto &child : v.args)
    walk(child, nodes);
}
bool scalar(const BehaviourValue &v, const Json &fields) {
  if (v.kind == "literal")
    return v.value.is_boolean() || v.value.is_number();
  if (v.kind == "records" || v.kind == "primitive")
    return false;
  if (v.kind == "state" || v.kind == "prev" || v.kind == "next")
    return fields[v.name]["type"].text() == "number";
  return std::all_of(v.args.begin(), v.args.end(),
                     [&](const auto &a) { return scalar(a, fields); });
}
bool future(const BehaviourValue &v) {
  return v.kind == "next" || std::any_of(v.args.begin(), v.args.end(), future);
}
Term encode(TermManager &tm, const BehaviourValue &v, const Bindings &old,
            const Bindings &next) {
  if (v.kind == "literal")
    return v.value.is_boolean() ? tm.mkBoolean(v.value.flag())
                                : tm.mkReal(solver_decimal(v.value));
  if (v.kind == "prev" || v.kind == "state")
    return old.at(v.name);
  if (v.kind == "next")
    return next.at(v.name);
  std::vector<Term> args;
  for (const auto &a : v.args)
    args.push_back(encode(tm, a, old, next));
  if (v.name == "positive")
    return args.at(0);
  if (v.kind == "conditional")
    return tm.mkTerm(Kind::ITE, args);
  return tm.mkTerm(operations.at(v.name), args);
}
BehaviourValue decode(const Term &term, const Bindings &formals,
                      size_t &budget) {
  if (!budget)
    throw ResourceLimit("Synthesized transition exceeds expression budget");
  --budget;
  auto k = term.getKind();
  if (k == Kind::VARIABLE) {
    for (auto &[name, old] : formals)
      if (term == old)
        return {"state", name, {}, {}};
    throw Unsupported("Unexpected solver binding");
  }
  if (term.isRealValue())
    return {
        "literal", "", Json::real(solver_binary64(term.getRealValue())), {}};
  if (term.isBooleanValue())
    return {"literal", "", Json::boolean(term.getBooleanValue()), {}};
  std::vector<BehaviourValue> args;
  for (const auto &child : term)
    args.push_back(decode(child, formals, budget));
  if (k == Kind::ITE)
    return {"conditional", "", {}, std::move(args)};
  for (auto &[name, op] : operations)
    if (op == k) {
      if (k == Kind::NOT || k == Kind::NEG)
        return {"unary", name, {}, std::move(args)};
      if (args.size() < 2)
        throw Unsupported("Unexpected synthesized arity");
      auto value = std::move(args.front());
      for (size_t i = 1; i < args.size(); ++i)
        value = {"binary", name, {}, {std::move(value), std::move(args[i])}};
      return value;
    }
  throw Unsupported("Unexpected solver expression: " + term.toString());
}
} // namespace
TransitionSolution synthesize_transition(const BehaviourSpec &spec,
                                         const std::string &action_id,
                                         int timeout_ms) {
  using namespace cvc5;
  auto started = std::chrono::steady_clock::now();
  if (timeout_ms < 1 || timeout_ms > 180000)
    throw Unsupported("Invalid transition time budget");
  auto fields = spec.state();
  Json action;
  for (auto a : spec.actions().items())
    if (a["id"].text() == action_id)
      action = a;
  if (!action.contains("ensure"))
    throw Unsupported("Expected an action with a transition postcondition");
  auto post = behaviour_expression(action["ensure"], fields, true).value;
  if (!scalar(post, fields))
    throw Unsupported(
        "Transition requires scalar arithmetic or Boolean predicates");
  std::vector<const BehaviourValue *> nodes;
  walk(post, nodes);
  std::set<std::string> names, outputs;
  for (auto &[name, field] : fields.members())
    if (field["type"].text() == "number")
      names.insert(name);
  bool nonlinear = false;
  std::set<double> constants{0, 1};
  for (auto v : nodes) {
    if (v->kind == "next")
      outputs.insert(v->name);
    if (v->kind == "literal" && v->value.is_number())
      constants.insert(v->value.real());
    if (v->kind == "binary" && v->name == "multiply" &&
        v->args[0].kind != "literal" && v->args[1].kind != "literal")
      nonlinear = true;
  }
  TermManager tm;
  Solver solver(tm);
  solver.setOption("sygus", "true");
  solver.setOption("incremental", "false");
  solver.setOption("tlimit", std::to_string(timeout_ms));
  solver.setLogic(nonlinear ? "NRA" : "LRA");
  auto real = tm.getRealSort(), boolean = tm.getBooleanSort();
  std::vector<Term> variables, inputs;
  Bindings formals, old, functions, next;
  auto input_names = Json::array(), output_names = Json::array();
  for (const auto &name : names) {
    variables.push_back(tm.mkVar(real, "old_" + name));
    formals[name] = variables.back();
    inputs.push_back(solver.declareSygusVar("input_" + name, real));
    old[name] = inputs.back();
    input_names.push(Json::string(name));
  }
  auto expr = tm.mkVar(real, "Expression"),
       cond = tm.mkVar(boolean, "Condition");
  auto grammar = solver.mkGrammar(variables, {expr, cond});
  std::vector<Term> productions = variables;
  std::set<std::string> rational_keys;
  auto add_constant = [&](const Term &t) {
    if (!rational_keys.insert(t.getRealValue()).second)
      return;
    productions.push_back(t);
    if (t != tm.mkReal(0) && t != tm.mkReal(1))
      productions.push_back(tm.mkTerm(Kind::MULT, {t, expr}));
  };
  for (double n : constants) {
    auto t = tm.mkReal(solver_decimal(Json::real(n)));
    add_constant(t);
    if (n != 0)
      add_constant(
          solver.simplify(tm.mkTerm(Kind::DIVISION, {tm.mkReal(1), t})));
  }
  for (auto k : {Kind::ADD, Kind::SUB})
    productions.push_back(tm.mkTerm(k, {expr, expr}));
  productions.push_back(tm.mkTerm(Kind::ITE, {cond, expr, expr}));
  grammar.addRules(expr, productions);
  grammar.addRules(cond, {tm.mkBoolean(true), tm.mkBoolean(false),
                          tm.mkTerm(Kind::LEQ, {expr, expr}),
                          tm.mkTerm(Kind::EQUAL, {expr, expr}),
                          tm.mkTerm(Kind::NOT, {cond}),
                          tm.mkTerm(Kind::AND, {cond, cond})});
  for (auto v : nodes)
    if ((v->kind == "binary" || v->kind == "conditional") && !future(*v)) {
      auto term = encode(tm, *v, formals, {});
      grammar.addRule(term.getSort() == boolean ? cond : expr, term);
    }
  for (const auto &name : outputs) {
    auto fn = solver.synthFun("update_" + name, variables, real, grammar);
    functions[name] = fn;
    std::vector<Term> args{fn};
    args.insert(args.end(), inputs.begin(), inputs.end());
    next[name] = tm.mkTerm(Kind::APPLY_UF, args);
    output_names.push(Json::string(name));
  }
  auto obligation = encode(tm, post, old, next);
  bool encoded = false;
  if (action.contains("requires")) {
    auto guard = behaviour_expression(action["requires"], fields).value;
    if (scalar(guard, fields)) {
      obligation =
          tm.mkTerm(Kind::IMPLIES, {encode(tm, guard, old, {}), obligation});
      encoded = true;
    }
  }
  solver.addSygusConstraint(obligation);
  if (!solver.checkSynth().hasSolution())
    throw Unsupported("No transition found within the synthesis budget");
  TransitionSolution result;
  auto solutions = Json::object();
  for (auto &[name, fn] : functions) {
    auto term = solver.getSynthSolution(fn);
    size_t budget = 256;
    result.updates[name] = decode(term[1], formals, budget);
    solutions.set(name, Json::string(term.toString()));
  }
  auto &e = result.evidence;
  e = Json::object();
  e.set("engine", Json::string("cvc5"));
  e.set("version", Json::string(solver.getVersion()));
  e.set("method", Json::string("relational SyGuS"));
  e.set("postcondition", action["ensure"]);
  e.set("inputs", input_names);
  e.set("outputs", output_names);
  e.set("solutions", solutions);
  e.set("precondition", action["requires"]);
  e.set("precondition_encoded", Json::boolean(encoded));
  e.set("simultaneous", Json::boolean(true));
  e.set("domain", Json::string("mathematical polynomial reals (degree <= 2); "
                               "finite binary64 validated separately"));
  e.set("elapsed_ms", Json::real(std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - started)
                                     .count()));
  return result;
}
} // namespace unllm::native
