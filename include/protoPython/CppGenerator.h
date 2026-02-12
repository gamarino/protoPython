#ifndef PROTOPYTHON_CPP_GENERATOR_H
#define PROTOPYTHON_CPP_GENERATOR_H

#include <protoPython/Parser.h>
#include <string>
#include <iostream>

namespace protoPython {

class CppGenerator {
public:
    explicit CppGenerator(std::ostream& out) : out_(out) {}
    
    bool generate(ModuleNode* module, const std::string& filename);

private:
    std::ostream& out_;
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
    bool generateReturn(ReturnNode* n);
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
};

} // namespace protoPython

#endif
