#ifndef PROTOPYTHON_COMPILER_H
#define PROTOPYTHON_COMPILER_H

#include <protoPython/Parser.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace protoPython {
    
constexpr int CO_OPTIMIZED = 1;
constexpr int CO_NEWLOCALS = 2;
constexpr int CO_VARARGS = 4;
constexpr int CO_VARKEYWORDS = 8;
constexpr int CO_NESTED = 16;

/** Compiles AST to protoPython bytecode (constants list, names list, flat bytecode). */
class Compiler {
public:
    Compiler(proto::ProtoContext* ctx, const std::string& filename);

    /** Compile an expression (eval mode). Returns true on success; use getConstants(), getNames(), getBytecode(). */
    bool compileExpression(ASTNode* expr);
    /** Compile a module (exec mode). Returns true on success. */
    bool compileModule(ModuleNode* mod);

    const proto::ProtoTuple* getConstants();
    const proto::ProtoTuple* getNames();
    const proto::ProtoTuple* getBytecode();
    const proto::ProtoTuple* getLnotab();
    int getFirstLine() const { return firstLine_; }

private:
    proto::ProtoContext* ctx_ = nullptr;
    std::string filename_;
    const proto::ProtoList* constantsVec_ = nullptr;
    const proto::ProtoList* namesVec_ = nullptr;
    const proto::ProtoList* bytecodeVec_ = nullptr;
    std::unordered_map<std::string, int> namesIndex_;
    std::unordered_map<long long, int> constIntIndex_;
    std::unordered_map<double, int> constFloatIndex_;
    std::unordered_map<std::string, int> constStrIndex_;

    // Result holders for get*() methods
    const proto::ProtoTuple* constants_ = nullptr;
    const proto::ProtoTuple* names_ = nullptr;
    const proto::ProtoTuple* bytecode_ = nullptr;

    int addConstant(const proto::ProtoObject* obj);
    int addName(const std::string& name);
    void emit(int op, int arg = 0);
    bool compileNode(ASTNode* node);
    bool compileConstant(ConstantNode* n);
    bool compileName(NameNode* n, bool pushNull = false);
    bool compileBinOp(BinOpNode* n);
    bool compileUnaryOp(UnaryOpNode* n);
    bool compileCall(CallNode* n);
    bool compileAttribute(AttributeNode* n, bool pushNull = false);
    bool compileSubscript(SubscriptNode* n);
    bool compileSlice(SliceNode* n);
    bool compileListLiteral(ListLiteralNode* n);
    bool compileDictLiteral(DictLiteralNode* n);
    bool compileTupleLiteral(TupleLiteralNode* n);
    bool compileSetLiteral(SetLiteralNode* n);
    bool compileAssign(AssignNode* n);
    bool compileAnnAssign(AnnAssignNode* n);
    bool compileNamedExpr(NamedExprNode* n);
    bool compileAugAssign(AugAssignNode* n);
    bool compileStarred(StarredNode* n);
    bool compileDeleteNode(DeleteNode* n);
    bool compileAssert(AssertNode* n);
    bool compileListComp(ListCompNode* n);
    bool compileDictComp(DictCompNode* n);
    bool compileSetComp(SetCompNode* n);
    bool compileGeneratorExp(GeneratorExpNode* n);
    bool compileLambda(LambdaNode* n);
    bool compileJoinedStr(JoinedStrNode* n);
    bool compileFormattedValue(FormattedValueNode* n);
    bool compileWhile(WhileNode* n);
    bool compileFor(ForNode* n);
    bool compileBreak(BreakNode* n);
    bool compileContinue(ContinueNode* n);
    bool compileIf(IfNode* n);
    bool compileGlobal(GlobalNode* n);
    bool compileNonlocal(NonlocalNode* n);
    bool compileReturn(ReturnNode* n);
    bool compileYield(YieldNode* n);
    bool compileImport(ImportNode* n);
    bool compileImportFrom(ImportFromNode* n);
    bool compileTry(TryNode* n);
    bool compileRaise(RaiseNode* n);
    bool compileWith(WithNode* n);
    bool compileFunctionDef(FunctionDefNode* n);
    bool compileAsyncFunctionDef(AsyncFunctionDefNode* n);
    bool compileClassDef(ClassDefNode* n);
    bool compileAwait(AwaitNode* n);
    bool compileAsyncFor(AsyncForNode* n);
    bool compileAsyncWith(AsyncWithNode* n);
    bool compileCondExpr(ConditionalExprNode* n);
    bool compileTypeAlias(TypeAliasNode* n);
    bool compileComprehension(const std::vector<Comprehension>& generators, size_t index, std::function<bool()> innerBody);
    bool compileWithItems(const std::vector<WithItem>& items, size_t index, ASTNode* body);
    bool compileSuite(SuiteNode* n);
    enum class TargetCtx { Load, Store, Delete };
    bool compileTarget(ASTNode* target, TargetCtx ctx);
    bool emitNameOp(const std::string& id, TargetCtx ctx, bool pushNull = false);
    /** True if executing this node leaves a value on the stack (expression stmt). */
    static bool statementLeavesValue(ASTNode* node);
    /** Collect names from function body: globals from GlobalNode, locals (ordered) from Name/Assign. */
    static void collectLocalsFromBody(ASTNode* body,
        std::unordered_set<std::string>& globalsOut,
        std::unordered_set<std::string>& nonlocalsOut,
        std::vector<std::string>& localsOrdered);
    /** Collect names that are free in any nested def (captured by closures). */
    static void collectCapturedNames(ASTNode* body,
        const std::unordered_set<std::string>& globalsInScope,
        std::unordered_set<std::string>& capturedOut);
    /** True if body contains locals(), exec(), or eval()—dynamic locals access that forces MAPPED. */
    static bool hasDynamicLocalsAccess(ASTNode* node);
    std::unordered_set<std::string> globalNames_;
    std::unordered_set<std::string> nonlocalNames_;
    /** Optional: name -> slot index for LOAD_FAST/STORE_FAST. Set when compiling a function body. */
    std::unordered_map<std::string, int> localSlotMap_;
    bool isGenerator_ = false;
    bool isClassBody_ = false;
    bool isFunctionScope_ = false;
    bool isAsyncFunction_ = false;  // PC2: tracking for yield-from check (PEP 525)
    bool forceMapped_ = false;
    /** The name of the immediately enclosing class, propagated to method compilers
     *  so that zero-argument super() can be rewritten to super(ClassName, self). */
    std::string currentClassName_;
    /** PI: the dotted qualified-name prefix for nested definitions
     *  ("Outer", "Outer.Inner", etc.).  compileClassDef uses this to
     *  emit __qualname__ on class objects. */
    std::string qualnamePrefix_;
    int bytecodeOffset() const;
    /** Record a jump arg slot to be patched later with target (bytecode list index). */
    void addPatch(int argSlotIndex, int targetBytecodeIndex);
    void applyPatches();
    std::vector<std::pair<int, int>> patches_;
    
