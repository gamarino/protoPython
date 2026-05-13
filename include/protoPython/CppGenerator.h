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
};

} // namespace protoPython

#endif
