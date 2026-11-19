#include "behaviour_requirements.h"

#include <algorithm>
#include <charconv>

namespace unllm::native::frames {
using namespace gui;
namespace {
std::optional<double> linked_amount(const Graph &g, const Json &ep,
                                    const std::string &relation,
                                    std::set<size_t> &used, Json &sources,
                                    std::set<size_t> &prepositions) {
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
  for (size_t j = 0; j < g.eps.size(); ++j) {
    const auto prep = g.eps.at(j);
    if (lemma(predicate(prep)) == relation &&
        (relation == "at" ? arg(ep, "ARG0") == arg(prep, "ARG1")
                          : vars.contains(arg(prep, "ARG1")))) {
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
        if (card.contains("literal_sign"))
          n = card["literal_sign"].text() == "-" ? -std::abs(n) : std::abs(n);
        found.insert(n);
        if (!used.insert(i).second)
          throw Ambiguous("A literal is linked to multiple numeric roles");
        prepositions.insert(j);
        sources.push(evidence(prep));
        sources.push(evidence(card));
      }
    }
  }
  if (found.size() > 1)
    throw Ambiguous("More than one amount is linked to the action");
  if (found.empty())
    return {};
  return *found.begin();
}
Conversion unit_roles(const Graph &g, const Json &domain, const Json &ep,
                      Json &sources) {
  Terms source, target, linked{arg(ep, "ARG0"), arg(ep, "ARG1")};
  auto units = [&](const std::string &v, Terms &out) {
    for (auto &n : g.terms(v))
      if (domain["units"].contains(n))
        out.insert(n);
  };
  for (auto e : g.eps.items())
    if (linked.contains(arg(e, "ARG1"))) {
      auto w = lemma(predicate(e));
      if (w == "from" || w == "to") {
        units(arg(e, "ARG2"), w == "from" ? source : target);
        sources.push(evidence(e));
      }
    }
  if (source.empty())
    units(arg(ep, "ARG2"), source);
  if (source.size() != 1 || target.size() != 1)
    throw Ambiguous("Specify one source and one target unit");
  auto from = *source.begin(), to = *target.begin();
  return {from, to};
}
Json ground(const Graph &g, const Json &domain, const Json &v,
            BehaviourRequirements &r) {
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
  auto sources_for = [&](const Terms &terms) {
    Json sources = Json::array();
    for (auto ep : g.eps.items())
      if (terms.contains(word(ep)))
        sources.push(evidence(ep));
    return sources;
  };
  for (auto ep : g.eps.items())
    if (in(v["CREATE"], word(ep))) {
      r.add(Intent{}, "creation_predicate", arr({evidence(ep)}));
      break;
    }
  for (auto e : g.eps.items()) {
    auto p = predicate(e), w = word(e);
    if (p == "neg" || any({w}, {"without", "only", "no"})) {
      r.add(
          ScopeConstraint{
              "Scoped exclusions need an explicit supported contract"},
          "scope_operator", arr({evidence(e)}), Status::excluded);
      return lower(r, domain, v);
    }
  }
  for (auto e : g.eps.items()) {
    auto p = predicate(e), w = word(e);
    if (lemma(p) == "at")
      continue; // Every such relation must bind below.
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
  if (parts.size() > 1 || conversions.size() > 1)
    throw Ambiguous("Specify separate relations");
  if (!parts.empty())
    r.add(Percentage{*parts.begin()}, "percentage_relation",
          sources_for(parts));
  if (!conversions.empty()) {
    auto sources = arr({evidence(conversions.front())});
    auto units = unit_roles(g, domain, conversions.front(), sources);
    for (const auto &e : sources_for({units.from, units.to}).items())
      sources.push(e);
    r.add(units, "conversion_roles", sources);
  }
  const Terms collection_words{"list", "collection"},
      clock_words{"elapsed_time", "timer", "clock"},
      counter_words{"counter", "number", "value"},
      document_words{"text",      "document", "editor", "word",
                     "character", "editable", "line"};
  auto has = [&](const Terms &terms) {
    return std::any_of(terms.begin(), terms.end(),
                       [&](const auto &w) { return words.contains(w); });
  };
  bool coll = has(collection_words), clock = has(clock_words),
       scalar = has(counter_words) && parts.empty() && conversions.empty();
  if (coll) {
    auto fields = obj({{"text", str("text")}}),
         sources = sources_for(collection_words);
    for (auto &w : words) {
      auto declared = domain["concepts"][w]["fields"];
      if (declared.size())
        for (auto ep : sources_for({w}).items())
          sources.push(ep);
      for (auto &[n, t] : declared.members())
        fields.set(n, t);
    }
    r.add(StateRequirement{Entity::collection, false, false, fields},
          "record_collection", sources);
  }
  if (clock)
    r.add(StateRequirement{Entity::clock}, "elapsed_time",
          sources_for(clock_words));
  if (any(words,
          {"text", "document", "editor", "word", "character", "editable"}) ||
      (words.contains("line") && words.contains("count"))) {
    bool editor = words.contains("editor");
    r.add(StateRequirement{Entity::document, editor,
                           any(words, {"area", "document", "editor"})},
          "editable_text", sources_for(document_words));
    if (editor)
      r.add(DefaultControls{Entity::document}, "editor_controls",
            sources_for({"editor"}), Status::justified_default);
  }
  if (scalar)
    r.add(StateRequirement{Entity::counter}, "numeric_state",
          sources_for(counter_words));
  for (auto ep : g.eps.items()) {
    if (predicate(ep).find("_n_") == std::string::npos ||
        lemma(predicate(ep)) != "count")
      continue;
    auto nouns = g.terms(arg(ep, "ARG0"));
    std::vector<std::string> metrics;
    for (auto [noun, op] : {std::pair{"word", "words"},
                            {"character", "chars"},
                            {"line", "lines"}})
      if (nouns.contains(noun))
        metrics.emplace_back(op);
    if (metrics.size() == 1) {
      auto target = r.bind(g, ep, metrics.front(), {Entity::document});
      auto sources = arr({evidence(ep)});
      for (auto e : g.eps.items())
        if (predicate(e) == "compound" && arg(e, "ARG1") == arg(ep, "ARG0"))
          sources.push(evidence(e));
      r.add(Metric{target, metrics.front()}, "count_quantity", sources);
    } else if (coll && any(nouns, {"item", "list", "collection", "task"})) {
      r.add(Metric{Entity::collection, "records_count"}, "count_records",
            arr({evidence(ep)}));
    } else
      throw Unsupported("The count has no uniquely bound quantity");
  }
  std::set<Entity> with_controls;
  std::set<size_t> used, prepositions;
  bool explicit_initial = false;
  for (auto ep : g.eps.items()) {
    auto w = word(ep), p = predicate(ep);
    if ((ep["lexical_kind"].text() != "action" && p.find("_v_") == p.npos) ||
        in(v["CREATE"], w) || in(v["FUNCTION_WORDS"], w) || w == "convert" ||
        w == "calculate")
      continue;
    auto sources = arr({evidence(ep)});
    if (scalar && w == "start" && p.find("_v_") != p.npos &&
        any(g.terms(arg(ep, "ARG1")), {"counter", "number", "value"})) {
      if (auto value =
              linked_amount(g, ep, "at", used, sources, prepositions)) {
        r.add(InitialValue{*value}, "start_at_amount", sources);
        explicit_initial = true;
        continue;
      }
    }
    auto add_action = [&](ActionKind kind, const std::string &verb,
                          const std::string &op,
                          const std::set<Entity> &types) {
      auto target = r.bind(g, ep, verb, types);
      r.add(Action{target, kind, verb, op}, "action_target", sources);
      if (kind == ActionKind::append || kind == ActionKind::remove ||
          kind == ActionKind::clock)
        with_controls.insert(target);
    };
    if (v["TEXT_OPS"].contains(w)) {
      auto op = v["TEXT_OPS"][w].text();
      add_action(ActionKind::transform, op, op, {Entity::document});
    } else if (v["IO"].contains(w)) {
      auto op = v["IO"][w].text();
      add_action(ActionKind::io, op, op,
                 {Entity::document, Entity::collection});
    } else if (coll && any({w}, {"append", "add", "insert"}))
      add_action(ActionKind::append, "append", "append", {Entity::collection});
    else if (coll && any({w}, {"remove", "delete"}))
      add_action(ActionKind::remove, "remove", "remove", {Entity::collection});
    else if (clock && any({w}, {"start", "stop", "pause", "reset"}) &&
             (w != "reset" || !scalar ||
              r.bind(g, ep, w, {Entity::clock, Entity::counter}) ==
                  Entity::clock))
      add_action(ActionKind::clock, w, w == "pause" ? "stop" : w,
                 {Entity::clock});
    else if (v["MATH_OPS"].contains(w)) {
      auto target = r.bind(g, ep, w, {Entity::counter});
      auto math = v["MATH_OPS"][w];
      auto amount = linked_amount(g, ep, "by", used, sources, prepositions);
      if (!amount && math.at(1).null())
        throw Unsupported("The action needs an explicit numeric operand");
      auto value = amount ? *amount : Json::parse(math.at(1).text()).real();
      r.add(Change{{target, ActionKind::numeric, w, math.at(0).text()},
                   value,
                   amount ? Status::explicit_ : Status::justified_default},
            "numeric_transition", sources);
      with_controls.insert(target);
    } else
      throw Unsupported("No action frame is defined for " + p);
  }
  if (scalar && !explicit_initial)
    r.add(InitialValue{0}, "counter_initial_zero", sources_for(counter_words),
          Status::justified_default);
  for (auto [entity, terms] : {std::pair{Entity::counter, counter_words},
                               {Entity::clock, clock_words},
                               {Entity::collection, collection_words}})
    if (r.entities.contains(entity) && !with_controls.contains(entity))
      r.add(DefaultControls{entity}, "default_controls", sources_for(terms),
            Status::justified_default);
  for (size_t i = 0; i < g.eps.size(); ++i) {
    auto ep = g.eps.at(i);
    if ((predicate(ep) == "card" && !used.contains(i)) ||
        (any({lemma(predicate(ep))}, {"at", "by"}) &&
         !prepositions.contains(i)))
      r.add(ScopeConstraint{"A numeric requirement has no bound role"},
            "unbound_numeric_role", arr({evidence(ep)}), Status::unresolved);
  }
  return lower(r, domain, v);
}
} // namespace
} // namespace unllm::native::frames
namespace unllm::native {
Json ground_program(const Meaning &m, const std::filesystem::path &root,
                    Json *trace, Json *requirement_readings) {
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
    BehaviourRequirements requirements;
    try {
      auto spec = gui_request ? ground(Graph(eps), domain, v, requirements)
                              : stream_spec(eps, v);
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
      auto [choice, inserted] = choices.try_emplace(identity, spec);
      if (inserted && !requirements.nodes.empty())
        choice->second["interpretation"].set("requirements",
                                             requirements.describe());
      if (trace)
        trace->push(Json::string("contract " + identity));
    } catch (const Unsupported &e) {
      if (trace)
        trace->push(Json::string(std::string("unsupported: ") + e.what()));
      rejections.insert(e.what());
    } catch (const Ambiguous &e) {
      if (trace)
        trace->push(Json::string(std::string("ambiguous: ") + e.what()));
      clarifications.insert(e.what());
    }
    if (requirement_readings)
      requirement_readings->push(requirements.describe());
  }
  if (choices.empty()) {
    auto reasons = rejections;
    reasons.insert(clarifications.begin(), clarifications.end());
    std::string detail;
    for (const auto &reason : reasons) {
      if (!detail.empty())
        detail += " | ";
      detail += reason;
    }
    if (m.exhausted_program_readings)
      throw ResourceLimit("No complete program contract among the first " +
                          std::to_string(m.exhausted_program_readings) +
                          " grammatical readings. Observed reasons: " + detail);
    if (rejections.empty() && !clarifications.empty())
      throw Ambiguous(detail);
    if (!detail.empty())
      throw Unsupported(detail);
    throw Unsupported("No supported program interpretation");
  }
  if (choices.size() != 1)
    throw Ambiguous("The grammatical readings bind different requirements; "
                    "specify the intended roles");
  return choices.begin()->second;
}
} // namespace unllm::native