    enum class BlockType { TryFinally, With };
    struct BlockEnv {
        BlockType type;
        ASTNode* cleanupNode; // ASTNode for finally body. Null for With.
    };
    std::vector<BlockEnv> blockEnvStack_;
    bool unwindBlocks(bool isLoopExit, bool hasValueOnStack = false);
    
    struct LoopInfo {
        int start;
        std::vector<int> breakPatches;
        size_t blockDepth;
        bool hasIterator;
    };
    std::vector<LoopInfo> loopStack_;
    
    // Line number tracking
    int firstLine_ = -1;
    int currentLine_ = 0;
    int lastPC_ = 0;
    int lastLine_ = 0;
    std::vector<unsigned char> lnotabVec_;
    void setLineNumber(int line);

    // Stack depth tracking — updated by emit() for each opcode emitted.
    // currentStack_: depth after the last emitted instruction.
    // maxStack_:     highest depth seen; used as co_stacksize for slot allocation.
    int currentStack_ = 0;
    int maxStack_     = 0;

public:
    int getMaxStack() const { return maxStack_; }

private:
};

/** Build a code object (ProtoObject with co_consts, co_names, co_code) from compiler output.
 * Optional: co_varnames (slot-ordered names), co_nparams, co_automatic_count for fast local slots. */
const proto::ProtoObject* makeCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoTuple* constants,
    const proto::ProtoTuple* names,
    const proto::ProtoTuple* bytecode,
    const proto::ProtoString* filename = nullptr,
    const proto::ProtoTuple* varnames = nullptr,
    int nparams = 0,
    int kwonlyargcount = 0,
    int automatic_count = 0,
    int flags = 0,
    bool isGenerator = false,
    const proto::ProtoString* co_name = nullptr,
    int firstlineno = 0,
    const proto::ProtoTuple* lnotab = nullptr);

/** Run a code object with the given frame. Returns execution result. */
const proto::ProtoObject* runCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoObject* codeObj,
    proto::ProtoObject*& frame);

} // namespace protoPython

#endif
