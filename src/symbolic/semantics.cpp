#include "unllm/symbolic/semantics.h"
#include "unllm/symbolic/grounding.h"
#include <algorithm>
#include <set>
#include <tuple>
namespace unllm::native {
namespace {
std::string get(const std::map<std::string, std::string> &values,
                const std::string &name, const std::string &fallback = "") {
  auto it = values.find(name);
  return it == values.end() ? fallback : it->second;
}
std::string arg(const Predication &e, const std::string &name) {
  return get(e.args, name);
}
bool has(const std::string &s, const std::string &part) {
  return s.find(part) != std::string::npos;
}
bool oneof(const std::string &s, std::initializer_list<const char *> values) {
  return std::any_of(values.begin(), values.end(),
                     [&](auto v) { return s == v; });
}
bool same(const Goal &a, const Goal &b) {
  return std::tie(a.predicate, a.subject, a.object, a.negative) ==
         std::tie(b.predicate, b.subject, b.object, b.negative);
}
Json strings(const std::vector<std::string> &values) {
  auto out = Json::array();
  for (auto &v : values)
    out.push(Json::string(v));
  return out;
}
Json goals_data(const std::vector<Goal> &goals) {
  auto out = Json::array();
  for (auto &g : goals)
    out.push(g.data());
  return out;
}
} // namespace
const std::map<std::string, std::string> &type_words() {
  static const std::map<std::string, std::string> words{
      {"person", "person"},   {"people", "person"},
      {"country", "country"}, {"city", "city"},
      {"place", "place"},     {"book", "work"},
      {"novel", "work"},      {"work", "work"},
      {"program", "program"}, {"application", "program"},
      {"library", "program"}, {"component", "entity"},
      {"thing", "entity"}};
  return words;
}
std::string lemma(const std::string &p) {
  if (!p.starts_with('_'))
    return p;
  auto end = p.find('_', 1);
  return end == std::string::npos ? p : p.substr(1, end - 1);
}
Json Meaning::data() const {
  auto out = Json::object();
  out.set("act", Json::string(act));
  out.set("goals", goals_data(goals));
  out.set("assumptions", goals_data(assumptions));
  out.set("select", strings(select));
  out.set("count", Json::boolean(count));
  out.set("explain", Json::boolean(explain));
  out.set("temporal", Json::string(temporal));
  auto names = Json::object();
  for (auto &[id, name] : labels)
    names.set(id, Json::string(name));
  out.set("labels", names);
  out.set("semantics", semantics);
  out.set("analyses", analyses);
  out.set("diagnostics", strings(diagnostics));
  return out;
}
std::string Meaning::signature() const {
  std::map<std::string, std::string> variables;
  auto normalize = [&](const std::string &value) {
    if (!value.starts_with('?'))
      return value;
    auto it = variables.find(value);
    if (it != variables.end())
      return it->second;
    auto name = "?v" + std::to_string(variables.size());
    variables[value] = name;
    return name;
  };
  auto converted = [&](std::vector<Goal> values) {
    auto order = [](const Goal &g) {
      return std::make_tuple(
          g.predicate, g.subject.starts_with('?') ? "?" : g.subject,
          g.object.starts_with('?') ? "?" : g.object, g.negative);
    };
    std::stable_sort(values.begin(), values.end(),
                     [&](auto &a, auto &b) { return order(a) < order(b); });
    auto out = Json::array();
    for (auto &g : values) {
      auto row = Json::array();
      row.push(Json::string(g.predicate));
      row.push(Json::string(normalize(g.subject)));
      row.push(Json::string(normalize(g.object)));
      row.push(Json::boolean(g.negative));
      out.push(row);
    }
    return out;
  };
  auto out = Json::object();
  out.set("act", Json::string(act));
  out.set("goals", converted(goals));
  out.set("assumptions", converted(assumptions));
  std::vector<std::string> selected;
  for (auto &s : select)
    selected.push_back(normalize(s));
  std::sort(selected.begin(), selected.end());
  out.set("select", strings(selected));
  out.set("count", Json::boolean(count));
  out.set("temporal", Json::string(temporal));
  return digest(out);
}
Meaning translate(const Mrs &mrs, const std::string &text,
                  const Resolver &resolve,
                  const std::vector<std::string> &focus, bool programming) {
  auto &eps = mrs.rels;
  if (eps.empty())
    throw Unsupported("Empty grammatical analysis.");
  auto variable = [&](const std::string &id, const std::string &property,
                      const std::string &fallback = "") {
    auto it = mrs.variables.find(id);
    return it == mrs.variables.end() ? fallback
                                     : get(it->second, property, fallback);
  };
  std::set<std::string> wh, how, counted;
  bool why = false, creation = false;
  for (auto &e : eps) {
    if (oneof(e.predicate, {"which_q", "_which_q"}) && e.args.contains("ARG0"))
      wh.insert(arg(e, "ARG0"));
    if (e.predicate == "_how_x_comp")
      how.insert(arg(e, "ARG1"));
    why |= e.predicate == "reason";
    creation |= oneof(lemma(e.predicate),
                      {"write", "create", "generate", "build", "make"}) &&
                has(e.predicate, "_v_");
  }
  for (auto &e : eps)
    if (e.predicate == "much-many_a" && how.contains(arg(e, "ARG0")))
      counted.insert(arg(e, "ARG1"));
  wh.insert(counted.begin(), counted.end());
  if (why)
    for (auto &e : eps)
      if (e.predicate == "reason")
        wh.erase(arg(e, "ARG0"));
  auto force = variable(mrs.index, "SF", "prop");
  auto end = text.find_last_not_of(" \t\r\n");
  bool question = force == "ques" || !wh.empty() || why ||
                  (end != std::string::npos && text[end] == '?');
  std::map<std::string, std::string> hq;
  for (auto h : mrs.hcons.items())
    hq[h["hi"].text()] = h["lo"].text();
  auto scope = [&](const std::string &handle) {
    std::set<std::string> seen;
    std::vector<std::string> todo{handle};
    while (!todo.empty()) {
      auto h = todo.back();
      todo.pop_back();
      if (!seen.insert(h).second)
        continue;
      if (hq.contains(h))
        todo.push_back(hq.at(h));
      for (auto &e : eps)
        if (e.label == h)
          for (auto &[k, v] : e.args)
            if (k != "ARG0" && v.starts_with('h'))
              todo.push_back(v);
    }
    return seen;
  };
  auto semantics = [&](bool construction) {
    auto out = Json::array();
    for (auto &e : eps) {
      auto row = e.data();
      row.set("carg",
              e.args.contains("CARG") ? Json::string(arg(e, "CARG")) : Json{});
      if (construction) {
        auto scopes = Json::array();
        for (auto &[k, v] : e.args)
          if (k != "ARG0" && v.starts_with('h'))
            for (auto &label : scope(v))
              scopes.push(Json::string(label));
        row.set("scope", scopes);
      } else
        row.erase("span");
      out.push(row);
    }
    return out;
  };
  if (programming || (force == "comm" && creation)) {
    Meaning out;
    out.act = "synthesis";
    out.semantics = semantics(true);
    return out;
  }
  for (auto &e : eps)
    if (oneof(lemma(e.predicate), {"every", "all", "no", "neither", "or"}))
      throw Unsupported("This quantified or disjunctive interpretation needs a "
                        "completeness/scope model not yet supported.");
  std::vector<std::set<std::string>> negatives;
  std::vector<const Predication *> conditional;
  for (auto &e : eps) {
    if (e.predicate == "neg")
      negatives.push_back(scope(arg(e, "ARG1")));
    if (oneof(e.predicate, {"_if_x", "_if_x_then", "_how+about_x"}))
      conditional.push_back(&e);
  }
  if (conditional.size() > 1)
    throw Unsupported("Nested conditional scope is not supported.");
  std::set<std::string> assumption_scope;
  if (!conditional.empty()) {
    auto &e = *conditional[0];
    assumption_scope = scope(
        arg(e, oneof(e.predicate, {"_if_x_then", "_how+about_x"}) ? "ARG1"
                                                                  : "ARG2"));
  }
  std::map<std::string, std::string> parent;
  std::function<std::string(const std::string &)> find =
      [&](const std::string &x) {
        if (!parent.contains(x))
          parent[x] = x;
        if (parent[x] != x)
          parent[x] = find(parent[x]);
        return parent[x];
      };
  auto unite = [&](const std::string &a, const std::string &b) {
    auto x = find(a), y = find(b);
    if (x != y)
      parent[y] = x;
  };
  std::map<std::string, std::vector<std::pair<int64_t, std::string>>> names;
  std::map<std::string, std::vector<std::string>> nouns;
  std::vector<std::string> noun_order, name_order;
  for (auto &e : eps) {
    auto v = arg(e, "ARG0");
    if (e.predicate == "named" && !arg(e, "CARG").empty()) {
      if (!names.contains(v))
        name_order.push_back(v);
      names[v].push_back(
          {e.span.null() ? 0 : e.span.at(0).integer(), arg(e, "CARG")});
    }
    if (has(e.predicate, "_n_")) {
      if (!nouns.contains(v))
        noun_order.push_back(v);
      nouns[v].push_back(lemma(e.predicate));
    }
  }
  std::map<std::string, std::string> owners;
  auto owner = [&](const std::string &entity, const std::string &who) {
    if (owners.contains(entity) && owners[entity] != who)
      throw Unsupported(
          "A relational noun has incompatible argument attachments.");
    owners[entity] = who;
  };
  for (auto &e : eps)
    if (oneof(e.predicate, {"_of_p", "poss"}))
      owner(arg(e, "ARG1"), arg(e, "ARG2"));
  for (auto &e : eps)
    if (has(e.predicate, "_n_") && arg(e, "ARG1").starts_with('x'))
      owner(arg(e, "ARG0"), arg(e, "ARG1"));
  struct Raw {
    std::string p, s, o;
    const Predication *ep;
  };
  std::vector<Raw> raw;
  std::set<const Predication *> consumed;
  std::set<std::string> literals;
  auto add = [&](const std::string &p, const std::string &s,
                 const std::string &o, const Predication &e) {
    raw.push_back({p, s, o, &e});
    consumed.insert(&e);
  };
  for (auto &e : eps)
    if (e.predicate == "compound" && names.contains(arg(e, "ARG1")) &&
        names.contains(arg(e, "ARG2"))) {
      unite(arg(e, "ARG1"), arg(e, "ARG2"));
      consumed.insert(&e);
    }
  auto types = [&](const std::string &v) {
    std::vector<std::string> result;
    for (auto &w : nouns[v])
      if (type_words().contains(w))
        result.push_back(type_words().at(w));
    return result;
  };
  for (auto &e : eps)
    if (e.predicate == "_be_v_id") {
      auto a = arg(e, "ARG1"), b = arg(e, "ARG2");
      auto at = types(a), bt = types(b);
      if (!bt.empty() && !owners.contains(b) && !names.contains(b) &&
          !wh.contains(a)) {
        add("type", a, "@" + bt[0], e);
        literals.insert(b);
      } else if (!at.empty() && !owners.contains(a) && !names.contains(a) &&
                 !wh.contains(b)) {
        add("type", b, "@" + at[0], e);
        literals.insert(a);
      } else
        unite(a, b);
      consumed.insert(&e);
    }
  auto any_relation = [&](const std::string &p) {
    return std::any_of(raw.begin(), raw.end(),
                       [&](auto &g) { return g.p == p; });
  };
  for (auto &e : eps) {
    auto &p = e.predicate;
    auto word = lemma(p), a0 = arg(e, "ARG0"), a1 = arg(e, "ARG1"),
         a2 = arg(e, "ARG2");
    if (oneof(p, {"_in_p_loc", "loc_nonsp"})) {
      const Predication *birth = nullptr;
      for (auto &b : eps)
        if (arg(b, "ARG0") == a1 &&
            oneof(lemma(b.predicate), {"born", "bear"})) {
          birth = &b;
          break;
        }
      auto subject = a1;
      if (birth) {
        if (has(birth->predicate, "_v_")) {
          if (arg(*birth, "ARG1").starts_with('x'))
            throw Unsupported(
                "The explicit birth-event agent cannot be represented.");
          subject = arg(*birth, "ARG2");
        } else
          subject = arg(*birth, "ARG1");
        consumed.insert(birth);
      }
      add(birth ? "birthplace" : "located_in", subject, a2, e);
    } else if (has(p, "_v_") && oneof(word, {"depend", "require", "need"})) {
      if (a2.starts_with('x'))
        add("depends_on", a1, a2, e);
    } else if (has(p, "_v_") && word == "write")
      add("authored", a1, a2, e);
    else if (has(p, "_v_") && word == "contain")
      add("contains", a1, a2, e);
    else if (word == "available" && has(p, "_a_"))
      add("available", a1, "@true", e);
    else if (oneof(word, {"blocked", "block"}) &&
             (has(p, "_a_") || has(p, "_v_"))) {
      if (has(p, "_v_") && a1.starts_with('x'))
        throw Unsupported(
            "An explicit blocking agent is outside the availability rule.");
      add("blocked", has(p, "_v_") ? a2 : a1, "@true", e);
    } else if (has(p, "_n_") &&
               oneof(word, {"author", "birthplace", "country", "capital",
                            "height", "population"})) {
      if (owners.contains(a0) && !owners[a0].empty())
        add(word, owners[a0], a0, e);
    } else if (oneof(word, {"born", "bear"}) && any_relation("birthplace"))
      consumed.insert(&e);
  }
  for (auto &e : eps)
    if (e.predicate == "more_comp")
      for (auto &a : eps)
        if (arg(a, "ARG0") == arg(e, "ARG1") && a.predicate == "_tall_a_1" &&
            arg(e, "ARG2").starts_with('x')) {
          add("taller_than", arg(a, "ARG1"), arg(e, "ARG2"), e);
          consumed.insert(&a);
          break;
        }
  for (auto &v : noun_order) {
    if (literals.contains(v)) {
      for (auto &e : eps)
        if (arg(e, "ARG0") == v && has(e.predicate, "_n_") &&
            type_words().contains(lemma(e.predicate)))
          consumed.insert(&e);
      continue;
    }
    for (auto &word : nouns[v])
      if (type_words().contains(word) && type_words().at(word) != "entity") {
        bool used = std::any_of(eps.begin(), eps.end(), [&](auto &e) {
          return consumed.contains(&e) && arg(e, "ARG0") == v;
        });
        if (!used)
          for (auto &e : eps)
            if (arg(e, "ARG0") == v && has(e.predicate, "_n_")) {
              raw.push_back({"type", v, "@" + type_words().at(word), &e});
              break;
            }
        for (auto &e : eps)
          if (arg(e, "ARG0") == v && lemma(e.predicate) == word)
            consumed.insert(&e);
      }
  }
  const std::set<std::string> structural{"named",
                                         "proper_q",
                                         "pronoun_q",
                                         "pron",
                                         "which_q",
                                         "_which_q",
                                         "udef_q",
                                         "def_explicit_q",
                                         "def_implicit_q",
                                         "_the_q",
                                         "_a_q",
                                         "_an_q",
                                         "thing",
                                         "person",
                                         "place_n",
                                         "reason",
                                         "neg",
                                         "_be_v_id",
                                         "_of_p",
                                         "poss",
                                         "_and_c",
                                         "implicit_conj",
                                         "_if_x",
                                         "_if_x_then",
                                         "_how+about_x",
                                         "unknown"};
  for (auto &e : eps) {
    if (consumed.contains(&e) || structural.contains(e.predicate))
      continue;
    if (e.predicate == "_for_p" && why)
      continue;
    if (e.predicate == "_on_p_state" && any_relation("depends_on"))
      continue;
    if (e.predicate == "generic_verb" && assumption_scope.contains(e.label) &&
        !conditional.empty() && conditional[0]->predicate == "_how+about_x" &&
        e.args.size() == 2 && e.args.contains("ARG0") &&
        e.args.contains("ARG1"))
      continue;
    if (oneof(e.predicate, {"_how_x_comp", "much-many_a", "generic_entity"})) {
      if (e.predicate != "generic_entity" && counted.empty())
        throw Unsupported(
            "An unbounded quantity needs an explicit scope model.");
      continue;
    }
    throw Unsupported("Unsupported semantic content: " + e.predicate);
  }
  if (raw.empty()) {
    if (why) {
      Meaning out;
      out.act = "explain";
      out.explain = true;
      return out;
    }
    throw Unsupported("The analysis contains no supported relation.");
  }
  std::map<std::string, std::vector<std::pair<int64_t, std::string>>>
      canonical_names;
  std::vector<std::string> root_order;
  for (auto &v : name_order) {
    auto root = find(v);
    if (!canonical_names.contains(root))
      root_order.push_back(root);
    auto &parts = canonical_names[root];
    parts.insert(parts.end(), names[v].begin(), names[v].end());
  }
  std::map<std::string, std::string> bound;
  Meaning out;
  for (auto &v : root_order) {
    auto parts = canonical_names[v];
    std::sort(parts.begin(), parts.end());
    std::string label;
    for (auto &[_, part] : parts) {
      if (!label.empty())
        label += ' ';
      label += part;
    }
    auto id = resolve(label, !question);
    bound[v] = id;
    out.labels[id] = label;
  }
  for (auto &e : eps)
    if (e.predicate == "pron" && variable(arg(e, "ARG0"), "PERS") == "3") {
      std::set<std::string> identities(focus.begin(), focus.end());
      if (identities.size() != 1)
        throw Ambiguous(
            "The pronoun has no unique referent. Please name the entity.");
      bound[find(arg(e, "ARG0"))] = focus[0];
    }
  auto term = [&](const std::string &value) {
    if (value.empty())
      throw Unsupported("A required grammatical argument is missing.");
    if (value.starts_with('@'))
      return value.substr(1);
    if (!oneof(value.substr(0, 1), {"x", "i", "u"}))
      throw Unsupported(
          "This event/scope argument cannot be used as an entity.");
    auto root = find(value);
    return bound.contains(root) ? bound[root] : "?" + root;
  };
  for (auto &g : raw) {
    bool negative =
        std::any_of(negatives.begin(), negatives.end(),
                    [&](auto &s) { return s.contains(g.ep->label); });
    Goal goal{g.p, term(g.s), term(g.o), negative};
    auto &target =
        assumption_scope.contains(g.ep->label) ? out.assumptions : out.goals;
    if (!std::any_of(target.begin(), target.end(),
                     [&](auto &x) { return same(x, goal); }))
      target.push_back(goal);
  }
  std::set<std::string> used, question_terms;
  for (auto &g : out.goals) {
    used.insert(g.subject);
    used.insert(g.object);
    question_terms.insert(g.subject);
    question_terms.insert(g.object);
  }
  for (auto &g : out.assumptions) {
    used.insert(g.subject);
    used.insert(g.object);
  }
  for (auto &[_, id] : bound)
    if (!used.contains(id))
      throw Unsupported(
          "A named entity is disconnected from the supported interpretation.");
  if (!conditional.empty() && out.assumptions.empty())
    throw Unsupported("The conditional premise could not be represented "
                      "without dropping its scope.");
  for (auto &scope : negatives) {
    std::set<std::tuple<std::string, std::string, std::string>> affected;
    for (auto &g : raw)
      if (scope.contains(g.ep->label))
        affected.insert({g.p, term(g.s), term(g.o)});
    if (affected.size() > 1)
      throw Unsupported("Negation over multiple propositions needs a richer "
                        "scope representation.");
  }
  for (auto &g : raw)
    if (std::count_if(negatives.begin(), negatives.end(), [&](auto &scope) {
          return scope.contains(g.ep->label);
        }) > 1)
      throw Unsupported("Nested negation needs a richer scope representation.");
  bool past = std::any_of(eps.begin(), eps.end(), [&](auto &e) {
    return variable(arg(e, "ARG0"), "TENSE") == "past";
  });
  if (past) {
    for (auto &g : out.goals)
      if (!oneof(g.predicate, {"birthplace", "authored", "author", "type"}))
        throw Unsupported("A historical state needs an explicit time; current "
                          "facts cannot answer it.");
    out.temporal = "record";
  }
  std::set<std::string> selected;
  for (auto &v : wh)
    if (term(v).starts_with('?'))
      selected.insert(term(v));
  if (selected.empty())
    for (auto &v : wh)
      selected.insert(term(v));
  for (auto &v : selected) {
    if (!question_terms.contains(v)) {
      if (conditional.empty())
        throw Unsupported("The question's requested value is disconnected from "
                          "the supported relations.");
    } else
      out.select.push_back(v);
  }
  out.act =
      conditional.empty() ? (question ? "query" : "assert") : "hypothetical";
  if (out.act == "assert")
    for (auto &g : out.goals)
      if (g.subject.starts_with('?') || g.object.starts_with('?'))
        throw Unsupported("Assertions must identify their entities; generic "
                          "rules require a declared rule module.");
  out.count = !counted.empty();
  out.explain = why;
  out.semantics = semantics(false);
  return out;
}
Meaning SemanticParser::parse(const std::string &source,
                              const Resolver &resolve,
                              const std::vector<std::string> &focus,
                              Deadline deadline, bool programming) {
  auto start = std::chrono::steady_clock::now();
  ProgramText linked{source};
  const bool program_request = programming || creation_request(source);
  if (program_request) {
    if (program_lexicon_.null())
      program_lexicon_ =
          Json::parse(read_file(root_ / "data/program_semantics.json"));
    linked = normalize_program(source, program_lexicon_);
  }
  auto &text = linked.text;
  // ACE's zero-weight unpacker breaks ties differently after previous inputs.
  // Give each program request its own bounded enumeration, including fallback.
  int limit = program_request ? program_maximum_ : maximum_;
  auto attempt = [&](bool fragment) {
    std::unique_ptr<AceClient> fresh;
    auto &client = program_request ? fresh : (fragment ? fragment_ : normal_);
    if (!client)
      client = std::make_unique<AceClient>(root_, programming && !fragment ? 4 : 1,
                                           limit, fragment ? "root_inffrag" : "root_informal");
    try {
      auto result = client->parse(text, deadline);
      if (observe_parse) observe_parse(text, linked.spans, fragment, result);
      if (!result.error.empty() && program_request) {
        if (result.error.starts_with("timeout") ||
            result.error.find("ran out of RAM") != std::string::npos)
          throw ResourceLimit("ACE could not complete program enumeration: " + result.error);
        throw std::runtime_error("ACE program parser error: " + result.error);
      }
      return result;
    } catch (...) {
      client.reset();
      throw;
    }
  };
  auto response = attempt(false);
  bool fragment = response.analyses.empty();
  if (fragment) response = attempt(true);
  last_ms += std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - start)
                 .count();
  if (response.analyses.empty())
    throw Unsupported(
        "The grammar could not analyze this request within its budget.");
  std::vector<Meaning> candidates, programs;
  std::set<std::string> signatures;
  std::exception_ptr first;
  for (auto &mrs : response.analyses) {
    try {
      auto m = translate(mrs, text, resolve, focus, programming);
      if (linked.spans.size())
        annotate_program(m.semantics, linked.spans);
      if (observe_translation) observe_translation(m.act);
      if (m.act == "synthesis")
        programs.push_back(m);
      else if (signatures.insert(m.signature()).second)
        candidates.push_back(m);
    } catch (const Unsupported &e) {
      if (observe_translation) observe_translation(std::string("unsupported: ") + e.what());
      if (!first)
        first = std::current_exception();
    } catch (const Ambiguous &e) {
      if (observe_translation) observe_translation(std::string("ambiguous: ") + e.what());
      if (!first)
        first = std::current_exception();
    }
  }
  if (!programs.empty()) {
    auto m = programs[0];
    if (response.reached_candidate_limit) {
      m.exhausted_program_readings = limit;
      m.diagnostics.push_back("Program reading limit reached: " + std::to_string(limit));
    }
    for (auto &p : programs)
      m.analyses.push(p.semantics);
    m.diagnostics.push_back("ACE/ERG; " + std::to_string(programs.size()) +
                            " program analyses; zero ranking weights");
    m.diagnostics.push_back(programming
                                ? "Explicit code grammar budget: 4 seconds"
                                : "Normal grammar budget: 1 second");
    return m;
  }
  if (candidates.empty()) {
    if (first)
      std::rethrow_exception(first);
    throw Unsupported("No supported interpretation.");
  }
  if (candidates.size() > 1)
    throw Ambiguous("The grammar found different supported meanings. Please "
                    "make the relation or reference explicit.");
  auto m = candidates[0];
  m.diagnostics = {"ACE/ERG; zero parse-ranking weights; " +
                       std::to_string(response.analyses.size()) + " analyses",
                   "semantic translation accepted 1 distinct meaning"};
  if (fragment)
    m.diagnostics.push_back(
        "fragment grammar used after no full-sentence parse");
  if (response.reached_candidate_limit)
    m.diagnostics.push_back("parse enumeration reached its configured bound");
  return m;
}
} // namespace unllm::native
