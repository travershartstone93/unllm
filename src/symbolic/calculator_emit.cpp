#include "unllm/symbolic/calculator.h"
#include <map>
#include <sstream>
namespace unllm::native {
namespace {
using Types = std::map<std::string, std::string>;
const Types cpp_types{{"text", "std::string"},
                      {"number", "double"},
                      {"bool", "bool"},
                      {"void", "void"}};
struct Value {
  std::string code, type;
};
struct Signature {
  std::string result;
  std::vector<std::string> parameters;
};
const std::map<std::string, Signature> primitives{
    {"evaluate_expression", {"number", {"text"}}},
    {"checked_number", {"number", {"number"}}},
    {"number_text", {"text", {"number"}}},
    {"display_number", {"text", {"number", "number"}}},
    {"length", {"number", {"text"}}},
    {"truncate_last", {"text", {"text"}}}};
std::string join(const std::vector<std::string> &a) {
  std::string out;
  for (auto &s : a) {
    if (!out.empty())
      out += ", ";
    out += s;
  }
  return out;
}
void require(bool ok, const std::string &message) {
  if (!ok)
    throw Unsupported("Calculator IR: " + message);
}
std::string id(const Json &v) {
  auto s = v.text();
  require(!s.empty() && s[0] >= 'a' && s[0] <= 'z' &&
              s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") ==
                  s.npos,
          "invalid identifier");
  return s;
}
class Emitter {
  const Json &ir;
  bool py;
  std::ostringstream out;
  Types fields;
  std::map<std::string, Signature> methods;
  std::string self() const { return py ? "self." : ""; }
  std::string end() const { return py ? "" : ";"; }
  void line(int level, const std::string &code) {
    out << std::string(4 * level, ' ') << code << '\n';
  }
  Value literal(const Json &v) {
    if (v.is_string())
      return {canonical(v), "text"};
    if (v.is_boolean())
      return {v.flag() ? (py ? "True" : "true") : (py ? "False" : "false"),
              "bool"};
    require(v.is_number(), "expected literal");
    return {canonical(v), "number"};
  }
  Value call(const std::string &name, const std::vector<Value> &args,
             bool method, const std::string &prefix) {
    auto &signature = method ? methods.at(name) : primitives.at(name);
    require(args.size() == signature.parameters.size(),
            "argument count for " + name);
    std::vector<std::string> codes;
    for (size_t i = 0; i < args.size(); ++i) {
      require(args[i].type == signature.parameters[i],
              "argument type for " + name);
      codes.push_back(args[i].code);
    }
    return {prefix + name + "(" + join(codes) + ")", signature.result};
  }
  Value value(const Json &expr, const Types &locals) {
    if (!expr.is_array())
      return literal(expr);
    auto op = expr.at(0).text();
    if (op == "local" || op == "state") {
      auto n = id(expr.at(1));
      return {(op == "state" ? self() : "") + n,
              (op == "state" ? fields : locals).at(n)};
    }
    if (op == "parameter") {
      require(expr.at(1).text() == "precision", "unknown parameter");
      return literal(ir["spec"]["precision"]);
    }
    std::vector<Value> args;
    for (size_t i = op == "invoke" ? 2 : 1; i < expr.size(); ++i)
      args.push_back(value(expr.at(i), locals));
    if (op == "invoke")
      return call(id(expr.at(1)), args, true, self());
    if (primitives.contains(op))
      return call(op, args, false, "");
    if (op == "not" || op == "negate" || op == "neg") {
      auto type = op == "not" ? "bool" : "number";
      require(args.size() == 1 && args[0].type == type, "unary type");
      return {"(" + std::string(op == "not" ? (py ? "not " : "!") : "-") +
                  args[0].code + ")",
              type};
    }
    const static Types operators{
        {"add", "+"}, {"subtract", "-"}, {"multiply", "*"}, {"divide", "/"},
        {"ge", ">="}, {"gt", ">"},       {"lt", "<"},       {"le", "<="},
        {"ne", "!="}, {"eq", "=="}};
    require(args.size() == 2 && args[0].type == args[1].type, "binary type");
    auto type = args[0].type;
    std::string symbol;
    if (op == "or" || op == "and") {
      require(type == "bool", "logical type");
      symbol = py ? op : (op == "or" ? "||" : "&&");
    } else {
      symbol = operators.at(op);
      if (op == "eq" || op == "ne")
        type = "bool";
      else if (op != "add" || type != "text") {
        require(type == "number", "arithmetic type");
        if (op == "gt" || op == "ge" || op == "lt" || op == "le")
          type = "bool";
      }
    }
    return {"(" + args[0].code + " " + symbol + " " + args[1].code + ")", type};
  }
  bool statements(const Json &body, Types scope, const std::string &result,
                  int level) {
    require(body.is_array() && body.size(), "empty block");
    bool terminated = false;
    for (auto s : body.items()) {
      require(!terminated, "unreachable statement");
      auto op = s.at(0).text();
      if (op == "if" || op == "try") {
        bool attempt = op == "try";
        auto head = std::string("try");
        if (!attempt) {
          auto cond = value(s.at(1), scope);
          require(cond.type == "bool", "branch condition");
          head = py ? "if " + cond.code : "if (" + cond.code + ")";
        }
        line(level, head + (py ? ":" : " {"));
        bool done = statements(s.at(attempt ? 1 : 2), scope, result, level + 1);
        if (attempt) {
          line(level, py ? "except ValueError as caught:"
                         : "} catch (const std::exception &caught) {");
          line(level + 1, py ? "problem = str(caught)"
                             : "const std::string problem = caught.what();");
          auto caught = scope;
          caught.emplace("problem", "text");
          terminated = statements(s.at(2), caught, result, level + 1) && done;
        }
        if (!py)
          line(level, "}");
      } else if (op == "set" || op == "let") {
        auto n = id(s.at(1));
        auto v = value(s.at(2), scope);
        if (op == "set")
          require(fields.at(n) == v.type, "field assignment");
        else
          require(scope.emplace(n, v.type).second, "duplicate local");
        line(level, (op == "set" ? self() : (py ? "" : "const auto ")) + n +
                        " = " + v.code + end());
      } else if (op == "call") {
        std::vector<Value> args;
        for (size_t i = 2; i < s.size(); ++i)
          args.push_back(value(s.at(i), scope));
        auto x = call(id(s.at(1)), args, true, self());
        require(x.type == "void", "unexpected method result");
        line(level, x.code + end());
      } else if (op == "return") {
        auto v = s.size() == 1 ? Value{"", "void"} : value(s.at(1), scope);
        require(v.type == result, "return type");
        line(level, "return" + (v.code.empty() ? "" : " " + v.code) + end());
        terminated = true;
      } else if (op == "raise") {
        auto v = value(s.at(1), scope);
        require(v.type == "text", "exception type");
        line(level, (py ? "raise ValueError(" : "throw std::runtime_error(") +
                        v.code + ")" + end());
        terminated = true;
      } else
        throw Unsupported("Unknown calculator statement");
    }
    return terminated;
  }
  void function(const std::string &name, const Json &def, bool method) {
    auto result = def["result"].text();
    auto args = def["parameters"];
    std::vector<std::string> parameters;
    if (method && py)
      parameters.push_back("self");
    Types scope;
    for (auto a : args.items()) {
      auto n = id(a.at(0)), t = a.at(1).text();
      require(scope.emplace(n, t).second, "duplicate parameter");
      parameters.push_back((py ? "" : cpp_types.at(t) + " ") + n);
    }
    int level = method ? 1 : 0;
    line(level, (py ? "def " : cpp_types.at(result) + " ") + name + "(" +
                    join(parameters) + ")" + (py ? ":" : " {"));
    bool done = statements(def["body"], scope, result, level + 1);
    require(done || result == "void", "missing return");
    if (!py)
      line(level, "}");
  }
  std::string binding(const Json &b, bool getter) {
    std::vector<Value> args;
    for (auto v : b["args"].items())
      args.push_back(literal(v));
    auto x = call(id(b["method"]), args, true,
                  py ? "self.controller." : "controller.");
    require(x.type == (getter ? "text" : "void"), "widget callback type");
    return py ? "lambda: " + x.code
              : "[this] { " + std::string(getter ? "return " : "") + x.code +
                    "; }";
  }

public:
  Emitter(const Json &ir, bool py) : ir(ir), py(py) {}
  std::string run(const std::filesystem::path &dir) {
    for (auto &[name, method] : ir["methods"].members()) {
      Signature sig{method["result"].text(), {}};
      for (auto p : method["parameters"].items())
        sig.parameters.push_back(p.at(1).text());
      methods.emplace(name, sig);
    }
    auto spec = ir["spec"];
    line(0,
         (py ? "ALLOW_PARENTHESES = " : "constexpr bool ALLOW_PARENTHESES = ") +
             literal(spec["parentheses"]).code + end());
    out << read_file(dir / (py ? "python_expression.py" : "cpp_expression.h"))
        << "\n";
    function("apply_operator", ir["operator"], false);
    line(0, py ? "class Controller:" : "struct Controller {");
    if (py)
      line(1, "def __init__(self):");
    for (auto &[n, v] : ir["state"].members()) {
      auto x = literal(v);
      fields.emplace(n, x.type);
      line(py ? 2 : 1, (py ? self() : cpp_types.at(x.type) + " ") + n + " = " +
                           x.code + end());
    }
    for (auto &[n, m] : ir["methods"].members())
      function(n, m, true);
    if (!py)
      line(0, "};");
    out << read_file(dir / (py ? "python_widgets.py" : "cpp_widgets.h"))
        << "\n";
    line(0, py ? "class ApplicationWindow(GenericWindow):"
               : "class ApplicationWindow : public GenericWindow {");
    if (!py) {
      line(0, "public:");
      line(1, "Controller controller;");
    }
    line(1, py ? "def __init__(self):" : "ApplicationWindow() {");
    if (py) {
      line(2, "super().__init__()");
      line(2, "self.controller = Controller()");
    }
    auto title = literal(spec["title"]).code;
    line(2, self() + "setWindowTitle(" +
                (py ? title : "QString::fromUtf8(" + title + ")") + ")" +
                end());
    std::set<std::string> ids;
    for (auto w : ir["widgets"].items()) {
      auto kind = w["kind"].text();
      bool button = kind == "button";
      require(ids.insert(w["id"].text()).second, "duplicate widget");
      std::vector<std::string> args{
          literal(w["id"]).code, literal(w[button ? "label" : "kind"]).code,
          canonical(w["row"]), canonical(w["column"])};
      if (!button)
        args.push_back(canonical(w["span"]));
      args.push_back(binding(w["binding"], !button));
      line(2, self() + (button ? "add_button(" : "add_display(") + join(args) +
                  ")" + end());
    }
    for (auto &[k, b] : ir["keys"].members())
      line(2, self() + "bind_key(" + canonical(Json::string(k)) + ", " +
                  binding(b, false) + ")" + end());
    line(2, self() + "refresh()" + end());
    if (py)
      out << R"(
if __name__ == "__main__":
    application = QApplication(sys.argv)
    application.setStyle("Fusion")
    window = ApplicationWindow()
    window.show()
    raise SystemExit(application.exec())
)";
    else
      out << R"(    }
};
#ifndef UNLLM_APP_LIBRARY
int main(int argc, char **argv) {
    QApplication application(argc,argv);
    application.setStyle("Fusion");
    ApplicationWindow window;
    window.show();
    return application.exec();
}
#endif
)";
    return out.str();
  }
};
} // namespace
std::string emit_calculator(const Json &ir, const std::string &language,
                            const std::filesystem::path &root) {
  if (language != "cpp" && language != "python")
    throw Unsupported("Invalid calculator target");
  return Emitter(ir, language == "python")
      .run(root / "resources/targets/calculator");
}
} // namespace unllm::native
