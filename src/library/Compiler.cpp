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
                   op == OP_JUMP_ABSOLUTE || op == OP_COMPARE_OP || op == OP_BINARY_SUBSCR ||
                   op == OP_STORE_SUBSCR);
    if (hasArg)
        bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(arg));
}

int Compiler::bytecodeOffset() const {
    return static_cast<int>(bytecode_ ? bytecode_->getSize(ctx_) : 0);
}

void Compiler::addPatch(int argSlotIndex, int targetBytecodeIndex) {
    patches_.emplace_back(argSlotIndex, targetBytecodeIndex);
}

void Compiler::applyPatches() {
    for (const auto& p : patches_) {
        if (bytecode_ && p.first >= 0 && static_cast<unsigned long>(p.first) < bytecode_->getSize(ctx_))
            bytecode_ = bytecode_->setAt(ctx_, p.first, ctx_->fromInteger(p.second));
    }
    patches_.clear();
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
    if (globalNames_.count(n->id))
        emit(OP_LOAD_GLOBAL, idx);
    else
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
    emit(op, 0);
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

bool Compiler::compileAttribute(AttributeNode* n) {
    if (!n || !compileNode(n->value.get())) return false;
    int idx = addName(n->attr);
    emit(OP_LOAD_ATTR, idx);
    return true;
}

bool Compiler::compileSubscript(SubscriptNode* n) {
    if (!n || !compileNode(n->value.get())) return false;
    if (auto* sl = dynamic_cast<SliceNode*>(n->index.get())) {
        if (!compileSlice(sl)) return false;
    } else {
        if (!compileNode(n->index.get())) return false;
    }
    emit(OP_BINARY_SUBSCR, 0);
    return true;
}

bool Compiler::compileSlice(SliceNode* n) {
    if (!n) return false;
    const proto::ProtoObject* noneObj = ctx_->newObject(true);
    if (n->start) {
        if (!compileNode(n->start.get())) return false;
    } else {
        int idx = addConstant(noneObj);
        emit(OP_LOAD_CONST, idx);
    }
    if (n->stop) {
        if (!compileNode(n->stop.get())) return false;
    } else {
        int idx = addConstant(noneObj);
        emit(OP_LOAD_CONST, idx);
    }
    if (n->step) {
        if (!compileNode(n->step.get())) return false;
        emit(OP_BUILD_SLICE, 3);
    } else {
        emit(OP_BUILD_SLICE, 2);
    }
    return true;
}

bool Compiler::compileListLiteral(ListLiteralNode* n) {
    if (!n) return false;
    for (auto& e : n->elements) {
        if (!compileNode(e.get())) return false;
    }
    emit(OP_BUILD_LIST, static_cast<int>(n->elements.size()));
    return true;
}

bool Compiler::compileDictLiteral(DictLiteralNode* n) {
    if (!n) return false;
    for (size_t i = 0; i < n->keys.size(); ++i) {
        if (!compileNode(n->values[i].get()) || !compileNode(n->keys[i].get())) return false;
    }
    emit(OP_BUILD_MAP, static_cast<int>(n->keys.size()));
    return true;
}

bool Compiler::compileTupleLiteral(TupleLiteralNode* n) {
    if (!n) return false;
    for (auto& e : n->elements) {
        if (!compileNode(e.get())) return false;
    }
    emit(OP_BUILD_TUPLE, static_cast<int>(n->elements.size()));
    return true;
}

bool Compiler::compileTarget(ASTNode* target, bool isStore) {
    if (!target) return false;
    if (auto* nm = dynamic_cast<NameNode*>(target)) {
        int idx = addName(nm->id);
        if (isStore && globalNames_.count(nm->id))
            emit(OP_STORE_GLOBAL, idx);
        else if (isStore)
            emit(OP_STORE_NAME, idx);
        else if (globalNames_.count(nm->id))
            emit(OP_LOAD_GLOBAL, idx);
        else
            emit(OP_LOAD_NAME, idx);
        return true;
    }
    if (auto* att = dynamic_cast<AttributeNode*>(target)) {
        if (!compileNode(att->value.get())) return false;
        int idx = addName(att->attr);
        if (isStore)
            emit(OP_STORE_ATTR, idx);
        else
            emit(OP_LOAD_ATTR, idx);
        return true;
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(target)) {
        if (!compileNode(sub->value.get()) || !compileNode(sub->index.get())) return false;
        if (isStore) {
            bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(OP_ROT_THREE));
            emit(OP_STORE_SUBSCR, 0);
        } else {
            emit(OP_BINARY_SUBSCR, 0);
        }
        return true;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(target)) {
        if (!isStore) return false;
        emit(OP_UNPACK_SEQUENCE, static_cast<int>(tup->elements.size()));
        for (auto it = tup->elements.rbegin(); it != tup->elements.rend(); ++it) {
            if (!compileTarget(it->get(), true)) return false;
        }
        return true;
    }
    return false;
}

bool Compiler::compileAssign(AssignNode* n) {
    if (!n) return false;
    if (!compileNode(n->value.get())) return false;
    return compileTarget(n->target.get(), true);
}

