#pragma once
#include "model.h"
#include "mrs.h"
#include <functional>
namespace unllm::native {
struct Meaning {
  std::string act = "query", temporal = "present";
  std::vector<Goal> goals, assumptions;
  std::vector<std::string> select, diagnostics;
  std::map<std::string, std::string> labels;
  bool count = false, explain = false;
  Json semantics = Json::array(), analyses = Json::array();
  std::string signature() const;
  Json data() const;
};
using Resolver = std::function<std::string(const std::string &, bool)>;
const std::map<std::string, std::string> &type_words();
std::string lemma(const std::string &predicate);
Meaning translate(const Mrs &mrs, const std::string &text,
                  const Resolver &resolve,
                  const std::vector<std::string> &focus = {},
                  bool programming = false);
class SemanticParser {
public:
  explicit SemanticParser(std::filesystem::path root, int max_parses = 64)
      : root_(std::move(root)), maximum_(max_parses) {}
  Meaning parse(const std::string &text, const Resolver &resolve,
                const std::vector<std::string> &focus = {},
                Deadline deadline = Deadline::max());
  double last_ms = 0;

private:
  std::filesystem::path root_;
  int maximum_;
  std::unique_ptr<AceClient> normal_, fragment_;
};
} // namespace unllm::native
