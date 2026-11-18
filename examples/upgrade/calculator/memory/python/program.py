ALLOW_PARENTHESES = True
'Reusable ANTLR expression evaluation component; no UI or application state.'
import math
from antlr4 import InputStream, CommonTokenStream
from antlr4.error.ErrorListener import ErrorListener
from ArithmeticLexer import ArithmeticLexer
from ArithmeticParser import ArithmeticParser
from ArithmeticVisitor import ArithmeticVisitor

def checked_number(value):
    if not math.isfinite(value) or abs(value) > 1000000000000000.0:
        raise ValueError('Number outside finite +/-1e15 domain')
    return value

def number_text(value):
    return format(0.0 if value == 0 else checked_number(value), '.17g')

def display_number(value, precision):
    return format(0.0 if value == 0 else checked_number(value), '.' + str(int(precision)) + 'g')

def length(text):
    return len(text)

def truncate_last(text):
    return text[:-1]

class RejectErrors(ErrorListener):

    def syntaxError(self, recognizer, offendingSymbol, line, column, msg, error):
        raise ValueError('Invalid expression')

class EvaluateTree(ArithmeticVisitor):

    def visitStart(self, ctx):
        return self.visit(ctx.expression())

    def visitExpression(self, ctx):
        items = ctx.term()
        value = self.visit(items[0])
        for index, item in enumerate(items[1:], 1):
            value = apply_operator(ctx.getChild(2 * index - 1).getText(), value, self.visit(item))
        return value

    def visitTerm(self, ctx):
        items = ctx.unary()
        value = self.visit(items[0])
        for index, item in enumerate(items[1:], 1):
            value = apply_operator(ctx.getChild(2 * index - 1).getText(), value, self.visit(item))
        return value

    def visitUnary(self, ctx):
        child = ctx.unary()
        if child is None:
            return self.visit(ctx.primary())
        value = self.visit(child)
        return checked_number(-value if ctx.getChild(0).getText() == '-' else value)

    def visitPrimary(self, ctx):
        if ctx.NUMBER() is not None:
            return checked_number(float(ctx.NUMBER().getText()))
        return self.visit(ctx.expression())

def evaluate_expression(text):
    if not text or len(text) > 1024:
        raise ValueError('Expression is empty or too long')
    lexer = ArithmeticLexer(InputStream(text))
    lexer.removeErrorListeners()
    lexer.addErrorListener(RejectErrors())
    tokens = CommonTokenStream(lexer)
    tokens.fill()
    if len(tokens.tokens) > 256:
        raise ValueError('Expression token budget exceeded')
    depth = 0
    unary_count = 0
    expecting_operand = True
    for token in tokens.tokens:
        if token.type in (ArithmeticLexer.PLUS, ArithmeticLexer.MINUS) and expecting_operand:
            unary_count += 1
            if unary_count > 32:
                raise ValueError('Unary sign budget exceeded')
        if token.type in (ArithmeticLexer.NUMBER, ArithmeticLexer.RPAREN):
            expecting_operand = False
        elif token.type != -1:
            expecting_operand = True
        if token.type in (ArithmeticLexer.LPAREN, ArithmeticLexer.RPAREN):
            if not ALLOW_PARENTHESES:
                raise ValueError('Parentheses are not enabled')
            depth += 1 if token.type == ArithmeticLexer.LPAREN else -1
            if depth > 32:
                raise ValueError('Parenthesis depth budget exceeded')
    parser = ArithmeticParser(tokens)
    parser.removeErrorListeners()
    parser.addErrorListener(RejectErrors())
    return checked_number(EvaluateTree().visit(parser.start()))

def apply_operator(operation, left, right):
    if operation == '+':
        return checked_number(left + right)
    if operation == '-':
        return checked_number(left - right)
    if operation == '*':
        return checked_number(left * right)
    if operation == '/':
        if not not right == 0.0:
            raise ValueError('Division by zero')
        return checked_number(left / right)
    raise ValueError('Operation is not enabled')

