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
} // namespace unllm::native
