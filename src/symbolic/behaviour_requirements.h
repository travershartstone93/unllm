#pragma once
#include "grounding_internal.h"
#include <optional>
#include <variant>

namespace unllm::native::frames {
// References name semantic entities. The lowering pass chooses storage and
// views.
enum class Entity { document, collection, clock, counter };
enum class Status { explicit_, justified_default, excluded, unresolved };
struct Intent {};
struct StateRequirement {
  Entity entity;
  bool editor = false, multiline = false;
  Json fields = {};
};
struct InitialValue {
  double value;
};
struct Metric {
  Entity target;
  std::string operation;
};
struct Percentage {
  std::string relation;
};
struct Conversion {
  std::string from, to;
};
enum class ActionKind { transform, io, append, remove, clock, numeric };
struct Action {
  Entity target;
  ActionKind kind;
  std::string verb, operation;
};
struct Change {
  Action action;
  double amount;
  Status amount_status;
};
struct DefaultControls {
  Entity target;
};
struct ScopeConstraint {
  std::string reason;
};
using Requirement =
    std::variant<Intent, StateRequirement, InitialValue, Metric, Percentage,
                 Conversion, Action, Change, DefaultControls, ScopeConstraint>;
struct RequirementNode {
  Requirement meaning;
  std::string rule;
  Status status;
  Json evidence;
};
struct BehaviourRequirements {
  std::vector<RequirementNode> nodes;
  std::set<Entity> entities;
  void add(Requirement meaning, std::string rule, Json evidence,
           Status status = Status::explicit_);
  Entity bind(const Graph &, const Json &ep, const std::string &operation,
              const std::set<Entity> &compatible) const;
  Json describe() const;
};
Json lower(const BehaviourRequirements &, const Json &domain,
           const Json &vocab);
Json evidence(const Json &ep);
} // namespace unllm::native::frames
