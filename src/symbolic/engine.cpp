#include "unllm/symbolic/engine.h"
#include "unllm/symbolic/behaviour_job.h"
#include "unllm/symbolic/calculator.h"
#include "unllm/symbolic/grounding.h"
#include "unllm/symbolic/linking.h"
#include "unllm/symbolic/proof.h"
#include "unllm/symbolic/stream_job.h"
#include <algorithm>
#include "algorithm_goal.h"
#include <cstdlib>
#include <ctime>
#include <set>
namespace unllm::native {
namespace {
Json message(const std::string &status, const std::string &text) {
  auto out = Json::object();
  out.set("status", Json::string(status));
  out.set("text", Json::string(text));
  return out;
}
Json array(const std::vector<std::string> &values) {
  auto out = Json::array();
  for (auto &v : values)
    out.push(Json::string(v));
  return out;
}
Json array(const std::vector<Goal> &values) {
  auto out = Json::array();
  for (auto &v : values)
    out.push(v.data());
  return out;
}
Json array(const std::vector<Claim> &values) {
  auto out = Json::array();
  for (auto &v : values)
    out.push(v.data());
  return out;
}
std::string join(const std::vector<std::string> &values,
                 const std::string &separator) {
  std::string out;
  for (auto &v : values) {
    if (!out.empty())
      out += separator;
    out += v;
  }
  return out;
}
void merge(Json destination, const Json &source) {
  for (auto &[k, v] : source.members())
    destination.set(k, v);
}
void append_unique(std::vector<std::string> &list, const std::string &item) {
  if (std::find(list.begin(), list.end(), item) == list.end())
    list.push_back(item);
}
std::string trim(std::string s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return "";
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}
std::vector<std::string> command_words(const std::string &text) {
  std::vector<std::string> result;
  std::string token;
  char quote = 0;
  bool escape = false, active = false;
  for (char c : text) {
    if (escape) {
      token += c;
      escape = false;
      active = true;
      continue;
    }
    if (c == '\\' && quote != '\'') {
      escape = true;
      active = true;
      continue;
    }
    if (quote) {
      if (c == quote)
        quote = 0;
      else
        token += c;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      active = true;
      continue;
    }
    if (c == ' ' || c == '\t') {
      if (active) {
        result.push_back(token);
        token.clear();
        active = false;
      }
    } else {
      token += c;
      active = true;
    }
  }
  if (quote || escape)
    throw Unsupported("Unterminated command quoting");
  if (active)
    result.push_back(token);
  return result;
}
} // namespace
Context &Engine::context(const std::string &name) {
  auto now = std::chrono::steady_clock::now();
  for (auto it = contexts_.begin(); it != contexts_.end();)
    if (now - it->second.touched >= std::chrono::minutes(30))
      it = contexts_.erase(it);
    else
      ++it;
  if (!contexts_.contains(name) && contexts_.size() >= 128) {
    auto it = std::min_element(
        contexts_.begin(), contexts_.end(),
        [](auto &a, auto &b) { return a.second.touched < b.second.touched; });
    contexts_.erase(it);
  }
  auto &value = contexts_[name];
  value.touched = now;
  return value;
}
Meaning Engine::interpret(const std::string &text, Context &ctx,
                          const std::string &id, Deadline deadline) {
  Resolver resolve = [&](const std::string &label, bool allow_new) {
    std::set<std::string> ids;
    for (auto row : store.resolve(label).items())
      ids.insert(row["id"].text());
    for (auto &[i, name] : ctx.labels)
      if (key(name) == key(label))
        ids.insert(i);
    if (ids.size() > 1)
      throw Ambiguous("The name identifies multiple entities: " + label);
    if (!ids.empty())
      return *ids.begin();
    if (allow_new)
      return "session:" +
             digest(array(std::vector<std::string>{id, key(label)}))
                 .substr(0, 24);
    throw Unsupported("I have no unique structured entity named " + label +
                      ".");
  };
  auto aliases = store.aliases();
  for (auto &[_, name] : ctx.labels)
    aliases.push_back(name);
  auto linked = link_entities(text, aliases);
  if (!linked.names.empty()) {
    Resolver linked_resolve = [&](const std::string &name, bool allow) {
      auto it = linked.names.find(key(name));
      return resolve(it == linked.names.end() ? name : it->second, allow);
    };
    try {
      auto result =
          parser.parse(linked.text, linked_resolve, ctx.focus, deadline);
      for (auto &[_, name] : result.labels) {
        auto it = linked.names.find(key(name));
        if (it != linked.names.end())
          name = it->second;
      }
      result.diagnostics.push_back(
          "registered entity names linked before grammar analysis");
      return result;
    } catch (const Unsupported &) {
      auto original = std::current_exception();
      try {
        return parser.parse(text, resolve, ctx.focus, deadline);
      } catch (const Unsupported &) {
        std::rethrow_exception(original);
      } catch (const Ambiguous &) {
        std::rethrow_exception(original);
      }
    }
  }
  return parser.parse(text, resolve, ctx.focus, deadline);
}
std::string Engine::label(const std::string &id, const Context &ctx) const {
  if (id == "true")
    return id;
  auto it = ctx.labels.find(id);
  if (it != ctx.labels.end())
    return it->second;
  auto matches = store.resolve(id);
  return matches.size() ? matches.at(0)["name"].text() : id;
}
std::string Engine::sentence(const Goal &g, const Context &ctx) const {
  std::string s = label(g.subject, ctx), o = label(g.object, ctx),
              no = g.negative ? "not " : "";
  if (g.predicate == "located_in")
    return s + " is " + no + "in " + o;
  if (g.predicate == "contains")
    return s + (g.negative ? " does not contain " : " contains ") + o;
  if (g.predicate == "depends_on")
    return s + (g.negative ? " does not depend on " : " depends on ") + o;
  if (g.predicate == "available" || g.predicate == "blocked")
    return s + " is " + no + g.predicate;
  if (g.predicate == "author")
    return o + " is " + no + "the author of " + s;
  if (g.predicate == "authored")
    return s + (g.negative ? " did not write " : " wrote ") + o;
  if (g.predicate == "birthplace")
    return s + " was " + no + "born in " + o;
  if (g.predicate == "taller_than")
    return s + " is " + no + "taller than " + o;
  if (g.predicate == "height")
    return s + "'s height is " + no + o + " metres";
  if (g.predicate == "type")
    return s + " is " + no + "a " + o;
  if (g.predicate == "subclass_of")
    return s + " is " + no + "a kind of " + o;
  auto p = g.predicate;
  std::replace(p.begin(), p.end(), '_', ' ');
  return s + ": " + p + (g.negative ? " is not " : " is ") + o;
}
void Engine::assert_goals(const Meaning &meaning, Context &ctx,
                          bool correction) {
  std::set<std::string> relations;
  for (auto r : store.relations().items())
    relations.insert(r["name"].text());
  std::vector<Claim> incoming;
  for (auto &g : meaning.goals) {
    if (!relations.contains(g.predicate))
      throw Unsupported("Undeclared relation: " + g.predicate);
    auto identity = Json::array();
    identity.push(g.data());
    identity.push(Json::number(std::min<size_t>(ctx.undo.size() + 1, 20)));
    auto data = g.data();
    data.set("id", Json::string("session-claim:" + digest(identity)));
    data.set("source", Json::string("conversation"));
    data.set("origin", Json::string("session"));
    incoming.push_back(Claim::from(data));
  }
  ctx.undo.push_back(ctx.facts);
  if (ctx.undo.size() > 20)
    ctx.undo.erase(ctx.undo.begin());
  if (correction)
    std::erase_if(ctx.facts, [&](auto &c) {
      return std::any_of(
          meaning.goals.begin(), meaning.goals.end(), [&](auto &g) {
            return g.predicate == c.predicate && g.subject == c.subject;
          });
    });
  for (auto &c : incoming) {
    auto it = std::find_if(ctx.facts.begin(), ctx.facts.end(),
                           [&](auto &x) { return x.id == c.id; });
    if (it == ctx.facts.end())
      ctx.facts.push_back(c);
    else
      *it = c;
  }
  for (auto &[id, name] : meaning.labels)
    ctx.labels[id] = name;
  std::set<std::string> focus;
  for (auto &g : meaning.goals)
    if (!g.subject.starts_with('?'))
      focus.insert(g.subject);
  ctx.focus.assign(focus.begin(), focus.end());
}
Json Engine::evaluate(const Meaning &meaning, Context &ctx, int64_t now,
                      const std::vector<Goal> &assumptions, Deadline deadline) {
  auto claims = store.claims(now);
  claims.insert(claims.end(), ctx.facts.begin(), ctx.facts.end());
  if (!assumptions.empty()) {
    std::erase_if(claims, [&](auto &c) {
      return std::any_of(assumptions.begin(), assumptions.end(), [&](auto &g) {
        return g.predicate == c.predicate && g.subject == c.subject &&
               g.object == c.object;
      });
    });
    for (auto &g : assumptions) {
      auto c = g.data();
      c.set("id", Json::string("assumption:" + digest(g.data())));
      c.set("source", Json::string("hypothesis"));
      c.set("origin", Json::string("hypothetical"));
      claims.push_back(Claim::from(c));
    }
  }
  auto facts = array(claims), relations = store.relations(),
       complete = store.completeness();
  for (auto c : ctx.complete.items())
    complete.push(c);
  auto result = native.query_with_prepared(
      facts, relations, complete, array(meaning.goals), deadline, 100,
      [&] { return store.prepared(native.input_key(facts, relations)); });
  auto checked = Json::array(), sources = Json::object();
  std::set<std::string> seen;
  ProofChecker checker(claims, relations, complete);
  std::map<std::string, Claim> by_id;
  for (auto &c : claims)
    by_id[c.id] = c;
  for (auto row : result["solutions"].items()) {
    auto signature = digest(row["bindings"]);
    if (seen.contains(signature))
      continue;
    auto proof = checker.check(row["proofs"], meaning.goals, row["bindings"]);
    row.set("derivation", proof);
    checked.push(row);
    seen.insert(signature);
    for (auto id : proof["claim_ids"].items()) {
      auto &claim = by_id.at(id.text());
      Json source;
      if (claim.origin == "source" || claim.origin == "metadata")
        source = store.source(claim.source);
      else {
        source = Json::object();
        source.set("id", Json::string(claim.source));
        source.set("title",
                   Json::string(claim.origin == "hypothetical"
                                    ? "Hypothetical assumption"
                                    : "User assertion in this conversation"));
      }
      auto entry = Json::object();
      entry.set("claim", claim.data());
      entry.set("source", source);
      sources.set(id.text(), entry);
    }
  }
  result.set("solutions", checked);
  result.set("completeness", complete);
  result.set("sources", sources);
  auto m = Json::object();
  m.set("act", Json::string(meaning.act));
  m.set("goals", array(meaning.goals));
  m.set("select", array(meaning.select));
  m.set("count", Json::boolean(meaning.count));
  m.set("diagnostics", array(meaning.diagnostics));
  result.set("meaning", m);
  return result;
}
Json Engine::answer(const Meaning &meaning, Context &ctx, int64_t now,
                    const std::vector<Goal> &assumptions, Deadline deadline) {
  auto result = evaluate(meaning, ctx, now, assumptions, deadline);
  if (result["status"].text() != "complete") {
    result.set("text", Json::string("Reasoning reached its resource budget; "
                                    "this is not a negative answer."));
    return result;
  }
  bool positive = result["solutions"].size() > 0;
  if (!meaning.select.empty()) {
    auto answers = Json::array();
    std::set<std::string> seen;
    std::vector<std::string> displays, focus;
    for (auto row : result["solutions"].items()) {
      std::vector<std::string> values;
      bool missing = false;
      for (auto &v : meaning.select) {
        auto value =
            row["bindings"].contains(v) ? row["bindings"][v] : Json::string(v);
        if (value.null()) {
          missing = true;
          break;
        }
        values.push_back(value.text());
      }
      if (missing)
        continue;
      auto items = array(values);
      if (!seen.insert(canonical(items)).second)
        continue;
      answers.push(items);
      std::vector<std::string> names;
      for (auto &v : values) {
        names.push_back(label(v, ctx));
        append_unique(focus, v);
      }
      displays.push_back(join(names, ", "));
    }
    result.set("answers", answers);
    if (answers.size() == 0) {
      result.set("status", Json::string("unknown"));
      result.set("truth", Json::string("unknown"));
      result.set("text",
                 Json::string(
                     "I have no supported match in the structured knowledge."));
    } else {
      result.set("truth", Json::string("supported"));
      result.set("complete_set", Json::boolean(false));
      if (meaning.count) {
        result.set("known_count", Json::number(answers.size()));
        result.set("text",
                   Json::string("I know " + std::to_string(answers.size()) +
                                " matching results. The knowledge does not "
                                "establish an exhaustive count."));
      } else
        result.set("text", Json::string(join(displays, "; ") + "."));
      ctx.focus = focus;
    }
  } else {
    bool negative = false;
    if (meaning.goals.size() == 1) {
      Meaning reverse;
      reverse.goals = meaning.goals;
      reverse.goals[0].negative = !reverse.goals[0].negative;
      auto opposite = evaluate(reverse, ctx, now, assumptions, deadline);
      if (opposite["status"].text() != "complete") {
        opposite.set(
            "text",
            Json::string("The opposite-evidence search exceeded its budget."));
        return opposite;
      }
      negative = opposite["solutions"].size() > 0;
      result.set("opposite_solutions", opposite["solutions"]);
      merge(result["sources"], opposite["sources"]);
    }
    std::string truth = positive ? (negative ? "both" : "supported")
                                 : (negative ? "refuted" : "unknown");
    result.set("truth", Json::string(truth));
    if (truth == "both")
      result.set("text", Json::string("The evidence conflicts: I can support "
                                      "both the statement and its negation."));
    else if (truth == "supported") {
      std::vector<std::string> statements;
      for (auto &g : meaning.goals)
        statements.push_back(sentence(g, ctx));
      result.set("text", Json::string("Yes. " + join(statements, "; ") + "."));
    } else if (truth == "refuted")
      result.set(
          "text",
          Json::string(
              "No. There is explicit evidence for the opposite statement."));
    else {
      result.set("status", Json::string("unknown"));
      result.set(
          "text",
          Json::string("I cannot establish this from the available premises."));
      if (meaning.goals.size() == 1 &&
          meaning.goals[0].predicate == "blocked") {
        Meaning dependency;
        dependency.goals = {
            {"depends_on", meaning.goals[0].subject, "?dependency"}};
        dependency.select = {"?dependency"};
        auto dependencies =
            evaluate(dependency, ctx, now, assumptions, deadline);
        std::vector<std::string> unknown;
        for (auto row : dependencies["solutions"].items()) {
          auto dep = row["bindings"]["?dependency"].text();
          Meaning available;
          available.goals = {{"available", dep, "true"}};
          auto a = evaluate(available, ctx, now, assumptions, deadline);
          available.goals[0].negative = true;
          auto b = evaluate(available, ctx, now, assumptions, deadline);
          if (a["status"].text() == "complete" &&
              b["status"].text() == "complete" && a["solutions"].size() == 0 &&
              b["solutions"].size() == 0)
            unknown.push_back(dep);
        }
        if (!unknown.empty()) {
          std::vector<Goal> missing;
          std::vector<std::string> names;
          for (auto &d : unknown) {
            missing.push_back({"available", d, "true"});
            names.push_back(label(d, ctx));
          }
          result.set("missing_premises", array(missing));
          result.set("text", Json::string(result["text"].text() +
                                          " Availability is unknown for: " +
                                          join(names, ", ") + "."));
        }
      }
    }
    ctx.focus.clear();
    for (auto &g : meaning.goals)
      if (!g.subject.starts_with('?'))
        append_unique(ctx.focus, g.subject);
  }
  if (meaning.explain && result["sources"].size()) {
    std::vector<std::string> premises;
    for (auto &[_, entry] : result["sources"].members()) {
      auto c = Claim::from(entry["claim"]);
      append_unique(
          premises,
          sentence(Goal{c.predicate, c.subject, c.object, c.negative}, ctx));
    }
    result.set("text", Json::string(result["text"].text() + " Premises: " +
                                    join(premises, "; ") + "."));
  }
  if (!assumptions.empty()) {
    result.set("hypothetical", Json::boolean(true));
    result.set("assumptions", array(assumptions));
    result.set("text",
               Json::string("Under that assumption: " + result["text"].text()));
  }
  return result;
}
Json Engine::generate(Json request, Context &ctx, const std::string &question) {
  if (synthesis_started)
    synthesis_started();
  auto deadline = after_ms(180000);
  Json spec = request["spec"];
  Json parser_diagnostics, goal_candidates;
  auto language = request["language"].text();
  if (request.contains("language") && language.empty())
    throw Unsupported("Provide a target language");
  if (question.starts_with(":code ") || question.starts_with(":revise ")) {
    if (ctx.last_spec.null())
      return message("unknown", "Generate a program first.");
    spec = ctx.last_spec.clone();
    if (question.starts_with(":code "))
      language = trim(question.substr(6));
    else {
      auto changes = Json::parse(question.substr(8));
      if (!changes.is_object())
        throw Unsupported(
            "A revision must be an object of specification fields");
      merge(spec, changes);
      language = ctx.last_language;
    }
  }
  if (!request.contains("spec") && spec.null()) {
    // An implicit creation intent can arrive from the factual parser. Ground
    // its original words with the same isolated code budget as an explicit request.
    auto m = parser.parse(
        question,
        [](const std::string &, bool) -> std::string {
          throw Unsupported("Unbound factual entity in programming request");
        },
        {}, deadline, true);
    goal_candidates = interval_candidates(m, question);
    if (goal_candidates.size() > 1) {
      auto reply = message("ambiguous",
                           "Specify which interval or operation you intend.");
      reply.set("goal_candidates", goal_candidates);
      return reply;
    }
    spec = goal_candidates.size() ? goal_candidates.at(0)["goal"]
                                  : ground_program(m, resources_);
    parser_diagnostics = array(m.diagnostics);
    if (language.empty())
      for (auto e : m.semantics.items()) {
        auto w = key(e["carg"].text()), l = lemma(e["predicate"].text());
        if (w == "cpp" || w == "c++" || l == "cpp" || l == "c++")
          language = "cpp";
      }
  }
  if (!spec.is_object())
    throw Unsupported("Expected a typed program specification");
  if (language.empty())
    language = "python";
  language = key(language);
  if (language == "c++")
    language = "cpp";
  if (language == "py")
    language = "python";
  if (language != "cpp" && language != "python")
    throw Unsupported("Code generation supports Python and C++");
  auto artifacts = resources_ / ".state/cpp-migration/generated";
  if (auto value = std::getenv("UNLLM_ARTIFACTS"); value && *value)
    artifacts = value;
  std::string python;
  if (auto value = std::getenv("UNLLM_PYTHON_TARGET"))
    python = value;
  if (request.contains("emit_only") && !request["emit_only"].is_boolean())
    throw Unsupported("emit_only must be Boolean");
  bool emit =
      request["emit_only"].flag() || (language == "python" && python.empty());
  if (spec.contains("status")) {
    auto status = required_text(spec, "status");
    if (status == "clarify") {
      fields_only(spec, {"status", "question"});
      return message("clarify", required_text(spec, "question"));
    }
    if (status == "unsupported") {
      fields_only(spec, {"status"});
      return message("unsupported", "Request exceeds the supplied requirement format");
    }
    if (status != "ok")
      throw Unsupported("Unknown interpretation status");
    spec = reduction_specification(spec);
  }
  Json result;
  if (!spec.contains("program") && spec["input"].text() == "ints" &&
      spec.contains("output") && spec.contains("examples"))
    result = build_example_function(spec, language, artifacts, python, emit);
  else if (spec["program"].text() == "function_contract")
    result = build_function_contract(spec, language, artifacts, python, emit);
  else if (spec["program"].text() == "algorithm_goal")
    result = build_algorithm_goal(spec, language, artifacts, python, emit);
  else if (spec.contains("state") || spec.contains("views") ||
           spec.contains("actions") || spec["schema"].integer() == 2 ||
           (spec.contains("program") && spec["program"].text() != "calculator"))
    result = build_behaviour(BehaviourSpec::from(spec), language, resources_,
                             artifacts, native, deadline, python, emit);
  else if (spec.contains("program"))
    result = build_calculator(CalculatorSpec::from(spec), language, resources_,
                              artifacts, native, deadline, python, emit);
  else
    result = build_stream(Program::from(spec), language, resources_, artifacts,
                          native, deadline, python, emit);
  if (goal_candidates.size())
    result.set("goal_candidates", goal_candidates);
  if (!parser_diagnostics.null())
    result.set("parser_diagnostics", parser_diagnostics);
  if (result["status"].text() == "validated" ||
      result["status"].text() == "emitted") {
    ctx.last_spec = result["specification"].clone();
    ctx.last_language = language;
  }
  return result;
}
Json Engine::ask(const Json &request, Deadline deadline) {
  if (!request.is_object())
    throw Unsupported("Request must be an object");
  parser.last_ms = 0;
  auto effective = request.clone();
  auto text = trim(request["question"].text());
  if (text.starts_with('{')) {
    auto structured = Json::parse(text);
    if (!structured.is_object())
      throw Unsupported("Structured request must be an object");
    if (structured.contains("input") || structured.contains("program") ||
        structured.contains("state") || structured.contains("status"))
      effective.set("spec", structured);
    else {
      for (auto field : {"context", "now", "language"})
        if (request.contains(field) && !structured.contains(field))
          structured.set(field, request[field]);
      effective = structured;
      text = trim(effective["question"].text());
    }
  }
  if (effective.contains("spec") || text.starts_with(":code ") ||
      text.starts_with(":revise ") || creation_request(text) ||
      (effective.contains("language") && !text.starts_with(':') &&
       !effective.contains("goals"))) {
    auto &ctx = context(effective.contains("context")
                            ? required_text(effective, "context")
                            : "default");
    return generate(effective, ctx, text);
  }
  store.begin_read();
  try {
    auto result = ask_snapshot(effective, deadline);
    store.end_read();
    return result;
  } catch (...) {
    store.end_read();
    throw;
  }
}
Json Engine::ask_snapshot(Json request, Deadline deadline) {
  parser.last_ms = 0;
  int64_t now = request["now"].null() || request["now"].integer() == 0
                    ? std::time(nullptr)
                    : request["now"].integer();
  auto id = request.contains("context") ? required_text(request, "context")
                                        : "default";
  auto &ctx = context(id);
  auto text = trim(request["question"].text());
  if (text.starts_with(":complete ")) {
    auto parts = command_words(text);
    if (parts.size() != 3)
      throw Unsupported("Use :complete RELATION \"entity name\".");
    bool declared = false;
    for (auto r : store.relations().items())
      declared |= r["name"].text() == parts[1];
    if (!declared)
      throw Unsupported("Unknown relation.");
    std::set<std::string> ids;
    for (auto r : store.resolve(parts[2]).items())
      ids.insert(r["id"].text());
    for (auto &[i, name] : ctx.labels)
      if (key(name) == key(parts[2]))
        ids.insert(i);
    if (ids.size() != 1)
      throw Ambiguous("The completeness declaration needs one known entity.");
    auto item = Json::object();
    item.set("predicate", Json::string(parts[1]));
    item.set("subject", Json::string(*ids.begin()));
    item.set("source", Json::string("conversation"));
    bool exists = false;
    for (auto c : ctx.complete.items())
      exists |= canonical(c) == canonical(item);
    if (!exists)
      ctx.complete.push(item);
    auto out = message(
        "complete", "Recorded a completeness boundary for this conversation.");
    out.set("complete", ctx.complete.clone());
    return out;
  }
  Meaning meaning;
  if (request["act"].text() == "query") {
    if (!request["goals"].is_array() || request["goals"].size() > 64)
      throw Unsupported("Query requires an array of at most 64 goals");
    for (auto g : request["goals"].items())
      meaning.goals.push_back(Goal::from(g));
    if (request.contains("select") && !request["select"].is_array())
      throw Unsupported("Select must be an array");
    for (auto s : request["select"].items()) {
      if (!s.is_string())
        throw Unsupported("Selected terms must be strings");
      meaning.select.push_back(s.text());
    }
    if (request.contains("explain") && !request["explain"].is_boolean())
      throw Unsupported("Explain must be Boolean");
    meaning.explain = request["explain"].flag();
  } else {
    if (text == ":reset") {
      ctx = Context{};
      return message("complete", "Conversation facts and assumptions cleared.");
    }
    if (text == ":undo") {
      if (ctx.undo.empty())
        return message("unknown", "There is no assertion to undo.");
      ctx.facts = ctx.undo.back();
      ctx.undo.pop_back();
      return message("complete", "The previous assertion was undone.");
    }
    if (text == ":why" || text == ":evidence" || text == ":missing") {
      if (!ctx.previous)
        return message("unknown", "Ask a factual question first.");
      meaning = *ctx.previous;
      meaning.explain = true;
    } else {
      bool correction = false;
      auto folded = key(text);
      for (auto marker : {":correct ", "no, ", "actually, "})
        if (folded.starts_with(marker)) {
          text = text.substr(std::string(marker).size());
          correction = true;
          break;
        }
      meaning = interpret(text, ctx, id, deadline);
      if (meaning.act == "synthesis") {
        store.end_read();
        return generate(request, ctx, text);
      }
      if (meaning.act == "explain") {
        if (!ctx.previous)
          return message("unknown", "Ask a factual question first.");
        meaning = *ctx.previous;
        meaning.explain = true;
      }
      if (meaning.act == "assert") {
        assert_goals(meaning, ctx, correction);
        std::vector<std::string> sentences;
        for (auto &g : meaning.goals)
          sentences.push_back(sentence(g, ctx));
        auto out = message(
            "complete",
            std::string(correction ? "Corrected: "
                                   : "Recorded for this conversation: ") +
                join(sentences, "; ") + ".");
        out.set("truth", Json::string("asserted"));
        out.set("claims", array(ctx.facts));
        return out;
      }
    }
  }
  if (meaning.act == "hypothetical") {
    Meaning goals;
    if (meaning.goals.empty()) {
      if (!ctx.previous)
        return message("unknown",
                       "Ask a question before changing its assumptions.");
      goals = *ctx.previous;
    } else {
      goals.goals = meaning.goals;
      goals.select = meaning.select;
      goals.explain = meaning.explain;
    }
    auto temporary = ctx;
    return answer(goals, temporary, now, meaning.assumptions, deadline);
  }
  ctx.previous = meaning;
  return answer(meaning, ctx, now, {}, deadline);
}
} // namespace unllm::native
