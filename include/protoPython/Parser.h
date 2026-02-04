#ifndef PROTOPYTHON_PARSER_H
#define PROTOPYTHON_PARSER_H

#include <protoPython/Tokenizer.h>
#include <memory>
#include <vector>
#include <string>

namespace protoPython {

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct ConstantNode : ASTNode {
    enum class ConstType { Int, Float, Str };
    ConstType constType = ConstType::Int;
    long long intVal = 0;
    double floatVal = 0.0;
    std::string strVal;
};

struct NameNode : ASTNode {
    std::string id;
};

struct BinOpNode : ASTNode {
    std::unique_ptr<ASTNode> left;
    TokenType op = TokenType::Plus;
    std::unique_ptr<ASTNode> right;
};

struct CallNode : ASTNode {
    std::unique_ptr<ASTNode> func;
    std::vector<std::unique_ptr<ASTNode>> args;
};

struct ModuleNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> body;
};

/** Minimal parser: expressions and single-expression module for eval. */
class Parser {
public:
    explicit Parser(const std::string& source);
    std::unique_ptr<ModuleNode> parseModule();
    std::unique_ptr<ASTNode> parseExpression();

private:
    Tokenizer tok_;
    Token cur_;
    void advance();
    bool accept(TokenType t);
    bool expect(TokenType t);
    std::unique_ptr<ASTNode> parseOrExpr();
    std::unique_ptr<ASTNode> parseAddExpr();
    std::unique_ptr<ASTNode> parseMulExpr();
    std::unique_ptr<ASTNode> parseUnary();
    std::unique_ptr<ASTNode> parsePrimary();
};

} // namespace protoPython

#endif
