#include "unllm/symbolic/behaviour.h"
#include <algorithm>
#include <set>
#include <unicode/utf8.h>
namespace unllm::native {
namespace {
void require(bool ok, const std::string &message) {
  if (!ok)
    throw Unsupported(message);
}
size_t text_length(const Json &v) {
  require(v.is_string(), "Text required");
  auto s = v.text();
  size_t count = 0;
  for (int32_t i = 0; i < static_cast<int32_t>(s.size());) {
    UChar32 cp;
    U8_NEXT(s.data(), i, static_cast<int32_t>(s.size()), cp);
    require(cp >= 0, "Invalid UTF-8 text");
    ++count;
  }
  return count;
}
std::string identifier(const Json &v) {
  require(v.is_string(), "Identifier required");
  auto s = v.text();
  auto letter = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  };
  require(!s.empty() && s.size() <= 48 && letter(s[0]) &&
              std::all_of(s.begin(), s.end(),
                          [&](char c) {
                            return letter(c) || (c >= '0' && c <= '9') ||
                                   c == '_';
                          }),
          "Identifiers must be short ASCII names");
  return s;
}
bool scalar(const Json &v, const std::string &type) {
  if (type == "text") {
    if (!v.is_string())
      return false;
    text_length(v);
    return true;
  }
  if (type == "bool")
    return v.is_boolean();
  return type == "number" && v.is_number() && std::isfinite(v.real()) &&
         std::abs(v.real()) <= 1e15;
}
bool record(const Json &v, const Json &schema) {
  if (!v.is_object() || v.size() != schema.size())
    return false;
  for (auto &[name, type] : schema.members())
    if (!v.contains(name) || !scalar(v[name], type.text()))
      return false;
  return true;
}
Json list_default(Json &data, const std::string &name, size_t bound) {
  if (!data.contains(name))
    data.set(name, Json::array());
  auto result = data[name];
  require(result.is_array() && result.size() <= bound,
          name + " budget exceeded");
  return result;
}
int transition_degree(const BehaviourValue &v, const Json &state,
                      bool &output) {
  if (v.kind == "literal")
    return 0;
  if (v.kind == "prev" || v.kind == "next") {
    require(state[v.name]["type"].text() == "number",
            "Scalar transitions require numeric states");
    if (v.kind == "next")
      output = true;
    return 1;
  }
  require(v.kind != "primitive",
          "Scalar transitions do not support primitive calls");
  if (v.kind == "binary" && v.name == "divide")
    require(v.args[1].kind == "literal" && v.args[1].value.is_number() &&
                v.args[1].value.real() != 0,
            "Formal transition division needs a nonzero literal denominator");
  int degree = 0;
  for (auto &a : v.args) {
    int n = transition_degree(a, state, output);
    degree = v.kind == "binary" && v.name == "multiply" ? degree + n
                                                        : std::max(degree, n);
  }
  return degree;
}
void record_shapes(const BehaviourValue &v, const Json &state,
                   const Json &schema) {
  if (v.kind == "records")
    return;
  if (v.kind == "state") {
    require(canonical(state[v.name]["fields"]) == canonical(schema),
            "Record schemas differ across assignment");
    return;
  }
  require(v.kind == "conditional", "Unknown record-producing expression");
  record_shapes(v.args[1], state, schema);
  record_shapes(v.args[2], state, schema);
}
} // namespace
BehaviourSpec BehaviourSpec::from(const Json &value) {
  fields_only(value, {"program", "schema", "title", "state", "views", "actions",
                      "events", "checks", "interpretation"});
  auto data = value.clone();
  if (!data.contains("schema"))
    data.set("schema", Json::number(2));
  require(data["schema"].is_integer() && data["schema"].integer() == 2,
          "Behaviour specifications use schema 2");
  if (!data.contains("program"))
    data.set("program", Json::string("application"));
  if (!data.contains("title"))
    data.set("title", data["program"]);
  for (auto name : {"program", "title"}) {
    size_t n = text_length(data[name]);
    auto text = data[name].text();
    require(n >= 1 && n <= 100 &&
                std::none_of(text.begin(), text.end(),
                             [](unsigned char c) { return c < 32; }),
            "Invalid application label");
  }
  auto state = data["state"];
  require(state.is_object() && state.size() >= 1 && state.size() <= 32,
          "Provide 1 to 32 typed state fields");
  for (auto &[name, field] : state.members()) {
    identifier(Json::string(name));
    fields_only(field, {"type", "initial", "fields"});
    auto kind = field["type"].text();
    auto initial = field["initial"];
    bool valid = scalar(initial, kind);
    if (kind == "text" && valid)
      valid = text_length(initial) <= 1000000;
    if (kind == "records") {
      auto schema = field["fields"];
      require(schema.is_object() && schema.size() >= 1 && schema.size() <= 16,
              "A record collection needs typed fields");
      for (auto &[key, type] : schema.members()) {
        identifier(Json::string(key));
        require(type.text() == "number" || type.text() == "text" ||
                    type.text() == "bool",
                "Unsupported record field type");
      }
      valid = initial.is_array() && initial.size() <= 10000;
      if (valid)
        for (auto row : initial.items())
          valid = record(row, schema) && valid;
    } else
      require(!field.contains("fields"),
              "Only record collections declare fields");
    require(valid, "Invalid initial state: " + name);
  }
  auto type = [&](const Json &name) {
    return name.is_string() ? state[name.text()]["type"].text() : "";
  };
  auto parse = [&](const Json &source, bool transition = false) {
    return behaviour_expression(source, state, transition);
  };
  auto views = list_default(data, "views", 48);
  std::set<std::string> ids, names, shortcuts;
  for (auto view : views.items()) {
    fields_only(view, {"id", "kind", "state", "value", "label", "selection",
                       "display", "checked"});
    auto id = identifier(view["id"]);
    require(ids.insert(id).second, "Repeated view identifier");
    auto kind = view["kind"].text();
    if (kind == "text_edit" || kind == "line_edit" || kind == "number_input") {
      require(type(view["state"]) ==
                  (kind == "number_input" ? "number" : "text"),
              "View/state types differ");
      require(!view.contains("value"),
              "An input view binds a state, not a display expression");
    } else if (kind == "record_list") {
      require(type(view["state"]) == "records" &&
                  type(view["selection"]) == "number",
              "A record view needs a collection and numeric selection");
      auto schema = state[view["state"].text()]["fields"];
      require(view["display"].is_string() &&
                  schema[view["display"].text()].text() == "text",
              "Record display field must be text");
      if (view.contains("checked"))
        require(view["checked"].is_string() &&
                    schema[view["checked"].text()].text() == "bool",
                "Check field must be Boolean");
    } else if (kind == "label") {
      require(!view.contains("state") && view.contains("value"),
              "A label needs a value expression");
      parse(view["value"]);
    } else
      throw Unsupported("Unknown widget primitive: " + kind);
    if (view.contains("label"))
      text_length(view["label"]);
  }
  auto actions = list_default(data, "actions", 48);
  const std::set<std::string> effects{
      "load",        "save",        "save_as",       "new",
      "undo",        "redo",        "cut",           "copy",
      "paste",       "select_all",  "append_record", "remove_record",
      "set_record",  "start_clock", "stop_clock",    "reset_clock",
      "sample_clock"};
  for (auto action : actions.items()) {
    fields_only(action, {"id", "label", "ensure", "assign", "effect", "state",
                         "shortcut", "values", "selection", "clear", "running",
                         "anchor", "visible", "requires"});
    auto id = identifier(action["id"]);
    require(!ids.contains(id) && names.insert(id).second,
            "Repeated action/view identifier");
    if (action.contains("label"))
      text_length(action["label"]);
    if (action.contains("visible"))
      require(action["visible"].is_boolean(),
              "Action visibility must be Boolean");
    if (action.contains("requires"))
      require(parse(action["requires"]).type == "bool",
              "An action precondition must be Boolean");
    require(int(action.contains("ensure")) + int(action.contains("assign")) +
                    int(action.contains("effect")) ==
                1,
            "An action needs one outcome contract");
    if (!action["shortcut"].null()) {
      auto n = text_length(action["shortcut"]);
      require(n >= 1 && n <= 32 &&
                  shortcuts.insert(action["shortcut"].text()).second,
              "Invalid/repeated shortcut");
    }
    if (action.contains("ensure")) {
      auto e = parse(action["ensure"], true);
      bool output = false;
      require(e.type == "bool" &&
                  transition_degree(e.value, state, output) <= 2 && output,
              "A transition must constrain numeric next states with degree at "
              "most two");
    } else if (action.contains("assign")) {
      auto assignments = action["assign"];
      require(assignments.is_object() && assignments.size(),
              "Provide assignment outcomes");
      for (auto &[target, source] : assignments.members()) {
        auto e = parse(source);
        require(type(Json::string(target)) == e.type,
                "Assignment outcome type mismatch");
        if (e.type == "records")
          record_shapes(e.value, state, state[target]["fields"]);
      }
    } else {
      auto target = action["state"];
      auto effect = action["effect"].text();
      require(effects.contains(effect) && !type(target).empty(),
              "Unknown effect or incompatible state");
      if (effect == "load" || effect == "save" || effect == "save_as" ||
          effect == "new")
        require(type(target) == "text" || type(target) == "records",
                "Persistence needs text or records");
      if (effect == "undo" || effect == "redo" || effect == "cut" ||
          effect == "copy" || effect == "paste" || effect == "select_all") {
        bool editable = false;
        for (auto v : views.items())
          if (v["state"].text() == target.text() &&
              (v["kind"].text() == "text_edit" ||
               v["kind"].text() == "line_edit"))
            editable = true;
        require(editable, "Edit effect needs an editable view");
      }
      if (effect == "append_record" || effect == "remove_record" ||
          effect == "set_record") {
        require(type(target) == "records", "Collection effects need records");
        if (effect != "append_record" || action.contains("selection"))
          require(type(action["selection"]) == "number",
                  "Collection change needs numeric selection");
        auto values =
            action.contains("values") ? action["values"] : Json::object();
        auto schema = state[target.text()]["fields"];
        require(values.is_object(), "Unknown record fields");
        for (auto &[key, source] : values.members()) {
          require(schema.contains(key), "Unknown record field");
          require(parse(source).type == schema[key].text(),
                  "Record field type mismatch");
        }
        if (effect == "append_record")
          require(values.size() == schema.size(),
                  "Append must supply every record field");
        if (effect == "set_record")
          require(values.size(), "Provide fields to update");
        auto clear = action.contains("clear") ? action["clear"] : Json::array();
        require(clear.is_array(), "Clear targets must be a list");
        for (auto name : clear.items())
          require(type(name) == "text", "Clear targets must be text states");
      }
      if (effect.ends_with("_clock"))
        require(type(target) == "number" &&
                    type(action["anchor"]) == "number" &&
                    type(action["running"]) == "bool",
                "Clock effects need elapsed/anchor numbers and a running flag");
    }
  }
  for (auto event : list_default(data, "events", 8).items()) {
    fields_only(event, {"kind", "interval_ms", "action"});
    require(event["kind"].text() == "periodic" &&
                event["interval_ms"].is_integer() &&
                event["interval_ms"].integer() >= 10 &&
                event["interval_ms"].integer() <= 60000 &&
                names.contains(event["action"].text()),
            "Invalid periodic event binding");
  }
  list_default(data, "checks", 64);
  return BehaviourSpec(data);
}
} // namespace unllm::native
