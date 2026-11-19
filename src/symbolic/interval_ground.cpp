#include "algorithm_goal.h"
#include "grounding_internal.h"
#include <charconv>
#include <optional>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

namespace unllm::native {
using namespace frames;
using namespace gui;
namespace {
struct Bound {
  std::string variable;
  int value;
  Json span;
};
std::optional<Bound> number(const Json &ep, const icu::UnicodeString &source) {
  const auto span = ep["span"];
  if (span.size() != 2)
    return {};
  int lo = source.moveIndex32(0, span.at(0).integer()),
      hi = source.moveIndex32(0, span.at(1).integer());
  // ERG may attach sentence punctuation to the last endpoint.
  if (hi > lo && source.charAt(hi - 1) == '.')
    --hi;
  if (lo > 0 &&
      (source.charAt(lo - 1) == '-' || source.charAt(lo - 1) == 0x2212) &&
      (lo == 1 || !u_isdigit(source.charAt(lo - 2))))
    --lo;
  std::string text;
  source.tempSubStringBetween(lo, hi).toUTF8String(text);
  if (text.starts_with("−"))
    text.replace(0, 3, "-");
  int n = 0;
  auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), n);
  if (err != std::errc{} || end != text.data() + text.size())
    return {};
  auto var = arg(ep, "ARG0");
  if (var.starts_with('e'))
    var = arg(ep, "ARG1");
  return Bound{var, n, span};
}

Json candidate(const Json &eps, const icu::UnicodeString &source) {
  static const Terms structural{"pron",           "pronoun_q",      "udef_q",
                                "def_implicit_q", "def_explicit_q", "number_q",
                                "generic_entity", "_a_q",           "_the_q"};
  static const Terms nouns{"array",  "sequence", "integer",
                           "number", "sum",      "square"};
  std::map<std::string, Json> objects;
  std::map<std::string, Bound> numbers;
  std::map<std::string, std::string> of;
  std::map<std::string, Json> links;
  Json command;
  for (auto ep : eps.items()) {
    auto p = predicate(ep), w = word(ep);
    if (structural.contains(p))
      continue;
    if (p == "card" || p == "basic_numbered_hour") {
      auto n = number(ep, source);
      if (!n || !numbers.emplace(n->variable, *n).second)
        return {};
    } else if (p.find("_v_") != p.npos &&
               any({w}, {"make", "create", "build", "return", "give",
                         "calculate", "generate"})) {
      if (!command.null())
        return {};
      command = ep;
    } else if (p.find("_n_") != p.npos && nouns.contains(w)) {
      if (!objects.emplace(arg(ep, "ARG0"), ep).second)
        return {};
    } else if (w == "of" && p.find("_p") != p.npos) {
      if (!of.emplace(arg(ep, "ARG1"), arg(ep, "ARG2")).second)
        return {};
    } else if (p == "interval" || p == "interval_p_start" ||
               p == "interval_p_end" ||
               ((w == "from" || w == "to" || w == "through") &&
                p.find("_p") != p.npos)) {
      if (!links.emplace(w, ep).second)
        return {};
    } else
      return {};
  }
  if (command.null() || objects.empty() || numbers.size() != 2 ||
      !links.contains("from"))
    return {};
  auto root = arg(command, "ARG2");
  // Endpoints constrain values; a bare numeric noun does not request a
  // collection or determine which single number the caller intends.
  if (!objects.contains(root) ||
      !any({word(objects.at(root))}, {"array", "sequence", "square", "sum"}))
    return {};
  Terms active{arg(command, "ARG0")}, visited;
  std::string map = "identity", reduce = "collect";
  while (objects.contains(root)) {
    if (!visited.insert(root).second)
      return {};
    active.insert(root);
    auto object = objects.at(root);
    auto w = word(object);
    if (w == "sum") {
      if (visited.size() != 1)
        return {};
      reduce = "sum";
    }
    if (w == "square") {
      if (map != "identity")
        return {};
      map = "square";
    }
    auto next = arg(object, "ARG1");
    if (of.contains(root)) {
      if (objects.contains(next) && next != of[root])
        return {};
      next = of[root];
      of.erase(root);
    }
    root = next;
  }
  if (visited.size() != objects.size() || !of.empty())
    return {};
  auto from = links.at("from");
  if (!active.contains(arg(from, "ARG1")))
    return {};
  auto first = arg(from, "ARG2");
  std::string last;
  if (links.contains("interval")) {
    if (links.size() != 4 || !links.contains("interval_p_start") ||
        !links.contains("interval_p_end"))
      return {};
    auto start = links.at("interval_p_start"), end = links.at("interval_p_end");
    if (first != arg(links.at("interval"), "ARG0") ||
        first != arg(start, "ARG1") || first != arg(end, "ARG1"))
      return {};
    first = arg(start, "ARG2");
    last = arg(end, "ARG2");
  } else {
    if (links.size() != 2)
      return {};
    auto it = links.find("to");
    if (it == links.end())
      it = links.find("through");
    if (it == links.end())
      return {};
    auto target = arg(it->second, "ARG1");
    if (target != first && !active.contains(target))
      return {};
    last = arg(it->second, "ARG2");
  }
  if (first == last || !numbers.contains(first) || !numbers.contains(last))
    return {};
  auto a = numbers.at(first), b = numbers.at(last);
  return obj(
      {{"goal",
        obj({{"program", str("algorithm_goal")},
             {"interval", arr({Json::number(a.value), Json::number(b.value)})},
             {"map", str(map)},
             {"reduce", str(reduce)}})},
       {"endpoint_spans", arr({a.span, b.span})}});
}
} // namespace
Json interval_candidates(const Meaning &meaning, const std::string &text) {
  auto result = Json::array();
  Terms seen;
  auto source = icu::UnicodeString::fromUTF8(text);
  auto readings =
      meaning.analyses.size() ? meaning.analyses : arr({meaning.semantics});
  size_t index = 0;
  for (auto reading : readings.items()) {
    ++index;
    auto entry = candidate(reading, source);
    if (entry.null() || !seen.insert(canonical(entry["goal"])).second)
      continue;
    entry.set("reading", Json::number(index));
    result.push(entry);
  }
  return result;
}
} // namespace unllm::native
