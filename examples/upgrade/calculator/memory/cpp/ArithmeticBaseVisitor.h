
// Generated from /home/whisper/repo-work/unllm-upgrade/synthesis/apps/Arithmetic.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "ArithmeticVisitor.h"


/**
 * This class provides an empty implementation of ArithmeticVisitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  ArithmeticBaseVisitor : public ArithmeticVisitor {
public:

  virtual std::any visitStart(ArithmeticParser::StartContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExpression(ArithmeticParser::ExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTerm(ArithmeticParser::TermContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnary(ArithmeticParser::UnaryContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimary(ArithmeticParser::PrimaryContext *ctx) override {
    return visitChildren(ctx);
  }


};

