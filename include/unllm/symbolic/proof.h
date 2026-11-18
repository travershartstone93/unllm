#pragma once
#include "model.h"
#include <map>
#include <set>
namespace unllm::native {
class ProofChecker {
public:
  ProofChecker(const std::vector<Claim> &claims, const Json &relations,
               const Json &complete);
  Json check(const Json &proofs, const std::vector<Goal> &goals,
             const Json &bindings);
  // Construction plans also contain proofs of intermediate obligations.
  Json check(const Json &proofs);

private:
  using Term = std::pair<std::string, std::vector<std::string>>;
  using Pair = std::pair<std::string, std::string>;
  std::map<std::string, Claim> claims_;
  std::set<std::string> transitive_, used_;
  std::set<Pair> inverses_, domains_, ranges_, complete_;
  Json steps_ = Json::array();
  size_t visited_ = 0;
  Term souffle(const Json &tree, unsigned depth);
  void witness(const Json &value, const Goal &goal, unsigned depth = 0);
  Goal witness_goal(const Json &value) const;
  Json check_impl(const Json &, const std::vector<Goal> *, const Json &);
  void budget(unsigned depth);
};
} // namespace unllm::native
