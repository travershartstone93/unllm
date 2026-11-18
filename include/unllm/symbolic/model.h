#pragma once
#include "common.h"
#include <optional>

namespace unllm::native {
bool identifier(const std::string &text);
struct Goal {
  std::string predicate, subject, object;
  bool negative = false;
  static Goal from(const Json &value);
  Json data() const;
};
struct Claim {
  std::string id, predicate, subject, object, source = "session",
                                              origin = "source";
  bool negative = false;
  std::optional<int64_t> valid_from, valid_to;
  Json qualifiers = Json::object();
  static Claim from(const Json &value);
  Json data() const;
  bool active(int64_t now) const;
};
Json relation(const Json &value);
} // namespace unllm::native
