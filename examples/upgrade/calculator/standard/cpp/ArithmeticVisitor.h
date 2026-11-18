
// Generated from /home/whisper/repo-work/unllm-upgrade/synthesis/apps/Arithmetic.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "ArithmeticParser.h"



/**
 * This class defines an abstract visitor for a parse tree
 * produced by ArithmeticParser.
 */
class  ArithmeticVisitor : public antlr4::tree::AbstractParseTreeVisitor {
public:

  /**
   * Visit parse trees produced by ArithmeticParser.
   */
    virtual std::any visitStart(ArithmeticParser::StartContext *context) = 0;

    virtual std::any visitExpression(ArithmeticParser::ExpressionContext *context) = 0;

    virtual std::any visitTerm(ArithmeticParser::TermContext *context) = 0;

    virtual std::any visitUnary(ArithmeticParser::UnaryContext *context) = 0;

    virtual std::any visitPrimary(ArithmeticParser::PrimaryContext *context) = 0;


};

