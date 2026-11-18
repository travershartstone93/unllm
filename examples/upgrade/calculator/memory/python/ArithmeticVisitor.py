# Generated from /home/whisper/repo-work/unllm-upgrade/synthesis/apps/Arithmetic.g4 by ANTLR 4.13.2
from antlr4 import *
if "." in __name__:
    from .ArithmeticParser import ArithmeticParser
else:
    from ArithmeticParser import ArithmeticParser

# This class defines a complete generic visitor for a parse tree produced by ArithmeticParser.

class ArithmeticVisitor(ParseTreeVisitor):

    # Visit a parse tree produced by ArithmeticParser#start.
    def visitStart(self, ctx:ArithmeticParser.StartContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ArithmeticParser#expression.
    def visitExpression(self, ctx:ArithmeticParser.ExpressionContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ArithmeticParser#term.
    def visitTerm(self, ctx:ArithmeticParser.TermContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ArithmeticParser#unary.
    def visitUnary(self, ctx:ArithmeticParser.UnaryContext):
        return self.visitChildren(ctx)


    # Visit a parse tree produced by ArithmeticParser#primary.
    def visitPrimary(self, ctx:ArithmeticParser.PrimaryContext):
        return self.visitChildren(ctx)



del ArithmeticParser