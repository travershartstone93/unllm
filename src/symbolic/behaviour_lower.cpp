#include "behaviour_requirements.h"
#include "unllm/symbolic/behaviour.h"
#include <format>
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
struct BehaviourBuilder {
  const Json &domain, &vocab;
  Json state = Json::object(), views = Json::array(), actions = Json::array(),
       events = Json::array(), bindings = Json::array(),
       defaults = Json::array();

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
      if (a["id"].text() == name) {
        auto existing = a.clone();
        existing.erase("id");
        existing.erase("label");
        if (canonical(existing) != canonical(args))
          throw Ambiguous("Conflicting requirements for " + name);
        return;
      }
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
  void binding(const std::string &op, const std::string &target,
               const Json &source) {
    bindings.push(obj(
        {{"operation", str(op)},
         {"state", str(target)},
         {"predicate", source.null() ? str("default") : source["predicate"]}}));
  }
  void document(bool editor, bool multiline) {
    field("body", "text", str(""));
    input("body_view", editor || multiline ? "text_edit" : "line_edit", "body");
  }
  void editor_controls() {
    defaults.push(str("Document editing supplies New/Open/Save, history and "
                      "clipboard controls."));
    for (auto op : {"new", "load", "save", "save_as", "undo", "redo", "cut",
                    "copy", "paste", "select_all"})
      effect(op, op, "body");
  }
  void collection(const Json &schema) {
    field("records", "records", Json::array(), schema);
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
  void scalar(double initial) {
    field("value", "number", num(initial));
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
  void conversion(const std::string &from, const std::string &to) {
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
std::string storage(Entity entity) {
  switch (entity) {
  case Entity::counter:
    return "value";
  case Entity::clock:
    return "elapsed";
  case Entity::collection:
    return "records";
  case Entity::document:
    return "body";
  }
  throw std::logic_error("Unknown entity storage");
}
} // namespace
Json lower(const BehaviourRequirements &requirements, const Json &domain,
           const Json &vocab) {
  std::optional<double> initial;
  for (const auto &node : requirements.nodes) {
    if (node.status == Status::excluded || node.status == Status::unresolved)
      throw Unsupported(std::get<ScopeConstraint>(node.meaning).reason);
    if (auto *value = std::get_if<InitialValue>(&node.meaning)) {
      if (initial && *initial != value->value)
        throw Ambiguous("Conflicting initial values for the counter");
      initial = value->value;
    }
  }
  if (requirements.entities.contains(Entity::counter) && !initial)
    throw Unsupported("A counter needs an initial value");
  BehaviourBuilder b{domain, vocab};
  for (auto &node : requirements.nodes) {
    const auto &meaning = node.meaning;
    auto source = node.evidence.size() ? node.evidence.at(0) : Json{};
    if (auto *state = std::get_if<StateRequirement>(&meaning)) {
      switch (state->entity) {
      case Entity::document:
        b.document(state->editor, state->multiline);
        break;
      case Entity::collection:
        b.collection(state->fields);
        break;
      case Entity::clock:
        b.clock();
        break;
      case Entity::counter:
        b.scalar(initial.value());
        break;
      }
    } else if (auto *percent = std::get_if<Percentage>(&meaning)) {
      b.percentage(percent->relation);
    } else if (auto *units = std::get_if<Conversion>(&meaning)) {
      b.conversion(units->from, units->to);
    } else if (auto *metric = std::get_if<Metric>(&meaning)) {
      auto &op = metric->operation;
      auto target = storage(metric->target);
      b.binding(op, target, op == "records_count" ? source : Json{});
      if (op != "records_count") {
        auto caption = op == "words"   ? "Words"
                       : op == "chars" ? "Characters"
                                       : "Lines";
        b.label(op + "_view", op + "(" + target + ")", caption);
      }
    } else if (auto *action = std::get_if<Action>(&meaning)) {
      auto &op = action->operation;
      auto target = storage(action->target);
      b.binding(action->verb, target, source);
      switch (action->kind) {
      case ActionKind::transform:
        b.action(op, obj({{"assign",
                           obj({{target, str(op + "(" + target + ")")}})}}));
        break;
      case ActionKind::io:
        b.effect(op, op, target);
        break;
      case ActionKind::append:
        b.append(target);
        break;
      case ActionKind::remove:
        b.effect(op, "remove_record", target,
                 obj({{"selection", str("selection")}}));
        break;
      case ActionKind::clock:
        b.clock_action(op, op + "_clock", target);
        break;
      case ActionKind::numeric:
        throw std::logic_error("Numeric action requires an operand");
      }
    } else if (auto *change = std::get_if<Change>(&meaning)) {
      const auto &action = change->action;
      auto target = storage(action.target);
      b.binding(action.verb, target, source);
      auto value = change->amount_status == Status::justified_default
                       ? std::format("{}", change->amount)
                       : canonical(num(change->amount));
      auto name = any({action.verb}, {"increment", "increase"})   ? "increment"
                  : any({action.verb}, {"decrement", "decrease"}) ? "decrement"
                                                                  : action.verb;
      auto rhs = action.operation == "reset"
                     ? value
                     : "prev." + target + " " + action.operation + " " + value;
      b.ensure(name, "next." + target + " == " + rhs);
    } else if (auto *controls = std::get_if<DefaultControls>(&meaning)) {
      switch (controls->target) {
      case Entity::document:
        b.editor_controls();
        break;
      case Entity::counter:
        b.ensure("increment", "next.value == prev.value + 1");
        b.ensure("decrement", "next.value == prev.value - 1");
        b.ensure("reset", "next.value == 0");
        b.defaults.push(
            str("A numeric counter supplies increment/decrement/reset when no "
                "change action is specified."));
        break;
      case Entity::clock:
        for (std::string op : {"start", "stop", "reset"})
          b.clock_action(op, op + "_clock", "elapsed");
        b.defaults.push(str(
            "Elapsed-time measurement supplies start/stop/reset controls."));
        break;
      case Entity::collection:
        b.append("records");
        b.effect("remove", "remove_record", "records",
                 obj({{"selection", str("selection")}}));
        b.defaults.push(
            str("Editable collections supply insert/remove controls."));
        break;
      }
    }
  }
  return b.result();
}
} // namespace unllm::native::frames
