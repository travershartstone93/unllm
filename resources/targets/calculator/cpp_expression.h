// Reusable ANTLR expression component, independent of widgets and state.
#include "ArithmeticLexer.h"
#include "ArithmeticParser.h"
#include "ArithmeticBaseVisitor.h"
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
    if(!std::isfinite(value) || std::abs(value)>1e15)
        throw std::runtime_error("Number outside finite +/-1e15 domain");
    return value;
}
std::string number_text(double value) {
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<std::setprecision(17)<<(value==0?0:checked_number(value));
    return out.str();
}
std::string display_number(double value,double precision) {
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<std::setprecision(static_cast<int>(precision))<<(value==0?0:checked_number(value));
    return out.str();
}
double length(const std::string& value) {return static_cast<double>(value.size());}
std::string truncate_last(const std::string& value) {return value.empty()?value:value.substr(0,value.size()-1);}
double apply_operator(std::string,double,double);
class RejectErrors:public antlr4::BaseErrorListener {
public:
    void syntaxError(antlr4::Recognizer*,antlr4::Token*,size_t,size_t,const std::string&,std::exception_ptr) override {
        throw std::runtime_error("Invalid expression");
    }
};
class EvaluateTree:public ArithmeticBaseVisitor {
public:
    std::any visitStart(ArithmeticParser::StartContext* ctx) override {return visit(ctx->expression());}
    std::any visitExpression(ArithmeticParser::ExpressionContext* ctx) override {
        auto items=ctx->term();double value=std::any_cast<double>(visit(items[0]));
        for(size_t i=1;i<items.size();++i)
            value=apply_operator(ctx->children[2*i-1]->getText(),value,std::any_cast<double>(visit(items[i])));
        return value;
    }
    std::any visitTerm(ArithmeticParser::TermContext* ctx) override {
        auto items=ctx->unary();double value=std::any_cast<double>(visit(items[0]));
        for(size_t i=1;i<items.size();++i)
            value=apply_operator(ctx->children[2*i-1]->getText(),value,std::any_cast<double>(visit(items[i])));
        return value;
    }
    std::any visitUnary(ArithmeticParser::UnaryContext* ctx) override {
        if(!ctx->unary())return visit(ctx->primary());
        double value=std::any_cast<double>(visit(ctx->unary()));
        return checked_number(ctx->children[0]->getText()=="-"?-value:value);
    }
    std::any visitPrimary(ArithmeticParser::PrimaryContext* ctx) override {
        if(ctx->NUMBER()) {
            return checked_number(std::strtod(ctx->NUMBER()->getText().c_str(),nullptr));
        }
        return visit(ctx->expression());
    }
};
double evaluate_expression(const std::string& text) {
    if(text.empty() || text.size()>1024)throw std::runtime_error("Expression is empty or too long");
    antlr4::ANTLRInputStream input(text);ArithmeticLexer lexer(&input);RejectErrors errors;
    lexer.removeErrorListeners();lexer.addErrorListener(&errors);
    antlr4::CommonTokenStream tokens(&lexer);tokens.fill();
    if(tokens.getTokens().size()>256)throw std::runtime_error("Expression token budget exceeded");
    int depth=0,unary_count=0;bool expecting_operand=true;
    for(auto* token:tokens.getTokens()) {
        if((token->getType()==ArithmeticLexer::PLUS || token->getType()==ArithmeticLexer::MINUS) && expecting_operand) {
            if(++unary_count>32)throw std::runtime_error("Unary sign budget exceeded");
        }
        if(token->getType()==ArithmeticLexer::NUMBER || token->getType()==ArithmeticLexer::RPAREN)expecting_operand=false;
        else if(token->getType()!=antlr4::Token::EOF)expecting_operand=true;
        if(token->getType()==ArithmeticLexer::LPAREN || token->getType()==ArithmeticLexer::RPAREN) {
            if(!ALLOW_PARENTHESES)throw std::runtime_error("Parentheses are not enabled");
            depth+=token->getType()==ArithmeticLexer::LPAREN?1:-1;
            if(depth>32)throw std::runtime_error("Parenthesis depth budget exceeded");
        }
    }
    ArithmeticParser parser(&tokens);parser.removeErrorListeners();parser.addErrorListener(&errors);
    EvaluateTree visitor;return checked_number(std::any_cast<double>(visitor.visit(parser.start())));
}
