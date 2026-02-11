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
    bool generateBinOp(BinOpNode* n);
    bool generatePass(PassNode* n);
    bool generateBreak(BreakNode* n);
    bool generateContinue(ContinueNode* n);
    bool generateFunctionDef(FunctionDefNode* n);
    bool generateReturn(ReturnNode* n);
    bool generateAttribute(AttributeNode* n);
    bool generateSubscript(SubscriptNode* n);
    bool generateListLiteral(ListLiteralNode* n);
    bool generateDictLiteral(DictLiteralNode* n);
    bool generateTupleLiteral(TupleLiteralNode* n);
};

} // namespace protoPython

#endif
