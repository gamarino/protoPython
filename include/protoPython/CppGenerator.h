#ifndef PROTOPYTHON_CPP_GENERATOR_H
#define PROTOPYTHON_CPP_GENERATOR_H

#include <protoPython/Parser.h>
#include <string>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <memory>

namespace protoPython {

class CppGenerator {
public:
    explicit CppGenerator(std::ostream& out) : finalOut_(out) {}
    
    bool generate(ModuleNode* module, const std::string& filename);
    
    // Collectors
    void collectLocals(ASTNode* node, std::unordered_set<std::string>& locals);
    bool containsYieldOrAwait(ASTNode* node);

private:
    std::ostream& finalOut_;
    std::ostringstream header_;
    std::ostringstream body_;
    std::ostream* out_ = &body_; // Current output target
    void emitLineDirective(int line, const std::string& filename);
    bool generateNode(ASTNode* node);
    bool generateConstant(ConstantNode* n);
    bool generateName(NameNode* n);
    bool generateCall(CallNode* n);
    bool generateAssign(AssignNode* n);
    bool generateIf(IfNode* n);
    bool generateWhile(WhileNode* n);
    bool generateBinOp(BinOpNode* n);
    bool generateUnaryOp(UnaryOpNode* n);
    bool generatePass(PassNode* n);
    bool generateBreak(BreakNode* n);
    bool generateContinue(ContinueNode* n);
    bool generateFunctionDef(FunctionDefNode* n);
    bool generateAsyncFunctionDef(AsyncFunctionDefNode* n);
    bool generateFunctionInternal(const std::string& name,
                                 const std::vector<std::string>& parameters,
                                 const std::string& vararg,
                                 const std::string& kwarg,
                                 ASTNode* body,
                                 const std::vector<std::unique_ptr<ASTNode>>& decorator_list,
                                 bool isAsync,
                                 const std::vector<std::unique_ptr<ASTNode>>* defaults = nullptr);
    bool generateReturn(ReturnNode* n);
    bool generateYield(YieldNode* n);
    bool generateAwait(AwaitNode* n);
    bool generateAugAssign(AugAssignNode* n);
    bool generateAttribute(AttributeNode* n);
    bool generateSubscript(SubscriptNode* n);
    bool generateListLiteral(ListLiteralNode* n);
    bool generateDictLiteral(DictLiteralNode* n);
    bool generateTupleLiteral(TupleLiteralNode* n);
    bool generateSetLiteral(SetLiteralNode* n);
    bool generateNamedExpr(NamedExprNode* n);
    bool generateConditionalExpr(ConditionalExprNode* n);
    bool generateFor(ForNode* n);
    bool generateTry(TryNode* n);
    bool generateRaise(RaiseNode* n);
    bool generateWith(WithNode* n);
    bool generateImport(ImportNode* n);
    bool generateImportFrom(ImportFromNode* n);
    bool generateClassDef(ClassDefNode* n);
    bool generateLambda(LambdaNode* n);
    bool generateJoinedStr(JoinedStrNode* n);
    bool generateFormattedValue(FormattedValueNode* n);
    bool generateSlice(SliceNode* n);
    bool generateDeleteNode(DeleteNode* n);
    bool generateAssert(AssertNode* n);
    bool generateStarred(StarredNode* n);
    bool generateAnnAssign(AnnAssignNode* n);
    bool generateAssignToTarget(ASTNode* target, const std::string& valueExpr);
    bool generateListComp(ListCompNode* n);
    bool generateDictComp(DictCompNode* n);
    bool generateSetComp(SetCompNode* n);
    bool generateGeneratorExp(GeneratorExpNode* n);
    // Shared body emitter for comprehension nests.  `kind`:
    //   0 = list, 1 = set, 2 = dict, 3 = generator-as-list (eager).
    bool emitComprehensionBody(const std::vector<Comprehension>& generators,
                               size_t depth,
                               ASTNode* elt,
                               ASTNode* dictKey,
                               int kind);
    // Bind a comprehension's target (single name, or tuple unpack) to
    // the iterator's current value, declared as `local_<name>` in C++.
    bool emitComprehensionAssign(ASTNode* target, const std::string& valExpr);
    
    bool inStateMachine_ = false;
    int stateCount_ = 0;
    std::unordered_set<std::string> localVars_;
    std::vector<std::string> orderedLocalVars_;
    // While emitting a function body, the Python-level name of the
    // function being generated and the unique C++ symbol it lowers to.
    // generateCall consults these to short-circuit self-recursion as a
    // direct C++ call to the same compiled symbol, bypassing the
    // env->callObject → invokeCallable → asMethod polymorphic chain.
    std::string currentFuncName_;
    std::string currentFuncCppName_;
    // Stack of enclosing function-scope locals, used to identify free
    // variables when a nested function references a name that is local
    // to an outer function (a Python closure).  Indexed outermost-first.
    std::vector<std::unordered_set<std::string>> enclosingLocals_;
    // Free variables of the function currently being emitted.  These are
    // the names whose values must be captured from the enclosing scope at
    // definition time and read from the closure storage object (passed via
    // `self`) when the function runs.
    std::unordered_set<std::string> freeVars_;
    // Collect every `NameNode` identifier referenced under `node` — the
    // raw set of names the body reads, before subtracting locals.
    void collectNameRefs(ASTNode* node, std::unordered_set<std::string>& refs);

    // ---------------------------------------------------------------
    // GC rooting of generated code (protoPython issue #2).
    //
    // Every compiled callable (function, generator, lambda, module
    // initialiser) runs in its own ProtoContext, opened by a
    // protoPython::ContextScope, and keeps every object it still uses in
    // that context's automatic slots, which the collector scans:
    //   * slots [0, locals): the Python locals, declared as C++ references
    //     to their slot (`local_<name>`);
    //   * anchors: values a statement keeps across its body (a for
    //     loop's iterator, a caught exception, a class namespace);
    //   * temporaries: the value of every expression node, stored as it
    //     is computed, so an object a callee returned and nothing else
    //     references stays visible while the rest of the statement
    //     allocates.
    // Temporary slots are never reused within one statement (C++ leaves
    // the evaluation order of function arguments unspecified), and are
    // cleared at every statement-level loop safepoint so they do not
    // retain garbage.  Safepoints are emitted only at statement-level loop
    // heads, where no temporary of the running statement is live.
    struct SlotFrame {
        int anchorTop;
        int tempTop;
        int high;
    };
    int anchorTop_ = 0;   // first slot above the locals and the live anchors
    int tempTop_ = 0;     // next free temporary slot in the current statement
    int slotHigh_ = 0;    // number of slots the current callable declares
    // Start a callable whose first `reserved` slots hold its locals.
    SlotFrame enterSlotFrame(int reserved);
    void leaveSlotFrame(const SlotFrame& saved);
    int allocTempSlot();
    int pushAnchor();
    void popAnchor();
    // A statement starts: its temporaries begin above the live anchors.
    void beginStatement() { tempTop_ = anchorTop_; }
    // Text that opens the callable's context; `callerCtx` names the
    // context the callable was invoked with.
    std::string openContextScope(const std::string& callerCtx) const;
    // A statement-level loop safepoint: clear the temporaries, then let
    // the collector run.
    void emitLoopSafepoint();
    bool generateNodeUnrooted(ASTNode* node);
    bool needsTempSlot(ASTNode* node) const;
};

} // namespace protoPython

#endif