bool Compiler::compileFor(ForNode* n) {
    if (!n || !compileNode(n->iter.get())) return false;
    emit(OP_GET_ITER, 0);
    int loopStart = bytecodeOffset();
    emit(OP_FOR_ITER, 0);
    int argSlot = bytecodeOffset() - 1;
    if (!compileTarget(n->target.get(), true)) return false;
    if (!compileNode(n->body.get())) return false;
    emit(OP_JUMP_ABSOLUTE, loopStart);
    int afterLoop = bytecodeOffset();
    addPatch(argSlot, afterLoop);
    return true;
}

bool Compiler::compileIf(IfNode* n) {
    if (!n || !compileNode(n->test.get())) return false;
    emit(OP_POP_JUMP_IF_FALSE, 0);
    int elseSlot = bytecodeOffset() - 1;
    if (!compileNode(n->body.get())) return false;
    int elseTarget = bytecodeOffset();
    if (n->orelse) {
        emit(OP_JUMP_ABSOLUTE, 0);
        int endSlot = bytecodeOffset() - 1;
        addPatch(elseSlot, elseTarget);
        if (!compileNode(n->orelse.get())) return false;
        addPatch(endSlot, bytecodeOffset());
    } else {
        addPatch(elseSlot, elseTarget);
    }
    return true;
}

bool Compiler::compileGlobal(GlobalNode* n) {
    if (!n) return false;
    for (const auto& name : n->names)
        globalNames_.insert(name);
    return true;
}

bool Compiler::statementLeavesValue(ASTNode* node) {
    if (!node) return false;
    if (dynamic_cast<AssignNode*>(node) || dynamic_cast<ForNode*>(node) ||
        dynamic_cast<IfNode*>(node) || dynamic_cast<GlobalNode*>(node) ||
        dynamic_cast<PassNode*>(node) || dynamic_cast<FunctionDefNode*>(node) ||
        dynamic_cast<SuiteNode*>(node))
        return false;
    return true;
}

bool Compiler::compileSuite(SuiteNode* n) {
    if (!n || n->statements.empty()) return false;
    for (size_t i = 0; i < n->statements.size(); ++i) {
        if (!compileNode(n->statements[i].get())) return false;
        if (i + 1 < n->statements.size() && statementLeavesValue(n->statements[i].get()))
            emit(OP_POP_TOP, 0);
    }
    return true;
}

bool Compiler::compileFunctionDef(FunctionDefNode* n) {
    if (!n) return false;
    Compiler bodyCompiler(ctx_, filename_);
    if (!bodyCompiler.compileNode(n->body.get())) return false;
    bodyCompiler.bytecode_ = bodyCompiler.bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_RETURN_VALUE)));
    bodyCompiler.applyPatches();
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode());
    if (!codeObj) return false;
    int idx = addConstant(codeObj);
    emit(OP_LOAD_CONST, idx);
    emit(OP_BUILD_FUNCTION, 0);
    int nameIdx = addName(n->name);
    emit(OP_STORE_NAME, nameIdx);
    return true;
}

bool Compiler::compileNode(ASTNode* node) {
    if (!node) return false;
    if (dynamic_cast<PassNode*>(node)) return true;
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) return compileFunctionDef(fn);
    if (auto* c = dynamic_cast<ConstantNode*>(node)) return compileConstant(c);
    if (auto* nm = dynamic_cast<NameNode*>(node)) return compileName(nm);
    if (auto* b = dynamic_cast<BinOpNode*>(node)) return compileBinOp(b);
    if (auto* cl = dynamic_cast<CallNode*>(node)) return compileCall(cl);
    if (auto* att = dynamic_cast<AttributeNode*>(node)) return compileAttribute(att);
    if (auto* sub = dynamic_cast<SubscriptNode*>(node)) return compileSubscript(sub);
    if (auto* sl = dynamic_cast<SliceNode*>(node)) return compileSlice(sl);
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) return compileListLiteral(lst);
    if (auto* d = dynamic_cast<DictLiteralNode*>(node)) return compileDictLiteral(d);
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) return compileTupleLiteral(tup);
    if (auto* a = dynamic_cast<AssignNode*>(node)) return compileAssign(a);
    if (auto* f = dynamic_cast<ForNode*>(node)) return compileFor(f);
    if (auto* iff = dynamic_cast<IfNode*>(node)) return compileIf(iff);
    if (auto* g = dynamic_cast<GlobalNode*>(node)) return compileGlobal(g);
    if (auto* s = dynamic_cast<SuiteNode*>(node)) return compileSuite(s);
    return false;
}

bool Compiler::compileExpression(ASTNode* expr) {
    if (!compileNode(expr)) return false;
    bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_RETURN_VALUE)));
    applyPatches();
    return true;
}

bool Compiler::compileModule(ModuleNode* mod) {
    if (!mod || mod->body.empty()) return false;
    globalNames_.clear();
    if (mod->body.size() == 1) {
        if (!compileNode(mod->body[0].get())) return false;
        bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_RETURN_VALUE)));
        applyPatches();
        return true;
    }
    for (size_t i = 0; i < mod->body.size(); ++i) {
        if (!compileNode(mod->body[i].get())) return false;
        if (i + 1 < mod->body.size())
            bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_POP_TOP)));
    }
    bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_RETURN_VALUE)));
    applyPatches();
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
