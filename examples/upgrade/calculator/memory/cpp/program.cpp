constexpr bool ALLOW_PARENTHESES = true;
// Reusable ANTLR expression component, independent of widgets and state.
#include "ArithmeticBaseVisitor.h"
#include "ArithmeticLexer.h"
#include "ArithmeticParser.h"
#include <antlr4-runtime.h>
#include <any>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

double checked_number(double value) {
  if (!std::isfinite(value) || std::abs(value) > 1e15)
    throw std::runtime_error("Number outside finite +/-1e15 domain");
  return value;
}
std::string number_text(double value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(17) << (value == 0 ? 0 : checked_number(value));
  return out.str();
}
std::string display_number(double value, double precision) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(static_cast<int>(precision))
      << (value == 0 ? 0 : checked_number(value));
  return out.str();
}
double length(const std::string &value) {
  return static_cast<double>(value.size());
}
std::string truncate_last(const std::string &value) {
  return value.empty() ? value : value.substr(0, value.size() - 1);
}
double apply_operator(std::string, double, double);
class RejectErrors : public antlr4::BaseErrorListener {
public:
  void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t, size_t,
                   const std::string &, std::exception_ptr) override {
    throw std::runtime_error("Invalid expression");
  }
};
class EvaluateTree : public ArithmeticBaseVisitor {
public:
  std::any visitStart(ArithmeticParser::StartContext *ctx) override {
    return visit(ctx->expression());
  }
  std::any visitExpression(ArithmeticParser::ExpressionContext *ctx) override {
    auto items = ctx->term();
    double value = std::any_cast<double>(visit(items[0]));
    for (size_t i = 1; i < items.size(); ++i)
      value = apply_operator(ctx->children[2 * i - 1]->getText(), value,
                             std::any_cast<double>(visit(items[i])));
    return value;
  }
  std::any visitTerm(ArithmeticParser::TermContext *ctx) override {
    auto items = ctx->unary();
    double value = std::any_cast<double>(visit(items[0]));
    for (size_t i = 1; i < items.size(); ++i)
      value = apply_operator(ctx->children[2 * i - 1]->getText(), value,
                             std::any_cast<double>(visit(items[i])));
    return value;
  }
  std::any visitUnary(ArithmeticParser::UnaryContext *ctx) override {
    if (!ctx->unary())
      return visit(ctx->primary());
    double value = std::any_cast<double>(visit(ctx->unary()));
    return checked_number(ctx->children[0]->getText() == "-" ? -value : value);
  }
  std::any visitPrimary(ArithmeticParser::PrimaryContext *ctx) override {
    if (ctx->NUMBER()) {
      return checked_number(
          std::strtod(ctx->NUMBER()->getText().c_str(), nullptr));
    }
    return visit(ctx->expression());
  }
};
double evaluate_expression(const std::string &text) {
  if (text.empty() || text.size() > 1024)
    throw std::runtime_error("Expression is empty or too long");
  antlr4::ANTLRInputStream input(text);
  ArithmeticLexer lexer(&input);
  RejectErrors errors;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);
  antlr4::CommonTokenStream tokens(&lexer);
  tokens.fill();
  if (tokens.getTokens().size() > 256)
    throw std::runtime_error("Expression token budget exceeded");
  int depth = 0, unary_count = 0;
  bool expecting_operand = true;
  for (auto *token : tokens.getTokens()) {
    if ((token->getType() == ArithmeticLexer::PLUS ||
         token->getType() == ArithmeticLexer::MINUS) &&
        expecting_operand) {
      if (++unary_count > 32)
        throw std::runtime_error("Unary sign budget exceeded");
    }
    if (token->getType() == ArithmeticLexer::NUMBER ||
        token->getType() == ArithmeticLexer::RPAREN)
      expecting_operand = false;
    else if (token->getType() != antlr4::Token::EOF)
      expecting_operand = true;
    if (token->getType() == ArithmeticLexer::LPAREN ||
        token->getType() == ArithmeticLexer::RPAREN) {
      if (!ALLOW_PARENTHESES)
        throw std::runtime_error("Parentheses are not enabled");
      depth += token->getType() == ArithmeticLexer::LPAREN ? 1 : -1;
      if (depth > 32)
        throw std::runtime_error("Parenthesis depth budget exceeded");
    }
  }
  ArithmeticParser parser(&tokens);
  parser.removeErrorListeners();
  parser.addErrorListener(&errors);
  EvaluateTree visitor;
  return checked_number(std::any_cast<double>(visitor.visit(parser.start())));
}

