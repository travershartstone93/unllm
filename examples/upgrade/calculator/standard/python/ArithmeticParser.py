# Generated from /home/whisper/repo-work/unllm-upgrade/synthesis/apps/Arithmetic.g4 by ANTLR 4.13.2
# encoding: utf-8
from antlr4 import *
from io import StringIO
import sys
if sys.version_info[1] > 5:
	from typing import TextIO
else:
	from typing.io import TextIO

def serializedATN():
    return [
        4,1,8,42,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,1,0,1,0,1,0,1,1,
        1,1,1,1,5,1,17,8,1,10,1,12,1,20,9,1,1,2,1,2,1,2,5,2,25,8,2,10,2,
        12,2,28,9,2,1,3,1,3,1,3,3,3,33,8,3,1,4,1,4,1,4,1,4,1,4,3,4,40,8,
        4,1,4,0,0,5,0,2,4,6,8,0,2,1,0,1,2,1,0,3,4,40,0,10,1,0,0,0,2,13,1,
        0,0,0,4,21,1,0,0,0,6,32,1,0,0,0,8,39,1,0,0,0,10,11,3,2,1,0,11,12,
        5,0,0,1,12,1,1,0,0,0,13,18,3,4,2,0,14,15,7,0,0,0,15,17,3,4,2,0,16,
        14,1,0,0,0,17,20,1,0,0,0,18,16,1,0,0,0,18,19,1,0,0,0,19,3,1,0,0,
        0,20,18,1,0,0,0,21,26,3,6,3,0,22,23,7,1,0,0,23,25,3,6,3,0,24,22,
        1,0,0,0,25,28,1,0,0,0,26,24,1,0,0,0,26,27,1,0,0,0,27,5,1,0,0,0,28,
        26,1,0,0,0,29,30,7,0,0,0,30,33,3,6,3,0,31,33,3,8,4,0,32,29,1,0,0,
        0,32,31,1,0,0,0,33,7,1,0,0,0,34,40,5,7,0,0,35,36,5,5,0,0,36,37,3,
        2,1,0,37,38,5,6,0,0,38,40,1,0,0,0,39,34,1,0,0,0,39,35,1,0,0,0,40,
        9,1,0,0,0,4,18,26,32,39
    ]

