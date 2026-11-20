#include "provider_plan.h"
#include "unllm/symbolic/proof.h"
#include <functional>
namespace unllm::native {
namespace {
template <class T> Json strings(const T &names) {
  auto a = Json::array();
  for (const auto &name : names)
    a.push(Json::string(name));
  return a;
}
} // namespace
Json checked_provider_plan(
    const ProviderCatalog &providers, const Names &needed,
    const std::vector<std::pair<std::string, std::string>> &ports,
    const std::string &language, const std::set<std::string> &disabled,
    const std::string &prefix, const std::string &source, NativeEngine &engine,
    Deadline deadline) {
  auto facts = Json::array(), relations = Json::array(), empty = Json::array();
  std::vector<Claim> claims;
  for (auto &[id, p] : providers) {
    auto claim = [&](const std::string &rel, const std::string &obj) {
      Claim c;
      c.id = prefix + digest(strings(Names{id, rel, obj}));
      c.predicate = rel;
      c.subject = id;
      c.object = obj;
      c.source = source;
      c.origin = "metadata";
      claims.push_back(c);
      facts.push(c.data());
    };
    claim("implements", p.capability);
    claim("target", p.target);
    if (!disabled.contains(id))
      claim("available", "true");
    for (auto &n : p.needs)
      claim("requires_capability", n);
    for (auto &n : p.accepts)
      claim("accepts", n);
    for (auto &n : p.produces)
      claim("produces", n);
  }
  for (auto predicate :
       {"implements", "target", "available", "requires_capability", "accepts",
        "produces", "compatible"}) {
    auto r = Json::object();
    r.set("name", Json::string(predicate));
    relations.push(relation(r));
  }
  auto result = engine.application_plan(facts, relations, strings(needed),
                                        language, deadline);
  if (result["status"].text() != "complete")
    throw Unsupported("Available primitive contracts cannot satisfy all "
                      "requested behaviours");
  auto proof = ProofChecker(claims, relations, empty).check(result["proofs"]);
  auto selected = Json::object(), order = Json::array();
  size_t nodes = 0;
  std::set<std::string> visiting;
  std::function<void(const Json &, const std::string &)> verify =
      [&](const Json &tree, const std::string &cap) {
        auto args = tree["args"];
        if (++nodes > 4096 || tree["functor"].text() != "node" ||
            args.size() != 3 || args.at(0).text() != cap ||
            !visiting.insert(cap).second)
          throw Unsupported("Invalid provider proof tree");
        auto id = args.at(1).text();
        auto it = providers.find(id);
        if (it == providers.end() || disabled.contains(id) ||
            it->second.capability != cap ||
            (it->second.target != "any" && it->second.target != language))
          throw Unsupported("Invalid provider selection");
        auto &p = it->second;
        auto children = args.at(2);
        if (!children.is_array())
          throw Unsupported("Expected provider dependencies");
        Names deps;
        for (auto child : children.items())
          deps.push_back(child["args"].at(0).text());
        if (std::multiset<std::string>(deps.begin(), deps.end()) !=
            std::multiset<std::string>(p.needs.begin(), p.needs.end()))
          throw Unsupported("Missing provider dependency");
        for (auto child : children.items())
          verify(child, child["args"].at(0).text());
        visiting.erase(cap);
        if (selected.contains(cap)) {
          if (selected[cap].text() != id)
            throw Unsupported("Conflicting provider selections");
          return;
        }
        selected.set(cap, Json::string(id));
        order.push(Json::string(cap));
      };
  auto trees = result["trees"];
  if (trees.size() != needed.size())
    throw Unsupported("Missing behaviour obligation");
  for (size_t i = 0; i < needed.size(); ++i)
    verify(trees.at(i), needed[i]);
  std::vector<Goal> goals;
  for (auto &[from, to] : ports)
    goals.push_back({"compatible", required_text(selected, from),
                     required_text(selected, to)});
  auto obligations = Json::array();
  for (auto &g : goals)
    obligations.push(g.data());
  auto wires = engine.query(facts, relations, empty, obligations, deadline);
  if (!wires["solutions"].size())
    throw Unsupported("Incompatible UI ports");
  auto row = wires["solutions"].at(0);
  auto wireproof = ProofChecker(claims, relations, empty)
                       .check(row["proofs"], goals, row["bindings"]);
  auto out = Json::object();
  out.set("requirements", strings(needed));
  out.set("providers", selected);
  out.set("order", order);
  out.set("trees", trees);
  out.set("derivation", proof);
  out.set("wire_derivation", wireproof);
  out.set("wires", obligations);
  return out;
}

// Minimize missing conversions first. A lower-ranked real dependency is always
// preferable to a hole. Only the requested semantic operation may change
// values.
Json checked_port_plan(const std::vector<PortProvider> &providers,
                       const std::string &input, const std::string &output,
                       const Names &guarantees, NativeEngine &engine,
                       Deadline deadline) {
  using State = std::pair<std::string, std::size_t>;
  using Cost =
      std::tuple<unsigned, unsigned, unsigned>; // holes, length, unranked
  using Visit = std::pair<Cost, State>;
  struct Link {
    State previous;
    std::string id;
  };
  const State start{input, 0}, finish{output, guarantees.size()};
  std::map<State, Cost> best{{start, {0, 0, 0}}};
  std::map<State, Link> previous;
  std::set<Visit> pending{{best.at(start), start}};
  std::set<std::string> ports{output}, ids;
  for (const auto &p : providers) {
    if (p.id.empty() || !ids.insert(p.id).second)
      throw Unsupported("Missing or duplicate port provider ID");
    ports.insert(p.accepts);
    ports.insert(p.produces);
  }
  if (guarantees.empty() || input == output)
    throw Unsupported(
        "Expected an observable result and distinct external ports");
  while (!pending.empty()) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw ResourceLimit("Port planning deadline exceeded");
    auto [cost, state] = *pending.begin();
    pending.erase(pending.begin());
    if (state == finish)
      break;
    if (state.first == output)
      continue;
    auto offer = [&](const State &to, const std::string &id, unsigned hole,
                     unsigned unranked) {
      if (to.first == input)
        return;
      const auto [holes, steps, rank] = cost;
      Cost next{holes + hole, steps + 1, rank + unranked};
      auto found = best.find(to);
      if (found != best.end()) {
        if (found->second <= next)
          return;
        pending.erase({found->second, to});
      }
      best[to] = next;
      previous[to] = {state, id};
      pending.emplace(next, to);
    };
    for (const auto &p : providers) {
      if (p.accepts != state.first)
        continue;
      if (!p.guarantee.empty() &&
          (state.second >= guarantees.size() || state.second != p.occurrence ||
           p.guarantee != guarantees[state.second]))
        continue;
      offer({p.produces, state.second + !p.guarantee.empty()}, p.id, 0,
            !p.preferred);
    }
    for (const auto &port : ports)
      if (port != state.first &&
          (port != output || state.second == guarantees.size()))
        offer({port, state.second}, "", 1, 0);
  }

