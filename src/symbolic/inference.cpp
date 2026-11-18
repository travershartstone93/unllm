#include "unllm/symbolic/inference.h"
#include <map>
#include <set>
namespace unllm::native {
NativeEngine::NativeEngine(std::filesystem::path root,
                           std::filesystem::path worker)
    : root_(std::move(root)), worker_(std::move(worker)) {
  auto texts = Json::array();
  for (auto p :
       {"reasoning/prolog/rules.pl", "reasoning/souffle/background.dl"})
    texts.push(Json::string(read_file(root_ / p)));
  rule_version_ = digest(texts);
}
void NativeEngine::reset() {
  process_.reset();
  loaded_key_.clear();
}
void NativeEngine::check_budget(const Json &facts, const Json &relations) {
  if (!facts.is_array() || !relations.is_array())
    throw Unsupported("Facts and relations must be arrays");
  if (facts.size() > 100000)
    throw ResourceLimit("Input fact budget exceeded (100000)");
  std::set<std::string> transitive;
  for (auto r : relations.items())
    if (relation(r)["transitive"].flag())
      transitive.insert(r["name"].text());
  std::map<std::string, std::map<std::string, std::set<std::string>>> graphs;
  for (auto f : facts.items()) {
    auto c = Claim::from(f);
    if (c.negative || !transitive.contains(c.predicate))
      continue;
    graphs[c.predicate][c.subject].insert(c.object);
    graphs[c.predicate][c.object].insert(c.subject);
  }
  size_t bound = facts.size() * 2;
  for (auto &[_, graph] : graphs) {
    std::set<std::string> seen;
    for (auto &[node, edges] : graph) {
      if (seen.contains(node))
        continue;
      std::vector<std::string> todo{node};
      size_t size = 0;
      while (!todo.empty()) {
        auto next = todo.back();
        todo.pop_back();
        if (!seen.insert(next).second)
          continue;
        ++size;
        for (auto &n : graph.at(next))
          if (!seen.contains(n))
            todo.push_back(n);
      }
      bound += size * size;
      if (bound > 500000)
        throw ResourceLimit("Background closure may exceed 500000 tuples");
    }
  }
}
std::string NativeEngine::input_key(const Json &facts, const Json &relations) const {
  auto a = Json::array();
  a.push(Json::string(rule_version_));
  a.push(facts);
  a.push(relations);
  return digest(a);
}
std::string NativeEngine::world_key(const Json &facts, const Json &relations,
                                    const Json &complete) const {
  auto a = Json::array();
  a.push(Json::string(rule_version_));
  a.push(facts);
  a.push(relations);
  a.push(complete);
  return digest(a);
}
Json NativeEngine::request(const Json &value, Deadline deadline) {
  try {
    if (!process_)
      process_ = std::make_unique<Process>(
          std::vector<std::string>{worker_.string(), root_.string()});
    process_->reset_output_budget();
    auto payload = canonical(value);
    if (payload.size() > 16 * 1024 * 1024)
      throw ResourceLimit("Inference request exceeds transport limit");
    process_->write(payload + "\n", deadline);
    auto result = Json::parse(process_->line(deadline));
    if (!result.is_object() || !result["status"].is_string())
      throw std::runtime_error("Malformed inference reply");
    return result;
  } catch (...) {
    reset();
    throw;
  }
}
static Json world(const std::string &key, const Json &facts,
                  const Json &relations, const Json &complete) {
  auto out = Json::object();
  out.set("world_key", Json::string(key));
  out.set("facts", facts);
  out.set("relations", relations);
  out.set("complete", complete);
  return out;
}
Json NativeEngine::snapshot(const Json &facts, const Json &relations,
                            const Json &complete, Deadline deadline) {
  check_budget(facts, relations);
  auto key = world_key(facts, relations, complete);
  auto input = world(key, facts, relations, complete);
  input.set("operation", Json::string("snapshot"));
  auto result = request(input, deadline);
  if (result["status"].text() != "complete") {
    reset();
    throw std::runtime_error(
        result["error"].text("Background preparation failed"));
  }
  loaded_key_ = key;
  return result;
}
Json NativeEngine::query(const Json &facts, const Json &relations,
                         const Json &complete, const Json &goals,
                         Deadline deadline, int limit, const Json &prepared) {
  if (!goals.is_array() || goals.size() > 64)
    throw Unsupported("Query requires at most 64 goals");
  for (auto g : goals.items())
    Goal::from(g);
  if (limit < 1 || limit > 1000)
    throw Unsupported("Answer limit must be between 1 and 1000");
  auto key = world_key(facts, relations, complete);
  auto input = Json::object();
  input.set("world_key", Json::string(key));
  input.set("goals", goals);
  input.set("limit", Json::number(limit));
  auto full = [&] {
    check_budget(facts, relations);
    input.set("facts", facts);
    input.set("relations", relations);
    input.set("complete", complete);
    if (!prepared.null())
      input.set("prepared", prepared);
  };
  if (key != loaded_key_)
    full();
  auto result = request(input, deadline);
  if (result["status"].text() == "error" &&
      result["error"].text() == "world snapshot is not loaded") {
    loaded_key_.clear();
    full();
    result = request(input, deadline);
  }
  if (result["status"].text() == "error") {
    reset();
    throw std::runtime_error(result["error"].text("Native inference failed"));
  }
  loaded_key_ = key;
  return result;
}
Json NativeEngine::application_plan(const Json &facts, const Json &relations,
                                    const Json &requirements,
                                    const std::string &language,
                                    Deadline deadline) {
  check_budget(facts, relations);
  if (!requirements.is_array() || requirements.size() > 32)
    throw Unsupported("Application requirements exceed budget");
  auto complete = Json::array();
  auto key = world_key(facts, relations, complete);
  auto input = world(key, facts, relations, complete);
  input.set("operation", Json::string("application_plan"));
  input.set("requirements", requirements);
  input.set("language", Json::string(language));
  auto result = request(input, deadline);
  if (result["status"].text() == "error") {
    reset();
    throw std::runtime_error(
        result["error"].text("Application planning failed"));
  }
  loaded_key_ = key;
  return result;
}
} // namespace unllm::native
