#include "unllm/symbolic/calculator.h"
#include "gui_json.h"
#include "provider_plan.h"
#include <algorithm>
#include <unicode/utf8.h>
namespace unllm::native {
using namespace gui;
namespace {
const Names operations{"add", "subtract", "multiply", "divide"};
}
CalculatorSpec CalculatorSpec::from(const Json &raw) {
  fields_only(raw, {"schema", "program", "title", "operations", "precision",
                    "keyboard", "memory", "backspace", "parentheses"});
  if (raw["program"].text() != "calculator" ||
      (raw.contains("schema") &&
       (!raw["schema"].is_integer() || raw["schema"].integer() != 1)))
    throw Unsupported("Expected a schema 1 calculator");
  CalculatorSpec out;
  std::set<std::string> selected;
  if (raw.contains("operations")) {
    if (!raw["operations"].is_array() || !raw["operations"].size())
      throw Unsupported("Expected a nonempty operation list");
    for (auto op : raw["operations"].items()) {
      auto name = op.text();
      if (std::find(native::operations.begin(), native::operations.end(),
                    name) == native::operations.end() ||
          !selected.insert(name).second)
        throw Unsupported("Unknown or repeated calculator operation");
    }
  }
  for (auto &op : native::operations)
    if (!raw.contains("operations") || selected.contains(op))
      out.operations.push_back(op);
  auto feature = [&](const char *name, bool fallback) {
    if (!raw.contains(name))
      return fallback;
    if (!raw[name].is_boolean())
      throw Unsupported(std::string(name) + " must be Boolean");
    return raw[name].flag();
  };
  out.keyboard = feature("keyboard", true);
  out.backspace = feature("backspace", true);
  out.parentheses = feature("parentheses", true);
  out.memory = feature("memory", false);
  if (raw.contains("precision")) {
    auto n = raw["precision"];
    if (!n.is_integer() || n.integer() < 3 || n.integer() > 15)
      throw Unsupported("Precision must be 3 to 15 significant digits");
    out.precision = n.integer();
  }
  if (raw.contains("title")) {
    out.title = required_text(raw, "title");
    int count = 0;
    auto bytes = reinterpret_cast<const uint8_t *>(out.title.data());
    for (int32_t i = 0, len = out.title.size(); i < len;) {
      UChar32 cp;
      U8_NEXT(bytes, i, len, cp);
      if (cp < 32 || ++count > 80)
        throw Unsupported("Invalid calculator title");
    }
  }
  return out;
}
Json CalculatorSpec::data() const {
  auto ops = Json::array();
  for (auto &name : operations)
    ops.push(str(name));
  return obj({{"program", str("calculator")},
              {"schema", Json::number(1)},
              {"operations", ops},
              {"keyboard", boolean(keyboard)},
              {"backspace", boolean(backspace)},
              {"memory", boolean(memory)},
              {"parentheses", boolean(parentheses)},
              {"precision", Json::number(precision)},
              {"title", str(title)}});
}
Json plan_calculator(const CalculatorSpec &spec, const std::string &lang,
                     NativeEngine &engine, Deadline limit,
                     const std::set<std::string> &disabled) {
  if (lang != "cpp" && lang != "python")
    throw Unsupported("Unknown calculator target");
  ProviderCatalog providers;
  auto add = [&](std::string id, std::string cap, Names needs = {},
                 std::string target = "any", Names accepts = {},
                 Names produces = {}) {
    providers.emplace("app-component:" + id,
                      Provider{cap, target, needs, accepts, produces});
  };
  add("interactive_expression", "app:interactive_expression",
      {"expression:parse", "expression:evaluate", "state:editable",
       "events:dispatch", "ui:window", "ui:display", "ui:grid"});
  add("antlr4", "expression:parse", {}, "any", {"text"}, {"expression_tree"});
  add("checked_arithmetic", "expression:evaluate", {"error:recover"}, "any",
      {"expression_tree"}, {"number"});
  add("editable_state", "state:editable", {}, "any", {"action"},
      {"application_state"});
  add("dispatch", "events:dispatch", {}, "any", {"ui_event"}, {"action"});
  add("recover", "error:recover");
  for (std::string target : {"python", "cpp"}) {
    add("qt_window_" + target, "ui:window", {}, target, {}, {"ui_event"});
    add("qt_display_" + target, "ui:display", {}, target,
        {"application_state"});
    add("qt_grid_" + target, "ui:grid", {}, target);
  }
  for (auto &op : operations)
    add(op, "arithmetic:" + op, {"expression:evaluate"});
  for (std::string feature : {"parentheses", "backspace", "keyboard"})
    add(feature, "control:" + feature, {"state:editable", "events:dispatch"});
  add("memory", "control:memory",
      {"state:numeric_memory", "arithmetic:add", "arithmetic:subtract"});
  add("numeric_memory", "state:numeric_memory",
      {"state:editable", "error:recover"});
  Names needed{"app:interactive_expression"};
  for (auto &op : spec.operations)
    needed.push_back("arithmetic:" + op);
  auto data = spec.data();
  for (std::string f : {"memory", "parentheses", "backspace", "keyboard"})
    if (data[f].flag())
      needed.push_back("control:" + f);
  return checked_provider_plan(providers, needed,
                               {{"expression:parse", "expression:evaluate"},
                                {"ui:window", "events:dispatch"},
                                {"events:dispatch", "state:editable"},
                                {"state:editable", "ui:display"}},
                               lang, disabled,
                               "app-contract:", "application-components-v1",
                               engine, limit);
}
} // namespace unllm::native
