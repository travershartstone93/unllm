
// Generated from /home/whisper/repo-work/unllm-upgrade/synthesis/apps/Arithmetic.g4 by ANTLR 4.13.2


#include "ArithmeticVisitor.h"

#include "ArithmeticParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct ArithmeticParserStaticData final {
  ArithmeticParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  ArithmeticParserStaticData(const ArithmeticParserStaticData&) = delete;
  ArithmeticParserStaticData(ArithmeticParserStaticData&&) = delete;
  ArithmeticParserStaticData& operator=(const ArithmeticParserStaticData&) = delete;
  ArithmeticParserStaticData& operator=(ArithmeticParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag arithmeticParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
std::unique_ptr<ArithmeticParserStaticData> arithmeticParserStaticData = nullptr;

void arithmeticParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (arithmeticParserStaticData != nullptr) {
    return;
  }
#else
  assert(arithmeticParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<ArithmeticParserStaticData>(
    std::vector<std::string>{
      "start", "expression", "term", "unary", "primary"
    },
    std::vector<std::string>{
      "", "'+'", "'-'", "'*'", "'/'", "'('", "')'"
    },
    std::vector<std::string>{
      "", "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", "RPAREN", "NUMBER", 
      "WS"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,8,42,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,1,0,1,0,1,0,1,1,1,1,
  	1,1,5,1,17,8,1,10,1,12,1,20,9,1,1,2,1,2,1,2,5,2,25,8,2,10,2,12,2,28,9,
  	2,1,3,1,3,1,3,3,3,33,8,3,1,4,1,4,1,4,1,4,1,4,3,4,40,8,4,1,4,0,0,5,0,2,
  	4,6,8,0,2,1,0,1,2,1,0,3,4,40,0,10,1,0,0,0,2,13,1,0,0,0,4,21,1,0,0,0,6,
  	32,1,0,0,0,8,39,1,0,0,0,10,11,3,2,1,0,11,12,5,0,0,1,12,1,1,0,0,0,13,18,
  	3,4,2,0,14,15,7,0,0,0,15,17,3,4,2,0,16,14,1,0,0,0,17,20,1,0,0,0,18,16,
  	1,0,0,0,18,19,1,0,0,0,19,3,1,0,0,0,20,18,1,0,0,0,21,26,3,6,3,0,22,23,
  	7,1,0,0,23,25,3,6,3,0,24,22,1,0,0,0,25,28,1,0,0,0,26,24,1,0,0,0,26,27,
  	1,0,0,0,27,5,1,0,0,0,28,26,1,0,0,0,29,30,7,0,0,0,30,33,3,6,3,0,31,33,
  	3,8,4,0,32,29,1,0,0,0,32,31,1,0,0,0,33,7,1,0,0,0,34,40,5,7,0,0,35,36,
  	5,5,0,0,36,37,3,2,1,0,37,38,5,6,0,0,38,40,1,0,0,0,39,34,1,0,0,0,39,35,
  	1,0,0,0,40,9,1,0,0,0,4,18,26,32,39
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  arithmeticParserStaticData = std::move(staticData);
}

}

ArithmeticParser::ArithmeticParser(TokenStream *input) : ArithmeticParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

ArithmeticParser::ArithmeticParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  ArithmeticParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *arithmeticParserStaticData->atn, arithmeticParserStaticData->decisionToDFA, arithmeticParserStaticData->sharedContextCache, options);
}

ArithmeticParser::~ArithmeticParser() {
  delete _interpreter;
}

const atn::ATN& ArithmeticParser::getATN() const {
  return *arithmeticParserStaticData->atn;
}

std::string ArithmeticParser::getGrammarFileName() const {
  return "Arithmetic.g4";
}

const std::vector<std::string>& ArithmeticParser::getRuleNames() const {
  return arithmeticParserStaticData->ruleNames;
}

