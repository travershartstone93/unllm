#include "grounding_internal.h"
#include "unllm/symbolic/behaviour.h"
#include <algorithm>
#include <charconv>
#include <numeric>
namespace unllm::native::frames {
using namespace gui;
namespace {
std::string title(std::string name) {
  bool start = true;
  for (auto &c : name) {
    if (c == '_') {
      c = ' ';
      start = true;
    } else {
      if (start && c >= 'a' && c <= 'z')
        c -= 32;
      start = false;
    }
  }
  return name;
}
std::string operand(const Graph &g, const Json &ep, std::set<size_t> &used,
                    const Json &fallback) {
  Terms vars;
  for (auto &[k, v] : ep["args"].members())
    vars.insert(v.text());
  std::map<std::string, Terms> aliases;
  for (auto e : g.eps.items())
    if (predicate(e) == "appos") {
      auto a = arg(e, "ARG1"), b = arg(e, "ARG2");
      aliases[a].insert(b);
      aliases[b].insert(a);
    }
  std::set<double> found;
  for (auto prep : g.eps.items())
    if (lemma(predicate(prep)) == "by" && vars.contains(arg(prep, "ARG1"))) {
      auto target = arg(prep, "ARG2");
      Terms targets = aliases[target];
      targets.insert(target);
      for (size_t i = 0; i < g.eps.size(); ++i) {
        auto card = g.eps.at(i);
        if (predicate(card) != "card")
          continue;
        bool linked = false;
        for (auto &[k, v] : card["args"].members())
          linked |= targets.contains(v.text());
        if (!linked)
          continue;
        auto s = card["carg"].text();
        double n;
        auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
        if (ec != std::errc{} || end != s.data() + s.size() ||
            !std::isfinite(n))
          throw Unsupported("Invalid numeric operand");
        found.insert(n);
        used.insert(i);
      }
    }
  if (found.size() > 1)
    throw Ambiguous("More than one amount is linked to the action");
  if (found.empty() && fallback.null())
    throw Unsupported("The action needs an explicit numeric operand");
  return found.empty() ? fallback.text() : canonical(num(*found.begin()));
}
struct Requirements {
  const Graph &g;
  const Json &domain, &vocab;
  Json state = Json::object(), views = Json::array(), actions = Json::array(),
       events = Json::array(), bindings = Json::array(),
       defaults = Json::array();
  std::map<std::string, std::string> targets;
  Requirements(const Graph &graph, const Json &domain_data,
               const Json &vocabulary)
      : g(graph), domain(domain_data), vocab(vocabulary) {}
  void field(const std::string &n, const std::string &type, Json val,
             Json schema = {}) {
    if (state.contains(n))
      throw Ambiguous("Two semantic roles need the same state name");
    auto f = obj({{"type", str(type)}, {"initial", val}});
    if (!schema.null())
      f.set("fields", schema);
    state.set(n, f);
  }
  void view(const std::string &id, const std::string &kind, Json args) {
    args.set("id", str(id));
    args.set("kind", str(kind));
    views.push(args);
  }
  void label(const std::string &id, const std::string &expression,
             const std::string &caption) {
    view(id, "label",
         obj({{"value", str(expression)}, {"label", str(caption)}}));
  }
  void input(const std::string &id, const std::string &kind,
             const std::string &target, const std::string &caption = "") {
    auto args = obj({{"state", str(target)}});
    if (!caption.empty())
      args.set("label", str(caption));
    view(id, kind, args);
  }
  void action(const std::string &name, Json args) {
    for (auto a : actions.items())
      if (a["id"].text() == name)
        return;
    args.set("id", str(name));
    args.set("label", str(title(name)));
    actions.push(args);
  }
  void ensure(const std::string &id, const std::string &formula) {
    action(id, obj({{"ensure", str(formula)}}));
  }
  void effect(const std::string &name, const std::string &effect,
              const std::string &target, Json args = Json::object()) {
    args.set("effect", str(effect));
    args.set("state", str(target));
    if (vocab["SHORTCUTS"].contains(effect))
      args.set("shortcut", vocab["SHORTCUTS"][effect]);
    action(name, args);
  }
  std::string bind(const std::string &op, const Terms &types,
                   const Json &ep = {}) {
    Terms terms, compatible, explicit_;
    for (auto k : {"ARG1", "ARG2", "ARG3", "ARG4"}) {
      auto role = g.terms(arg(ep, k));
      terms.insert(role.begin(), role.end());
    }
    for (auto &[name, kind] : targets)
      if (types.contains(kind)) {
        compatible.insert(name);
        if (terms.contains(name) ||
            (kind == "records" &&
             any(terms, {"list", "collection", "task", "item"})) ||
            (kind == "clock" &&
             any(terms, {"timer", "clock", "elapsed_time"})) ||
            (name == "body" && any(terms, {"text", "document", "editor"})) ||
            (name == "value" && any(terms, {"counter", "number", "value"})))
          explicit_.insert(name);
      }
    auto &candidates = explicit_.empty() ? compatible : explicit_;
    if (candidates.size() != 1)
      throw Ambiguous("Specify the target for " + op);
    auto target = *candidates.begin();
    bindings.push(
        obj({{"operation", str(op)},
             {"state", str(target)},
             {"predicate", str(ep.null() ? "default" : predicate(ep))}}));
    return target;
  }
  void document(bool editor, bool multiline) {
    field("body", "text", str(""));
    targets["body"] = "text";
    input("body_view", editor || multiline ? "text_edit" : "line_edit", "body");
    if (editor) {
      defaults.push(str("Document editing supplies New/Open/Save, history and "
                        "clipboard controls."));
      for (auto op : {"new", "load", "save", "save_as", "undo", "redo", "cut",
                      "copy", "paste", "select_all"})
        effect(op, op, "body");
    }
  }
  void collection() {
    auto schema = obj({{"text", str("text")}});
    for (auto &w : g.words)
      for (auto &[n, t] : domain["concepts"][w]["fields"].members())
        schema.set(n, t);
    field("records", "records", Json::array(), schema);
    targets["records"] = "records";
    field("selection", "number", num(-1));
    std::string display, checked;
    for (auto &[n, t] : schema.members()) {
      auto kind = t.text(), entry = "entry_" + n;
      field(entry, kind,
            kind == "bool"     ? boolean(false)
            : kind == "number" ? num(0)
                               : str(""));
      if (kind != "bool")
        input(entry + "_input", kind == "number" ? "number_input" : "line_edit",
              entry, title(n));
      if (kind == "text" && display.empty())
        display = n;
      if (kind == "bool" && checked.empty())
        checked = n;
    }
    auto args = obj({{"state", str("records")},
                     {"selection", str("selection")},
                     {"display", str(display)}});
    if (!checked.empty())
      args.set("checked", str(checked));
    view("records_view", "record_list", args);
    label("records_count_view", "records_count(records)", "Items");
  }
  void append(const std::string &target) {
    auto values = Json::object(), clear = Json::array();
    std::string guard;
    for (auto &[k, t] : state[target]["fields"].members()) {
      auto n = "entry_" + k;
      if (t.text() == "text") {
        values.set(k, str("strip(" + n + ")"));
        clear.push(str(n));
        if (!guard.empty())
          guard += " and ";
        guard += "chars(strip(" + n + ")) > 0";
      } else
        values.set(k, str(n));
    }
    effect("add", "append_record", target,
           obj({{"selection", str("selection")},
                {"values", values},
                {"clear", clear},
                {"requires", str(guard.empty() ? "True" : guard)}}));
  }
  void clock() {
    field("elapsed", "number", num(0));
    field("anchor", "number", num(0));
    field("running", "bool", boolean(false));
    targets["elapsed"] = "clock";
    label("elapsed_view", "elapsed", "Seconds");
    clock_action("tick", "sample_clock", "elapsed", true);
    events.push(obj({{"kind", str("periodic")},
                     {"interval_ms", Json::number(25)},
                     {"action", str("tick")}}));
  }
  void clock_action(const std::string &id, const std::string &op,
                    const std::string &target, bool hidden = false) {
    auto p = obj({{"anchor", str("anchor")}, {"running", str("running")}});
    if (hidden)
      p.set("visible", boolean(false));
    effect(id, op, target, p);
  }
  void scalar() {
    field("value", "number", num(0));
    targets["value"] = "number";
    input("number_view", "number_input", "value");
    label("number_result", "value", "Value");
  }
  void percentage(const std::string &name) {
    for (auto n : {"amount", "rate", "part", "total"})
      field(n, "number", num(0));
    auto def = domain["concepts"][name];
    input("amount_input", "number_input", "amount", def["base_label"].text());
    input("rate_input", "number_input", "rate", title(name) + " percentage");
    label("part_output", "part", title(name) + " amount");
    label("total_output", "total", "Total");
    auto sign = def["total_sign"];
    ensure("calculate", "100 * next.part == prev.amount * prev.rate and "
                        "next.total == prev.amount " +
                            std::string(sign.integer() == 1 ? "+" : "-") +
                            " next.part");
    bindings.push(obj({{"operation", str("percentage_of")},
                       {"base", str("amount")},
                       {"rate", str("rate")},
                       {"output", str("part")},
                       {"total_sign", sign}}));
  }
  void conversion(const Json &ep) {
    Terms source, target, linked{arg(ep, "ARG0"), arg(ep, "ARG1")};
    auto units = [&](const std::string &v, Terms &out) {
      for (auto &n : g.terms(v))
        if (domain["units"].contains(n))
          out.insert(n);
    };
    for (auto e : g.eps.items())
      if (linked.contains(arg(e, "ARG1"))) {
        auto w = lemma(predicate(e));
        if (w == "from" || w == "to")
          units(arg(e, "ARG2"), w == "from" ? source : target);
      }
    if (source.empty())
      units(arg(ep, "ARG2"), source);
    if (source.size() != 1 || target.size() != 1)
      throw Ambiguous("Specify one source and one target unit");
    auto from = *source.begin(), to = *target.begin();
    auto a = domain["units"][from], b = domain["units"][to];
    if (a["dimension"].text() != b["dimension"].text())
      throw Unsupported("Conversion dimensions differ");
    using Rat = std::pair<int64_t, int64_t>;
    auto ratio = [](const Json &v) -> Rat {
      auto s = v.text();
      auto slash = s.find('/');
      return {std::stoll(s.substr(0, slash)),
              slash == s.npos ? 1 : std::stoll(s.substr(slash + 1))};
    };
    auto [an, ad] = ratio(a["scale"]);
    auto [bn, bd] = ratio(b["scale"]);
    auto [ao, ap] = ratio(a["offset"]);
    auto [bo, bp] = ratio(b["offset"]);
    auto norm = [](int64_t &n, int64_t &d) {
      auto gcd = std::gcd(n, d);
      n /= gcd;
      d /= gcd;
    };
    norm(an, ad);
    norm(bn, bd);
    int64_t off = ao * bp - bo * ap, den = ap * bp;
    norm(off, den);
    auto l = std::lcm(std::lcm(ad, bd), den);
    field("input_value", "number", num(0));
    field("output_value", "number", num(double(off * bd) / (den * bn)));
    input("source_input", "number_input", "input_value", a["label"].text());
    label("converted_output", "output_value", b["label"].text());
    ensure("convert",
           std::to_string(bn * (l / bd)) + " * next.output_value == " +
               std::to_string(an * (l / ad)) + " * prev.input_value + (" +
               std::to_string(off * (l / den)) + ")");
    bindings.push(obj({{"operation", str("convert")},
                       {"source_unit", str(from)},
                       {"target_unit", str(to)},
                       {"dimension", a["dimension"]}}));
  }
  Json result() {
    return BehaviourSpec::from(
               obj({{"schema", Json::number(2)},
                    {"program", str("application")},
                    {"title", str("Application")},
                    {"state", state},
                    {"views", views},
                    {"actions", actions},
                    {"events", events},
                    {"interpretation",
                     obj({{"method",
                           str("ERG semantic frames, typed role binding and "
                               "explicit domain definitions")},
                          {"bindings", bindings},
                          {"defaults", defaults}})}}))
        .data();
  }
};
Json ground(const Graph &g, const Json &domain, const Json &v) {
  auto &words = g.words;
  Terms parts;
  std::vector<Json> conversions;
  bool interactive = false, math = false, creation = false;
  for (auto &w : words) {
    auto t = domain["concepts"][w]["type"].text();
    if (t == "percentage_relation")
      parts.insert(w);
    interactive |= t == "interactive_expression";
    creation |= in(v["CREATE"], w);
  }
  for (auto e : g.eps.items())
    if (predicate(e).find("_v_") != std::string::npos) {
      if (lemma(predicate(e)) == "convert")
        conversions.push_back(e);
      math |= v["MATH_OPS"].contains(word(e));
    }
  if (interactive && parts.empty() && conversions.empty() && !math) {
    if (any(words, {"list", "collection", "timer", "clock", "elapsed_time",
                    "text", "document", "editor", "counter", "number"}))
      throw Unsupported(
          "Specify an arithmetic operation for the combined behaviour goals");
    return calculator_spec(g.eps, v);
  }
  if (!creation)
    throw Unsupported("A construction request needs an explicit creation goal");
  if (any(words, {"without", "only", "no"}))
    throw Unsupported("Scoped exclusions need an explicit supported contract");
  for (auto e : g.eps.items()) {
    auto p = predicate(e), w = word(e);
    if (p == "neg")
      throw Unsupported(
          "Scoped exclusions need an explicit supported contract");
    if (lemma(p) == "operation" && e["lexical_kind"].text() != "action")
      throw Unsupported("An operation label needs an explicit lexical meaning");
    if (p.ends_with("_q") || p == "card" || in(v["STRUCTURAL"], p) ||
        (p == "named" && any({w}, {"basic", "python", "c++", "cpp"})))
      continue;
    bool known =
        domain["concepts"].contains(w) || domain["units"].contains(w) ||
        any({w}, {"start", "stop", "pause", "append", "add", "insert", "remove",
                  "delete", "reset", "select", "all", "duplicate", "calculate",
                  "convert", "operation", "python", "c++", "cpp"});
    for (auto group : {"CREATE", "FUNCTION_WORDS", "ARTIFACTS", "QUALIFIERS",
                       "INTERFACE", "NOUNS"})
      known |= in(v[group], w);
    for (auto group : {"TEXT_OPS", "MATH_OPS", "IO"})
      known |= v[group].contains(w);
    if (!known)
      throw Unsupported("No semantic frame is defined for " + p);
  }
  Requirements r{g, domain, v};
  if (parts.size() > 1 || conversions.size() > 1)
    throw Ambiguous("Specify separate relations");
  if (!parts.empty())
    r.percentage(*parts.begin());
  if (!conversions.empty())
    r.conversion(conversions.front());
  bool coll = any(words, {"list", "collection"}),
       clock = any(words, {"elapsed_time", "timer", "clock"}),
       scalar = any(words, {"counter", "number", "value"}) && parts.empty() &&
                conversions.empty();
  if (coll)
    r.collection();
  if (clock)
    r.clock();
  if (any(words,
          {"text", "document", "editor", "word", "character", "editable"}) ||
      (words.contains("line") && words.contains("count")))
    r.document(words.contains("editor"),
               any(words, {"area", "document", "editor"}));
  if (scalar)
    r.scalar();
  for (auto e : g.eps.items())
    if (predicate(e).find("_n_") != std::string::npos &&
        lemma(predicate(e)) == "count") {
      auto t = g.terms(arg(e, "ARG0"));
      std::vector<std::pair<std::string, std::string>> ops;
      for (auto [noun, op, caption] : {std::tuple{"word", "words", "Words"},
                                       {"character", "chars", "Characters"},
                                       {"line", "lines", "Lines"}})
        if (t.contains(noun))
          ops.emplace_back(op, caption);
      if (ops.size() == 1) {
        auto [op, caption] = ops.front();
        auto target = r.bind(op, {"text"});
        r.label(op + "_view", op + "(" + target + ")", caption);
      } else if (coll && any(t, {"item", "list", "collection", "task"}))
        r.bind("records_count", {"records"}, e);
      else
        throw Unsupported("The count has no uniquely bound quantity");
    }
  bool explicit_number = false, explicit_coll = false, explicit_clock = false;
  std::set<size_t> used;
  for (auto e : g.eps.items()) {
    auto w = word(e), p = predicate(e);
    if (e["lexical_kind"].text() != "action" && p.find("_v_") == p.npos)
      continue;
    if (in(v["CREATE"], w) || in(v["FUNCTION_WORDS"], w) || w == "convert" ||
        w == "calculate")
      continue;
    if (v["TEXT_OPS"].contains(w)) {
      auto op = v["TEXT_OPS"][w].text(), target = r.bind(op, {"text"}, e);
      r.action(
          op, obj({{"assign", obj({{target, str(op + "(" + target + ")")}})}}));
    } else if (v["IO"].contains(w)) {
      auto op = v["IO"][w].text(), target = r.bind(op, {"text", "records"}, e);
      r.effect(op, op, target);
    } else if (coll && any({w}, {"append", "add", "insert"})) {
      r.append(r.bind("append", {"records"}, e));
      explicit_coll = true;
    } else if (coll && any({w}, {"remove", "delete"})) {
      r.effect("remove", "remove_record", r.bind("remove", {"records"}, e),
               obj({{"selection", str("selection")}}));
      explicit_coll = true;
    } else if (clock && any({w}, {"start", "stop", "pause", "reset"})) {
      auto target = r.bind(
          w, w == "reset" && scalar ? Terms{"clock", "number"} : Terms{"clock"},
          e);
      if (r.targets[target] == "number") {
        r.ensure("reset", "next." + target + " == 0");
        explicit_number = true;
      } else {
        auto op = w == "pause" ? "stop" : w;
        r.clock_action(op, op + "_clock", target);
        explicit_clock = true;
      }
    } else if (v["MATH_OPS"].contains(w)) {
      auto target = r.bind(w, {"number"}, e);
      auto math = v["MATH_OPS"][w];
      auto op = math.at(0).text(), value = operand(g, e, used, math.at(1));
      auto name = any({w}, {"increment", "increase"})   ? "increment"
                  : any({w}, {"decrement", "decrease"}) ? "decrement"
                                                        : w;
      r.ensure(name,
               "next." + target + " == " +
                   (op == "reset" ? value
                                  : "prev." + target + " " + op + " " + value));
      explicit_number = true;
    } else
      throw Unsupported("No action frame is defined for " + p);
  }
  if (scalar && !explicit_number) {
    r.ensure("increment", "next.value == prev.value + 1");
    r.ensure("decrement", "next.value == prev.value - 1");
    r.ensure("reset", "next.value == 0");
    r.defaults.push(str("A numeric counter supplies increment/decrement/reset "
                        "when no change action is specified."));
  }
  if (clock && !explicit_clock) {
    for (std::string op : {"start", "stop", "reset"})
      r.clock_action(op, op + "_clock", "elapsed");
    r.defaults.push(
        str("Elapsed-time measurement supplies start/stop/reset controls."));
  }
  if (coll && !explicit_coll) {
    r.append("records");
    r.effect("remove", "remove_record", "records",
             obj({{"selection", str("selection")}}));
    r.defaults.push(str("Editable collections supply insert/remove controls."));
  }
  for (size_t i = 0; i < g.eps.size(); ++i)
    if (predicate(g.eps.at(i)) == "card" && !used.contains(i))
      throw Unsupported("A numeric requirement has no bound role");
  return r.result();
}
} // namespace
} // namespace unllm::native::frames
namespace unllm::native {
Json ground_program(const Meaning &m, const std::filesystem::path &root, Json *trace) {
  using namespace frames;
  if (m.act != "synthesis")
    throw Unsupported("A programming request needs a creation goal");
  auto domain = Json::parse(read_file(root / "data/program_semantics.json")),
       v = Json::parse(read_file(root / "data/program_frame_vocabulary.json"));
  auto analyses = m.analyses.size() ? m.analyses : gui::arr({m.semantics});
  bool gui_request = false;
  for (auto eps : analyses.items())
    for (auto ep : eps.items()) {
      auto w = word(ep), t = domain["concepts"][w]["type"].text();
      gui_request |=
          any({w}, {"app", "application", "window", "interface", "editable",
                    "clock", "collection"}) ||
          any({t}, {"collection", "elapsed_time", "editable_document",
                    "interactive_expression", "numeric_state"});
    }
  std::map<std::string, Json> choices;
  std::set<std::string> rejections, clarifications;
  for (auto eps : analyses.items()) {
    try {
      auto spec =
          gui_request ? ground(Graph(eps), domain, v) : stream_spec(eps, v);
      auto body = spec.clone();
      body.erase("interpretation");
      if (body.contains("actions")) {
        auto actions = body["actions"].items();
        std::sort(actions.begin(), actions.end(),
                  [](const auto &a, const auto &b) {
                    return a["id"].text() < b["id"].text();
                  });
        auto sorted = Json::array();
        for (auto &a : actions)
          sorted.push(a);
        body.set("actions", sorted);
      }
      auto identity = digest(body);
      choices.try_emplace(identity, spec);
      if (trace) trace->push(Json::string("contract " + identity));
    } catch (const Unsupported &e) {
      if (trace) trace->push(Json::string(std::string("unsupported: ") + e.what()));
      rejections.insert(e.what());
    } catch (const Ambiguous &e) {
      if (trace) trace->push(Json::string(std::string("ambiguous: ") + e.what()));
      clarifications.insert(e.what());
    }
  }
  if (choices.empty()) {
    auto reasons = rejections;
    reasons.insert(clarifications.begin(), clarifications.end());
    std::string detail;
    for (const auto &reason : reasons) {
      if (!detail.empty()) detail += " | ";
      detail += reason;
    }
    if (m.exhausted_program_readings)
      throw ResourceLimit("No complete program contract among the first " +
                          std::to_string(m.exhausted_program_readings) +
                          " grammatical readings. Observed reasons: " + detail);
    if (rejections.empty() && !clarifications.empty()) throw Ambiguous(detail);
    if (!detail.empty()) throw Unsupported(detail);
    throw Unsupported("No supported program interpretation");
  }
  if (choices.size() != 1)
    throw Ambiguous("The grammatical readings bind different requirements; "
                    "specify the intended roles");
  return choices.begin()->second;
}
} // namespace unllm::native
