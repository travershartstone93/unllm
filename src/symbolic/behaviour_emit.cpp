#include "unllm/symbolic/behaviour_emit.h"
#include <cstdio>
namespace unllm::native {
namespace {
using Lines = std::vector<std::string>;
std::string join(const Lines &items, const std::string &sep = ", ") {
  std::string out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i)
      out += sep;
    out += items[i];
  }
  return out;
}
std::string q(const std::string &s, bool py) {
  if (py)
    return canonical(Json::string(s));
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += char(c);
    } else if (c < 32 || c == 127) {
      char octal[5];
      std::snprintf(octal, sizeof(octal), "\\%03o", c);
      out += octal;
    } else
      out += char(c);
  }
  out += '"';
  if (s.find('\0') != std::string::npos)
    return "std::string(" + out + ", " + std::to_string(s.size()) + ")";
  return out;
}
std::string literal(const Json &v, bool py) {
  if (v.is_string())
    return q(v.text(), py);
  if (v.null())
    return py ? "None" : "nullptr";
  if (v.is_boolean())
    return v.flag() ? (py ? "True" : "true") : (py ? "False" : "false");
  if (v.is_number())
    return py ? canonical(v) : canonical(Json::real(v.real()));
  if (!py)
    return std::string(v.is_array() ? "parse_array(" : "parse_object(") +
           q(canonical(v), false) + ")";
  Lines items;
  if (v.is_array()) {
    for (auto item : v.items())
      items.push_back(literal(item, true));
    return "[" + join(items) + "]";
  }
  for (auto &[key, item] : v.members())
    items.push_back(q(key, true) + ": " + literal(item, true));
  return "{" + join(items) + "}";
}
const std::map<std::string, std::string> types{{"number", "double"},
                                               {"bool", "bool"},
                                               {"text", "std::string"},
                                               {"records", "QJsonArray"}};
