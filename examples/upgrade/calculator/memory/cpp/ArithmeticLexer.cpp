
// Generated from /home/whisper/repo-work/unllm-upgrade/synthesis/apps/Arithmetic.g4 by ANTLR 4.13.2


#include "ArithmeticLexer.h"


using namespace antlr4;



using namespace antlr4;

namespace {

struct ArithmeticLexerStaticData final {
  ArithmeticLexerStaticData(std::vector<std::string> ruleNames,
                          std::vector<std::string> channelNames,
                          std::vector<std::string> modeNames,
                          std::vector<std::string> literalNames,
                          std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), channelNames(std::move(channelNames)),
        modeNames(std::move(modeNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  ArithmeticLexerStaticData(const ArithmeticLexerStaticData&) = delete;
  ArithmeticLexerStaticData(ArithmeticLexerStaticData&&) = delete;
  ArithmeticLexerStaticData& operator=(const ArithmeticLexerStaticData&) = delete;
  ArithmeticLexerStaticData& operator=(ArithmeticLexerStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> channelNames;
  const std::vector<std::string> modeNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag arithmeticlexerLexerOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
std::unique_ptr<ArithmeticLexerStaticData> arithmeticlexerLexerStaticData = nullptr;

void arithmeticlexerLexerInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (arithmeticlexerLexerStaticData != nullptr) {
    return;
  }
#else
  assert(arithmeticlexerLexerStaticData == nullptr);
#endif
  auto staticData = std::make_unique<ArithmeticLexerStaticData>(
    std::vector<std::string>{
      "PLUS", "MINUS", "STAR", "SLASH", "LPAREN", "RPAREN", "NUMBER", "WS"
    },
    std::vector<std::string>{
      "DEFAULT_TOKEN_CHANNEL", "HIDDEN"
    },
    std::vector<std::string>{
      "DEFAULT_MODE"
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
  	4,0,8,69,6,-1,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,
  	2,7,7,7,1,0,1,0,1,1,1,1,1,2,1,2,1,3,1,3,1,4,1,4,1,5,1,5,1,6,4,6,31,8,
  	6,11,6,12,6,32,1,6,1,6,5,6,37,8,6,10,6,12,6,40,9,6,3,6,42,8,6,1,6,1,6,
  	4,6,46,8,6,11,6,12,6,47,3,6,50,8,6,1,6,1,6,3,6,54,8,6,1,6,4,6,57,8,6,
  	11,6,12,6,58,3,6,61,8,6,1,7,4,7,64,8,7,11,7,12,7,65,1,7,1,7,0,0,8,1,1,
  	3,2,5,3,7,4,9,5,11,6,13,7,15,8,1,0,4,1,0,48,57,2,0,69,69,101,101,2,0,
  	43,43,45,45,3,0,9,10,13,13,32,32,77,0,1,1,0,0,0,0,3,1,0,0,0,0,5,1,0,0,
  	0,0,7,1,0,0,0,0,9,1,0,0,0,0,11,1,0,0,0,0,13,1,0,0,0,0,15,1,0,0,0,1,17,
  	1,0,0,0,3,19,1,0,0,0,5,21,1,0,0,0,7,23,1,0,0,0,9,25,1,0,0,0,11,27,1,0,
  	0,0,13,49,1,0,0,0,15,63,1,0,0,0,17,18,5,43,0,0,18,2,1,0,0,0,19,20,5,45,
  	0,0,20,4,1,0,0,0,21,22,5,42,0,0,22,6,1,0,0,0,23,24,5,47,0,0,24,8,1,0,
  	0,0,25,26,5,40,0,0,26,10,1,0,0,0,27,28,5,41,0,0,28,12,1,0,0,0,29,31,7,
  	0,0,0,30,29,1,0,0,0,31,32,1,0,0,0,32,30,1,0,0,0,32,33,1,0,0,0,33,41,1,
  	0,0,0,34,38,5,46,0,0,35,37,7,0,0,0,36,35,1,0,0,0,37,40,1,0,0,0,38,36,
  	1,0,0,0,38,39,1,0,0,0,39,42,1,0,0,0,40,38,1,0,0,0,41,34,1,0,0,0,41,42,
  	1,0,0,0,42,50,1,0,0,0,43,45,5,46,0,0,44,46,7,0,0,0,45,44,1,0,0,0,46,47,
  	1,0,0,0,47,45,1,0,0,0,47,48,1,0,0,0,48,50,1,0,0,0,49,30,1,0,0,0,49,43,
  	1,0,0,0,50,60,1,0,0,0,51,53,7,1,0,0,52,54,7,2,0,0,53,52,1,0,0,0,53,54,
  	1,0,0,0,54,56,1,0,0,0,55,57,7,0,0,0,56,55,1,0,0,0,57,58,1,0,0,0,58,56,
  	1,0,0,0,58,59,1,0,0,0,59,61,1,0,0,0,60,51,1,0,0,0,60,61,1,0,0,0,61,14,
  	1,0,0,0,62,64,7,3,0,0,63,62,1,0,0,0,64,65,1,0,0,0,65,63,1,0,0,0,65,66,
  	1,0,0,0,66,67,1,0,0,0,67,68,6,7,0,0,68,16,1,0,0,0,10,0,32,38,41,47,49,
  	53,58,60,65,1,6,0,0
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  arithmeticlexerLexerStaticData = std::move(staticData);
}

}

ArithmeticLexer::ArithmeticLexer(CharStream *input) : Lexer(input) {
  ArithmeticLexer::initialize();
  _interpreter = new atn::LexerATNSimulator(this, *arithmeticlexerLexerStaticData->atn, arithmeticlexerLexerStaticData->decisionToDFA, arithmeticlexerLexerStaticData->sharedContextCache);
}

ArithmeticLexer::~ArithmeticLexer() {
  delete _interpreter;
}

std::string ArithmeticLexer::getGrammarFileName() const {
  return "Arithmetic.g4";
}

const std::vector<std::string>& ArithmeticLexer::getRuleNames() const {
  return arithmeticlexerLexerStaticData->ruleNames;
}

const std::vector<std::string>& ArithmeticLexer::getChannelNames() const {
  return arithmeticlexerLexerStaticData->channelNames;
}

const std::vector<std::string>& ArithmeticLexer::getModeNames() const {
  return arithmeticlexerLexerStaticData->modeNames;
}

const dfa::Vocabulary& ArithmeticLexer::getVocabulary() const {
  return arithmeticlexerLexerStaticData->vocabulary;
}

antlr4::atn::SerializedATNView ArithmeticLexer::getSerializedATN() const {
  return arithmeticlexerLexerStaticData->serializedATN;
}

const atn::ATN& ArithmeticLexer::getATN() const {
  return *arithmeticlexerLexerStaticData->atn;
}




void ArithmeticLexer::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  arithmeticlexerLexerInitialize();
#else
  ::antlr4::internal::call_once(arithmeticlexerLexerOnceFlag, arithmeticlexerLexerInitialize);
#endif
}