const dfa::Vocabulary& ArithmeticParser::getVocabulary() const {
  return arithmeticParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView ArithmeticParser::getSerializedATN() const {
  return arithmeticParserStaticData->serializedATN;
}


//----------------- StartContext ------------------------------------------------------------------

ArithmeticParser::StartContext::StartContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

ArithmeticParser::ExpressionContext* ArithmeticParser::StartContext::expression() {
  return getRuleContext<ArithmeticParser::ExpressionContext>(0);
}

tree::TerminalNode* ArithmeticParser::StartContext::EOF() {
  return getToken(ArithmeticParser::EOF, 0);
}


size_t ArithmeticParser::StartContext::getRuleIndex() const {
  return ArithmeticParser::RuleStart;
}


std::any ArithmeticParser::StartContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<ArithmeticVisitor*>(visitor))
    return parserVisitor->visitStart(this);
  else
    return visitor->visitChildren(this);
}

ArithmeticParser::StartContext* ArithmeticParser::start() {
  StartContext *_localctx = _tracker.createInstance<StartContext>(_ctx, getState());
  enterRule(_localctx, 0, ArithmeticParser::RuleStart);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(10);
    expression();
    setState(11);
    match(ArithmeticParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExpressionContext ------------------------------------------------------------------

ArithmeticParser::ExpressionContext::ExpressionContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<ArithmeticParser::TermContext *> ArithmeticParser::ExpressionContext::term() {
  return getRuleContexts<ArithmeticParser::TermContext>();
}

ArithmeticParser::TermContext* ArithmeticParser::ExpressionContext::term(size_t i) {
  return getRuleContext<ArithmeticParser::TermContext>(i);
}

std::vector<tree::TerminalNode *> ArithmeticParser::ExpressionContext::PLUS() {
  return getTokens(ArithmeticParser::PLUS);
}

tree::TerminalNode* ArithmeticParser::ExpressionContext::PLUS(size_t i) {
  return getToken(ArithmeticParser::PLUS, i);
}

std::vector<tree::TerminalNode *> ArithmeticParser::ExpressionContext::MINUS() {
  return getTokens(ArithmeticParser::MINUS);
}

tree::TerminalNode* ArithmeticParser::ExpressionContext::MINUS(size_t i) {
  return getToken(ArithmeticParser::MINUS, i);
}


size_t ArithmeticParser::ExpressionContext::getRuleIndex() const {
  return ArithmeticParser::RuleExpression;
}


std::any ArithmeticParser::ExpressionContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<ArithmeticVisitor*>(visitor))
    return parserVisitor->visitExpression(this);
  else
    return visitor->visitChildren(this);
}

ArithmeticParser::ExpressionContext* ArithmeticParser::expression() {
  ExpressionContext *_localctx = _tracker.createInstance<ExpressionContext>(_ctx, getState());
  enterRule(_localctx, 2, ArithmeticParser::RuleExpression);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(13);
    term();
    setState(18);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == ArithmeticParser::PLUS

    || _la == ArithmeticParser::MINUS) {
      setState(14);
      _la = _input->LA(1);
      if (!(_la == ArithmeticParser::PLUS

      || _la == ArithmeticParser::MINUS)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(15);
      term();
      setState(20);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TermContext ------------------------------------------------------------------

ArithmeticParser::TermContext::TermContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<ArithmeticParser::UnaryContext *> ArithmeticParser::TermContext::unary() {
  return getRuleContexts<ArithmeticParser::UnaryContext>();
}

ArithmeticParser::UnaryContext* ArithmeticParser::TermContext::unary(size_t i) {
  return getRuleContext<ArithmeticParser::UnaryContext>(i);
}

std::vector<tree::TerminalNode *> ArithmeticParser::TermContext::STAR() {
  return getTokens(ArithmeticParser::STAR);
}

tree::TerminalNode* ArithmeticParser::TermContext::STAR(size_t i) {
  return getToken(ArithmeticParser::STAR, i);
}

std::vector<tree::TerminalNode *> ArithmeticParser::TermContext::SLASH() {
  return getTokens(ArithmeticParser::SLASH);
}

tree::TerminalNode* ArithmeticParser::TermContext::SLASH(size_t i) {
  return getToken(ArithmeticParser::SLASH, i);
}


size_t ArithmeticParser::TermContext::getRuleIndex() const {
  return ArithmeticParser::RuleTerm;
}


std::any ArithmeticParser::TermContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<ArithmeticVisitor*>(visitor))
    return parserVisitor->visitTerm(this);
  else
    return visitor->visitChildren(this);
}

