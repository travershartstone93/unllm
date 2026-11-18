#pragma once
#include "model.h"
#include "process.h"
#include <memory>
namespace unllm::native {
class NativeEngine {
public:
  NativeEngine(std::filesystem::path root, std::filesystem::path worker);
  static void check_budget(const Json &facts, const Json &relations);
  const std::string &rule_version() const { return rule_version_; }
  std::string input_key(const Json &facts, const Json &relations) const;
  Json snapshot(const Json &facts, const Json &relations, const Json &complete,
                Deadline deadline);
  Json query(const Json &facts, const Json &relations, const Json &complete,
             const Json &goals, Deadline deadline, int limit = 100,
             const Json &prepared = {});
  Json application_plan(const Json &facts, const Json &relations,
                        const Json &requirements, const std::string &language,
                        Deadline deadline);
  void reset();

private:
  std::filesystem::path root_, worker_;
  std::unique_ptr<Process> process_;
  std::string rule_version_, loaded_key_;
  Json request(const Json &, Deadline);
  std::string world_key(const Json &, const Json &, const Json &) const;
};
} // namespace unllm::native
