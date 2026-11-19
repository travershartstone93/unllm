#include "unllm/symbolic/grounding.h"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace unllm::native;
namespace fs = std::filesystem;
struct Seed {
  std::string id, question;
};
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  try {
    fs::path root = argv[1], out = argv[3];
    fs::create_directories(out);
    std::ifstream file(argv[2]);
    std::vector<Seed> seeds;
    for (std::string line; std::getline(file, line);)
      seeds.push_back(
          {line.substr(0, line.find('\t')), line.substr(line.rfind('\t') + 1)});
    if (seeds.size() != 6)
      throw Unsupported("Expected six frozen seeds");
    auto lexicon = Json::parse(read_file(root / "data/program_semantics.json"));
    for (std::string mode : {"cold", "resident", "reordered"}) {
      std::unique_ptr<AceClient> full, fragment;
      if (mode == "reordered")
        std::reverse(seeds.begin(), seeds.end());
      for (auto &seed : seeds) {
        if (mode == "cold" || !full) {
          full = std::make_unique<AceClient>(root, 4, 64);
          fragment = std::make_unique<AceClient>(root, 1, 64, "root_inffrag");
        }
        if (mode == "reordered")
          full->parse("Oslo is in Norway.");
        auto linked = normalize_program(seed.question, lexicon);
        auto result = full->parse(linked.text);
        auto path = out / (mode + "-" + seed.id);
        std::ofstream(path.string() + ".full") << result.notes << result.raw;
        bool fallback = result.analyses.empty();
        if (fallback) {
          result = fragment->parse(linked.text);
          std::ofstream(path.string() + ".fragment")
              << result.notes << result.raw;
        }
        auto readings = Json::array();
        for (auto &mrs : result.analyses)
          readings.push(mrs.data());
        std::ofstream(path.string() + ".json") << readings.dump();
        std::cout << mode << '\t' << seed.id << '\t' << linked.text << '\t'
                  << fallback << '\t' << readings.size() << '\t'
                  << result.reached_candidate_limit << std::endl;
      }
    }
    SemanticParser parser(root);
    for (auto &seed : seeds) {
      try {
        auto m = parser.parse(
            seed.question, [](auto &, bool) { return std::string(); }, {},
            after_ms(11500), true);
        auto spec = ground_program(m, root);
        std::ofstream(out / ("A-" + seed.id + ".json")) << spec.dump();
        std::cout << "baseline\t" << seed.id << "\taccepted\n";
      } catch (const std::exception &e) {
        std::cout << "baseline\t" << seed.id << '\t' << e.what() << '\n';
      }
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