struct Rendered {
  std::string code, type;
};
class Emitter {
  bool py;
  const BehaviourGraph &graph;
  Json spec, fields;
  Lines lines;
  std::string self, term;
  std::string quoted(const std::string &s) const { return q(s, py); }
  std::string prop(const Json &j, const std::string &key,
                   const std::string &fallback = "") const {
    return quoted(j[key].text(fallback));
  }
  std::string state(const std::string &n) const { return self + "state_" + n; }
  void line(int depth, const std::string &code) {
    lines.push_back(std::string(4 * depth, ' ') + code);
  }
  Rendered expr(const BehaviourValue &v) const {
    if (v.kind == "records")
      return {py ? "[]" : "QJsonArray{}", "records"};
    if (v.kind == "literal") {
      auto s = literal(v.value, py);
      if (v.value.is_string())
        return {py ? s : "std::string(" + s + ")", "text"};
      return {s, v.value.is_boolean() ? "bool" : "number"};
    }
    if (v.kind == "state")
      return {state(v.name), fields[v.name]["type"].text()};
    std::vector<Rendered> values;
    Lines args;
    for (auto &child : v.args) {
      values.push_back(expr(child));
      args.push_back(values.back().code);
    }
    if (v.kind == "primitive") {
      bool direct = v.name == "number_text" || v.name == "records_count" ||
                    v.name == "count_true";
      bool numeric = v.name == "words" || v.name == "chars" ||
                     v.name == "lines" || v.name == "records_count" ||
                     v.name == "count_true";
      return {(direct ? "" : "text_") + v.name + "(" + join(args) + ")",
              numeric ? "number" : "text"};
    }
    if (v.kind == "conditional")
      return {"(" +
                  (py ? args[1] + " if " + args[0] + " else " + args[2]
                      : args[0] + " ? " + args[1] + " : " + args[2]) +
                  ")",
              values[1].type};
    if (v.kind == "unary")
      return {"(" +
                  std::string(v.name == "not"   ? (py ? "not " : "!")
                              : v.name == "neg" ? "-"
                                                : "+") +
                  args.at(0) + ")",
              values[0].type};
    if (v.kind != "binary")
      throw Unsupported("Unexpected behaviour expression node");
    if (v.name == "divide")
      return {"checked_divide(" + join(args) + ")", "number"};
    static const std::map<std::string, std::string> signs{
        {"add", "+"}, {"subtract", "-"}, {"multiply", "*"}, {"eq", "=="},
        {"ne", "!="}, {"lt", "<"},       {"le", "<="},      {"gt", ">"},
        {"ge", ">="}, {"and", "&&"},     {"or", "||"}};
    auto sign =
        (py && (v.name == "and" || v.name == "or")) ? v.name : signs.at(v.name);
    std::string type =
        v.name == "add" || v.name == "subtract" || v.name == "multiply"
            ? values[0].type
            : "bool";
    auto code = "(" + args[0] + " " + sign + " " + args[1] + ")";
    return {type == "number" ? "checked_number(" + code + ")" : code, type};
  }
  void records(int depth, const BehaviourAction &a, const std::string &step) {
    auto target = quoted(a.target), selection = prop(a.parameters, "selection");
    Lines values, clear;
    for (auto &[key, source] : a.parameters["values"].members()) {
      auto item = expr(behaviour_expression(source, fields).value);
      if (!py && item.type == "text")
        item.code = "qtext(" + item.code + ")";
      values.push_back(py ? quoted(key) + ": " + item.code
                          : "{" + quoted(key) + ", " + item.code + "}");
    }
    auto record = (py ? "{" : "QJsonObject{") + join(values) + "}";
    for (auto field : a.parameters["clear"].items())
      clear.push_back(quoted(field.text()));
    auto to_clear = py ? "[" + join(clear) + "]"
                       : "std::vector<std::string>{" + join(clear) + "}";
    Lines args{target};
    if (step == "append_record")
      args.insert(args.end(), {record, selection, to_clear});
    else {
      args.push_back(selection);
      if (step == "set_record")
        args.push_back(record);
    }
    line(depth, self + step + "(" + join(args) + ")" + term);
  }
  void effect(int depth, const BehaviourAction &a, const std::string &step) {
    auto target = quoted(a.target), get = self + "encode_state(" + target + ")";
    auto put = [&](const std::string &s) { line(depth, s + term); };
    if (step == "confirm_discard")
      put(py ? "if not self.confirm_discard(" + target + "): return"
             : "if (!confirm_discard(" + target + ")) return");
    else if (step == "choose_open" || step == "choose_existing_or_save" ||
             step == "choose_save") {
      put((py ? "path = " : "auto path = ") + self + step + "(" + target + ")");
      put(py ? "if not path: return" : "if (path.empty()) return");
    } else if (step == "read_utf8")
      put(std::string(py ? "content = " : "auto content = ") + self +
          "read_utf8(path)");
    else if (step == "encode_state")
      put((py ? "content = " : "auto content = ") + get);
    else if (step == "empty_state")
      put((py ? "content = " : "auto content = ") + self + "empty_state(" +
          target + ")");
    else if (step == "commit_state")
      put(py ? "self.commit({" + target + ": self.decode_state(" + target +
                   ", content)})"
             : "put_encoded(" + target + ", content)");
    else if (step == "remember_path")
      put(self + "paths[" + target + "] = path");
    else if (step == "clear_path")
      put(py ? "self.paths[" + target + "] = ''"
             : "paths[" + target + "].clear()");
    else if (step == "mark_saved")
      put(self + "saved[" + target + "] = " + get);
    else if (step == "write_atomic_utf8")
      put(self + "write_atomic_utf8(path, content)");
    else if (step.starts_with("edit_"))
      put(self + "edit(" + target + ", " + quoted(step.substr(5)) + ")");
    else if (step == "append_record" || step == "remove_record" ||
             step == "set_record")
      records(depth, a, step);
    else {
      auto elapsed = state(a.target),
           anchor = state(a.parameters["anchor"].text()),
           running = state(a.parameters["running"].text());
      if (step == "read_clock")
        put(py ? "now = checked_number(self.clock())"
               : "double now = checked_number(monotonic_now())");
      else if (step == "anchor_clock") {
        put(py ? "if " + running + ": return" : "if (" + running + ") return");
        put(anchor + " = checked_number(now - " + elapsed + ")");
      } else if (step == "enable_clock")
        put(running + (py ? " = True" : " = true"));
      else if (step == "disable_clock")
        put(running + (py ? " = False" : " = false"));
      else if (step == "zero_duration")
        put(elapsed + " = 0.0");
      else if (step == "update_duration")
        put((py ? "if " + running + ": " : "if (" + running + ") ") + elapsed +
            " = checked_number(now - " + anchor + ")");
      else
        throw Unsupported("No target implementation for effect: " + step);
    }
  }
  void handler(const BehaviourAction &a) {
    line(0, "");
    line(1, py ? "def on_" + a.id + "(self):" : "void on_" + a.id + "() {");
    if (a.guard) {
      auto condition = expr(*a.guard).code;
      line(2,
           py ? "if not (" + condition +
                    "): raise ValueError('Check the inputs before continuing')"
              : "if (!(" + condition +
                    ")) throw std::runtime_error(\"Check the inputs before "
                    "continuing\");");
    }
    if (a.updates.empty())
      for (auto step : a.steps.items())
        effect(2, a, step.text());
    else {
      Lines updates;
      for (auto &[target, v] : a.updates) {
        auto e = expr(v);
        if (py) {
          updates.push_back(quoted(target) + ": " + e.code);
          continue;
        }
        auto local = "proposed_" + target;
        line(2, types.at(e.type) + " " + local + " = " + e.code + ";");
        if (e.type == "number")
          line(2, "checked_number(" + local + ");");
        else if (e.type == "text")
          line(2, "if (" + local +
                      ".size() > MAX_BYTES) throw std::runtime_error(\"Text "
                      "exceeds 4 MiB\");");
        else if (e.type == "records")
          line(2, "validate_records(" + quoted(target) + ", " + local + ");");
        updates.push_back(state(target) + " = " + local + ";");
      }
      if (py)
        line(2, "self.commit({" + join(updates) + "})");
      else
        for (auto &s : updates)
          line(2, s);
    }
    if (!py)
      line(1, "}");
  }
  void setup_fields() {
    for (auto &[name, field] : fields.members()) {
      auto n = quoted(name), type = field["type"].text(), member = state(name),
           persist =
               literal(Json::boolean(graph.persistent.contains(name)), py);
      if (py) {
        line(2, member + " = " + literal(field["initial"], true));
        line(2, "self.register(" + n + ", " + quoted(type) +
                    ", lambda: " + member + ", lambda value: setattr(self, " +
                    quoted("state_" + name) + ", value), " + persist + ", " +
                    literal(field["fields"], true) + ")");
      } else {
        auto prefix = "register_" + type + "(" + n + ", [this]{return " +
                      member + ";}, [this](";
        if (type == "text")
          line(2, prefix + "const std::string& v){" + member + " = v;}, " +
                      persist + ");");
        else if (type == "number")
          line(2, prefix + "double v){" + member + " = v;});");
        else if (type == "records")
          line(2, prefix + "const QJsonArray& v){" + member + " = v;}, " +
                      literal(field["fields"], false) + ", " + persist + ");");
      }
    }
  }
  void layout() {
    line(2, py ? "row = 0" : "int row = 0;");
    for (auto view : spec["views"].items()) {
      auto id = view["id"].text(), kind = view["kind"].text();
      Lines args{quoted(id)};
      std::string method;
      if (kind == "label") {
        method = "add_output";
        auto e = expr(graph.labels.at(id).value);
        if (!py) {
          if (e.type == "number")
            e.code = "number_text(" + e.code + ")";
          else if (e.type == "bool")
            e.code = "std::string(" + e.code + " ? \"true\" : \"false\")";
          else if (e.type == "records")
            e.code = "QJsonDocument(" + e.code +
                     ").toJson(QJsonDocument::Compact).toStdString()";
        }
        args.push_back(py ? "lambda: " + e.code
                          : "[this]{return " + e.code + ";}");
      } else if (kind == "record_list") {
        method = "add_records";
        for (auto key : {"state", "selection", "display", "checked"})
          args.push_back(prop(view, key));
      } else {
        method = "add_input";
        args.push_back(quoted(kind));
        args.push_back(prop(view, "state"));
      }
      args.push_back(prop(view, "label"));
      args.push_back("row");
      line(2, "row = " + self + method + "(" + join(args) + ")" + term);
    }
    size_t index = 0;
    for (auto action : spec["actions"].items()) {
      if (action.contains("visible") && !action["visible"].flag())
        continue;
      auto id = action["id"].text(),
           callback = py ? "self.on_" + id : "[this]{on_" + id + "();}";
      line(2, self + "add_action(" +
                  join({quoted(id), prop(action, "label", id), callback,
                        prop(action, "shortcut"),
                        "row + " + std::to_string(index / 4),
                        std::to_string(index % 4)}) +
                  ")" + term);
      ++index;
    }
    line(2,
         self + "finish(row + " + std::to_string((index + 3) / 4) + ")" + term);
    for (auto event : spec["events"].items()) {
      auto id = event["action"].text(),
           callback = py ? "self.on_" + id : "[this]{on_" + id + "();}";
      line(2, self + "add_periodic(" +
                  std::to_string(event["interval_ms"].integer()) + ", " +
                  callback + ")" + term);
    }
  }

public:
  Emitter(bool python, const BehaviourSpec &specification,
          const BehaviourGraph &g)
      : py(python), graph(g), spec(specification.data()), fields(spec["state"]),
        self(py ? "self." : ""), term(py ? "" : ";") {}
  std::string run(const std::filesystem::path &root) {
    line(0, read_file(root / "resources/targets/behaviour" /
                      (py ? "widgets.py" : "widgets.cpp.inc")));
    line(0, py ? "class ApplicationWindow(BehaviourWindow):"
               : "class ApplicationWindow : public BehaviourWindow {");
    if (py) {
      line(1, "def __init__(self, dialogs=None, clock=None):");
      line(2, "super().__init__(dialogs, clock)");
    } else {
      line(0, "public:");
      for (auto &[name, f] : fields.members())
        line(1, types.at(f["type"].text()) + " " + state(name) + " = " +
                    literal(f["initial"], false) + ";");
      line(1, "explicit ApplicationWindow(Dialogs service = {}, "
              "std::function<double()> clock = {}) : "
              "BehaviourWindow(std::move(service), std::move(clock)) {");
    }
    auto title =
        quoted(spec["title"].text() + (graph.persistent.empty() ? "" : " [*]"));
    line(2, py ? "self.setWindowTitle(" + title + ")"
               : "setWindowTitle(qtext(" + title + "));");
    setup_fields();
    layout();
    if (!py)
      line(1, "}");
    for (const auto &a : graph.actions)
      handler(a);
    if (py) {
      line(0, "");
      line(0, "def main():");
      line(1, "application = QApplication(sys.argv)");
      line(1, "application.setStyle('Fusion')");
      line(1, "window = ApplicationWindow()");
      line(1, "window.show()");
      line(1, "return application.exec()");
      line(0, "");
      line(0, "if __name__ == '__main__':");
      line(1, "raise SystemExit(main())");
    } else {
      line(0, "};");
      line(0, "#ifndef UNLLM_APP_LIBRARY");
      line(0, "int main(int argc, char** argv) {");
      line(1, "QApplication application(argc, argv);");
      line(1, "application.setStyle(\"Fusion\");");
      line(1, "ApplicationWindow window;");
      line(1, "window.show();");
      line(1, "return application.exec();");
      line(0, "}");
      line(0, "#endif");
    }
    return join(lines, "\n") + "\n";
  }
};
} // namespace
std::string emit_behaviour(const BehaviourSpec &spec,
                           const BehaviourGraph &graph,
                           const std::string &language,
                           const std::filesystem::path &root) {
  if (language != "cpp" && language != "python")
    throw Unsupported("Unknown target language");
  return Emitter(language == "python", spec, graph).run(root);
}
} // namespace unllm::native