double apply_operator(std::string operation, double left, double right) {
  if ((operation == "+")) {
    return checked_number((left + right));
  }
  if ((operation == "-")) {
    return checked_number((left - right));
  }
  if ((operation == "*")) {
    return checked_number((left * right));
  }
  if ((operation == "/")) {
    if ((!(!(right == 0.0)))) {
      throw std::runtime_error("Division by zero");
    }
    return checked_number((left / right));
  }
  throw std::runtime_error("Operation is not enabled");
}
struct Controller {
  std::string entry = "";
  bool after_result = false;
  std::string error = "";
  double last_value = 0.0;
  double memory = 0.0;
  void clear() {
    entry = "";
    error = "";
    after_result = false;
  }
  void write_result(double value) {
    last_value = value;
    entry = number_text(value);
    after_result = true;
    error = "";
  }
  double current_value() {
    if ((entry == "")) {
      return 0.0;
    }
    return evaluate_expression(entry);
  }
  void append(std::string text, bool fresh) {
    if ((error != "")) {
      clear();
    }
    if ((after_result && fresh)) {
      clear();
    }
    if (((length(entry) + length(text)) > 1024.0)) {
      error = "Expression too long";
      return;
    }
    after_result = false;
    entry = (entry + text);
  }
  void backspace() {
    if ((after_result || (error != ""))) {
      clear();
      return;
    }
    entry = truncate_last(entry);
  }
  void evaluate() {
    try {
      write_result(current_value());
    } catch (const std::exception &caught) {
      const std::string problem = caught.what();
      error = problem;
      after_result = false;
    }
  }
  std::string render() {
    if ((error != "")) {
      return ("Error: " + error);
    }
    if (after_result) {
      return display_number(last_value, 12.0);
    }
    if ((entry == "")) {
      return "0";
    }
    return entry;
  }
  void memory_add() {
    try {
      const auto operand = current_value();
      memory = checked_number((memory + operand));
      error = "";
    } catch (const std::exception &caught) {
      const std::string problem = caught.what();
      error = problem;
      after_result = false;
    }
  }
  void memory_subtract() {
    try {
      const auto operand = current_value();
      memory = checked_number((memory - operand));
      error = "";
    } catch (const std::exception &caught) {
      const std::string problem = caught.what();
      error = problem;
      after_result = false;
    }
  }
  void memory_clear() { memory = 0.0; }
  void memory_recall() { write_result(memory); }
  std::string memory_display() {
    return ("Memory: " + display_number(memory, 12.0));
  }
};
// Generic Qt widget and event primitives; independent of app business logic.
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>
#include <functional>
#include <map>
#include <vector>

class GenericWindow : public QWidget {
  QGridLayout *grid;
  std::vector<std::function<void()>> updates;
  std::map<std::string, std::function<void()>> keys;

public:
  GenericWindow() : grid(new QGridLayout(this)) {
    setMinimumSize(320, 360);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("QPushButton {min-height:40px;font-size:18px;} QLineEdit "
                  "{font-size:24px;min-height:52px;} QLabel {font-size:12px;}");
  }
  void add_display(const std::string &identifier, const std::string &kind,
                   int row, int col, int span,
                   std::function<std::string()> getter) {
    if (kind == "display") {
      auto *widget = new QLineEdit;
      widget->setObjectName(QString::fromStdString(identifier));
      widget->setReadOnly(true);
      widget->setAlignment(Qt::AlignRight);
      widget->setFocusPolicy(Qt::NoFocus);
      grid->addWidget(widget, row, col, 1, span);
      updates.push_back([widget, getter] {
        widget->setText(QString::fromStdString(getter()));
      });
    } else {
      auto *widget = new QLabel;
      widget->setObjectName(QString::fromStdString(identifier));
      grid->addWidget(widget, row, col, 1, span);
      updates.push_back([widget, getter] {
        widget->setText(QString::fromStdString(getter()));
      });
    }
  }
  void add_button(const std::string &identifier, const std::string &label,
                  int row, int col, std::function<void()> handler) {
    auto *button = new QPushButton(QString::fromStdString(label));
    button->setObjectName(QString::fromStdString(identifier));
    button->setFocusPolicy(Qt::NoFocus);
    QObject::connect(button, &QPushButton::clicked, this, [this, handler] {
      handler();
      refresh();
    });
    grid->addWidget(button, row, col);
  }
  void bind_key(const std::string &key, std::function<void()> handler) {
    keys[key] = handler;
  }
  void refresh() {
    for (auto &update : updates)
      update();
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event->modifiers() &
        (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
      QWidget::keyPressEvent(event);
      return;
    }
    std::string key = event->text().toStdString();
    switch (event->key()) {
    case Qt::Key_Return:
      key = "Return";
      break;
    case Qt::Key_Enter:
      key = "Enter";
      break;
    case Qt::Key_Escape:
      key = "Escape";
      break;
    case Qt::Key_Backspace:
      key = "Backspace";
      break;
    case Qt::Key_Delete:
      key = "Delete";
      break;
    default:
      break;
    }
    auto found = keys.find(key);
    if (found == keys.end())
      QWidget::keyPressEvent(event);
    else {
      found->second();
      refresh();
      event->accept();
    }
  }
};

