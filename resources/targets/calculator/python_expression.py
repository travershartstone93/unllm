"""Reusable ANTLR expression evaluation component; no UI or application state."""
import math
from antlr4 import InputStream, CommonTokenStream
from antlr4.error.ErrorListener import ErrorListener
from ArithmeticLexer import ArithmeticLexer
from ArithmeticParser import ArithmeticParser
from ArithmeticVisitor import ArithmeticVisitor


def checked_number(value):
    if not math.isfinite(value) or abs(value)>1e15:
        raise ValueError("Number outside finite +/-1e15 domain")
    return value


def number_text(value):
    return format(0.0 if value==0 else checked_number(value),".17g")


def display_number(value,precision):
    return format(0.0 if value==0 else checked_number(value),"."+str(int(precision))+"g")


def length(text):return len(text)
def truncate_last(text):return text[:-1]


class RejectErrors(ErrorListener):
    def syntaxError(self,recognizer,offendingSymbol,line,column,msg,error):
        raise ValueError("Invalid expression")


class EvaluateTree(ArithmeticVisitor):
    def visitStart(self,ctx):return self.visit(ctx.expression())
    def visitExpression(self,ctx):
        items=ctx.term();value=self.visit(items[0])
        for index,item in enumerate(items[1:],1):
            value=apply_operator(ctx.getChild(2*index-1).getText(),value,self.visit(item))
        return value
    def visitTerm(self,ctx):
        items=ctx.unary();value=self.visit(items[0])
        for index,item in enumerate(items[1:],1):
            value=apply_operator(ctx.getChild(2*index-1).getText(),value,self.visit(item))
        return value
    def visitUnary(self,ctx):
        child=ctx.unary()
        if child is None:return self.visit(ctx.primary())
        value=self.visit(child)
        return checked_number(-value if ctx.getChild(0).getText()=="-" else value)
    def visitPrimary(self,ctx):
        if ctx.NUMBER() is not None:return checked_number(float(ctx.NUMBER().getText()))
        return self.visit(ctx.expression())


def evaluate_expression(text):
    if not text or len(text)>1024:raise ValueError("Expression is empty or too long")
    lexer=ArithmeticLexer(InputStream(text));lexer.removeErrorListeners();lexer.addErrorListener(RejectErrors())
    tokens=CommonTokenStream(lexer);tokens.fill()
    if len(tokens.tokens)>256:raise ValueError("Expression token budget exceeded")
    depth=0;unary_count=0;expecting_operand=True
    for token in tokens.tokens:
        if token.type in (ArithmeticLexer.PLUS,ArithmeticLexer.MINUS) and expecting_operand:
            unary_count+=1
            if unary_count>32:raise ValueError("Unary sign budget exceeded")
        if token.type in (ArithmeticLexer.NUMBER,ArithmeticLexer.RPAREN):expecting_operand=False
        elif token.type!=-1:expecting_operand=True
        if token.type in (ArithmeticLexer.LPAREN,ArithmeticLexer.RPAREN):
            if not ALLOW_PARENTHESES:raise ValueError("Parentheses are not enabled")
            depth+=1 if token.type==ArithmeticLexer.LPAREN else -1
            if depth>32:raise ValueError("Parenthesis depth budget exceeded")
    parser=ArithmeticParser(tokens);parser.removeErrorListeners();parser.addErrorListener(RejectErrors())
    return checked_number(EvaluateTree().visit(parser.start()))
