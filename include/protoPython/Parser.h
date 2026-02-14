#ifndef PROTOPYTHON_PARSER_H
#define PROTOPYTHON_PARSER_H

#include <protoPython/Tokenizer.h>
#include <memory>
#include <vector>
#include <string>
#include <map>

namespace protoPython {

struct ASTNode {
    virtual ~ASTNode() = default;
    int line = 0;
    int column = 0;
};

struct ConstantNode : ASTNode {
    enum class ConstType { Int, Float, Str, None, Bool, Ellipsis };
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

struct StarredNode : ASTNode {
    std::unique_ptr<ASTNode> value;
};

struct CallNode : ASTNode {
    std::unique_ptr<ASTNode> func;
    std::vector<std::unique_ptr<ASTNode>> args; // can contain StarredNode
    std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> keywords; // "" name for **kwargs
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

/** Set literal {a, b, c}. */
struct SetLiteralNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> elements;
};

/** Tuple literal (a,) or (a, b). */
struct TupleLiteralNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> elements;
};

/** Part of an f-string that contains an expression to be formatted. */
struct FormattedValueNode : ASTNode {
    std::unique_ptr<ASTNode> value;
    char conversion = 0; // 'r', 's', 'a'
    std::string format_spec;
};

/** An f-string containing constant parts and FormattedValueNodes. */
struct JoinedStrNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> values;
};

/** target := value (PEP 572). */
struct NamedExprNode : ASTNode {
    std::unique_ptr<ASTNode> target;
    std::unique_ptr<ASTNode> value;
};

