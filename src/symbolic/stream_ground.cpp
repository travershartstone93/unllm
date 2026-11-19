#include "grounding_internal.h"
#include "unllm/symbolic/program.h"
#include <algorithm>
#include <charconv>
namespace unllm::native::frames {
using namespace gui;
namespace {
std::optional<double> literal(const Json &s) {
  if (!s.is_string())
    return {};
  auto text = s.text();
  double v;
  auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
  if (ec != std::errc{} || end != text.data() + text.size() ||
      !std::isfinite(v))
    return {};
  return v;
}
} // namespace
Json stream_spec(const Json &eps, const Json &v) {
  Terms words, outputs, used;
  std::map<std::string, std::string> nouns;
  std::map<std::string, double> literals;
  auto nodes = eps.items();
  for (auto e : nodes) {
    auto p = predicate(e), w = lemma(p);
    words.insert(w);
    if (!in(v["STREAM_STRUCTURAL"], p) && !p.ends_with("_q") &&
        p != "more_comp" && p != "less_comp" && !in(v["STREAM_WORDS"], w) &&
        !w.starts_with("lowercase/"))
      throw Unsupported("Unsupported programming concept: " + p);
    if (p.find("_n_") != p.npos)
      nouns[arg(e, "ARG0")] = w;
    if (auto number = literal(e["carg"]))
      literals[arg(e, "ARG0")] = *number;
  }
  std::string kind = any(words, {"record", "group", "key"})       ? "records"
                     : any(words, {"line", "text", "word"})       ? "lines"
                     : any(words, {"number", "value", "numeric"}) ? "numbers"
                                                                  : "";
  if (kind.empty())
    throw Unsupported("Specify the input type");
  for (auto &[w, out] : std::map<std::string, std::string>{{"sum", "sum"},
                                                           {"total", "sum"},
                                                           {"average", "mean"},
                                                           {"mean", "mean"},
                                                           {"minimum", "min"},
                                                           {"min", "min"},
                                                           {"maximum", "max"},
                                                           {"max", "max"},
                                                           {"count", "count"}})
    if (words.contains(w))
      outputs.insert(out);
  std::string output = "values";
  if (words.contains("statistic") ||
      (outputs.contains("count") && outputs.contains("min") &&
       outputs.contains("max") && outputs.contains("mean")))
    output = "stats";
  else if (any(words, {"frequency", "occurrence"}))
    output = "frequencies";
  else if (outputs.size() == 1)
    output = *outputs.begin();
  else if (!outputs.empty())
    throw Ambiguous("Specify one aggregate or full statistics");
  struct Event {
    int64_t pos;
    std::string op;
    Json ep;
  };
  std::vector<Event> events;
  for (auto e : nodes) {
    auto p = predicate(e), w = lemma(p);
    auto pos = e["span"].size() ? e["span"].at(0).integer() : 0;
    auto event = [&](const std::string &op, Json data) {
      events.push_back({pos, op, data});
    };
    if (any({w}, {"sort", "reverse", "double", "triple", "multiply", "add",
                  "subtract", "strip", "trim"})) {
      if (p.find("_v_") == p.npos)
        throw Unsupported("Operation needs an explicit action: " + w);
      event(w, e);
    }
    if (w == "unique" || w == "deduplicate")
      event("unique", e);
    if (w == "duplicate") {
      if (std::none_of(nodes.begin(), nodes.end(), [&](const auto &r) {
            return lemma(predicate(r)) == "remove" &&
                   arg(r, "ARG2") == arg(e, "ARG0");
          }))
        throw Unsupported("Specify removal of duplicates");
      event("unique", e);
    }
    if (w == "lowercase" || w == "lower" || w.starts_with("lowercase/"))
      event("lowercase", e);
    if (w == "positive" || w == "negative") {
      if (!any({nouns[arg(e, "ARG1")]}, {"number", "value", "record"}))
        throw Unsupported("A numeric condition must modify an input value");
      event("filter", str(w == "positive" ? "x > 0" : "x < 0"));
    }
    if (p == "more_comp" || p == "less_comp") {
      auto id = arg(e, "ARG2");
      std::string cmp;
      for (auto r : nodes)
        if (arg(r, "ARG0") == arg(e, "ARG1")) {
          auto adj = lemma(predicate(r));
          if (any({adj}, {"great", "large", "more"}))
            cmp = ">";
          if (any({adj}, {"small", "less"}))
            cmp = "<";
          break;
        }
      if (cmp.empty() || !literals.contains(id))
        throw Unsupported("A comparison needs an explicit numeric boundary");
      event("filter", str("x " + cmp + " " + canonical(num(literals.at(id)))));
      used.insert(id);
    }
  }
  std::stable_sort(events.begin(), events.end(),
                   [](const auto &a, const auto &b) {
                     return std::tie(a.pos, a.op) < std::tie(b.pos, b.op);
                   });
  auto steps = Json::array();
  if (kind == "lines" && words.contains("word"))
    steps.push(obj({{"op", str("words")}}));
  Terms seen;
  bool filter = false;
  for (auto &[pos, op, e] : events) {
    auto s = obj({{"op", str(op == "trim" ? "strip" : op)}});
    if (op == "sort")
      s.set("descending", boolean(words.contains("descending")));
    if (op == "filter") {
      s.set("expr", e);
      filter = true;
    }
    if (op == "multiply" || op == "add" || op == "subtract" || op == "double" ||
        op == "triple") {
      if (op == "subtract")
        throw Unsupported(
            "Specify subtraction operand order with a typed expression");
      std::string expr;
      if (op == "double" || op == "triple")
        expr = op == "double" ? "x * 2" : "x * 3";
      else {
        std::set<double> vals;
        auto collect = [&](const Json &r) {
          for (auto &[k, val] : r["args"].members())
            if (k != "ARG0" && literals.contains(val.text())) {
              vals.insert(literals.at(val.text()));
              used.insert(val.text());
            }
        };
        collect(e);
        for (auto other : nodes)
          if (arg(other, "ARG1") == arg(e, "ARG0"))
            collect(other);
        if (vals.size() != 1)
          throw Unsupported(
              "Arithmetic action needs one bound numeric argument");
        expr = "x " + std::string(op == "multiply" ? "*" : "+") + " " +
               canonical(num(*vals.begin()));
      }
      s = obj({{"op", str("map")}, {"expr", str(expr)}});
    }
    if (seen.insert(digest(arr({Json::number(pos), s}))).second)
      steps.push(s);
  }
  for (auto &[id, n] : literals)
    if (!used.contains(id))
      throw Unsupported("Numeric requirement has no bound role");
  if (words.contains("ascending") && words.contains("descending"))
    throw Ambiguous("Specify each sort direction");
  if (any(words, {"keep", "filter"}) && !filter)
    throw Unsupported("Filtering needs an explicit condition");
  if (words.contains("remove") &&
      !any(words, {"duplicate", "whitespace", "space"}))
    throw Unsupported("Specify the removal condition");
  if (kind == "lines" && words.contains("empty"))
    throw Unsupported("Empty text removal needs a supported predicate");
  return Program::from(
             obj({{"input", str(kind)},
                  {"steps", steps},
                  {"output", str(output)},
                  {"invalid", str(words.contains("invalid") &&
                                          any(words, {"skip", "ignore"})
                                      ? "skip"
                                      : "error")},
                  {"assumptions",
                   arr({str("Input is printable ASCII and tabs; numbers are "
                            "finite decimals within +/-1e15."),
                        str("Records use key TAB number. Results are written "
                            "to standard output."),
                        str("A single optional filename may replace standard "
                            "input.")})}}))
      .data();
}
Json calculator_spec(const Json &eps, const Json &v) {
  auto nodes = eps.items();
  auto features = v["CALCULATOR_FEATURES"];
  Terms words, neg, labels;
  std::vector<double> numbers;
  for (auto e : nodes) {
    auto p = predicate(e), w = lemma(p);
    words.insert(w);
    if (!in(v["CALCULATOR_STRUCTURAL"], p) && !p.ends_with("_q") &&
        p != "card" && !in(v["CALCULATOR_WORDS"], w))
      throw Unsupported("Unsupported calculator requirement: " + p);
    if (w == "no")
      neg.insert(arg(e, "ARG0"));
    if (p == "neg")
      for (auto l : e["scope"].items())
        labels.insert(l.text());
    if (auto n = literal(e["carg"]))
      numbers.push_back(*n);
  }
  if (!words.contains("calculator"))
    throw Unsupported("No calculator requirement");
  for (auto e : nodes)
    if (labels.contains(e["label"].text())) {
      auto w = lemma(predicate(e));
      if (in(v["CREATE"], w))
        throw Unsupported("Negated creation goal");
      if (any({w}, {"support", "include", "enable", "allow", "have"}))
        neg.insert(arg(e, "ARG2"));
    }
  auto expand = [&](Terms &vars) {
    size_t before;
    do {
      before = vars.size();
      for (auto e : nodes) {
        if (predicate(e) == "compound" && vars.contains(arg(e, "ARG1")))
          vars.insert(arg(e, "ARG2"));
        if (any({predicate(e)}, {"_and_c", "implicit_conj"}) &&
            vars.contains(arg(e, "ARG0")))
          for (auto &[k, val] : e["args"].members())
            if (k != "ARG0")
              vars.insert(val.text());
      }
    } while (before != vars.size());
  };
  expand(neg);
  for (auto e : nodes) {
    auto w = lemma(predicate(e));
    if (w == "no" || w == "without" || w == "except") {
      Terms targets{arg(e, w == "no" ? "ARG0" : "ARG2")};
      expand(targets);
      if (std::none_of(nodes.begin(), nodes.end(), [&](auto n) {
            return targets.contains(arg(n, "ARG0")) &&
                   features.contains(lemma(predicate(n)));
          }))
        throw Unsupported("Excluded calculator feature has no supported scope");
      neg.insert(targets.begin(), targets.end());
    }
  }
  Terms enabled, disabled;
  for (auto e : nodes) {
    auto w = lemma(predicate(e));
    if (features.contains(w))
      (neg.contains(arg(e, "ARG0")) || labels.contains(e["label"].text())
           ? disabled
           : enabled)
          .insert(features[w].text());
  }
  if (!labels.empty() && disabled.empty()) throw Unsupported("Negative requirement has no supported feature scope");
  for (auto &w : enabled)
    if (disabled.contains(w))
      throw Ambiguous("Calculator feature is both required and excluded");
  auto ops = Json::array();
  for (auto op : {"add", "subtract", "multiply", "divide"})
    if (!disabled.contains(op) &&
        (!words.contains("only") || enabled.contains(op)))
      ops.push(str(op));
  if (!ops.size())
    throw Unsupported("Select at least one arithmetic operation");
  auto result = obj({{"program", str("calculator")},
                     {"schema", Json::number(1)},
                     {"title", str("Calculator")},
                     {"operations", ops},
                     {"precision", Json::number(12)}});
  for (std::string n : {"memory", "parentheses", "backspace", "keyboard"})
    result.set(n, boolean(!disabled.contains(n) &&
                          (enabled.contains(n) || n != "memory")));
  if (!numbers.empty()) {
    double n = numbers.front();
    if (numbers.size() != 1 || n < 3 || n > 15 || std::floor(n) != n ||
        !any(words, {"precision", "significant"}))
      throw Unsupported("Unsupported calculator precision");
    result.set("precision", Json::number(n));
  }
  return result;
}
} // namespace unllm::native::frames