ArithmeticParser::TermContext* ArithmeticParser::term() {
  TermContext *_localctx = _tracker.createInstance<TermContext>(_ctx, getState());
  enterRule(_localctx, 4, ArithmeticParser::RuleTerm);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(21);
    unary();
    setState(26);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == ArithmeticParser::STAR

    || _la == ArithmeticParser::SLASH) {
      setState(22);
      _la = _input->LA(1);
      if (!(_la == ArithmeticParser::STAR

      || _la == ArithmeticParser::SLASH)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(23);
      unary();
      setState(28);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnaryContext ------------------------------------------------------------------

ArithmeticParser::UnaryContext::UnaryContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

ArithmeticParser::UnaryContext* ArithmeticParser::UnaryContext::unary() {
  return getRuleContext<ArithmeticParser::UnaryContext>(0);
}

tree::TerminalNode* ArithmeticParser::UnaryContext::PLUS() {
  return getToken(ArithmeticParser::PLUS, 0);
}

tree::TerminalNode* ArithmeticParser::UnaryContext::MINUS() {
  return getToken(ArithmeticParser::MINUS, 0);
}

ArithmeticParser::PrimaryContext* ArithmeticParser::UnaryContext::primary() {
  return getRuleContext<ArithmeticParser::PrimaryContext>(0);
}


size_t ArithmeticParser::UnaryContext::getRuleIndex() const {
  return ArithmeticParser::RuleUnary;
}


std::any ArithmeticParser::UnaryContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<ArithmeticVisitor*>(visitor))
    return parserVisitor->visitUnary(this);
  else
    return visitor->visitChildren(this);
}

ArithmeticParser::UnaryContext* ArithmeticParser::unary() {
  UnaryContext *_localctx = _tracker.createInstance<UnaryContext>(_ctx, getState());
  enterRule(_localctx, 6, ArithmeticParser::RuleUnary);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(32);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case ArithmeticParser::PLUS:
      case ArithmeticParser::MINUS: {
        enterOuterAlt(_localctx, 1);
        setState(29);
        _la = _input->LA(1);
        if (!(_la == ArithmeticParser::PLUS

        || _la == ArithmeticParser::MINUS)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(30);
        unary();
        break;
      }

      case ArithmeticParser::LPAREN:
      case ArithmeticParser::NUMBER: {
        enterOuterAlt(_localctx, 2);
        setState(31);
        primary();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimaryContext ------------------------------------------------------------------

ArithmeticParser::PrimaryContext::PrimaryContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* ArithmeticParser::PrimaryContext::NUMBER() {
  return getToken(ArithmeticParser::NUMBER, 0);
}

tree::TerminalNode* ArithmeticParser::PrimaryContext::LPAREN() {
  return getToken(ArithmeticParser::LPAREN, 0);
}

ArithmeticParser::ExpressionContext* ArithmeticParser::PrimaryContext::expression() {
  return getRuleContext<ArithmeticParser::ExpressionContext>(0);
}

tree::TerminalNode* ArithmeticParser::PrimaryContext::RPAREN() {
  return getToken(ArithmeticParser::RPAREN, 0);
}


size_t ArithmeticParser::PrimaryContext::getRuleIndex() const {
  return ArithmeticParser::RulePrimary;
}


std::any ArithmeticParser::PrimaryContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<ArithmeticVisitor*>(visitor))
    return parserVisitor->visitPrimary(this);
  else
    return visitor->visitChildren(this);
}

ArithmeticParser::PrimaryContext* ArithmeticParser::primary() {
  PrimaryContext *_localctx = _tracker.createInstance<PrimaryContext>(_ctx, getState());
  enterRule(_localctx, 8, ArithmeticParser::RulePrimary);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(39);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case ArithmeticParser::NUMBER: {
        enterOuterAlt(_localctx, 1);
        setState(34);
        match(ArithmeticParser::NUMBER);
        break;
      }

      case ArithmeticParser::LPAREN: {
        enterOuterAlt(_localctx, 2);
        setState(35);
        match(ArithmeticParser::LPAREN);
        setState(36);
        expression();
        setState(37);
        match(ArithmeticParser::RPAREN);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

void ArithmeticParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  arithmeticParserInitialize();
#else
  ::antlr4::internal::call_once(arithmeticParserOnceFlag, arithmeticParserInitialize);
#endif
}
