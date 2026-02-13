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
                                 bool isAsync);
    bool generateReturn(ReturnNode* n);
    bool generateYield(YieldNode* n);
    bool generateAwait(AwaitNode* n);
    bool generateAugAssign(AugAssignNode* n);
    bool generateAttribute(AttributeNode* n);
    bool generateSubscript(SubscriptNode* n);
    bool generateListLiteral(ListLiteralNode* n);
    bool generateDictLiteral(DictLiteralNode* n);
    bool generateTupleLiteral(TupleLiteralNode* n);
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
    
    bool inStateMachine_ = false;
    int stateCount_ = 0;
    std::unordered_set<std::string> localVars_;
    std::vector<std::string> orderedLocalVars_;
};

} // namespace protoPython

#endif
