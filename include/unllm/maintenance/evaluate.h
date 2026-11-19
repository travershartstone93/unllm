#pragma once
#include "sources.h"
namespace unllm::maintenance {
struct Evaluation {
  int known = 0, answered = 0, hedged = 0, right = 0, confident_right = 0;
  int unknowable = 0, declined_unknown = 0;
  void observe(const Json &row);
  Json data() const;
  std::string summary(const std::string &name) const;
};
using Answer =
    std::function<std::pair<std::string, std::string>(const std::string &)>;
std::vector<Json> read_questions(const Path &tsv);
Json judge(const Json &question, const std::string &branch,
           const std::string &answer);
std::pair<std::string, std::string> ask_candidate(const Path &cli,
                                                  const std::string &question, const std::vector<std::string>& env = {});
Evaluation evaluate_tsv(const Path &input, const Answer &,
                        const std::function<void(const Json &)> &record = {},
                        unsigned workers = 1, const Json &completed = {});
} // namespace unllm::maintenance