class Controller:

    def __init__(self):
        self.entry = ''
        self.after_result = False
        self.error = ''
        self.last_value = 0.0
        self.memory = 0.0

    def clear(self):
        self.entry = ''
        self.error = ''
        self.after_result = False

    def write_result(self, value):
        self.last_value = value
        self.entry = number_text(value)
        self.after_result = True
        self.error = ''

    def current_value(self):
        if self.entry == '':
            return 0.0
        return evaluate_expression(self.entry)

    def append(self, text, fresh):
        if self.error != '':
            self.clear()
        if self.after_result and fresh:
            self.clear()
        if length(self.entry) + length(text) > 1024.0:
            self.error = 'Expression too long'
            return
        self.after_result = False
        self.entry = self.entry + text

    def backspace(self):
        if self.after_result or self.error != '':
            self.clear()
            return
        self.entry = truncate_last(self.entry)

    def evaluate(self):
        try:
            self.write_result(self.current_value())
        except ValueError as caught:
            problem = str(caught)
            self.error = problem
            self.after_result = False

    def render(self):
        if self.error != '':
            return 'Error: ' + self.error
        if self.after_result:
            return display_number(self.last_value, 12)
        if self.entry == '':
            return '0'
        return self.entry

    def memory_add(self):
        try:
            operand = self.current_value()
            self.memory = checked_number(self.memory + operand)
            self.error = ''
        except ValueError as caught:
            problem = str(caught)
            self.error = problem
            self.after_result = False

    def memory_subtract(self):
        try:
            operand = self.current_value()
            self.memory = checked_number(self.memory - operand)
            self.error = ''
        except ValueError as caught:
            problem = str(caught)
            self.error = problem
            self.after_result = False

    def memory_clear(self):
        self.memory = 0.0

    def memory_recall(self):
        self.write_result(self.memory)

    def memory_display(self):
        return 'Memory: ' + display_number(self.memory, 12)
'Generic Qt widget construction and event wiring; no calculator logic.'
import sys
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication, QWidget, QGridLayout, QLineEdit, QLabel, QPushButton

class GenericWindow(QWidget):

    def __init__(self):
        super().__init__()
        self.grid = QGridLayout(self)
        self.updates = []
        self.keys = {}
        self.setMinimumSize(320, 360)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setStyleSheet('QPushButton {min-height:40px;font-size:18px;} QLineEdit {font-size:24px;min-height:52px;} QLabel {font-size:12px;}')

    def add_display(self, identifier, kind, row, column, span, getter):
        widget = QLineEdit() if kind == 'display' else QLabel()
        widget.setObjectName(identifier)
        if kind == 'display':
            widget.setReadOnly(True)
            widget.setAlignment(Qt.AlignmentFlag.AlignRight)
            widget.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.grid.addWidget(widget, row, column, 1, span)
        self.updates.append((widget, getter))

    def add_button(self, identifier, label, row, column, handler):
        button = QPushButton(label)
        button.setObjectName(identifier)
        button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        button.clicked.connect(lambda checked=False: self.perform(handler))
        self.grid.addWidget(button, row, column)

    def bind_key(self, key, handler):
        self.keys[key] = handler

    def refresh(self):
        for widget, getter in self.updates:
            widget.setText(getter())

    def perform(self, handler):
        handler()
        self.refresh()

    def keyPressEvent(self, event):
        if event.modifiers() & (Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.AltModifier | Qt.KeyboardModifier.MetaModifier):
            return super().keyPressEvent(event)
        names = {Qt.Key.Key_Return: 'Return', Qt.Key.Key_Enter: 'Enter', Qt.Key.Key_Escape: 'Escape', Qt.Key.Key_Backspace: 'Backspace', Qt.Key.Key_Delete: 'Delete'}
        key = names.get(event.key(), event.text())
        if key in self.keys:
            self.perform(self.keys[key])
            event.accept()
        else:
            super().keyPressEvent(event)