struct ConditionalExprNode : ASTNode {
    std::unique_ptr<ASTNode> body;
    std::unique_ptr<ASTNode> test;
    std::unique_ptr<ASTNode> orelse;
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

/** Generator expression (elt for ...). */
struct GeneratorExpNode : ASTNode {
    std::unique_ptr<ASTNode> elt;
    std::vector<Comprehension> generators;
};

/** Assignment: target = expr. Target is Name, Attribute, or Subscript. */
struct AssignNode : ASTNode {
    std::vector<std::unique_ptr<ASTNode>> targets;
    std::unique_ptr<ASTNode> value;
};

/** Annotated assignment: target: annotation [= value]. */
struct AnnAssignNode : ASTNode {
    std::unique_ptr<ASTNode> target;
    std::unique_ptr<ASTNode> annotation;
    std::unique_ptr<ASTNode> value; /* optional */
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
    std::unique_ptr<ASTNode> orelse;  /* optional */
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

struct FunctionDefNode : ASTNode {
    std::string name;
    std::vector<std::string> parameters; // positional (can include pos-only)
    std::vector<std::string> kwonlyargs;
    std::vector<std::unique_ptr<ASTNode>> defaults;
    std::vector<std::unique_ptr<ASTNode>> kw_defaults;
    std::unique_ptr<ASTNode> body;
    std::vector<std::unique_ptr<ASTNode>> decorator_list;
    std::string vararg;
    std::string kwarg;
    int posonlyargcount = 0;
    std::unique_ptr<ASTNode> returns;
    std::map<std::string, std::unique_ptr<ASTNode>> parameter_annotations;
};

/** lambda params: body. */
struct LambdaNode : ASTNode {
    std::vector<std::string> parameters;
    std::vector<std::string> kwonlyargs;
    std::vector<std::unique_ptr<ASTNode>> defaults;
    std::vector<std::unique_ptr<ASTNode>> kw_defaults;
    std::unique_ptr<ASTNode> body;
    std::string vararg;
    std::string kwarg;
    int posonlyargcount = 0;
    std::unique_ptr<ASTNode> returns;
    std::map<std::string, std::unique_ptr<ASTNode>> parameter_annotations;
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
    std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> keywords;
    std::unique_ptr<ASTNode> body;
    std::vector<std::unique_ptr<ASTNode>> decorator_list;
};

/** await expr. */
struct AwaitNode : ASTNode {
    std::unique_ptr<ASTNode> value;
};

/** async def name(params): body. */
struct AsyncFunctionDefNode : ASTNode {
    std::string name;
    std::vector<std::string> parameters;
    std::vector<std::string> kwonlyargs;
    std::vector<std::unique_ptr<ASTNode>> defaults;
    std::vector<std::unique_ptr<ASTNode>> kw_defaults;
    std::unique_ptr<ASTNode> body;
    std::vector<std::unique_ptr<ASTNode>> decorator_list;
    std::string vararg;
    std::string kwarg;
    int posonlyargcount = 0;
    std::unique_ptr<ASTNode> returns;
    std::map<std::string, std::unique_ptr<ASTNode>> parameter_annotations;
};

/** async for target in iter: body else: orelse. */
struct AsyncForNode : ASTNode {
    std::unique_ptr<ASTNode> target;
    std::unique_ptr<ASTNode> iter;
    std::unique_ptr<ASTNode> body;
    std::unique_ptr<ASTNode> orelse; /* optional */
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
    bool isStar = false;
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

/** async with items: body. */
struct AsyncWithNode : ASTNode {
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
struct NonlocalNode : ASTNode {
    std::vector<std::string> names;
};

class Parser {
public:
    explicit Parser(const std::string& source);
    std::unique_ptr<ModuleNode> parseModule();
    std::unique_ptr<ASTNode> parseExpression();
    std::unique_ptr<ASTNode> parseTestList();
    bool isCompound(TokenType t);

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
    void skipTrash();
    bool parseParameters(FunctionDefNode* fn);
    std::unique_ptr<ASTNode> parseYieldExpression();
    std::unique_ptr<ASTNode> parseGlobal();
    std::unique_ptr<ASTNode> parseNonlocal();
    std::unique_ptr<ASTNode> parseReturn();
    std::unique_ptr<ASTNode> parseRaise();
    std::unique_ptr<ASTNode> parseWith();
    std::unique_ptr<ASTNode> parseTry();
    std::unique_ptr<ASTNode> parseIf();
    std::unique_ptr<ASTNode> parseWhile();
    std::unique_ptr<ASTNode> parseFor();
    std::unique_ptr<ASTNode> parseImport();
    std::unique_ptr<ASTNode> parseImportFrom();
    std::unique_ptr<ASTNode> parseAssert();
    std::unique_ptr<ASTNode> parseDelete();
    std::unique_ptr<ASTNode> parseFunctionDef();
    std::unique_ptr<ASTNode> parseClassDef();
    std::unique_ptr<ASTNode> parseAsync();
    std::unique_ptr<ASTNode> parseLambda();
    std::unique_ptr<ASTNode> parseFString();
    std::unique_ptr<ASTNode> parseOrExpr();
    std::unique_ptr<ASTNode> parseAndExpr();
    std::unique_ptr<ASTNode> parseNotExpr();
    std::unique_ptr<ASTNode> parseCompareExpr();
    std::unique_ptr<ASTNode> parseBitOr();
    std::unique_ptr<ASTNode> parseBitXor();
    std::unique_ptr<ASTNode> parseBitAnd();
    std::unique_ptr<ASTNode> parseShiftExpr();
    std::unique_ptr<ASTNode> parseAddExpr();
    std::unique_ptr<ASTNode> parseMulExpr();
    std::unique_ptr<ASTNode> parseUnary();
    std::unique_ptr<ASTNode> parsePower();
    std::unique_ptr<ASTNode> parsePrimary();
    std::unique_ptr<ASTNode> parseAtom();
    std::unique_ptr<ASTNode> parseSubscript();
    /** Statement: assignment, for, if, global, or expression. */
    std::unique_ptr<ASTNode> parseStatement();
    /** Suite: after ':', either Newline+Indent+statements+Dedent or single statement. */
    std::unique_ptr<ASTNode> parseSuite();
    std::vector<Comprehension> parseComprehensions();
    /** Target list: e.g. x, y (for loops, comprehensions). Returns TupleLiteralNode if more than one. */
    std::unique_ptr<ASTNode> parseTargetList();
};

} // namespace protoPython

#endif
