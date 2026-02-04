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

/** Attribute access: expr.attr (load or store context). */
struct AttributeNode : ASTNode {
    std::unique_ptr<ASTNode> value;
    std::string attr;
};

/** Subscript: expr[index] or expr[start:stop] or expr[start:stop:step]. */
struct SubscriptNode : ASTNode {
    std::unique_ptr<ASTNode> value;
    std::unique_ptr<ASTNode> index;  /* single index expr or SliceNode */
};

/** Slice: start:stop or start:stop:step (index in SubscriptNode). */
struct SliceNode : ASTNode {
    std::unique_ptr<ASTNode> start;
    std::unique_ptr<ASTNode> stop;
    std::unique_ptr<ASTNode> step;
};

/** List literal [a, b, c]. */
struct ListLiteralNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> elements;
};

/** Dict literal {k1: v1, k2: v2}. */
struct DictLiteralNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> keys;
    std::vector<std::unique_ptr<ASTNode>> values;
};

/** Tuple literal (a,) or (a, b). */
struct TupleLiteralNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> elements;
};

/** Assignment: target = expr. Target is Name, Attribute, or Subscript. */
struct AssignNode : ASTNode {
    std::unique_ptr<ASTNode> target;
    std::unique_ptr<ASTNode> value;
};

/** for target in iter: body (single statement body). */
struct ForNode : ASTNode {
    std::unique_ptr<ASTNode> target;  /* NameNode or TupleLiteralNode for unpacking */
    std::unique_ptr<ASTNode> iter;
    std::unique_ptr<ASTNode> body;
};

/** if test: body else: orelse (single statement each). */
struct IfNode : ASTNode {
    std::unique_ptr<ASTNode> test;
    std::unique_ptr<ASTNode> body;
    std::unique_ptr<ASTNode> orelse;  /* optional */
};

/** global name1, name2, ... */
struct GlobalNode : ASTNode {
    std::vector<std::string> names;
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
    void skipNewlines();
    std::unique_ptr<ASTNode> parseOrExpr();
    std::unique_ptr<ASTNode> parseAddExpr();
    std::unique_ptr<ASTNode> parseMulExpr();
    std::unique_ptr<ASTNode> parseUnary();
    std::unique_ptr<ASTNode> parsePrimary();
    std::unique_ptr<ASTNode> parseAtom();
    std::unique_ptr<ASTNode> parseSubscript();
    /** Statement: assignment, for, if, global, or expression. */
    std::unique_ptr<ASTNode> parseStatement();
};

} // namespace protoPython

#endif