class ApplicationWindow(GenericWindow):

    def __init__(self):
        super().__init__()
        self.controller = Controller()
        self.setWindowTitle('Calculator')
        self.add_display('display', 'display', 0, 0, 4, lambda: self.controller.render())
        self.add_display('memory_display', 'label', 1, 0, 4, lambda: self.controller.memory_display())
        self.add_button('memory_clear', 'MC', 2, 0, lambda: self.controller.memory_clear())
        self.add_button('memory_recall', 'MR', 2, 1, lambda: self.controller.memory_recall())
        self.add_button('memory_add', 'M+', 2, 2, lambda: self.controller.memory_add())
        self.add_button('memory_subtract', 'M-', 2, 3, lambda: self.controller.memory_subtract())
        self.add_button('clear', 'C', 3, 0, lambda: self.controller.clear())
        self.add_button('backspace', '⌫', 3, 1, lambda: self.controller.backspace())
        self.add_button('lparen', '(', 3, 2, lambda: self.controller.append('(', True))
        self.add_button('rparen', ')', 3, 3, lambda: self.controller.append(')', False))
        self.add_button('digit_7', '7', 4, 0, lambda: self.controller.append('7', True))
        self.add_button('digit_8', '8', 4, 1, lambda: self.controller.append('8', True))
        self.add_button('digit_9', '9', 4, 2, lambda: self.controller.append('9', True))
        self.add_button('digit_4', '4', 5, 0, lambda: self.controller.append('4', True))
        self.add_button('digit_5', '5', 5, 1, lambda: self.controller.append('5', True))
        self.add_button('digit_6', '6', 5, 2, lambda: self.controller.append('6', True))
        self.add_button('digit_1', '1', 6, 0, lambda: self.controller.append('1', True))
        self.add_button('digit_2', '2', 6, 1, lambda: self.controller.append('2', True))
        self.add_button('digit_3', '3', 6, 2, lambda: self.controller.append('3', True))
        self.add_button('digit_0', '0', 7, 0, lambda: self.controller.append('0', True))
        self.add_button('decimal', '.', 7, 1, lambda: self.controller.append('.', True))
        self.add_button('evaluate', '=', 7, 2, lambda: self.controller.evaluate())
        self.add_button('divide', '÷', 4, 3, lambda: self.controller.append('/', False))
        self.add_button('multiply', '×', 5, 3, lambda: self.controller.append('*', False))
        self.add_button('subtract', '−', 6, 3, lambda: self.controller.append('-', False))
        self.add_button('add', '+', 7, 3, lambda: self.controller.append('+', False))
        self.bind_key('0', lambda: self.controller.append('0', True))
        self.bind_key('1', lambda: self.controller.append('1', True))
        self.bind_key('2', lambda: self.controller.append('2', True))
        self.bind_key('3', lambda: self.controller.append('3', True))
        self.bind_key('4', lambda: self.controller.append('4', True))
        self.bind_key('5', lambda: self.controller.append('5', True))
        self.bind_key('6', lambda: self.controller.append('6', True))
        self.bind_key('7', lambda: self.controller.append('7', True))
        self.bind_key('8', lambda: self.controller.append('8', True))
        self.bind_key('9', lambda: self.controller.append('9', True))
        self.bind_key('.', lambda: self.controller.append('.', True))
        self.bind_key('+', lambda: self.controller.append('+', False))
        self.bind_key('-', lambda: self.controller.append('-', False))
        self.bind_key('*', lambda: self.controller.append('*', False))
        self.bind_key('/', lambda: self.controller.append('/', False))
        self.bind_key('(', lambda: self.controller.append('(', True))
        self.bind_key(')', lambda: self.controller.append(')', False))
        self.bind_key('Return', lambda: self.controller.evaluate())
        self.bind_key('Enter', lambda: self.controller.evaluate())
        self.bind_key('=', lambda: self.controller.evaluate())
        self.bind_key('Escape', lambda: self.controller.clear())
        self.bind_key('Delete', lambda: self.controller.clear())
        self.bind_key('c', lambda: self.controller.clear())
        self.bind_key('C', lambda: self.controller.clear())
        self.bind_key('Backspace', lambda: self.controller.backspace())
        self.refresh()

def main():
    application = QApplication(sys.argv)
    application.setStyle('Fusion')
    window = ApplicationWindow()
    window.show()
    return application.exec()
if __name__ == '__main__':
    raise SystemExit(main())
