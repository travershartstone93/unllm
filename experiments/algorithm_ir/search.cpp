#include "search.h"
#include "repair.h"
#include <chrono>
#include <set>
namespace unllm::algorithm {
namespace {
using Clock = std::chrono::steady_clock;
Json word(const std::string &s) { return Json::string(s); }
Json array(std::initializer_list<Json> values) {
  auto a = Json::array();
  for (auto &v : values)
    a.push(v);
  return a;
}
Json node(const std::string &op, std::initializer_list<Json> children = {}) {
  auto a = array({word(op)});
  for (auto &c : children)
    a.push(c);
  return a;
}
Json var(const std::string &s) { return node("var", {word(s)}); }
Json number(int n) { return node("int", {word(std::to_string(n))}); }
Json assign(const std::string &s, const Json &e) {
  return node("set", {word(s), e});
}
Json guarded(Json guard, Json body) {
  return guard.null() ? body
                      : array({node("if", {guard, body, Json::array()})});
}
bool refers_to_state(const Json &e) {
  if (!e.is_array())
    return false;
  if (e.size() == 2 && e.at(0).text() == "var")
    return e.at(1).text() == "state";
  for (auto child : e.items())
    if (refers_to_state(child))
      return true;
  return false;
}
Json result(const std::string &status, const std::string &message) {
  auto r = Json::object();
  r.set("status", word(status));
  r.set("reason", word(message));
  return r;
}
struct Example {
  Value input, output;
};
struct Search {
  Type type;
  std::vector<Example> examples;
  Clock::time_point start = Clock::now();
  size_t candidates = 0;
  std::vector<size_t> active;
  std::set<std::string> tested;
  Json refinements = Json::array(), winner;
  bool limited = false;
  bool test(const Json &ir) {
    if (++candidates > 100000 ||
        Clock::now() - start >= std::chrono::seconds(120)) {
      limited = true;
      return true;
    }
    if (!tested.insert(native::canonical(ir)).second)
      return false;
    Program p;
    try {
      p = decode(ir);
    } catch (const native::Unsupported &) {
      return false;
    }

    auto fits = [&](size_t i) {
      try {
        return interpret(p, {examples[i].input}) == examples[i].output;
      } catch (const native::Unsupported &) {
        return false;
      }
    };
    for (auto i : active)
      if (!fits(i))
        return false;
    for (size_t i = 0; i < examples.size(); ++i) {
      if (!fits(i)) {
        active.push_back(i);
        auto ref = Json::object();
        ref.set("candidate", Json::number(candidates));
        ref.set("failed_example", Json::number(i));
        ref.set("rejected_ir", ir);
        refinements.push(ref);
        return false;
      }
    }
    winner = ir;
    return true;
  }
  Json program(Json locals, Json body, Json output) {
    auto f = Json::object();
    f.set("name", word("compute"));
    f.set("params", array({array({word("values"), word("ints")})}));
    f.set("locals", locals);
    f.set("result", word(type == Type::integer ? "int" : "ints"));
    f.set("body", body);
    f.set("return", output);
    auto p = Json::object();
    p.set("version", Json::number(1));
    p.set("functions", array({f}));
    return p;
  }
  bool try_fragments(const std::vector<Json> &fragments) {
    if (fragments.size() > 32)
      throw native::Unsupported("32 fragment shortlist limit");
    auto x = var("element"), state = var("state"), out = var("out");
    auto local_state = array({word("state"), word("int"), number(0)});
    auto local_out = array({word("out"), word("ints"), node("empty")});
    auto append = [&](Json value) {
      return assign("out", node("append", {out, value}));
    };
    auto loop = [&](Json body) {
      return array({node("each", {word("element"), var("values"), body})});
    };
    for (auto &expr : fragments) {
      auto update = assign("state", node("add", {state, expr}));
      for (Json guard :
           {Json{}, node("lt", {number(0), x}), node("lt", {x, number(0)})}) {
        auto step = guarded(guard, array({update}));
        if (type == Type::integer) {
          if (test(program(array({local_state}), loop(step), state)))
            return true;
          continue;
        }
        if (test(program(array({local_out}),
                         loop(guarded(guard, array({append(expr)}))), out)))
          return true;
        auto exclusive = array({append(state)});
        for (auto item : step.items())
          exclusive.push(item);
        step.push(append(state));
        for (auto body : {step, exclusive})
          if (test(program(array({local_state, local_out}), loop(body), out)))
            return true;
      }
    }
    return false;
  }
  bool try_transitions(const std::vector<std::string> &operations) {
    auto x = var("element"), state = var("state"), zero = number(0);
    auto locals = array({array({word("state"), word("int"), zero})});
    bool sequence = type == Type::sequence;
    if (sequence)
      locals.push(array({word("out"), word("ints"), node("empty")}));
    auto emit = [&](Json slots, Json body, Json output) {
      auto loop = node("each", {word("element"), var("values"), body});
      return test(program(slots, array({loop}), output));
    };
    auto finish = [&](Json body) {
      if (sequence)
        body.push(assign("out", node("append", {var("out"), state})));
      return emit(locals, body, sequence ? var("out") : state);
    };
    auto bound = [&](const std::string &op, Json value) {
      auto condition =
          op == "max" ? node("lt", {state, value}) : node("lt", {value, state});
      return node("if",
                  {condition, array({assign("state", value)}), Json::array()});
    };
    for (auto &op : operations) {
      if (op == "min" || op == "max") {
        if (finish(array({bound(op, x)})))
          return true;
        continue;
      }
      if (op != "add" && op != "sub")
        throw native::Unsupported("unknown transition operation");
      auto next = node(op, {state, x});
      auto update = assign("state", next);
      if (finish(array({update})))
        return true;
      for (auto &limit : operations) {
        if ((limit == "min" || limit == "max") &&
            finish(array({update, bound(limit, zero)})))
          return true;
      }
      if (sequence)
        continue;
      auto slots = locals;
      slots.push(array({word("count"), word("int"), zero}));
      auto increment = assign("count", node("add", {var("count"), number(1)}));
      for (bool positive : {false, true}) {
        auto predicate = [&](Json v) {
          return positive ? node("lt", {zero, v}) : node("lt", {v, zero});
        };
        for (bool entering : {true, false}) {
          Json empty = Json::array(), hit = array({increment});
          auto after = node("if", {predicate(next), entering ? hit : empty,
                                   entering ? empty : hit});
          auto event = array({after});
          auto before = node("if", {predicate(state), entering ? empty : event,
                                    entering ? event : empty});
          if (emit(slots, array({before, update}), var("count")))
            return true;
        }
      }
    }
    return false;
  }
  void enumerate() {
    auto xs = var("values"), x = var("element"), s = var("state"),
         out = var("out"), empty = node("empty");
    bool sequence = type == Type::sequence;
    if (sequence) {
      if (test(program(array({}), array({}), empty)) ||
          test(program(array({}), array({}), xs)))
        return;
    } else
      for (auto e : {number(0), number(1), node("len", {xs})})
        if (test(program(array({}), array({}), e)))
          return;
    std::vector<Json> atoms = {x, s, number(0), number(1)};
    std::vector<Json> guards{Json()}, expressions = atoms;
    for (size_t i = 0; i < atoms.size(); ++i)
      for (size_t j = 0; j < atoms.size(); ++j) {
        if (i >= 2 && j >= 2)
          continue;
        if (i != j)
          guards.push_back(node("lt", {atoms[i], atoms[j]}));
        if (i < j)
          guards.push_back(node("eq", {atoms[i], atoms[j]}));
        for (std::string op : {"add", "mul", "sub"}) {
          if (op != "sub" && j < i)
            continue;
          expressions.push_back(node(op, {atoms[i], atoms[j]}));
        }
      }
    auto local_out = array({word("out"), word("ints"), empty});
    auto loop = [&](Json body) {
      return array({node("each", {word("element"), xs, body})});
    };
    auto append = [&](Json e) {
      return assign("out", node("append", {out, e}));
    };
    if (sequence)
      for (auto g : guards)
        for (auto e : expressions) {
          if (refers_to_state(g) || refers_to_state(e))
            continue;
          if (test(program(array({local_out}),
                           loop(guarded(g, array({append(e)}))), out)))
            return;
        }
    for (auto g : guards)
      for (auto e : expressions)
        for (int initial : {0, 1}) {
          auto locals =
              array({array({word("state"), word("int"), number(initial)})});
          auto update = assign("state", e);
          if (!sequence) {
            if (test(program(locals, loop(guarded(g, array({update}))), s)))
              return;
            continue;
          }
          locals.push(local_out);
          for (bool before : {false, true}) {
            auto body = guarded(g, array({update}));
            if (before) {
              auto temp = array({append(s)});
              for (auto statement : body.items())
                temp.push(statement);
              body = temp;
            } else
              body.push(append(s));
            if (test(program(locals, loop(body), out)))
              return;
          }
        }
  }
};
} // namespace
static Json search_contract(const Json &contract, const Json &previous,
                            const FragmentSource &source,
                            const std::vector<std::string> &operations) {
  try {
    native::fields_only(contract, {"input", "output", "examples"});
    if (!contract.contains("input") || !contract.contains("output"))
      return result("incomplete", "input and output types required");
    auto out = contract["output"].text();
    if (contract["input"].text() != "ints" || (out != "int" && out != "ints"))
      return result("unsupported", "requires ints -> int or ints -> ints");
    auto examples = contract["examples"];
    if (!examples.is_array() || !examples.size())
      return result("incomplete",
                    "supply input-output examples including empty input");
    if (examples.size() > 128)
      return result("unsupported", "128 example limit");
    Search search{};
    search.type = out == "ints" ? Type::sequence : Type::integer;
    std::map<Sequence, Value> observed;
    bool empty = false;
    for (auto row : examples.items()) {
      native::fields_only(row, {"input", "output"});
      if (!row.contains("input") || !row.contains("output"))
        return result("incomplete", "example is missing a role");
      auto in = argument(row["input"], Type::sequence),
           expected = argument(row["output"], search.type);
      const auto &values = std::get<Sequence>(in);
      empty |= values.empty();
      auto [it, added] = observed.emplace(values, expected);
      if (!added && it->second != expected)
        return result("inconsistent", "same input has conflicting outputs");
      search.examples.push_back({in, expected});
    }
    if (!empty)
      return result("incomplete", "specify the empty-input behavior");
    std::string method = "bounded_enumeration", site;
    if (!previous.null()) {
      auto prior = decode(previous);
      const auto &entry = prior.functions.back();
      if (entry.parameters != 1 || entry.slots[0] != Type::sequence ||
          entry.result != search.type)
        return result("unsupported",
                      "prior program signature differs from contract");
      if (search.test(previous))
        method = "retained_revision";
      else if (visit_edits(previous, [&](const Json &candidate,
                                         const std::string &where) {
                 if (!search.test(candidate))
                   return false;
                 site = where;
                 return true;
               }))
        method = "local_edit";
    }
    if (source && search.winner.null() && !search.limited &&
        search.try_fragments(source(contract)))
      method = "fragment_search";
    if (search.winner.null() && !search.limited &&
        search.try_transitions(operations))
      method = "state_composition";
    if (search.winner.null() && !search.limited)
      search.enumerate();
    auto r =
        search.winner.null()
            ? result("exhausted", search.limited ? "search budget reached"
                                                 : "bounded grammar exhausted")
            : result("candidate", "matched supplied examples");
    r.set("method", word(method));
    if (!site.empty())
      r.set("edit_path", word(site));
    r.set("unique_candidates", Json::number(search.tested.size()));
    r.set("candidates", Json::number(search.candidates));
    r.set("counterexamples", search.refinements);
    r.set("search_ms", Json::real(std::chrono::duration<double, std::milli>(
                                      Clock::now() - search.start)
                                      .count()));
    if (!search.winner.null()) {
      r.set("ir", search.winner);
      r.set("ir_sha256", word(native::digest(search.winner)));
    }
    return r;
  } catch (const std::exception &e) {
    return result("unsupported", e.what());
  }
}
Json synthesize(const Json &contract, const Json &previous) {
  return search_contract(contract, previous, {}, {});
}
Json synthesize_with_fragments(const Json &contract,
                               const FragmentSource &source,
                               const std::vector<std::string> &operations) {
  return search_contract(contract, {}, source, operations);
}
} // namespace unllm::algorithm
