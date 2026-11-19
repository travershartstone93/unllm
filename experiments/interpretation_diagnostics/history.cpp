#include "identity.h"
#include "unllm/symbolic/grounding.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>

using namespace unllm::native;
namespace fs = std::filesystem;

int main(int argc, char **argv) {
  if (argc != 3) return 2;
  fs::path root = argv[1], out = argv[2];
  if (!fs::create_directory(out)) return 2;
  auto cases = Json::parse(read_file(root / "tests/cpp_migration/fixtures/interpretation.json")).items();
  auto resolve = [](const std::string &, bool) {return std::string();};
  std::ofstream index(out / "trials.jsonl");
  for (std::string mode : {"fresh", "resident", "repeat", "unrelated", "shuffled"}) {
    if (mode == "shuffled") std::shuffle(cases.begin(), cases.end(), std::mt19937(131));
    auto parser = std::make_unique<SemanticParser>(root);
    for (auto c : cases) {
      if (mode == "fresh") parser = std::make_unique<SemanticParser>(root);
      auto id = c["id"].text(), question = c["question"].text();
      auto parse = [&](const std::string &q) {return parser->parse(q, resolve, {}, after_ms(11500), true);};
      if (mode == "repeat" || mode == "unrelated") {
        try {parse(mode == "repeat" ? question : "Build an app that decreases a counter by 11.");}
        catch (const Unsupported &) {} catch (const Ambiguous &) {} catch (const ResourceLimit &) {}
      }
      auto row = Json::object(), attempts = Json::array(), translated = Json::array(), grounded = Json::array();
      row.set("mode", Json::string(mode)); row.set("id", c["id"]); row.set("question", c["question"]);
      parser->observe_parse = [&](const auto &text, const Json &spans, bool fragment, const ParseResult &response) {
        auto item = Json::object(), ids = Json::array();
        auto file = mode + "-" + id + (fragment ? ".fragment" : ".full");
        std::ofstream(out / file) << response.notes << response.raw;
        for (auto &m : response.analyses) {
          try {
            ids.push(Json::string(diagnostics::identity(m)));
          } catch (const ResourceLimit &e) {
            throw std::runtime_error(std::string("Trace failed for ") + id + ": " + e.what());
          }
        }
        item.set("error", Json::string(response.error));
        item.set("raw", Json::string(file)); item.set("candidates", ids);
        item.set("cap", Json::boolean(response.reached_candidate_limit));
        attempts.push(item); row.set("normalized", Json::string(text)); row.set("spans", spans);
      };
      parser->observe_translation = [&](auto &s) {translated.push(Json::string(s));};
      std::string status = "accepted", reason;
      try {
        auto spec = ground_program(parse(question), root, &grounded);
        auto expected = c["spec"].clone(), body = spec.clone();
        if (expected.contains("interpretation")) expected.erase("interpretation");
        if (body.contains("interpretation")) body.erase("interpretation");
        row.set("fixture_contract_match", Json::boolean(canonical(body) == canonical(expected)));
        row.set("fixture_spec_match", Json::boolean(canonical(spec) == canonical(c["spec"])));
      } catch (const Unsupported &e) {status = "unsupported"; reason = e.what();}
      catch (const Ambiguous &e) {status = "ambiguous"; reason = e.what();}
      catch (const ResourceLimit &e) {status = "resource_limit"; reason = e.what();}
      row.set("status", Json::string(status)); row.set("reason", Json::string(reason));
      row.set("attempts", attempts); row.set("translation", translated); row.set("grounding", grounded);
      index << row.dump() << std::endl;
      parser->observe_parse = {}; parser->observe_translation = {};
    }
  }
}
