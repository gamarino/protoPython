#include <protoPython/Compiler.h>
#include <protoPython/ExecutionEngine.h>

namespace protoPython {

Compiler::Compiler(proto::ProtoContext* ctx, const std::string& filename)
    : ctx_(ctx), filename_(filename) {
    constants_ = ctx_->newList();
    names_ = ctx_->newList();
    bytecode_ = ctx_->newList();
}

int Compiler::addConstant(const proto::ProtoObject* obj) {
    if (!obj || !constants_) return -1;
    int idx = static_cast<int>(constants_->getSize(ctx_));
    constants_ = constants_->appendLast(ctx_, obj);
    return idx;
}

int Compiler::addName(const std::string& name) {
    auto it = namesIndex_.find(name);
    if (it != namesIndex_.end()) return it->second;
    int idx = static_cast<int>(names_->getSize(ctx_));
    const proto::ProtoString* str = proto::ProtoString::fromUTF8String(ctx_, name.c_str());
    names_ = names_->appendLast(ctx_, reinterpret_cast<const proto::ProtoObject*>(str));
    namesIndex_[name] = idx;
    return idx;
}

void Compiler::emit(int op, int arg) {
    bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(op));
    bool hasArg = (op == OP_LOAD_CONST || op == OP_LOAD_NAME || op == OP_STORE_NAME ||
                   op == OP_CALL_FUNCTION || op == OP_LOAD_ATTR || op == OP_STORE_ATTR ||
                   op == OP_BUILD_LIST || op == OP_BUILD_MAP || op == OP_BUILD_TUPLE ||
                   op == OP_UNPACK_SEQUENCE || op == OP_LOAD_GLOBAL || op == OP_STORE_GLOBAL ||
                   op == OP_BUILD_SLICE || op == OP_FOR_ITER || op == OP_POP_JUMP_IF_FALSE ||
                   op == OP_JUMP_ABSOLUTE || op == OP_COMPARE_OP);
    if (hasArg)
        bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(arg));
}

const proto::ProtoList* Compiler::getConstants() { return constants_; }
const proto::ProtoList* Compiler::getNames() { return names_; }
const proto::ProtoList* Compiler::getBytecode() { return bytecode_; }

bool Compiler::compileConstant(ConstantNode* n) {
    if (!n) return false;
    const proto::ProtoObject* obj = nullptr;
    if (n->constType == ConstantNode::ConstType::Int)
        obj = ctx_->fromInteger(n->intVal);
    else if (n->constType == ConstantNode::ConstType::Float)
        obj = ctx_->fromDouble(n->floatVal);
    else if (n->constType == ConstantNode::ConstType::Str)
        obj = ctx_->fromUTF8String(n->strVal.c_str());
    if (!obj) return false;
    int idx = addConstant(obj);
    emit(OP_LOAD_CONST, idx);
    return true;
}

bool Compiler::compileName(NameNode* n) {
    if (!n) return false;
    int idx = addName(n->id);
    emit(OP_LOAD_NAME, idx);
    return true;
}

bool Compiler::compileBinOp(BinOpNode* n) {
    if (!n || !compileNode(n->left.get()) || !compileNode(n->right.get()))
        return false;
    int op = OP_BINARY_ADD;
    if (n->op == TokenType::Minus) op = OP_BINARY_SUBTRACT;
    else if (n->op == TokenType::Star) op = OP_BINARY_MULTIPLY;
    else if (n->op == TokenType::Slash) op = OP_BINARY_TRUE_DIVIDE;
    bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(op));
    return true;
}

bool Compiler::compileCall(CallNode* n) {
    if (!n || !compileNode(n->func.get())) return false;
    for (auto& arg : n->args) {
        if (!compileNode(arg.get())) return false;
    }
    emit(OP_CALL_FUNCTION, static_cast<int>(n->args.size()));
    return true;
}

bool Compiler::compileNode(ASTNode* node) {
    if (!node) return false;
    if (auto* c = dynamic_cast<ConstantNode*>(node)) return compileConstant(c);
    if (auto* nm = dynamic_cast<NameNode*>(node)) return compileName(nm);
    if (auto* b = dynamic_cast<BinOpNode*>(node)) return compileBinOp(b);
    if (auto* cl = dynamic_cast<CallNode*>(node)) return compileCall(cl);
    return false;
}

bool Compiler::compileExpression(ASTNode* expr) {
    if (!compileNode(expr)) return false;
    bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_RETURN_VALUE)));
    return true;
}

bool Compiler::compileModule(ModuleNode* mod) {
    if (!mod || mod->body.empty()) return false;
    if (mod->body.size() == 1) {
        if (!compileNode(mod->body[0].get())) return false;
        bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_RETURN_VALUE)));
        return true;
    }
    for (size_t i = 0; i < mod->body.size(); ++i) {
        if (!compileNode(mod->body[i].get())) return false;
        if (i + 1 < mod->body.size())
            bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_POP_TOP)));
    }
    bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_RETURN_VALUE)));
    return true;
}

const proto::ProtoObject* makeCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* names,
    const proto::ProtoList* bytecode) {
    if (!ctx || !constants || !bytecode) return nullptr;
    const proto::ProtoObject* code = ctx->newObject(true);
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_consts"), reinterpret_cast<const proto::ProtoObject*>(constants));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_names"), names ? reinterpret_cast<const proto::ProtoObject*>(names) : reinterpret_cast<const proto::ProtoObject*>(ctx->newList()));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_code"), reinterpret_cast<const proto::ProtoObject*>(bytecode));
    return code;
}

const proto::ProtoObject* runCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoObject* codeObj,
    proto::ProtoObject* frame) {
    if (!ctx || !codeObj || !frame) return PROTO_NONE;
    const proto::ProtoObject* co_consts = codeObj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_consts"));
    const proto::ProtoObject* co_names = codeObj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_names"));
    const proto::ProtoObject* co_code = codeObj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_code"));
    if (!co_consts || !co_consts->asList(ctx) || !co_code || !co_code->asList(ctx))
        return PROTO_NONE;
    const proto::ProtoList* names = co_names && co_names->asList(ctx) ? co_names->asList(ctx) : nullptr;
    return executeMinimalBytecode(ctx, co_consts->asList(ctx), co_code->asList(ctx), names, frame);
}

} // namespace protoPython
