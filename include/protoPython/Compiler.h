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

/** Compiles AST to protoPython bytecode (constants list, names list, flat bytecode). */
class Compiler {
public:
    Compiler(proto::ProtoContext* ctx, const std::string& filename);

    /** Compile an expression (eval mode). Returns true on success; use getConstants(), getNames(), getBytecode(). */
    bool compileExpression(ASTNode* expr);
    /** Compile a module (exec mode). Returns true on success. */
    bool compileModule(ModuleNode* mod);

    const proto::ProtoList* getConstants();
    const proto::ProtoList* getNames();
    const proto::ProtoList* getBytecode();

private:
    proto::ProtoContext* ctx_ = nullptr;
    std::string filename_;
    const proto::ProtoList* constants_ = nullptr;
    const proto::ProtoList* names_ = nullptr;
    const proto::ProtoList* bytecode_ = nullptr;
    std::unordered_map<std::string, int> namesIndex_;
    std::unordered_map<long long, int> constIntIndex_;
    std::unordered_map<double, int> constFloatIndex_;
    std::unordered_map<std::string, int> constStrIndex_;

    int addConstant(const proto::ProtoObject* obj);
    int addName(const std::string& name);
    void emit(int op, int arg = 0);
    bool compileNode(ASTNode* node);
    bool compileConstant(ConstantNode* n);
    bool compileName(NameNode* n);
    bool compileBinOp(BinOpNode* n);
    bool compileUnaryOp(UnaryOpNode* n);
    bool compileCall(CallNode* n);
    bool compileAttribute(AttributeNode* n);
    bool compileSubscript(SubscriptNode* n);
    bool compileSlice(SliceNode* n);
    bool compileListLiteral(ListLiteralNode* n);
    bool compileDictLiteral(DictLiteralNode* n);
    bool compileTupleLiteral(TupleLiteralNode* n);
    bool compileAssign(AssignNode* n);
    bool compileAugAssign(AugAssignNode* n);
    bool compileDeleteNode(DeleteNode* n);
    bool compileAssert(AssertNode* n);
    bool compileListComp(ListCompNode* n);
    bool compileDictComp(DictCompNode* n);
    bool compileSetComp(SetCompNode* n);
    bool compileWhile(WhileNode* n);
    bool compileFor(ForNode* n);
    bool compileBreak(BreakNode* n);
    bool compileContinue(ContinueNode* n);
    bool compileIf(IfNode* n);
    bool compileGlobal(GlobalNode* n);
    bool compileReturn(ReturnNode* n);
    bool compileYield(YieldNode* n);
    bool compileImport(ImportNode* n);
    bool compileImportFrom(ImportFromNode* n);
    bool compileTry(TryNode* n);
    bool compileRaise(RaiseNode* n);
    bool compileWith(WithNode* n);
    bool compileFunctionDef(FunctionDefNode* n);
    bool compileClassDef(ClassDefNode* n);
    bool compileCondExpr(CondExprNode* n);
    bool compileComprehension(const std::vector<Comprehension>& generators, size_t index, std::function<bool()> innerBody);
    bool compileSuite(SuiteNode* n);
    enum class TargetCtx { Load, Store, Delete };
    bool compileTarget(ASTNode* target, TargetCtx ctx);
    bool emitNameOp(const std::string& id, TargetCtx ctx);
    /** True if executing this node leaves a value on the stack (expression stmt). */
    static bool statementLeavesValue(ASTNode* node);
    /** Collect names from function body: globals from GlobalNode, locals (ordered) from Name/Assign. */
    static void collectLocalsFromBody(ASTNode* body,
        std::unordered_set<std::string>& globalsOut,
        std::vector<std::string>& localsOrdered);
    /** Collect names that are free in any nested def (captured by closures). */
    static void collectCapturedNames(ASTNode* body,
        const std::unordered_set<std::string>& globalsInScope,
        std::unordered_set<std::string>& capturedOut);
    /** True if body contains locals(), exec(), or eval()—dynamic locals access that forces MAPPED. */
    static bool hasDynamicLocalsAccess(ASTNode* node);
    std::unordered_set<std::string> globalNames_;
    /** Optional: name -> slot index for LOAD_FAST/STORE_FAST. Set when compiling a function body. */
    std::unordered_map<std::string, int> localSlotMap_;
    bool isGenerator_ = false;
    int bytecodeOffset() const;
    /** Record a jump arg slot to be patched later with target (bytecode list index). */
    void addPatch(int argSlotIndex, int targetBytecodeIndex);
    void applyPatches();
    std::vector<std::pair<int, int>> patches_;
    struct LoopInfo {
        int start;
        std::vector<int> breakPatches;
    };
    std::vector<LoopInfo> loopStack_;
};

/** Build a code object (ProtoObject with co_consts, co_names, co_code) from compiler output.
 * Optional: co_varnames (slot-ordered names), co_nparams, co_automatic_count for fast local slots. */
const proto::ProtoObject* makeCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* names,
    const proto::ProtoList* bytecode,
    const proto::ProtoString* filename = nullptr,
    const proto::ProtoList* varnames = nullptr,
    int nparams = 0,
    int automatic_count = 0,
    bool isGenerator = false);

/** Run a code object with the given frame. Returns execution result. */
const proto::ProtoObject* runCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoObject* codeObj,
    proto::ProtoObject*& frame);

} // namespace protoPython

#endif