class ApplicationWindow : public GenericWindow {
public:
  Controller controller;
  ApplicationWindow() {
    setWindowTitle(QString::fromUtf8("Calculator"));
    add_display("display", "display", 0, 0, 4,
                [this] { return controller.render(); });
    add_display("memory_display", "label", 1, 0, 4,
                [this] { return controller.memory_display(); });
    add_button("memory_clear", "MC", 2, 0,
               [this] { controller.memory_clear(); });
    add_button("memory_recall", "MR", 2, 1,
               [this] { controller.memory_recall(); });
    add_button("memory_add", "M+", 2, 2, [this] { controller.memory_add(); });
    add_button("memory_subtract", "M-", 2, 3,
               [this] { controller.memory_subtract(); });
    add_button("clear", "C", 3, 0, [this] { controller.clear(); });
    add_button("backspace", "⌫", 3, 1, [this] { controller.backspace(); });
    add_button("lparen", "(", 3, 2, [this] { controller.append("(", true); });
    add_button("rparen", ")", 3, 3, [this] { controller.append(")", false); });
    add_button("digit_7", "7", 4, 0, [this] { controller.append("7", true); });
    add_button("digit_8", "8", 4, 1, [this] { controller.append("8", true); });
    add_button("digit_9", "9", 4, 2, [this] { controller.append("9", true); });
    add_button("digit_4", "4", 5, 0, [this] { controller.append("4", true); });
    add_button("digit_5", "5", 5, 1, [this] { controller.append("5", true); });
    add_button("digit_6", "6", 5, 2, [this] { controller.append("6", true); });
    add_button("digit_1", "1", 6, 0, [this] { controller.append("1", true); });
    add_button("digit_2", "2", 6, 1, [this] { controller.append("2", true); });
    add_button("digit_3", "3", 6, 2, [this] { controller.append("3", true); });
    add_button("digit_0", "0", 7, 0, [this] { controller.append("0", true); });
    add_button("decimal", ".", 7, 1, [this] { controller.append(".", true); });
    add_button("evaluate", "=", 7, 2, [this] { controller.evaluate(); });
    add_button("divide", "÷", 4, 3, [this] { controller.append("/", false); });
    add_button("multiply", "×", 5, 3,
               [this] { controller.append("*", false); });
    add_button("subtract", "−", 6, 3,
               [this] { controller.append("-", false); });
    add_button("add", "+", 7, 3, [this] { controller.append("+", false); });
    bind_key("0", [this] { controller.append("0", true); });
    bind_key("1", [this] { controller.append("1", true); });
    bind_key("2", [this] { controller.append("2", true); });
    bind_key("3", [this] { controller.append("3", true); });
    bind_key("4", [this] { controller.append("4", true); });
    bind_key("5", [this] { controller.append("5", true); });
    bind_key("6", [this] { controller.append("6", true); });
    bind_key("7", [this] { controller.append("7", true); });
    bind_key("8", [this] { controller.append("8", true); });
    bind_key("9", [this] { controller.append("9", true); });
    bind_key(".", [this] { controller.append(".", true); });
    bind_key("+", [this] { controller.append("+", false); });
    bind_key("-", [this] { controller.append("-", false); });
    bind_key("*", [this] { controller.append("*", false); });
    bind_key("/", [this] { controller.append("/", false); });
    bind_key("(", [this] { controller.append("(", true); });
    bind_key(")", [this] { controller.append(")", false); });
    bind_key("Return", [this] { controller.evaluate(); });
    bind_key("Enter", [this] { controller.evaluate(); });
    bind_key("=", [this] { controller.evaluate(); });
    bind_key("Escape", [this] { controller.clear(); });
    bind_key("Delete", [this] { controller.clear(); });
    bind_key("c", [this] { controller.clear(); });
    bind_key("C", [this] { controller.clear(); });
    bind_key("Backspace", [this] { controller.backspace(); });
    refresh();
  }
};
#ifndef UNLLM_APP_LIBRARY
int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setStyle("Fusion");
  ApplicationWindow window;
  window.show();
  return application.exec();
}
#endif