  Json result = Json::object(), steps = Json::array(), holes = Json::array();
  result.set("steps", steps);
  result.set("holes", holes);
  if (!best.contains(finish)) {
    result.set("status", Json::string("unsupported"));
    result.set("requested_outcomes", strings(guarantees));
    return result;
  }
  std::vector<State> path;
  for (auto s = finish; s != start; s = previous.at(s).previous)
    path.push_back(s);
  std::reverse(path.begin(), path.end());
  // The established application planner resolves the dependency tree and its
  // port proofs. In a partial plan the proof is conditional on named
  // interfaces.
  ProviderCatalog proof_catalog{
      {"source", {"value_0", "cpp", {}, {}, {input}}}};
  std::vector<std::pair<std::string, std::string>> wires;
  for (std::size_t i = 0; i < path.size(); ++i) {
    const auto &to = path[i];
    const auto &link = previous.at(to);
    auto before = "value_" + std::to_string(i);
    auto after = "value_" + std::to_string(i + 1);
    auto id = link.id.empty() ? "assumption_" + std::to_string(i)
                              : "brick_" + link.id + "_" + std::to_string(i);
    proof_catalog.emplace(
        id,
        Provider{after, "cpp", {before}, {link.previous.first}, {to.first}});
    wires.emplace_back(before, after);
    auto step = Json::object();
    step.set("from", Json::string(link.previous.first));
    step.set("to", Json::string(to.first));
    if (link.id.empty())
      holes.push(step);
    else
      step.set("provider", Json::string(link.id));
    steps.push(step);
  }
  result.set("proof",
             checked_provider_plan(
                 proof_catalog, {wires.back().second}, wires, "cpp", {},
                 "ports_", "brick port contracts and explicit assumptions",
                 engine, deadline));
  result.set("status", Json::string(holes.size() ? "partial" : "complete"));
  return result;
}
} // namespace unllm::native
