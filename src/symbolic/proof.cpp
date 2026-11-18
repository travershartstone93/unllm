#include "unllm/symbolic/proof.h"
#include "ProvenanceLexer.h"
#include "ProvenanceParser.h"
#include <algorithm>
#include <antlr4-runtime.h>
namespace unllm::native {
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw Unsupported(message);
}
class ParseErrors : public antlr4::BaseErrorListener {
  void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t, size_t,
                   const std::string &, std::exception_ptr) override {
    throw Unsupported("Malformed provenance term");
  }
};
using Term = std::pair<std::string, std::vector<std::string>>;
Term term(const Json &value) {
  require(value.is_string(), "Provenance term must be text");
  auto text = value.text();
  if (text.size() > 65536)
    throw ResourceLimit("Provenance term is too large");
  antlr4::ANTLRInputStream input(text);
  ProvenanceLexer lexer(&input);
  ParseErrors errors;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);
  antlr4::CommonTokenStream tokens(&lexer);
  ProvenanceParser parser(&tokens);
  parser.removeErrorListeners();
  parser.addErrorListener(&errors);
  auto *tree = parser.term();
  Term result{tree->NEQ() ? "neq" : tree->IDENT()->getText(), {}};
  for (auto *v : tree->value())
    result.second.push_back(v->STRING() ? Json::parse(v->getText()).text()
                                        : v->getText());
  return result;
}
bool equal(const Goal &a, const Goal &b) {
  return a.predicate == b.predicate && a.subject == b.subject &&
         a.object == b.object && a.negative == b.negative;
}
std::vector<std::string> texts(const Json &values) {
  require(values.is_array(), "Witness arguments must be an array");
  std::vector<std::string> out;
  for (auto v : values.items()) {
    require(v.is_string(), "Witness arguments must be ground strings");
    out.push_back(v.text());
  }
  return out;
}
Json list(const std::vector<std::string> &values) {
  auto out = Json::array();
  for (auto &v : values)
    out.push(Json::string(v));
  return out;
}
double numeric(const std::string &text) {
  size_t end = 0;
  double n = std::stod(text, &end);
  require(end == text.size() && std::isfinite(n) && std::abs(n) <= 1e15,
          "Numeric proof is outside its domain");
  return n;
}
} // namespace
ProofChecker::ProofChecker(const std::vector<Claim> &claims,
                           const Json &relations, const Json &complete) {
  for (auto &c : claims) {
    require(!claims_.contains(c.id), "Duplicate proof source ID");
    claims_.emplace(c.id, c);
  }
  for (auto r : relations.items()) {
    r = relation(r);
    auto name = r["name"].text();
    if (r["transitive"].flag())
      transitive_.insert(name);
    if (!r["inverse"].text().empty())
      inverses_.insert({name, r["inverse"].text()});
    domains_.insert({name, r["domain"].text()});
    ranges_.insert({name, r["range"].text()});
  }
  for (auto c : complete.items())
    complete_.insert(
        {required_text(c, "predicate"), required_text(c, "subject")});
}
void ProofChecker::budget(unsigned depth) {
  if (depth > 100 || ++visited_ > 100000)
    throw ResourceLimit("Proof verification budget exceeded");
}
ProofChecker::Term ProofChecker::souffle(const Json &tree, unsigned depth) {
  budget(depth);
  require(tree.is_object(), "Provenance node must be an object");
  if (tree.contains("axiom")) {
    require(!tree.contains("premises") && !tree.contains("children"),
            "Ambiguous provenance node");
    auto [name, a] = term(tree["axiom"]);
    bool valid = false;
    if (name == "source" && a.size() == 4) {
      auto it = claims_.find(a[0]);
      valid = it != claims_.end() && !it->second.negative &&
              it->second.predicate == a[1] && it->second.subject == a[2] &&
              it->second.object == a[3];
      if (valid)
        used_.insert(a[0]);
    } else if (name == "transitive" && a.size() == 1)
      valid = transitive_.contains(a[0]);
    else if (name == "inverse" && a.size() == 2)
      valid = inverses_.contains({a[0], a[1]});
    else if (name == "arg_domain" && a.size() == 2)
      valid = domains_.contains({a[0], a[1]});
    else if (name == "arg_range" && a.size() == 2)
      valid = ranges_.contains({a[0], a[1]});
    else if (name == "neq" && a.size() == 2)
      valid = a[0] != a[1];
    require(valid, "Unsupported or invalid provenance axiom");
    return {name, a};
  }
  auto [name, a] = term(tree["premises"]);
  require(name == "closure" && a.size() == 3,
          "Unsupported derived provenance relation");
  auto children = tree.contains("children") ? tree["children"] : Json::array();
  require(children.is_array() && children.size() <= 4,
          "Malformed derivation premises");
  std::vector<Term> cs;
  for (auto child : children.items())
    cs.push_back(souffle(child, depth + 1));
  const auto &p = a[0];
  const auto &s = a[1];
  const auto &o = a[2];
  bool valid = false;
  if (cs.size() == 1) {
    auto &[kind, b] = cs[0];
    valid =
        (kind == "source" && b.size() == 4 &&
         std::vector<std::string>(b.begin() + 1, b.end()) == a) ||
        (kind == "closure" && b == std::vector<std::string>{"country", s, o} &&
         p == "located_in");
  }
  if (cs.size() == 2) {
    for (size_t i = 0; i < 2; ++i) {
      auto &[kind, b] = cs[i];
      auto &[other, c] = cs[1 - i];
      if (other != "closure")
        continue;
      if (kind == "inverse")
        valid |=
            b == std::vector<std::string>{c[0], p} && c[1] == o && c[2] == s;
      if (kind == "arg_domain")
        valid |=
            p == "type" && b == std::vector<std::string>{c[0], o} && c[1] == s;
      if (kind == "arg_range")
        valid |=
            p == "type" && b == std::vector<std::string>{c[0], o} && c[2] == s;
      if (kind == "closure") {
        valid |= p == "type" &&
                 b == std::vector<std::string>{"type", s, c[1]} &&
                 c == std::vector<std::string>{"subclass_of", b[2], o};
        valid |= p == "country" &&
                 b == std::vector<std::string>{"located_in", s, o} &&
                 c == std::vector<std::string>{"type", o, "country"};
        valid |= p == "compatible" && b[0] == "produces" && b[1] == s &&
                 c == std::vector<std::string>{"accepts", o, b[2]};
      }
    }
  }
  if (cs.size() == 3 || cs.size() == 4) {
    std::vector<std::vector<std::string>> links;
    bool declared = false, allowed = true;
    for (auto &[kind, b] : cs) {
      if (kind == "closure")
        links.push_back(b);
      else if (kind == "transitive")
        declared = b == std::vector<std::string>{p};
      else if (kind != "neq")
        allowed = false;
    }
    if (allowed && declared && links.size() == 2)
      for (size_t i = 0; i < 2; ++i) {
        auto &b = links[i];
        auto &c = links[1 - i];
        valid |=
            b[0] == p && b[1] == s && c == std::vector<std::string>{p, b[2], o};
      }
  }
  require(valid, "Invalid Souffle derivation");
  auto step = Json::object();
  step.set("engine", Json::string("souffle"));
  step.set("rule", tree["rule-number"]);
  step.set("goal", list(a));
  steps_.push(step);
  return {"closure", a};
}
Goal ProofChecker::witness_goal(const Json &value) const {
  auto name = required_text(value, "functor");
  auto args = texts(value["args"]);
  if (name == "claim" && args.size() == 1) {
    auto it = claims_.find(args[0]);
    require(it != claims_.end(), "Unknown source witness");
    auto &c = it->second;
    return {c.predicate, c.subject, c.object, c.negative};
  }
  if (name == "background" && args.size() == 3)
    return {args[0], args[1], args[2], false};
  throw Unsupported("Compound witness requires an explicit conclusion");
}
void ProofChecker::witness(const Json &value, const Goal &goal,
                           unsigned depth) {
  budget(depth);
  auto name = required_text(value, "functor");
  auto args = value["args"];
  require(args.is_array(), "Malformed witness arguments");
  if (name == "claim") {
    auto actual = witness_goal(value);
    require(equal(actual, goal), "Source witness does not match the goal");
    used_.insert(args.at(0).text());
    return;
  }
  if (name == "background") {
    auto actual = witness_goal(value);
    require(equal(actual, goal), "Background witness does not match the goal");
    require(souffle(value["tree"], depth + 1) == Term{"closure", texts(args)},
            "Background provenance has a different conclusion");
    return;
  }
  require(name == "rule" && args.size() == 2 && args.at(0).is_string() &&
              args.at(1).is_array(),
          "Malformed rule witness");
  auto rule = args.at(0).text();
  auto premises = args.at(1);
  if (rule == "height_comparison" && goal.predicate == "taller_than" &&
      premises.size() == 2) {
    auto a = witness_goal(premises.at(0)), b = witness_goal(premises.at(1));
    require(a.predicate == "height" && b.predicate == "height" && !a.negative &&
                !b.negative && a.subject == goal.subject &&
                b.subject == goal.object,
            "Invalid height premises");
    witness(premises.at(0), a, depth + 1);
    witness(premises.at(1), b, depth + 1);
    require((numeric(a.object) > numeric(b.object)) != goal.negative,
            "Invalid height comparison");
  } else {
    require(goal.predicate == "blocked" && goal.object == "true",
            "Rule applied to wrong proposition");
    if (rule == "requirement_unavailable" && !goal.negative &&
        premises.size() == 2) {
      auto dep = witness_goal(premises.at(0));
      require(dep.predicate == "depends_on" && dep.subject == goal.subject &&
                  !dep.negative,
              "Invalid dependency premise");
      witness(premises.at(0), dep, depth + 1);
      witness(premises.at(1), Goal{"available", dep.object, "true", true},
              depth + 1);
    } else if (rule == "requirements_satisfied" && goal.negative) {
      require(complete_.contains({"depends_on", goal.subject}),
              "Missing completeness boundary");
      std::map<std::string, std::vector<const Claim *>> edges;
      for (auto &[_, c] : claims_)
        if (c.predicate == "depends_on" && !c.negative)
          edges[c.subject].push_back(&c);
      std::set<std::string> expanded, required, witnessed;
      std::vector<std::string> todo{goal.subject};
      while (!todo.empty()) {
        auto s = todo.back();
        todo.pop_back();
        if (!expanded.insert(s).second)
          continue;
        for (auto *c : edges[s]) {
          used_.insert(c->id);
          required.insert(c->object);
          todo.push_back(c->object);
        }
      }
      for (auto w : premises.items()) {
        auto g = witness_goal(w);
        require(g.predicate == "available" && !g.negative && g.object == "true",
                "Invalid availability premise");
        witness(w, g, depth + 1);
        witnessed.insert(g.subject);
      }
      require(required == witnessed,
              "Availability proof omits a required dependency");
      auto step = Json::object();
      step.set("engine", Json::string("scope"));
      auto c = Json::object();
      c.set("predicate", Json::string("depends_on"));
      c.set("subject", Json::string(goal.subject));
      step.set("completeness", c);
      steps_.push(step);
    } else
      throw Unsupported("Unsupported Prolog rule");
  }
  auto step = Json::object();
  step.set("engine", Json::string("prolog"));
  step.set("rule", Json::string(rule));
  step.set("goal", goal.data());
  steps_.push(step);
}
Json ProofChecker::check_impl(const Json &proofs,
                              const std::vector<Goal> *goals,
                              const Json &bindings) {
  used_.clear();
  steps_ = Json::array();
  visited_ = 0;
  require(proofs.is_array(), "Proofs must be an array");
  require(!goals || proofs.size() == goals->size(),
          "Answer omits a query obligation");
  for (size_t i = 0; i < proofs.size(); ++i) {
    auto proof = proofs.at(i), args = proof["args"];
    require(proof["functor"].text() == "proof" && args.is_array() &&
                args.size() == 5,
            "Malformed answer proof");
    for (size_t j = 0; j < 4; ++j)
      require(args.at(j).is_string(), "Proof conclusion must be ground text");
    auto sign = args.at(0).text();
    require(sign == "pos" || sign == "neg", "Invalid proof polarity");
    Goal actual{args.at(1).text(), args.at(2).text(), args.at(3).text(),
                sign == "neg"};
    if (goals) {
      auto expected = goals->at(i);
      auto bind = [&](std::string &s) {
        if (!s.starts_with('?'))
          return;
        auto v = bindings[s];
        require(v.is_string() && !v.text().empty() &&
                    !v.text().starts_with('?'),
                "Missing or unground answer binding");
        s = v.text();
      };
      bind(expected.subject);
      bind(expected.object);
      require(equal(expected, actual),
              "Proof does not establish the query under its bindings");
    }
    witness(args.at(4), actual);
  }
  auto out = Json::object();
  out.set("valid", Json::boolean(true));
  out.set("claim_ids",
          list(std::vector<std::string>(used_.begin(), used_.end())));
  out.set("steps", steps_);
  return out;
}
Json ProofChecker::check(const Json &proofs, const std::vector<Goal> &goals,
                         const Json &bindings) {
  return check_impl(proofs, &goals, bindings);
}
Json ProofChecker::check(const Json &proofs) {
  return check_impl(proofs, nullptr, Json::object());
}
} // namespace unllm::native
