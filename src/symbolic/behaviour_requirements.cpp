#include "behaviour_requirements.h"
#include <type_traits>

namespace unllm::native::frames {
using namespace gui;
namespace {
std::string name(Entity entity) {
  switch (entity) {
  case Entity::document:
    return "document";
  case Entity::collection:
    return "collection";
  case Entity::clock:
    return "clock";
  case Entity::counter:
    return "counter";
  }
  throw std::logic_error("Unknown requirement entity");
}
std::string name(Status status) {
  switch (status) {
  case Status::explicit_:
    return "explicit";
  case Status::justified_default:
    return "justified_default";
  case Status::excluded:
    return "excluded";
  case Status::unresolved:
    return "unresolved";
  }
  throw std::logic_error("Unknown requirement status");
}
bool matches(Entity entity, const Terms &terms) {
  switch (entity) {
  case Entity::document:
    return any(terms, {"text", "document", "editor", "body"});
  case Entity::collection:
    return any(terms, {"list", "collection", "task", "item", "records"});
  case Entity::clock:
    return any(terms, {"timer", "clock", "elapsed_time", "elapsed"});
  case Entity::counter:
    return any(terms, {"counter", "number", "value"});
  }
  return false;
}
} // namespace
Json evidence(const Json &ep) {
  auto source =
      obj({{"predicate", ep["predicate"]},
           {"arguments", ep["args"]},
           {"source_span",
            ep.contains("source_span") ? ep["source_span"] : ep["span"]}});
  if (ep.contains("literal_sign"))
    source.set("literal_sign", ep["literal_sign"]);
  if (ep.contains("scope") && ep["scope"].size())
    source.set("scope", ep["scope"]);
  return source;
}
void BehaviourRequirements::add(Requirement meaning, std::string rule,
                                Json sources, Status status) {
  if (auto *state = std::get_if<StateRequirement>(&meaning))
    entities.insert(state->entity);
  nodes.push_back(
      {std::move(meaning), std::move(rule), status, std::move(sources)});
}
Entity BehaviourRequirements::bind(const Graph &g, const Json &ep,
                                   const std::string &op,
                                   const std::set<Entity> &allowed) const {
  Terms terms;
  for (auto key : {"ARG1", "ARG2", "ARG3", "ARG4"}) {
    auto role = g.terms(arg(ep, key));
    terms.insert(role.begin(), role.end());
  }
  std::set<Entity> compatible, mentioned;
  for (auto entity : entities) {
    if (!allowed.contains(entity))
      continue;
    compatible.insert(entity);
    if (matches(entity, terms))
      mentioned.insert(entity);
  }
  auto &choices = mentioned.empty() ? compatible : mentioned;
  if (choices.size() != 1)
    throw Ambiguous("Specify the target for " + op);
  return *choices.begin();
}
Json BehaviourRequirements::describe() const {
  Json result = Json::array();
  for (auto &node : nodes) {
    auto data = obj({{"rule", str(node.rule)},
                     {"status", str(name(node.status))},
                     {"evidence", node.evidence}});
    auto kind = std::visit(
        [&](const auto &v) -> std::string {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, Intent>)
            return "creation_intent";
          if constexpr (std::is_same_v<T, StateRequirement>) {
            data.set("entity", str(name(v.entity)));
            if (!v.fields.null())
              data.set("record_fields", v.fields);
            if (v.entity == Entity::document) {
              data.set("editor", boolean(v.editor));
              data.set("multiline", boolean(v.multiline));
            }
            return "state";
          }
          if constexpr (std::is_same_v<T, InitialValue>) {
            data.set("target", str("counter"));
            data.set("value", num(v.value));
            return "initial_value";
          }
          if constexpr (std::is_same_v<T, Percentage>) {
            data.set("relation", str(v.relation));
            return "percentage";
          }
          if constexpr (std::is_same_v<T, Conversion>) {
            data.set("source_unit", str(v.from));
            data.set("target_unit", str(v.to));
            return "unit_conversion";
          }
          if constexpr (std::is_same_v<T, Metric> ||
                        std::is_same_v<T, Action>) {
            data.set("target", str(name(v.target)));
            data.set("operation", str(v.operation));
            if constexpr (std::is_same_v<T, Metric>)
              return "derived_value";
            else
              return "action";
          }
          if constexpr (std::is_same_v<T, Change>) {
            data.set("target", str(name(v.action.target)));
            data.set("operation", str(v.action.operation));
            data.set("amount", num(v.amount));
            data.set("amount_status", str(name(v.amount_status)));
            return "numeric_change";
          }
          if constexpr (std::is_same_v<T, DefaultControls>) {
            data.set("target", str(name(v.target)));
            return "default_controls";
          }
          if constexpr (std::is_same_v<T, ScopeConstraint>) {
            data.set("reason", str(v.reason));
            return "constraint";
          }
        },
        node.meaning);
    data.set("kind", str(kind));
    result.push(data);
  }
  return result;
}
} // namespace unllm::native::frames
