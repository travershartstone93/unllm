#include "unllm/symbolic/expression.h"
#include "ScalarExprLexer.h"
#include "ScalarExprParser.h"
#include <algorithm>
#include <antlr4-runtime.h>
#include <map>
namespace unllm::native {
Json Expr::data() const {
  auto out = Json::object(), children = Json::array();
  for (auto &a : args)
    children.push(a.data());
  out.set("op", Json::string(op));
  out.set("args", children);
  out.set("value", value);
  return out;
}
ExprType Expr::type(unsigned depth) const {
  if (depth > 16)
    throw Unsupported("Expression nesting budget exceeded.");
  static const std::map<std::string, size_t> arities{
      {"variable", 0}, {"constant", 0}, {"negate", 1}, {"not", 1}, {"add", 2},
      {"subtract", 2}, {"multiply", 2}, {"gt", 2},     {"ge", 2},  {"lt", 2},
      {"le", 2},       {"eq", 2},       {"and", 2},    {"or", 2},  {"if", 3}};
  auto it = arities.find(op);
  if (it == arities.end() || args.size() != it->second)
    throw Unsupported("Invalid expression operation or arity.");
  if (op == "constant") {
    if (value.is_boolean())
      return ExprType::Boolean;
    if (!value.is_number() || !std::isfinite(value.real()) ||
        std::abs(value.real()) > 1e12)
      throw Unsupported(
          "Constants must be finite bounded numbers or Booleans.");
    return ExprType::Number;
  }
  if (!value.null())
    throw Unsupported("Only constants have a literal value.");
  if (op == "variable")
    return ExprType::Number;
  std::vector<ExprType> types;
  for (auto &a : args)
    types.push_back(a.type(depth + 1));
  if (op == "and" || op == "or" || op == "not") {
    for (auto t : types)
      if (t != ExprType::Boolean)
        throw Unsupported("Logical operations need Boolean operands.");
    return ExprType::Boolean;
  }
  if (op == "if") {
    if (types[0] != ExprType::Boolean || types[1] != types[2])
      throw Unsupported(
          "A conditional needs a Boolean and matching branch types.");
    return types[1];
  }
  for (auto t : types)
    if (t != ExprType::Number)
      throw Unsupported("Arithmetic and comparison operands must be numeric.");
  return op == "gt" || op == "ge" || op == "lt" || op == "le" || op == "eq"
             ? ExprType::Boolean
             : ExprType::Number;
}
bool Expr::contains(const std::string &operation) const {
  if (op == operation)
    return true;
  return std::any_of(args.begin(), args.end(),
                     [&](auto &a) { return a.contains(operation); });
}
std::vector<double> Expr::constants() const {
  std::vector<double> result;
  if (op == "constant" && value.is_number())
    result.push_back(value.real());
  for (auto &a : args) {
    auto more = a.constants();
    result.insert(result.end(), more.begin(), more.end());
  }
  return result;
}
namespace {
class Errors : public antlr4::BaseErrorListener {
  void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t, size_t,
                   const std::string &, std::exception_ptr) override {
    throw Unsupported("Only x, bounded constants, arithmetic, comparisons and "
                      "conditionals are supported.");
  }
};
class Decode {
public:
  Expr expression(ScalarExprParser::ExpressionContext *c) {
    auto a = disjunction(c->disjunction(0));
    if (c->expression())
      return {"if",
              {disjunction(c->disjunction(1)), a, expression(c->expression())},
              {}};
    return a;
  }
  Expr disjunction(ScalarExprParser::DisjunctionContext *c) {
    auto a = conjunction(c->conjunction(0));
    for (size_t i = 1; i < c->conjunction().size(); ++i)
      a = {"or", {a, conjunction(c->conjunction(i))}, {}};
    return a;
  }
  Expr conjunction(ScalarExprParser::ConjunctionContext *c) {
    auto a = negation(c->negation(0));
    for (size_t i = 1; i < c->negation().size(); ++i)
      a = {"and", {a, negation(c->negation(i))}, {}};
    return a;
  }
  Expr negation(ScalarExprParser::NegationContext *c) {
    return c->negation() ? Expr{"not", {negation(c->negation())}, {}}
                         : comparison(c->comparison());
  }
  Expr comparison(ScalarExprParser::ComparisonContext *c) {
    auto a = sum(c->sum(0));
    if (!c->COMPARE())
      return a;
    static const std::map<std::string, std::string> ops{
        {">", "gt"}, {">=", "ge"}, {"<", "lt"}, {"<=", "le"}, {"==", "eq"}};
    return {ops.at(c->COMPARE()->getText()), {a, sum(c->sum(1))}, {}};
  }
  Expr sum(ScalarExprParser::SumContext *c) {
    auto a = product(c->product(0));
    for (size_t i = 1; i < c->product().size(); ++i)
      a = {c->children[2 * i - 1]->getText() == "+" ? "add" : "subtract",
           {a, product(c->product(i))},
           {}};
    return a;
  }
  Expr product(ScalarExprParser::ProductContext *c) {
    auto a = unary(c->unary(0));
    for (size_t i = 1; i < c->unary().size(); ++i)
      a = {"multiply", {a, unary(c->unary(i))}, {}};
    return a;
  }
  Expr unary(ScalarExprParser::UnaryContext *c) {
    return c->unary() ? Expr{"negate", {unary(c->unary())}, {}}
                      : atom(c->atom());
  }
  Expr atom(ScalarExprParser::AtomContext *c) {
    if (c->expression())
      return expression(c->expression());
    auto text = c->getText();
    if (text == "x")
      return {"variable", {}, {}};
    if (text == "True" || text == "False")
      return {"constant", {}, Json::boolean(text == "True")};
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
      if (text.size() > 1 && text[0] == '0' &&
          text.find_first_not_of('0') != std::string::npos)
        throw Unsupported("Leading zeros are not valid decimal literals");
      value = Json::number(std::stoll(text));
    }
    return {"constant", {}, value};
  }
};
} // namespace
Expr expression(const Json &value, unsigned depth) {
  if (depth > 16)
    throw Unsupported("Expression nesting budget exceeded.");
  Expr result;
  if (value.is_object()) {
    fields_only(value, {"op", "args", "value"});
    auto children = value.contains("args") ? value["args"] : Json::array();
    if (!children.is_array() || children.size() > 3)
      throw Unsupported("Invalid expression argument list");
    result.op = required_text(value, "op");
    result.value = value["value"].clone();
    for (auto a : children.items())
      result.args.push_back(expression(a, depth + 1));
  } else {
    if (!value.is_string() || value.text().size() > 1000)
      throw Unsupported(
          "An expression must be a small typed tree or expression string.");
    auto text = value.text();
    antlr4::ANTLRInputStream input(text);
    ScalarExprLexer lexer(&input);
    Errors errors;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&errors);
    antlr4::CommonTokenStream tokens(&lexer);
    ScalarExprParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&errors);
    try {
      result = Decode{}.expression(parser.document()->expression());
    } catch (const std::out_of_range &) {
      throw Unsupported("Expression number exceeds supported bounds");
    }
  }
  result.type(depth);
  return result;
}
} // namespace unllm::native
