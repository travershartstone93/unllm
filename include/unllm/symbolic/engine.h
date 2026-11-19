#pragma once
#include "inference.h"
#include "semantics.h"
#include "store.h"
namespace unllm::native {
struct Context {
  std::vector<Claim> facts;
  std::map<std::string, std::string> labels;
  std::vector<std::string> focus;
  std::optional<Meaning> previous;
  std::vector<std::vector<Claim>> undo;
  Json complete = Json::array();
  Json last_spec;
  std::string last_language;
  Deadline touched = std::chrono::steady_clock::now();
};
class Engine {
public:
  Engine(const std::filesystem::path &database,
         const std::filesystem::path &root, const std::filesystem::path &worker)
      : store(database, root), native(root, worker), parser(root),
        resources_(root) {}
  Json ask(const Json &request, Deadline deadline);
  std::function<void()> synthesis_started;
  FactStore store;
  NativeEngine native;
  SemanticParser parser;

private:
  std::filesystem::path resources_;
  Json generate(Json request, Context &ctx, const std::string &question);
  std::map<std::string, Context> contexts_;
  Context &context(const std::string &name);
  Meaning interpret(const std::string &, Context &, const std::string &,
                    Deadline);
  std::string label(const std::string &, const Context &) const;
  std::string sentence(const Goal &, const Context &) const;
  void assert_goals(const Meaning &, Context &, bool correction);
  Json evaluate(const Meaning &, Context &, int64_t, const std::vector<Goal> &,
                Deadline);
  Json answer(const Meaning &, Context &, int64_t, const std::vector<Goal> &,
              Deadline);
  Json ask_snapshot(Json, Deadline);
};
} // namespace unllm::native
