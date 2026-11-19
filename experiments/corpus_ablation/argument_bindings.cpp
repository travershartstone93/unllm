#include "unllm/symbolic/common.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using unllm::native::Json;
bool digit(char c) { return c >= '0' && c <= '9'; }
bool word(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Integers in the request are proposal evidence. Examples decide which call
// fits.
std::vector<int> literals(std::string_view text) {
  std::vector<int> found;
  for (size_t i = 0; i < text.size() && found.size() < 4;) {
    size_t start = i;
    if (text[i] == '-' && i + 1 < text.size() && digit(text[i + 1]))
      ++i;
    if (!digit(text[i])) {
      ++i;
      continue;
    }
    while (i < text.size() && digit(text[i]))
      ++i;
    if ((start && (word(text[start - 1]) || text[start - 1] == '.')) ||
        (i < text.size() &&
         (word(text[i]) ||
          (text[i] == '.' && i + 1 < text.size() && digit(text[i + 1])))))
      continue;
    int value = 0;
    auto result = std::from_chars(text.data() + start, text.data() + i, value);
    if (result.ec == std::errc{} && value >= -1024 && value <= 1024 &&
        std::find(found.begin(), found.end(), value) == found.end())
      found.push_back(value);
  }
  return found;
}

std::string arguments(const std::vector<std::string> &values) {
  std::string result;
  for (const auto &value : values) {
    if (!result.empty())
      result += ", ";
    result += value;
  }
  return result;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 5) {
    std::cerr << "usage: corpus_argument_bindings python|cpp FUNCTION ARITY "
                 "REQUEST\n";
    return 2;
  }
  std::string lang = argv[1], name = argv[2], count = argv[3];
  int arity = 0;
  auto parsed =
      std::from_chars(count.data(), count.data() + count.size(), arity);
  if ((lang != "cpp" && lang != "python") || name.empty() ||
      digit(name.front()) || !std::all_of(name.begin(), name.end(), word) ||
      parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size() ||
      arity < 1 || arity > 4)
    return 2;
  std::vector<std::string> params, formals;
  for (int i = 0; i < arity; ++i) {
    auto p = "binding_arg" + std::to_string(i);
    params.push_back(p);
    formals.push_back(lang == "cpp" ? "auto&& " + p : p);
  }
  auto wrapper =
      name == "unllm_bound_call" ? "unllm_bound_call_1" : "unllm_bound_call";
  auto plans = Json::array();
  for (int literal : literals(argv[4]))
    for (int slot = 0; slot <= arity; ++slot) {
      auto actual = params;
      actual.insert(actual.begin() + slot, std::to_string(literal));
      auto call = name + "(" + arguments(actual) + ")";
      auto code = lang == "cpp"
                      ? "\ninline constexpr auto " + std::string(wrapper) +
                            " = [](" + arguments(formals) +
                            ") -> decltype(auto) { return " + call + "; };\n"
                      : "\ndef " + std::string(wrapper) + "(" +
                            arguments(formals) + "):\n    return " + call +
                            "\n";
      auto plan = Json::object();
      plan.set("entry", Json::string(wrapper));
      plan.set("code", Json::string(code));
      plan.set("slot", Json::number(slot));
      plan.set("literal", Json::number(literal));
      plans.push(plan);
    }
  std::cout << plans.dump() << '\n';
}
