#include "unllm/symbolic/behaviour_expression.h"
#include "BehaviourExprLexer.h"
#include "BehaviourExprParser.h"
#include <algorithm>
#include <antlr4-runtime.h>
#include <map>
#include <set>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>
namespace unllm::native {
Json BehaviourValue::data() const {
  auto out = Json::object(), children = Json::array();
  for (auto &a : args)
    children.push(a.data());
  out.set("kind", Json::string(kind));
  out.set("name", Json::string(name));
  out.set("value", value);
  out.set("args", children);
  return out;
}
namespace {
using E = BehaviourExpression;
using P = BehaviourExprParser;
E node(const std::string &kind, const std::string &name,
       std::vector<BehaviourValue> args, const std::string &type,
       Json value = {}) {
  return {{kind, name, value, std::move(args)}, type};
}
void require(bool yes, const std::string &message) {
  if (!yes)
    throw Unsupported(message);
}
void codepoint(std::string &s, UChar32 cp) {
  require(cp >= 0 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff),
          "String escapes must represent Unicode scalar values");
  icu::UnicodeString(cp).toUTF8String(s);
}
std::string string_literal(std::string text) {
  bool raw = text[0] == 'r' || text[0] == 'R';
  if (raw || text[0] == 'u' || text[0] == 'U')
    text.erase(0, 1);
  size_t quote = text.starts_with("\"\"\"") || text.starts_with("'''") ? 3 : 1;
  text = text.substr(quote, text.size() - 2 * quote);
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n')
        ++i;
      out += '\n';
      continue;
    }
    if (c != '\\' || raw) {
      out += c;
      continue;
    }
    require(++i < text.size(), "Incomplete string escape");
    c = text[i];
    static const std::map<char, char> escapes{
        {'n', '\n'}, {'r', '\r'}, {'t', '\t'},  {'a', '\a'},  {'b', '\b'},
        {'f', '\f'}, {'v', '\v'}, {'\\', '\\'}, {'\'', '\''}, {'"', '"'}};
    if (escapes.contains(c))
      out += escapes.at(c);
    else if (c == '\n')
      continue;
    else if (c == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n')
        ++i;
    } else if (c == 'x' || c == 'u' || c == 'U') {
      size_t digits = c == 'x' ? 2 : c == 'u' ? 4 : 8;
      require(i + digits < text.size(), "Incomplete Unicode escape");
      uint32_t cp = 0;
      for (size_t j = 0; j < digits; ++j) {
        char d = text[++i];
        int v = d >= '0' && d <= '9'   ? d - '0'
                : d >= 'a' && d <= 'f' ? d - 'a' + 10
                : d >= 'A' && d <= 'F' ? d - 'A' + 10
                                       : -1;
        require(v >= 0, "Invalid Unicode escape");
        cp = cp * 16 + v;
      }
      codepoint(out, cp);
    } else if (c >= '0' && c <= '7') {
      int cp = c - '0';
      for (int j = 1; j < 3 && i + 1 < text.size() && text[i + 1] >= '0' &&
                      text[i + 1] <= '7';
           ++j)
        cp = cp * 8 + text[++i] - '0';
      codepoint(out, cp);
    } else if (c == 'N') {
      require(i + 1 < text.size() && text[++i] == '{',
              "Invalid named Unicode escape");
      auto end = text.find('}', i + 1);
      require(end != std::string::npos, "Incomplete named Unicode escape");
      auto name = text.substr(i + 1, end - i - 1);
      UErrorCode status = U_ZERO_ERROR;
      auto cp = u_charFromName(U_UNICODE_CHAR_NAME, name.c_str(), &status);
      require(U_SUCCESS(status), "Unknown Unicode character name");
      codepoint(out, cp);
      i = end;
    } else {
      out += '\\';
      out += c;
    }
  }
  return out;
}
Json number(std::string text) {
  text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
  Json value;
  if (text.starts_with("0x") || text.starts_with("0X"))
    value = Json::number(std::stoll(text.substr(2), nullptr, 16));
  else if (text.starts_with("0o") || text.starts_with("0O"))
    value = Json::number(std::stoll(text.substr(2), nullptr, 8));
  else if (text.starts_with("0b") || text.starts_with("0B"))
    value = Json::number(std::stoll(text.substr(2), nullptr, 2));
  else if (text.find_first_of(".eE") != std::string::npos)
    value = Json::real(std::stod(text));
  else {
    require(text.size() == 1 || text[0] != '0' ||
                text.find_first_not_of('0') == std::string::npos,
            "Leading zeros are not valid decimal literals");
    value = Json::number(std::stoll(text));
  }
  require(std::isfinite(value.real()) && std::abs(value.real()) <= 1e15,
          "Invalid expression literal");
  return value;
}
class Errors : public antlr4::BaseErrorListener {
  void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t, size_t,
                   const std::string &, std::exception_ptr) override {
    throw Unsupported("Invalid requirement expression");
  }
};
class Decode {
  const Json &state_;
  bool transition_;
  size_t nodes_ = 0;
  void count(size_t n) {
    nodes_ += n;
    require(nodes_ <= 128, "Expression tree budget exceeded");
  }
  std::string state_type(const std::string &name) {
    require(state_.contains(name), "Unbound state: " + name);
    auto type = state_[name]["type"].text();
    require(type == "number" || type == "text" || type == "bool" ||
                type == "records",
            "Invalid state type");
    return type;
  }
  E binary(const std::string &op, E a, E b) {
    auto type = a.type;
    if (op == "and" || op == "or")
      require(a.type == "bool" && b.type == "bool",
              "Boolean operands required");
    else if (op == "eq" || op == "ne") {
      require(a.type == b.type, "Comparison type mismatch");
      type = "bool";
    } else if (op == "gt" || op == "ge" || op == "lt" || op == "le") {
      require(a.type == "number" && b.type == "number",
              "Comparison type mismatch");
      type = "bool";
    } else
      require(a.type == b.type &&
                  (a.type == "number" || (op == "add" && a.type == "text")),
              "Arithmetic type mismatch");
    return node("binary", op, {a.value, b.value}, type);
  }

public:
  Decode(const Json &state, bool transition)
      : state_(state), transition_(transition) {}
  E expression(P::ExpressionContext *c) {
    auto yes = disjunction(c->disjunction(0));
    if (!c->expression())
      return yes;
    count(1);
    auto condition = disjunction(c->disjunction(1)),
         no = expression(c->expression());
    require(condition.type == "bool" && yes.type == no.type,
            "Conditional expression types differ");
    return node("conditional", "", {condition.value, yes.value, no.value},
                yes.type);
  }
  E disjunction(P::DisjunctionContext *c) {
    auto a = conjunction(c->conjunction(0));
    if (c->conjunction().size() > 1)
      count(2);
    for (size_t i = 1; i < c->conjunction().size(); ++i)
      a = binary("or", a, conjunction(c->conjunction(i)));
    return a;
  }
  E conjunction(P::ConjunctionContext *c) {
    auto a = negation(c->negation(0));
    if (c->negation().size() > 1)
      count(2);
    for (size_t i = 1; i < c->negation().size(); ++i)
      a = binary("and", a, negation(c->negation(i)));
    return a;
  }
  E negation(P::NegationContext *c) {
    if (!c->negation())
      return comparison(c->comparison());
    count(2);
    auto a = negation(c->negation());
    require(a.type == "bool", "Unary operand type mismatch");
    return node("unary", "not", {a.value}, "bool");
  }
  E comparison(P::ComparisonContext *c) {
    auto previous = sum(c->sum(0));
    if (c->COMPARE().empty())
      return previous;
    count(1 + c->COMPARE().size());
    E result;
    static const std::map<std::string, std::string> ops{
        {"==", "eq"}, {"!=", "ne"}, {">", "gt"},
        {">=", "ge"}, {"<", "lt"},  {"<=", "le"}};
    for (size_t i = 0; i < c->COMPARE().size(); ++i) {
      auto current = sum(c->sum(i + 1));
      auto clause = binary(ops.at(c->COMPARE(i)->getText()), previous, current);
      result = i == 0 ? clause : binary("and", result, clause);
      previous = current;
    }
    return result;
  }
  E sum(P::SumContext *c) {
    auto a = product(c->product(0));
    for (size_t i = 1; i < c->product().size(); ++i) {
      count(2);
      a = binary(c->children[2 * i - 1]->getText() == "+" ? "add" : "subtract",
                 a, product(c->product(i)));
    }
    return a;
  }
  E product(P::ProductContext *c) {
    auto a = unary(c->unary(0));
    for (size_t i = 1; i < c->unary().size(); ++i) {
      count(2);
      a = binary(c->children[2 * i - 1]->getText() == "*" ? "multiply"
                                                          : "divide",
                 a, unary(c->unary(i)));
    }
    return a;
  }
  E unary(P::UnaryContext *c) {
    if (!c->unary())
      return atom(c->atom());
    count(2);
    auto a = unary(c->unary());
    require(a.type == "number", "Unary operand type mismatch");
    return node("unary", c->children[0]->getText() == "-" ? "neg" : "positive",
                {a.value}, "number");
  }
  E atom(P::AtomContext *c) {
    if (c->expression())
      return expression(c->expression());
    auto text = c->getText();
    if (text == "True" || text == "False") {
      count(1);
      return node("literal", "", {}, "bool", Json::boolean(text == "True"));
    }
    if (c->NUMBER()) {
      count(1);
      return node("literal", "", {}, "number", number(text));
    }
    if (!c->STRING().empty()) {
      count(1);
      std::string s;
      for (auto token : c->STRING())
        s += string_literal(token->getText());
      return node("literal", "", {}, "text", Json::string(s));
    }
    if (text == "[]") {
      count(2);
      return node("records", "", {}, "records", Json::array());
    }
    auto name = c->NAME(0)->getText();
    static const std::set<std::string> reserved{
        "None",    "as",       "assert",   "async",  "await",  "break",
        "class",   "continue", "def",      "del",    "elif",   "except",
        "finally", "for",      "from",     "global", "import", "in",
        "is",      "lambda",   "nonlocal", "pass",   "raise",  "return",
        "try",     "while",    "with",     "yield"};
    require(!reserved.contains(name), "Unsupported expression construct");
    if (c->NAME().size() == 2) {
      count(4);
      require(transition_ && (name == "prev" || name == "next"),
              "Only transition state attributes are supported");
      auto field = c->NAME(1)->getText();
      return node(name, field, {}, state_type(field));
    }
    if (c->children.size() == 1) {
      count(2);
      require(!transition_, "Use prev/next state in transition expressions");
      return node("state", name, {}, state_type(name));
    }
    count(3);
    using Signature = std::pair<std::vector<std::string>, std::string>;
    static const std::map<std::string, Signature> calls{
        {"upper", {{"text"}, "text"}},
        {"lower", {{"text"}, "text"}},
        {"strip", {{"text"}, "text"}},
        {"reverse_lines", {{"text"}, "text"}},
        {"sort_lines", {{"text"}, "text"}},
        {"unique_lines", {{"text"}, "text"}},
        {"words", {{"text"}, "number"}},
        {"chars", {{"text"}, "number"}},
        {"lines", {{"text"}, "number"}},
        {"number_text", {{"number"}, "text"}},
        {"replace", {{"text", "text", "text"}, "text"}},
        {"records_count", {{"records"}, "number"}},
        {"count_true", {{"records", "text"}, "number"}}};
    require(calls.contains(name), "Unsupported primitive: " + name);
    std::vector<BehaviourValue> args;
    std::vector<std::string> types;
    if (c->arguments())
      for (auto argument : c->arguments()->expression()) {
        auto a = expression(argument);
        args.push_back(a.value);
        types.push_back(a.type);
      }
    require(types == calls.at(name).first,
            "Primitive argument types do not match: " + name);
    if (name == "count_true") {
      require(args[0].kind == "state" && args[1].kind == "literal" &&
                  state_[args[0].name]["fields"][args[1].value.text()].text() ==
                      "bool",
              "count_true needs a declared Boolean record field");
    }
    return node("primitive", name, args, calls.at(name).second);
  }
};
} // namespace
BehaviourExpression behaviour_expression(const Json &source, const Json &state,
                                         bool transition) {
  require(source.is_string() && state.is_object(),
          "Expression and typed state required");
  auto text = source.text();
  require(text.size() <= 8192, "Expression budget exceeded");
  size_t length = 0;
  for (int32_t i = 0; i < static_cast<int32_t>(text.size());) {
    UChar32 cp;
    U8_NEXT(text.data(), i, static_cast<int32_t>(text.size()), cp);
    require(cp > 0, "Invalid UTF-8 or null in expression");
    ++length;
  }
  require(length <= 2048, "Expression budget exceeded");
  antlr4::ANTLRInputStream input(text);
  BehaviourExprLexer lexer(&input);
  Errors errors;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);
  antlr4::CommonTokenStream tokens(&lexer);
  tokens.fill();
  require(tokens.size() <= 1024, "Expression token budget exceeded");
  int nesting = 0;
  for (auto token : tokens.getTokens()) {
    auto t = token->getText();
    if (t == "(" || t == "[")
      require(++nesting <= 128, "Expression nesting budget exceeded");
    else if (t == ")" || t == "]")
      --nesting;
  }
  P parser(&tokens);
  parser.removeErrorListeners();
  parser.addErrorListener(&errors);
  try {
    return Decode(state, transition)
        .expression(parser.document()->expression());
  } catch (const std::out_of_range &) {
    throw Unsupported("Expression number exceeds supported bounds");
  }
}
} // namespace unllm::native
