#include "unllm/symbolic/behaviour_plan.h"
#include "provider_plan.h"
#include <algorithm>
#include <functional>
namespace unllm::native {
namespace {
Names names(const Json &a) {
  if (!a.is_array())
    throw Unsupported("Expected a list of names");
  Names out;
  for (auto x : a.items()) {
    if (!x.is_string())
      throw Unsupported("Expected a name");
    out.push_back(x.text());
  }
  return out;
}
template <class T> Json strings(const T &names) {
  auto a = Json::array();
  for (const auto &name : names)
    a.push(Json::string(name));
  return a;
}
void collect(const BehaviourValue &v, std::set<std::string> &out) {
  if (v.kind == "primitive")
    out.insert(v.name);
  for (const auto &a : v.args)
    collect(a, out);
}
} // namespace
Json plan_behaviour(const BehaviourSpec &spec, const std::string &language,
                    const std::filesystem::path &root, NativeEngine &engine,
                    Deadline deadline, const std::set<std::string> &disabled) {
  if (language != "cpp" && language != "python")
    throw Unsupported("Unknown behaviour target");
  auto data = spec.data(), fields = data["state"], actions = data["actions"],
       views = data["views"];
  auto catalog = Json::parse(read_file(root / "data/behaviour_operators.json"));
  if (catalog["schema"].integer() != 1)
    throw Unsupported("Unknown operator schema");
  ProviderCatalog providers;
  std::map<std::string, std::tuple<std::string, std::string, std::string>>
      instructions;
  auto add = [&](std::string cap, Names needs = {}, std::string target = "any",
                 Names accepts = {}, Names produces = {}) -> Provider & {
    auto [it, inserted] = providers.try_emplace(
        "behaviour:" + cap, Provider{cap, target, needs, accepts, produces});
    if (!inserted &&
        (it->second.needs != needs || it->second.target != target ||
         it->second.accepts != accepts || it->second.produces != produces))
      throw Unsupported("Conflicting provider: " + cap);
    return it->second;
  };
  add("state", {}, "any", {}, {"state"});
  add("dispatch", {}, "any", {"event"}, {"action"});
  for (std::string lang : {"python", "cpp"})
    for (auto widget : {"window", "text_edit", "line_edit", "number_input",
                        "record_list", "label", "button"})
      add(std::string("ui.") + widget + "." + lang, {"state", "dispatch"}, lang,
          {"state"}, {"event"});
  for (auto cap : {"solver.transition", "assign", "clock.monotonic"})
    add(cap, {"state"});
  add("event.periodic", {"clock.monotonic", "dispatch"});
  std::set<std::string> steps;
  for (auto &[_, graph] : catalog["graphs"].members())
    for (auto &[condition, op] : graph.members())
      if (!op.null())
        steps.insert(op.at(0).text());
  auto dependencies = catalog["dependencies"];
  for (auto &[_, deps] : dependencies.members())
    for (auto &dep : names(deps))
      steps.insert(dep);
  for (auto &step : steps)
    add(step, dependencies.contains(step) ? names(dependencies[step])
                                          : Names{"state"});
  std::set<std::string> primitives;
  auto source = [&](const Json &s) {
    collect(behaviour_expression(s, fields).value, primitives);
  };
  Names needed{"ui.window." + language};
  for (auto view : views.items()) {
    needed.push_back("ui." + view["kind"].text() + "." + language);
    if (view.contains("value"))
      source(view["value"]);
  }
  if (actions.size())
    needed.push_back("ui.button." + language);
  std::map<std::string, std::pair<Json, std::string>> contracts;
  for (auto a : actions.items()) {
    auto id = a["id"].text();
    if (a.contains("requires"))
      source(a["requires"]);
    for (auto k : {"assign", "values"})
      for (auto &[_, s] : a[k].members())
        source(s);
    if (!a.contains("effect")) {
      needed.push_back(a.contains("ensure") ? "solver.transition" : "assign");
      continue;
    }
    auto effect = a["effect"].text(), tag = "action." + id + ".";
    auto def = catalog["effects"][effect],
         graph = catalog["graphs"][def["graph"].text()];
    if (!def.is_object() || !graph.is_object())
      throw Unsupported("Missing effect contract: " + effect);
    auto goal = required_text(def, "goal");
    contracts[id] = {graph, goal};
    add("effect." + effect, {"state"});
    for (auto &[condition, op] : graph.members()) {
      if (op.null()) {
        add(tag + condition, {"state"});
        continue;
      }
      auto execute = op.at(0).text();
      Names needs;
      for (auto &n : names(op.at(1)))
        needs.push_back(tag + n);
      needs.push_back(execute);
      add(tag + condition, needs);
      instructions.emplace(tag + condition,
                           std::make_tuple(id, condition, execute));
    }
    add(tag + "goal", {tag + goal, "effect." + effect});
    needed.push_back(tag + "goal");
  }
  if (data["events"].size())
    needed.push_back("event.periodic");
  for (auto &p : primitives) {
    add("primitive." + p, {"state"});
    needed.push_back("primitive." + p);
  }
  std::set<std::string> seen;
  std::erase_if(needed, [&](const auto &s) { return !seen.insert(s).second; });
  auto window = "ui.window." + language;
  auto out = checked_provider_plan(
      providers, needed, {{"state", window}, {window, "dispatch"}}, language,
      disabled, "behaviour-contract:", "primitive-behaviour-contracts-v2",
      engine, deadline);
  auto plans = Json::object();
  std::map<std::string, std::set<std::string>> achieved;
  std::map<std::string, Names> execution;
  for (auto &[id, _] : contracts)
    achieved[id].insert("state");
  for (auto cap : out["order"].items()) {
    auto step = instructions.find(cap.text());
    if (step == instructions.end())
      continue;
    auto &[action, condition, execute] = step->second;
    auto op = contracts.at(action).first[condition];
    for (auto &pre : names(op.at(1)))
      if (!achieved.at(action).contains(pre))
        throw Unsupported("Unsatisfied effect prerequisite");
    achieved.at(action).insert(condition);
    execution[action].push_back(execute);
  }
  for (auto &[id, def] : contracts) {
    if (!achieved.at(id).contains(def.second))
      throw Unsupported("Requested effect was not achieved");
    auto p = Json::object();
    p.set("steps", strings(execution[id]));
    p.set("goal", Json::string(def.second));
    p.set("achieved", strings(achieved.at(id)));
    plans.set(id, p);
  }
  out.set("application_name_used", Json::boolean(false));
  out.set("action_plans", plans);
  return out;
}
BehaviourGraph construct_behaviour(const BehaviourSpec &spec, const Json &plan,
                                   Deadline deadline) {
  BehaviourGraph graph;
  auto fields = spec.state(), providers = plan["providers"];
  for (auto a : spec.actions().items()) {
    BehaviourAction action;
    action.id = a["id"].text();
    action.steps = Json::array();
    if (a.contains("ensure")) {
      if (!providers.contains("solver.transition"))
        throw Unsupported("Missing transition provider");
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now())
                    .count();
      if (ms <= 0)
        throw ResourceLimit("Construction deadline exceeded");
      auto solved =
          synthesize_transition(spec, action.id, std::min<int64_t>(3000, ms));
      action.updates = std::move(solved.updates);
      graph.reports.push(solved.evidence);
    } else if (a.contains("assign")) {
      if (!providers.contains("assign"))
        throw Unsupported("Missing assignment provider");
      for (auto &[target, s] : a["assign"].members())
        action.updates[target] = behaviour_expression(s, fields).value;
    } else {
      action.target = a["state"].text();
      action.parameters = a.clone();
      for (auto k : {"id", "label", "visible", "shortcut"})
        action.parameters.erase(k);
      auto effect = a["effect"].text();
      if (effect == "load" || effect == "save" || effect == "save_as" ||
          effect == "new")
        graph.persistent.insert(action.target);
      action.steps = plan["action_plans"][action.id]["steps"];
      if (!action.steps.is_array() || !action.steps.size())
        throw Unsupported("Missing effect plan");
      for (auto step : action.steps.items())
        if (!providers.contains(step.text()))
          throw Unsupported("Unplanned effect instruction");
    }
    if (a.contains("requires"))
      action.guard = behaviour_expression(a["requires"], fields).value;
    graph.actions.push_back(std::move(action));
  }
  for (auto view : spec.views().items())
    if (view["kind"].text() == "label")
      graph.labels[view["id"].text()] =
          behaviour_expression(view["value"], fields);
  return graph;
}
} // namespace unllm::native
