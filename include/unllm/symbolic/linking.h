#pragma once
#include "common.h"
#include <map>
namespace unllm::native {
struct Word {
  std::string normalized;
  size_t begin, end;
};
std::vector<Word> words(const std::string &text);
struct LinkedText {
  std::string text;
  std::map<std::string, std::string> names;
};
LinkedText link_entities(const std::string &text,
                         const std::vector<std::string> &aliases);
} // namespace unllm::native
