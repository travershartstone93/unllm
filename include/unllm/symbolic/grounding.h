#pragma once
#include "semantics.h"
namespace unllm::native {
struct ProgramText {
  std::string text;
  Json spans = Json::array();
};
bool creation_request(const std::string &text);
ProgramText normalize_program(const std::string &text, const Json &lexicon);
void annotate_program(Json semantics, const Json &spans,
                      const std::string &source);
Json ground_program(const Meaning &meaning, const std::filesystem::path &root,
                    Json *trace = nullptr,
                    Json *requirement_readings = nullptr);
} // namespace unllm::native