class ArithmeticParser ( Parser ):

    grammarFileName = "Arithmetic.g4"

    atn = ATNDeserializer().deserialize(serializedATN())

    decisionsToDFA = [ DFA(ds, i) for i, ds in enumerate(atn.decisionToState) ]

    sharedContextCache = PredictionContextCache()

    literalNames = [ "<INVALID>", "'+'", "'-'", "'*'", "'/'", "'('", "')'" ]

    symbolicNames = [ "<INVALID>", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", 
                      "RPAREN", "NUMBER", "WS" ]

    RULE_start = 0
    RULE_expression = 1
    RULE_term = 2
    RULE_unary = 3
    RULE_primary = 4

    ruleNames =  [ "start", "expression", "term", "unary", "primary" ]

    EOF = Token.EOF
    PLUS=1
    MINUS=2
    STAR=3
    SLASH=4
    LPAREN=5
    RPAREN=6
    NUMBER=7
    WS=8

    def __init__(self, input:TokenStream, output:TextIO = sys.stdout):
        super().__init__(input, output)
        self.checkVersion("4.13.2")
        self._interp = ParserATNSimulator(self, self.atn, self.decisionsToDFA, self.sharedContextCache)
        self._predicates = None




    class StartContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def expression(self):
            return self.getTypedRuleContext(ArithmeticParser.ExpressionContext,0)


        def EOF(self):
            return self.getToken(ArithmeticParser.EOF, 0)

        def getRuleIndex(self):
            return ArithmeticParser.RULE_start

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitStart" ):
                return visitor.visitStart(self)
            else:
                return visitor.visitChildren(self)




    def start(self):

        localctx = ArithmeticParser.StartContext(self, self._ctx, self.state)
        self.enterRule(localctx, 0, self.RULE_start)
        try:
            self.enterOuterAlt(localctx, 1)
            self.state = 10
            self.expression()
            self.state = 11
            self.match(ArithmeticParser.EOF)
        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class ExpressionContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def term(self, i:int=None):
            if i is None:
                return self.getTypedRuleContexts(ArithmeticParser.TermContext)
            else:
                return self.getTypedRuleContext(ArithmeticParser.TermContext,i)


        def PLUS(self, i:int=None):
            if i is None:
                return self.getTokens(ArithmeticParser.PLUS)
            else:
                return self.getToken(ArithmeticParser.PLUS, i)

        def MINUS(self, i:int=None):
            if i is None:
                return self.getTokens(ArithmeticParser.MINUS)
            else:
                return self.getToken(ArithmeticParser.MINUS, i)

        def getRuleIndex(self):
            return ArithmeticParser.RULE_expression

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitExpression" ):
                return visitor.visitExpression(self)
            else:
                return visitor.visitChildren(self)




    def expression(self):

        localctx = ArithmeticParser.ExpressionContext(self, self._ctx, self.state)
        self.enterRule(localctx, 2, self.RULE_expression)
        self._la = 0 # Token type
        try:
            self.enterOuterAlt(localctx, 1)
            self.state = 13
            self.term()
            self.state = 18
            self._errHandler.sync(self)
            _la = self._input.LA(1)
            while _la==1 or _la==2:
                self.state = 14
                _la = self._input.LA(1)
                if not(_la==1 or _la==2):
                    self._errHandler.recoverInline(self)
                else:
                    self._errHandler.reportMatch(self)
                    self.consume()
                self.state = 15
                self.term()
                self.state = 20
                self._errHandler.sync(self)
                _la = self._input.LA(1)

        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class TermContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def unary(self, i:int=None):
            if i is None:
                return self.getTypedRuleContexts(ArithmeticParser.UnaryContext)
            else:
                return self.getTypedRuleContext(ArithmeticParser.UnaryContext,i)


        def STAR(self, i:int=None):
            if i is None:
                return self.getTokens(ArithmeticParser.STAR)
            else:
                return self.getToken(ArithmeticParser.STAR, i)

        def SLASH(self, i:int=None):
            if i is None:
                return self.getTokens(ArithmeticParser.SLASH)
            else:
                return self.getToken(ArithmeticParser.SLASH, i)

        def getRuleIndex(self):
            return ArithmeticParser.RULE_term

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitTerm" ):
                return visitor.visitTerm(self)
            else:
                return visitor.visitChildren(self)




    def term(self):

        localctx = ArithmeticParser.TermContext(self, self._ctx, self.state)
        self.enterRule(localctx, 4, self.RULE_term)
        self._la = 0 # Token type
        try:
            self.enterOuterAlt(localctx, 1)
            self.state = 21
            self.unary()
            self.state = 26
            self._errHandler.sync(self)
            _la = self._input.LA(1)
            while _la==3 or _la==4:
                self.state = 22
                _la = self._input.LA(1)
                if not(_la==3 or _la==4):
                    self._errHandler.recoverInline(self)
                else:
                    self._errHandler.reportMatch(self)
                    self.consume()
                self.state = 23
                self.unary()
                self.state = 28
                self._errHandler.sync(self)
                _la = self._input.LA(1)

        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class UnaryContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def unary(self):
            return self.getTypedRuleContext(ArithmeticParser.UnaryContext,0)


        def PLUS(self):
            return self.getToken(ArithmeticParser.PLUS, 0)

        def MINUS(self):
            return self.getToken(ArithmeticParser.MINUS, 0)

        def primary(self):
            return self.getTypedRuleContext(ArithmeticParser.PrimaryContext,0)


        def getRuleIndex(self):
            return ArithmeticParser.RULE_unary

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitUnary" ):
                return visitor.visitUnary(self)
            else:
                return visitor.visitChildren(self)




    def unary(self):

        localctx = ArithmeticParser.UnaryContext(self, self._ctx, self.state)
        self.enterRule(localctx, 6, self.RULE_unary)
        self._la = 0 # Token type
        try:
            self.state = 32
            self._errHandler.sync(self)
            token = self._input.LA(1)
            if token in [1, 2]:
                self.enterOuterAlt(localctx, 1)
                self.state = 29
                _la = self._input.LA(1)
                if not(_la==1 or _la==2):
                    self._errHandler.recoverInline(self)
                else:
                    self._errHandler.reportMatch(self)
                    self.consume()
                self.state = 30
                self.unary()
                pass
            elif token in [5, 7]:
                self.enterOuterAlt(localctx, 2)
                self.state = 31
                self.primary()
                pass
            else:
                raise NoViableAltException(self)

        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx


    class PrimaryContext(ParserRuleContext):
        __slots__ = 'parser'

        def __init__(self, parser, parent:ParserRuleContext=None, invokingState:int=-1):
            super().__init__(parent, invokingState)
            self.parser = parser

        def NUMBER(self):
            return self.getToken(ArithmeticParser.NUMBER, 0)

        def LPAREN(self):
            return self.getToken(ArithmeticParser.LPAREN, 0)

        def expression(self):
            return self.getTypedRuleContext(ArithmeticParser.ExpressionContext,0)


        def RPAREN(self):
            return self.getToken(ArithmeticParser.RPAREN, 0)

        def getRuleIndex(self):
            return ArithmeticParser.RULE_primary

        def accept(self, visitor:ParseTreeVisitor):
            if hasattr( visitor, "visitPrimary" ):
                return visitor.visitPrimary(self)
            else:
                return visitor.visitChildren(self)




    def primary(self):

        localctx = ArithmeticParser.PrimaryContext(self, self._ctx, self.state)
        self.enterRule(localctx, 8, self.RULE_primary)
        try:
            self.state = 39
            self._errHandler.sync(self)
            token = self._input.LA(1)
            if token in [7]:
                self.enterOuterAlt(localctx, 1)
                self.state = 34
                self.match(ArithmeticParser.NUMBER)
                pass
            elif token in [5]:
                self.enterOuterAlt(localctx, 2)
                self.state = 35
                self.match(ArithmeticParser.LPAREN)
                self.state = 36
                self.expression()
                self.state = 37
                self.match(ArithmeticParser.RPAREN)
                pass
            else:
                raise NoViableAltException(self)

        except RecognitionException as re:
            localctx.exception = re
            self._errHandler.reportError(self, re)
            self._errHandler.recover(self, re)
        finally:
            self.exitRule()
        return localctx





