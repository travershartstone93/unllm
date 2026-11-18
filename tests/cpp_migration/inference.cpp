#include "unllm/symbolic/inference.h"
#include "unllm/symbolic/proof.h"
#include <iostream>
using namespace unllm::native;
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Expected invalid proof to be rejected");
}
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  try {
    auto relations = Json::parse(read_file(std::filesystem::path(argv[1]) /
                                           "data/reasoning_relations.json"));
    auto facts = Json::parse(R"([
 {"id":"p-f","predicate":"located_in","subject":"paris","object":"france"},
 {"id":"f-e","predicate":"located_in","subject":"france","object":"europe"},
 {"id":"app-lib","predicate":"depends_on","subject":"app","object":"lib"},
 {"id":"lib-ok","predicate":"available","subject":"lib","object":"true"},
 {"id":"height-a","predicate":"height","subject":"alice","object":"1.8"},
 {"id":"height-b","predicate":"height","subject":"bob","object":"1.6"}
 ])");
    std::vector<Claim> claims;
    for (auto f : facts.items())
      claims.push_back(Claim::from(f));
    auto complete =
        Json::parse(R"([{"predicate":"depends_on","subject":"app"}])");
    NativeEngine engine(argv[1], argv[2]);
    ProofChecker verifier(claims, relations, complete);
    auto run = [&](const std::vector<Goal> &goals) {
      auto query = Json::array();
      for (auto &g : goals)
        query.push(g.data());
      auto result =
          engine.query(facts, relations, complete, query, after_ms(5000));
      check(result["status"].text() == "complete" &&
                result["solutions"].size() > 0,
            "Inference expected a complete answer");
      for (auto row : result["solutions"].items())
        check(verifier.check(row["proofs"], goals, row["bindings"])["valid"]
                  .flag(),
              "Proof validation");
      return result;
    };
    Goal transit{"located_in", "paris", "europe"};
    auto answer = run({transit});
    auto proofs = answer["solutions"].at(0)["proofs"];
    auto corrupted = proofs.clone();
    auto invalid_tree = Json::object();
    invalid_tree.set(
        "axiom",
        Json::string(
            "source(\"missing\",\"located_in\",\"paris\",\"europe\")"));
    corrupted.at(0)["args"].at(4).set("tree", invalid_tree);
    rejects([&] { verifier.check(corrupted, {transit}, Json::object()); });
    rejects([&] {
      verifier.check(proofs, {Goal{"located_in", "paris", "asia"}},
                     Json::object());
    });
    rejects([&] { verifier.check(Json::array(), {transit}, Json::object()); });
    run({Goal{"contains", "europe", "paris"}});
    run({Goal{"type", "paris", "entity"}});
    run({Goal{"taller_than", "alice", "bob"}});
    run({Goal{"taller_than", "bob", "alice", true}});
    run({Goal{"blocked", "app", "true", true}});
    auto unbound = run({Goal{"located_in", "paris", "?where"}});
    rejects([&] {
      verifier.check(unbound["solutions"].at(0)["proofs"],
                     {Goal{"located_in", "paris", "?where"}}, Json::object());
    });
    auto prepared = engine.snapshot(facts, relations, complete, after_ms(5000));
    check(prepared["closure"].size() > 0, "Materialization snapshot");
    engine.reset();
    auto goals = Json::array();
    goals.push(transit.data());
    auto cached = engine.query(facts, relations, complete, goals,
                               after_ms(5000), 100, prepared);
    check(cached["used_prepared"].flag(), "Prepared materialization reuse");
    verifier.check(cached["solutions"].at(0)["proofs"], {transit},
                   Json::object());
    // Independent worker state must not contaminate an existing engine.
    NativeEngine other(argv[1], argv[2]);
    other.snapshot(Json::array(), relations, Json::array(), after_ms(5000));
    run({transit});
    engine.reset();
    bool timeout = false;
    try {
      engine.query(facts, relations, complete, goals, after_ms(-1));
    } catch (const ResourceLimit &) {
      timeout = true;
    }
    check(timeout, "Expired inference deadline");
    run({transit});
    auto blocked = facts.clone();
    blocked.at(3).set("negative", Json::boolean(true));
    std::vector<Claim> blocked_claims;
    for (auto f : blocked.items())
      blocked_claims.push_back(Claim::from(f));
    ProofChecker blocked_verifier(blocked_claims, relations, complete);
    Goal g{"blocked", "app", "true"};
    auto q = Json::array();
    q.push(g.data());
    auto result = engine.query(blocked, relations, complete, q, after_ms(5000));
    check(result["solutions"].size() > 0, "Unavailable dependency inference");
    blocked_verifier.check(result["solutions"].at(0)["proofs"], {g},
                           Json::object());
    auto chain = Json::array();
    for (int i = 0; i < 710; ++i) {
      auto c = Claim::from(facts.at(0));
      c.id = "edge:" + std::to_string(i);
      c.subject = std::to_string(i);
      c.object = std::to_string(i + 1);
      chain.push(c.data());
    }
    rejects([&] { NativeEngine::check_budget(chain, relations); });
    std::cout << "Native inference, provenance rejection, warm state, prepared "
                 "cache and deadline checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
