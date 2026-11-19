#pragma once
#include "common.h"
#include "process.h"
#include <map>
#include <memory>

namespace unllm::native {
struct Predication {
  std::string predicate, label;
  std::map<std::string, std::string> args;
  Json span;
  Json data() const;
};
struct Mrs {
  std::string top, index;
  std::vector<Predication> rels;
  std::map<std::string, std::map<std::string, std::string>> variables;
  Json hcons = Json::array(), icons = Json::array();
  Json data() const;
};
Mrs decode_mrs(const std::string &source);

struct ParseResult {
  std::vector<Mrs> analyses;
  std::string raw, notes, error;
  bool reached_candidate_limit = false;
};
ParseResult decode_ace_response(const std::string &response);
class AceClient {
public:
  AceClient(const std::filesystem::path &root, int timeout_seconds = 1,
            int max_parses = 64,
            const std::string &grammar_root = "root_informal");
  ParseResult parse(const std::string &text,
                    Deadline deadline = Deadline::max());

private:
  Process process_;
  int timeout_, maximum_;
};
} // namespace unllm::native
