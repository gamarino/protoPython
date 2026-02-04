#ifndef PROTOPYTHON_COMPILER_H
#define PROTOPYTHON_COMPILER_H

#include <protoPython/Parser.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>
#include <vector>
#include <string>
#include <unordered_map>

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
    bool compileCall(CallNode* n);
};

/** Build a code object (ProtoObject with co_consts, co_names, co_code) from compiler output. */
const proto::ProtoObject* makeCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* names,
    const proto::ProtoList* bytecode);

/** Run a code object with the given frame. Returns execution result. */
const proto::ProtoObject* runCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoObject* codeObj,
    proto::ProtoObject* frame);

} // namespace protoPython

#endif
