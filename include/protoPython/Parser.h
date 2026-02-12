#ifndef PROTOPYTHON_PARSER_H
#define PROTOPYTHON_PARSER_H

#include <protoPython/Tokenizer.h>
#include <memory>
#include <vector>
#include <string>

namespace protoPython {

struct ASTNode {
    virtual ~ASTNode() = default;
    int line = 0;
    int column = 0;
};

struct ConstantNode : ASTNode {
    enum class ConstType { Int, Float, Str, None, Bool };
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

struct UnaryOpNode : ASTNode {
    TokenType op = TokenType::Not;
    std::unique_ptr<ASTNode> operand;
};

struct CondExprNode : ASTNode {
    std::unique_ptr<ASTNode> body;
    std::unique_ptr<ASTNode> cond;
    std::unique_ptr<ASTNode> orelse;
};

struct CallNode : ASTNode {
    std::unique_ptr<ASTNode> func;
    std::vector<std::unique_ptr<ASTNode>> args;
    std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> keywords;
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

struct Comprehension {
    std::unique_ptr<ASTNode> target;
    std::unique_ptr<ASTNode> iter;
    std::vector<std::unique_ptr<ASTNode>> ifs;
};

/** List comprehension [elt for ...]. */
struct ListCompNode : ASTNode {
    std::unique_ptr<ASTNode> elt;
    std::vector<Comprehension> generators;
};

/** Dict comprehension {key: value for ...}. */
struct DictCompNode : ASTNode {
    std::unique_ptr<ASTNode> key;
    std::unique_ptr<ASTNode> value;
    std::vector<Comprehension> generators;
};

/** Set comprehension {elt for ...}. */
struct SetCompNode : ASTNode {
    std::unique_ptr<ASTNode> elt;
    std::vector<Comprehension> generators;
};

/** Assignment: target = expr. Target is Name, Attribute, or Subscript. */
struct AssignNode : ASTNode {
    std::unique_ptr<ASTNode> target;
    std::unique_ptr<ASTNode> value;
};

/** Augmented assignment: target += expr. */
struct AugAssignNode : ASTNode {
    std::unique_ptr<ASTNode> target;
    TokenType op = TokenType::PlusAssign;
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

/** while test: body else: orelse. */
struct WhileNode : ASTNode {
    std::unique_ptr<ASTNode> test;
    std::unique_ptr<ASTNode> body;
    std::unique_ptr<ASTNode> orelse;  /* optional */
};

/** global name1, name2, ... */
struct GlobalNode : ASTNode {
    std::vector<std::string> names;
};

/** def name(params): body. params is list of parameter names in order. */
struct FunctionDefNode : ASTNode {
    std::string name;
    std::vector<std::string> parameters;
    std::unique_ptr<ASTNode> body;
    std::vector<std::unique_ptr<ASTNode>> decorator_list;
};

/** return expr. */
struct ReturnNode : ASTNode {
    std::unique_ptr<ASTNode> value;
};

/** yield [expr]. */
struct YieldNode : ASTNode {
    std::unique_ptr<ASTNode> value; /* optional */
    bool isFrom = false;           /* yield from */
};

/** class name[(bases)]: body. */
struct ClassDefNode : ASTNode {
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> bases;
    std::unique_ptr<ASTNode> body;
    std::vector<std::unique_ptr<ASTNode>> decorator_list;
};

/** import name [as rename]. */
struct ImportNode : ASTNode {
    std::string moduleName;
    std::string alias;
    bool isAs = false;
};

/** from module import name [as alias], ... */
struct ImportFromNode : ASTNode {
    std::string moduleName;
    std::vector<std::pair<std::string, std::string>> names; // {name, alias}
    int level = 0; // for relative imports
};

/** except [type] [as name]: body. */
struct ExceptHandler {
    std::unique_ptr<ASTNode> type;
    std::string name;
    std::unique_ptr<ASTNode> body;
};

/** try: body except ... [finally: finalbody]. */
struct TryNode : ASTNode {
    std::unique_ptr<ASTNode> body;
    std::vector<ExceptHandler> handlers;
    std::unique_ptr<ASTNode> orelse;
    std::unique_ptr<ASTNode> finalbody;
};

/** raise [exc [from cause]]. */
struct RaiseNode : ASTNode {
    std::unique_ptr<ASTNode> exc;
    std::unique_ptr<ASTNode> cause;
};

/** item in with statement: context_expr [as optional_vars]. */
struct WithItem {
    std::unique_ptr<ASTNode> context_expr;
    std::unique_ptr<ASTNode> optional_vars;
};

/** with items: body. */
struct WithNode : ASTNode {
    std::vector<WithItem> items;
    std::unique_ptr<ASTNode> body;
};

/** del targets... */
struct DeleteNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> targets;
};

/** assert test [, msg]. */
struct AssertNode : ASTNode {
    std::unique_ptr<ASTNode> test;
    std::unique_ptr<ASTNode> msg; /* optional */
};

/** break statement. */
struct BreakNode : ASTNode {};
/** continue statement. */
struct ContinueNode : ASTNode {};
/** pass (no-op). */
struct PassNode : ASTNode {};

/** Suite: ordered list of statements (block body). */
struct SuiteNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> statements;
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
    std::unique_ptr<ASTNode> parseYieldExpression();

    bool hasError() const { return hasError_; }
    const std::string& getLastErrorMsg() const { return lastErrorMsg_; }
    int getLastErrorLine() const { return lastErrorLine_; }
    int getLastErrorColumn() const { return lastErrorColumn_; }
    bool atEOF() const { return cur_.type == TokenType::EndOfFile; }

private:
    template<typename T, typename... Args>
    std::unique_ptr<T> createNode(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        node->line = cur_.line;
        node->column = cur_.column;
        return node;
    }

    Tokenizer tok_;
    Token cur_;
    bool hasError_ = false;
    std::string lastErrorMsg_;
    int lastErrorLine_ = 0;
    int lastErrorColumn_ = 0;

    void error(const std::string& msg);
    void advance();
    bool accept(TokenType t);
    bool expect(TokenType t);
    void skipNewlines();
    std::unique_ptr<ASTNode> parseOrExpr();
    std::unique_ptr<ASTNode> parseAndExpr();
    std::unique_ptr<ASTNode> parseNotExpr();
    std::unique_ptr<ASTNode> parseCompareExpr();
    std::unique_ptr<ASTNode> parseAddExpr();
    std::unique_ptr<ASTNode> parseMulExpr();
    std::unique_ptr<ASTNode> parseUnary();
    std::unique_ptr<ASTNode> parsePrimary();
    std::unique_ptr<ASTNode> parseAtom();
    std::unique_ptr<ASTNode> parseSubscript();
    /** Statement: assignment, for, if, global, or expression. */
    std::unique_ptr<ASTNode> parseStatement();
    /** Suite: after ':', either Newline+Indent+statements+Dedent or single statement. */
    std::unique_ptr<ASTNode> parseSuite();
    /** Target list: e.g. x, y (for loops, comprehensions). Returns TupleLiteralNode if more than one. */
    std::unique_ptr<ASTNode> parseTargetList();
};

} // namespace protoPython

#endif
