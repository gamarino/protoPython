#include <protoPython/Compiler.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/PythonEnvironment.h>
#include <iostream>

namespace protoPython {

static void collectGlobalsFromNode(ASTNode* node, std::unordered_set<std::string>& globalsOut);
static void collectDefinedNames(ASTNode* node, std::unordered_set<std::string>& out);

Compiler::Compiler(proto::ProtoContext* ctx, const std::string& filename)
    : ctx_(ctx), filename_(filename) {
    constants_ = ctx_->newList();
    names_ = ctx_->newList();
    bytecode_ = ctx_->newList();
}

int Compiler::addConstant(const proto::ProtoObject* obj) {
    if (!constants_) return -1;
    // Basic de-duplication to save space and potentially speed up comparisons
    int n = static_cast<int>(constants_->getSize(ctx_));
    for (int i = 0; i < n; ++i) {
        if (constants_->getAt(ctx_, i) == obj) return i;
    }
    int idx = n;
    constants_ = constants_->appendLast(ctx_, obj);
    if (std::getenv("PROTO_ENV_DIAG")) {
        std::cerr << "[proto-compiler-diag] addConstant obj=" << obj << " idx=" << idx << " size=" << constants_->getSize(ctx_) << "\n";
    }
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
    else if (n->constType == ConstantNode::ConstType::None)
        obj = PROTO_NONE;
    else if (n->constType == ConstantNode::ConstType::Bool)
        obj = n->intVal ? PROTO_TRUE : PROTO_FALSE;
    if (!obj && n->constType != ConstantNode::ConstType::None) return false;
    int idx = addConstant(obj);
    emit(OP_LOAD_CONST, idx);
    return true;
}

bool Compiler::compileName(NameNode* n) {
    if (!n) return false;
    return emitNameOp(n->id, TargetCtx::Load);
}

bool Compiler::compileBinOp(BinOpNode* n) {
    if (!n || !compileNode(n->left.get()) || !compileNode(n->right.get()))
        return false;
    int op = OP_BINARY_ADD;
    if (n->op == TokenType::Minus) op = OP_BINARY_SUBTRACT;
    else if (n->op == TokenType::Star) op = OP_BINARY_MULTIPLY;
    else if (n->op == TokenType::Slash) op = OP_BINARY_TRUE_DIVIDE;
    else if (n->op == TokenType::EqEqual) {
        emit(OP_COMPARE_OP, 0); // 0 is '=='
        return true;
    } else if (n->op == TokenType::Is) {
        emit(OP_COMPARE_OP, 8); // 8 is 'is' 
        return true;
    } else if (n->op == TokenType::IsNot) {
        emit(OP_COMPARE_OP, 9); // 9 is 'is not'
        return true;
    } else if (n->op == TokenType::In) {
        emit(OP_COMPARE_OP, 6); // 6 is 'in'
        return true;
    } else if (n->op == TokenType::NotIn) {
        emit(OP_COMPARE_OP, 7); // 7 is 'not in'
        return true;
    } else if (n->op == TokenType::NotEqual) {
        emit(OP_COMPARE_OP, 1);
        return true;
    } else if (n->op == TokenType::Less) {
        emit(OP_COMPARE_OP, 2);
        return true;
    } else if (n->op == TokenType::LessEqual) {
        emit(OP_COMPARE_OP, 3);
        return true;
    } else if (n->op == TokenType::Greater) {
        emit(OP_COMPARE_OP, 4);
        return true;
    } else if (n->op == TokenType::GreaterEqual) {
        emit(OP_COMPARE_OP, 5);
        return true;
    } else if (n->op == TokenType::Modulo) {
        op = OP_BINARY_MODULO;
    } else if (n->op == TokenType::And) {
        emit(OP_BINARY_AND, 0);
        return true;
    } else if (n->op == TokenType::Or) {
        emit(OP_BINARY_OR, 0);
        return true;
    }
    emit(op, 0);
    return true;
}

bool Compiler::compileUnaryOp(UnaryOpNode* n) {
    if (!n || !compileNode(n->operand.get())) return false;
    if (n->op == TokenType::Not) {
        emit(OP_UNARY_NOT, 0); // OP_UNARY_NOT does not take an argument, but emit adds 0 if not specified.
        return true;
    }
    return false;
}

bool Compiler::compileCall(CallNode* n) {
    if (!n || !compileNode(n->func.get())) return false;
    if (n->star_args || n->kw_args) {
        // Handle positional arguments: fixed + star_args
        if (!n->args.empty()) {
            for (auto& arg : n->args) {
                if (!compileNode(arg.get())) return false;
            }
            emit(OP_BUILD_LIST, static_cast<int>(n->args.size()));
            if (n->star_args) {
                if (!compileNode(n->star_args.get())) return false;
                emit(OP_LIST_EXTEND, 0);
            }
        } else {
            if (n->star_args) {
                if (!compileNode(n->star_args.get())) return false;
            } else {
                emit(OP_BUILD_TUPLE, 0);
            }
        }

        int flags = 0;
        if (n->kw_args) {
            if (!compileNode(n->kw_args.get())) return false;
            flags |= 1;
        }
        
        emit(OP_CALL_FUNCTION_EX, flags);
        return true;
    }

    for (auto& arg : n->args) {
        if (!compileNode(arg.get())) return false;
    }

    if (n->keywords.empty()) {
        emit(OP_CALL_FUNCTION, static_cast<int>(n->args.size()));
    } else {
        const proto::ProtoList* kwList = ctx_->newList();
        for (auto& kw : n->keywords) {
            if (!compileNode(kw.second.get())) return false;
            kwList = kwList->appendLast(ctx_, ctx_->fromUTF8String(kw.first.c_str()));
        }
        const proto::ProtoObject* nameTuple = ctx_->newTupleFromList(kwList)->asObject(ctx_);
        
        int idx = addConstant(nameTuple);
        emit(OP_LOAD_CONST, idx);
        emit(OP_CALL_FUNCTION_KW, static_cast<int>(n->args.size() + n->keywords.size()));
    }
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
    if (n->start) {
        if (!compileNode(n->start.get())) return false;
    } else {
        int idx = addConstant(PROTO_NONE);
        emit(OP_LOAD_CONST, idx);
    }
    if (n->stop) {
        if (!compileNode(n->stop.get())) return false;
    } else {
        int idx = addConstant(PROTO_NONE);
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
        if (!compileNode(n->keys[i].get()) || !compileNode(n->values[i].get())) return false;
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

bool Compiler::compileSetLiteral(SetLiteralNode* n) {
    if (!n) return false;
    for (auto& e : n->elements) {
        if (!compileNode(e.get())) return false;
    }
    emit(OP_BUILD_SET, static_cast<int>(n->elements.size()));
    return true;
}

bool Compiler::emitNameOp(const std::string& id, TargetCtx ctx) {
    if (globalNames_.count(id)) {
        int idx = addName(id);
        int op = OP_LOAD_GLOBAL;
        if (ctx == TargetCtx::Store) op = OP_STORE_GLOBAL;
        else if (ctx == TargetCtx::Delete) op = OP_DELETE_GLOBAL;
        emit(op, idx);
        return true;
    }
    if (nonlocalNames_.count(id)) {
        int idx = addName(id);
        int op = OP_LOAD_DEREF;
        if (ctx == TargetCtx::Store) op = OP_STORE_DEREF;
        emit(op, idx);
        return true;
    }
    auto it = localSlotMap_.find(id);
    if (it != localSlotMap_.end()) {
        int op = OP_LOAD_FAST;
        if (ctx == TargetCtx::Store) op = OP_STORE_FAST;
        else if (ctx == TargetCtx::Delete) op = OP_DELETE_FAST;
        emit(op, it->second);
        return true;
    }
    int idx = addName(id);
    int op = OP_LOAD_NAME;
    if (ctx == TargetCtx::Store) op = OP_STORE_NAME;
    else if (ctx == TargetCtx::Delete) op = OP_DELETE_NAME;
    emit(op, idx);
    return true;
}

bool Compiler::compileTarget(ASTNode* target, TargetCtx ctx) {
    if (!target) return false;
    if (auto* nm = dynamic_cast<NameNode*>(target)) {
        return emitNameOp(nm->id, ctx);
    }
    if (auto* att = dynamic_cast<AttributeNode*>(target)) {
        if (!compileNode(att->value.get())) return false;
        int idx = addName(att->attr);
        if (ctx == TargetCtx::Store)
            emit(OP_STORE_ATTR, idx);
        else if (ctx == TargetCtx::Delete)
            emit(OP_DELETE_ATTR, idx);
        else
            emit(OP_LOAD_ATTR, idx);
        return true;
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(target)) {
        if (!compileNode(sub->value.get()) || !compileNode(sub->index.get())) return false;
        if (ctx == TargetCtx::Store) {
            emit(OP_ROT_THREE, 0);
            emit(OP_STORE_SUBSCR, 0);
        } else if (ctx == TargetCtx::Delete) {
            emit(OP_DELETE_SUBSCR, 0);
        } else {
            emit(OP_BINARY_SUBSCR, 0);
        }
        return true;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(target)) {
        if (ctx != TargetCtx::Store) return false;
        emit(OP_UNPACK_SEQUENCE, static_cast<int>(tup->elements.size()));
        for (auto& e : tup->elements) {
            if (!compileTarget(e.get(), TargetCtx::Store)) return false;
        }
        return true;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(target)) {
        if (ctx != TargetCtx::Store) return false;
        emit(OP_UNPACK_SEQUENCE, static_cast<int>(lst->elements.size()));
        for (auto& e : lst->elements) {
            if (!compileTarget(e.get(), TargetCtx::Store)) return false;
        }
        return true;
    }
    return false;
}

bool Compiler::compileAssign(AssignNode* n) {
    if (!n) return false;
    if (!compileNode(n->value.get())) return false;
    return compileTarget(n->target.get(), TargetCtx::Store);
}

bool Compiler::compileDeleteNode(DeleteNode* n) {
    if (!n) return false;
    for (auto& target : n->targets) {
        if (!compileTarget(target.get(), TargetCtx::Delete)) return false;
    }
    return true;
}

bool Compiler::compileAssert(AssertNode* n) {
    if (!n || !compileNode(n->test.get())) return false;
    // if test is true, jump to end
    emit(OP_POP_JUMP_IF_TRUE, 0);
    int jumpToEndSlot = bytecodeOffset() - 1;
    
    // if test is false, raise AssertionError
    int idxAlt = addName("AssertionError");
    emit(OP_LOAD_GLOBAL, idxAlt);
    if (n->msg) {
        if (!compileNode(n->msg.get())) return false;
        emit(OP_CALL_FUNCTION, 1);
    } else {
        emit(OP_CALL_FUNCTION, 0);
    }
    emit(OP_RAISE_VARARGS, 1);
    
    int endTarget = bytecodeOffset();
    addPatch(jumpToEndSlot, endTarget);
    return true;
}

bool Compiler::compileWhile(WhileNode* n) {
    if (!n || !n->test) return false;
    int startPC = bytecodeOffset();
    
    loopStack_.push_back({startPC, {}});
    
    if (!compileNode(n->test.get())) return false;
    
    emit(OP_POP_JUMP_IF_FALSE, 0);
    int jumpToEndSlot = bytecodeOffset() - 1;
    
    if (!compileNode(n->body.get())) return false;
    emit(OP_JUMP_ABSOLUTE, startPC);
    
    int afterLoopBody = bytecodeOffset();
    addPatch(jumpToEndSlot, afterLoopBody);
    
    if (n->orelse) {
        if (!compileNode(n->orelse.get())) return false;
    }
    
    int endPC = bytecodeOffset();
    for (int patchIdx : loopStack_.back().breakPatches) {
        addPatch(patchIdx, endPC);
    }
    loopStack_.pop_back();
    
    return true;
}

bool Compiler::compileFor(ForNode* n) {
    if (!n || !compileNode(n->iter.get())) return false;
    emit(OP_GET_ITER);
    int loopStart = bytecodeOffset();
    
    loopStack_.push_back({loopStart, {}});
    
    emit(OP_FOR_ITER, 0);
    int argSlot = bytecodeOffset() - 1;
    if (!compileTarget(n->target.get(), TargetCtx::Store)) return false;
    if (!compileNode(n->body.get())) return false;
    emit(OP_JUMP_ABSOLUTE, loopStart);
    int afterLoop = bytecodeOffset();
    addPatch(argSlot, afterLoop);
    
    // We don't have for-else in AST yet, but if we did, it would go here.
    
    for (int patchIdx : loopStack_.back().breakPatches) {
        addPatch(patchIdx, bytecodeOffset());
    }
    loopStack_.pop_back();
    
    return true;
}

bool Compiler::compileBreak(BreakNode* n) {
    if (loopStack_.empty()) return false;
    emit(OP_JUMP_ABSOLUTE, 0);
    loopStack_.back().breakPatches.push_back(bytecodeOffset() - 1);
    return true;
}

bool Compiler::compileContinue(ContinueNode* n) {
    if (loopStack_.empty()) return false;
    emit(OP_JUMP_ABSOLUTE, loopStack_.back().start);
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

bool Compiler::compileReturn(ReturnNode* n) {
    if (n->value) {
        if (!compileNode(n->value.get())) return false;
    } else {
        int idx = addConstant(PROTO_NONE);
        emit(OP_LOAD_CONST, idx);
    }
    emit(OP_RETURN_VALUE);
    return true;
}

bool Compiler::compileYield(YieldNode* n) {
    isGenerator_ = true;
    if (n->value) {
        if (!compileNode(n->value.get())) return false;
    } else {
        int idx = addConstant(PROTO_NONE);
        emit(OP_LOAD_CONST, idx);
    }
    
    if (n->isFrom) {
        emit(OP_GET_YIELD_FROM_ITER);
        int noneIdx = addConstant(PROTO_NONE);
        emit(OP_LOAD_CONST, noneIdx); // Initial send value is None
        emit(OP_YIELD_FROM);
    } else {
        emit(OP_YIELD_VALUE);
    }
    return true;
}

bool Compiler::compileListComp(ListCompNode* n) {
    if (!n) return false;
    
    // Python 3: List comprehensions have their own scope
    // [elt for target in iter] -> (__listcomp)(iter)
    
    if (!compileNode(n->generators[0].iter.get())) return false;
    emit(OP_GET_ITER);
    
    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.localSlotMap_[".0"] = 0;
    
    // Create the list inside the function
    bodyCompiler.emit(OP_BUILD_LIST, 0);
    
    auto oldIter = std::move(n->generators[0].iter);
    auto itNode = std::make_unique<NameNode>();
    itNode->id = ".0";
    n->generators[0].iter = std::move(itNode);
    
    int nGen = static_cast<int>(n->generators.size());
    auto innerOk = bodyCompiler.compileComprehension(n->generators, 0, [&]() {
        if (!bodyCompiler.compileNode(n->elt.get())) return false;
        bodyCompiler.emit(OP_LIST_APPEND, nGen + 1);
        return true;
    });
    
    n->generators[0].iter = std::move(oldIter);
    if (!innerOk) return false;
    
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    // Create code object
    const proto::ProtoList* co_varnames = ctx_->newList();
    co_varnames = co_varnames->appendLast(ctx_, ctx_->fromUTF8String(".0"));
    
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), ctx_->fromUTF8String(filename_.c_str())->asString(ctx_), co_varnames, 1, 1, 0, false);
    emit(OP_LOAD_CONST, addConstant(codeObj));
    emit(OP_BUILD_FUNCTION, 0);
    emit(OP_ROT_TWO);
    emit(OP_CALL_FUNCTION, 1);
    
    return true;
}

bool Compiler::compileDictComp(DictCompNode* n) {
    if (!n) return false;
    
    if (!compileNode(n->generators[0].iter.get())) return false;
    emit(OP_GET_ITER);
    
    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.localSlotMap_[".0"] = 0;
    bodyCompiler.emit(OP_BUILD_MAP, 0);
    
    auto oldIter = std::move(n->generators[0].iter);
    auto itNode = std::make_unique<NameNode>();
    itNode->id = ".0";
    n->generators[0].iter = std::move(itNode);
    
    int nGen = static_cast<int>(n->generators.size());
    auto innerOk = bodyCompiler.compileComprehension(n->generators, 0, [&]() {
        if (!bodyCompiler.compileNode(n->value.get())) return false;
        if (!bodyCompiler.compileNode(n->key.get())) return false;
        bodyCompiler.emit(OP_MAP_ADD, nGen + 1);
        return true;
    });
    
    n->generators[0].iter = std::move(oldIter);
    if (!innerOk) return false;
    
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    const proto::ProtoList* co_varnames = ctx_->newList();
    co_varnames = co_varnames->appendLast(ctx_, ctx_->fromUTF8String(".0"));
    
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), ctx_->fromUTF8String(filename_.c_str())->asString(ctx_), co_varnames, 1, 1, 0, false);
    emit(OP_LOAD_CONST, addConstant(codeObj));
    emit(OP_BUILD_FUNCTION, 0);
    emit(OP_ROT_TWO);
    emit(OP_CALL_FUNCTION, 1);
    
    return true;
}

bool Compiler::compileSetComp(SetCompNode* n) {
    if (!n) return false;
    
    if (!compileNode(n->generators[0].iter.get())) return false;
    emit(OP_GET_ITER);
    
    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.localSlotMap_[".0"] = 0;
    bodyCompiler.emit(OP_BUILD_SET, 0);
    
    auto oldIter = std::move(n->generators[0].iter);
    auto itNode = std::make_unique<NameNode>();
    itNode->id = ".0";
    n->generators[0].iter = std::move(itNode);
    
    int nGen = static_cast<int>(n->generators.size());
    auto innerOk = bodyCompiler.compileComprehension(n->generators, 0, [&]() {
        if (!bodyCompiler.compileNode(n->elt.get())) return false;
        bodyCompiler.emit(OP_SET_ADD, nGen + 1);
        return true;
    });
    
    n->generators[0].iter = std::move(oldIter);
    if (!innerOk) return false;
    
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    const proto::ProtoList* co_varnames = ctx_->newList();
    co_varnames = co_varnames->appendLast(ctx_, ctx_->fromUTF8String(".0"));
    
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), ctx_->fromUTF8String(filename_.c_str())->asString(ctx_), co_varnames, 1, 1, 0, false);
    emit(OP_LOAD_CONST, addConstant(codeObj));
    emit(OP_BUILD_FUNCTION, 0);
    emit(OP_ROT_TWO);
    emit(OP_CALL_FUNCTION, 1);
    
    return true;
}

bool Compiler::compileGeneratorExp(GeneratorExpNode* n) {
    if (!n) return false;
    
    if (!compileNode(n->generators[0].iter.get())) return false;
    emit(OP_GET_ITER);
    
    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.isGenerator_ = true;
    bodyCompiler.localSlotMap_[".0"] = 0;
    
    auto oldIter = std::move(n->generators[0].iter);
    auto itNode = std::make_unique<NameNode>();
    itNode->id = ".0";
    n->generators[0].iter = std::move(itNode);
    
    auto innerOk = bodyCompiler.compileComprehension(n->generators, 0, [&]() {
        if (!bodyCompiler.compileNode(n->elt.get())) return false;
        bodyCompiler.emit(OP_YIELD_VALUE);
        bodyCompiler.emit(OP_POP_TOP);
        return true;
    });
    
    n->generators[0].iter = std::move(oldIter);
    if (!innerOk) return false;
    
    bodyCompiler.emit(OP_LOAD_CONST, bodyCompiler.addConstant(PROTO_NONE));
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    const proto::ProtoList* co_varnames = ctx_->newList();
    co_varnames = co_varnames->appendLast(ctx_, ctx_->fromUTF8String(".0"));
    
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), ctx_->fromUTF8String(filename_.c_str())->asString(ctx_), co_varnames, 1, 1, 0, true);
    emit(OP_LOAD_CONST, addConstant(codeObj));
    emit(OP_BUILD_FUNCTION, 0);
    emit(OP_ROT_TWO);
    emit(OP_CALL_FUNCTION, 1);
    
    return true;
}

bool Compiler::compileComprehension(const std::vector<Comprehension>& generators, size_t index, std::function<bool()> innerBody) {
    if (index == generators.size()) {
        return innerBody();
    }
    
    const auto& gen = generators[index];
    if (!compileNode(gen.iter.get())) return false;
    emit(OP_GET_ITER);
    int loopStart = bytecodeOffset();
    emit(OP_FOR_ITER, 0);
    int endSlot = bytecodeOffset() - 1;
    if (!compileTarget(gen.target.get(), TargetCtx::Store)) return false;
    
    std::vector<int> ifSlots;
    for (const auto& condition : gen.ifs) {
        if (!compileNode(condition.get())) return false;
        emit(OP_POP_JUMP_IF_FALSE, 0);
        ifSlots.push_back(bytecodeOffset() - 1);
    }
    
    if (!compileComprehension(generators, index + 1, innerBody)) return false;
    
    emit(OP_JUMP_ABSOLUTE, loopStart);
    int loopEnd = bytecodeOffset();
    addPatch(endSlot, loopEnd);
    for (int slot : ifSlots) {
        addPatch(slot, loopStart);
    }
    return true;
}

bool Compiler::compileImport(ImportNode* n) {
    // Load __import__
    int idxImport = addName("__import__");
    emit(OP_LOAD_NAME, idxImport);
    // Load module name string
    int idxMod = addConstant(ctx_->fromUTF8String(n->moduleName.c_str()));
    emit(OP_LOAD_CONST, idxMod);
    
    if (n->isAs) {
        // Pass True to return the leaf module
        int idxTrue = addConstant(PROTO_TRUE);
        emit(OP_LOAD_CONST, idxTrue);
        emit(OP_CALL_FUNCTION, 2);
    } else {
        emit(OP_CALL_FUNCTION, 1);
    }
    
    // Store in alias
    return emitNameOp(n->alias, TargetCtx::Store);
}

bool Compiler::compileImportFrom(ImportFromNode* n) {
    // Load __import__
    int idxImport = addName("__import__");
    emit(OP_LOAD_NAME, idxImport);
    
    // name
    int idxMod = addConstant(ctx_->fromUTF8String(n->moduleName.c_str()));
    emit(OP_LOAD_CONST, idxMod);
    
    // globals (None for now or actual globals object)
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    // locals (None)
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    
    // fromlist: list of names
    std::vector<const proto::ProtoObject*> fromNames;
    for (auto& p : n->names) {
        fromNames.push_back(ctx_->fromUTF8String(p.first.c_str()));
    }
    const proto::ProtoList* fromList = ctx_->newList();
    for (auto* s : fromNames) fromList = fromList->appendLast(ctx_, s);
    emit(OP_LOAD_CONST, addConstant(fromList->asObject(ctx_)));
    
    // level
    emit(OP_LOAD_CONST, addConstant(ctx_->fromInteger(n->level)));
    
    emit(OP_CALL_FUNCTION, 5);
    
    if (n->names.size() == 1 && n->names[0].first == "*") {
        // star import: engine needs to copy all attrs. 
        // For now, we can only do this if we have an OP_IMPORT_STAR.
        // Simplified: push module, call import_star helper if exists.
        emit(OP_POP_TOP, 0); // Discard for now as not fully supported
        return true;
    }
    
    // Stack has module object.
    for (auto& p : n->names) {
        emit(OP_DUP_TOP, 0);
        int idxAttr = addName(p.first);
        emit(OP_LOAD_ATTR, idxAttr);
        std::string alias = p.second.empty() ? p.first : p.second;
        emitNameOp(alias, TargetCtx::Store);
    }
    
    emit(OP_POP_TOP, 0);
    return true;
}

bool Compiler::compileTry(TryNode* n) {
    if (!n || !n->body) return false;
    
    // Setup exception handler
    emit(OP_SETUP_FINALLY, 0);
    int setupFinallySlot = bytecodeOffset() - 1;
    
    if (!compileNode(n->body.get())) return false;
    
    // No exception: pop the block and jump over handlers
    emit(OP_POP_BLOCK, 0);
    emit(OP_JUMP_ABSOLUTE, 0); 
    int jumpToPostHandlersSlot = bytecodeOffset() - 1;
    
    // Exception handler starts here
    addPatch(setupFinallySlot, bytecodeOffset());
    
    if (!n->handlers.empty()) {
        for (auto& h : n->handlers) {
            // For now, we don't push exception to match type, 
            // but the engine pushed the exception object to the stack.
            // We should POP it if we don't bind it.
            if (h.name.empty()) {
                emit(OP_POP_TOP, 0); 
            } else {
                emitNameOp(h.name, TargetCtx::Store);
            }
            
            if (!compileNode(h.body.get())) return false;
            
            // After one handler matched/executed, jump to post-handlers
            // (In a more complete impl, we'd check exception type)
        }
    }
    
    addPatch(jumpToPostHandlersSlot, bytecodeOffset());

    if (n->orelse) {
        if (!compileNode(n->orelse.get())) return false;
    }
    
    if (n->finalbody) {
        if (!compileNode(n->finalbody.get())) return false;
    }
    return true;
}

bool Compiler::compileRaise(RaiseNode* n) {
    if (n->exc) {
        if (!compileNode(n->exc.get())) return false;
        emit(OP_RAISE_VARARGS, 1);
    } else {
        emit(OP_RAISE_VARARGS, 0); // re-raise
    }
    return true;
}

bool Compiler::compileWith(WithNode* n) {
    for (auto& item : n->items) {
        if (!compileNode(item.context_expr.get())) return false;
        emit(OP_SETUP_WITH, 0);
    }
    if (!compileNode(n->body.get())) return false;
    for (size_t i = 0; i < n->items.size(); ++i) {
        emit(OP_WITH_CLEANUP);
    }
    return true;
}

bool Compiler::compileAugAssign(AugAssignNode* n) {
    if (!n || !n->target) return false;
    // Load old value
    if (!compileNode(n->target.get())) return false;
    // Load value to add/sub/...
    if (!compileNode(n->value.get())) return false;
    
    // Perform operation
    int op = OP_INPLACE_ADD;
    switch (n->op) {
        case TokenType::PlusAssign: op = OP_INPLACE_ADD; break;
        case TokenType::MinusAssign: op = OP_INPLACE_SUBTRACT; break;
        case TokenType::StarAssign: op = OP_INPLACE_MULTIPLY; break;
        case TokenType::SlashAssign: op = OP_INPLACE_TRUE_DIVIDE; break;
        default: return false;
    }
    emit(op, 0);
    
    // Store back
    if (auto* name = dynamic_cast<NameNode*>(n->target.get())) {
        return emitNameOp(name->id, TargetCtx::Store);
    } else if (auto* att = dynamic_cast<AttributeNode*>(n->target.get())) {
        if (!compileNode(att->value.get())) return false;
        int nameIdx = addName(att->attr);
        emit(OP_STORE_ATTR, nameIdx);
        return true;
    }
    // TODO: support subscript aug assign
    return false;
}

bool Compiler::statementLeavesValue(ASTNode* node) {
    if (!node) return false;
    if (dynamic_cast<AssignNode*>(node) || dynamic_cast<AugAssignNode*>(node) || dynamic_cast<ForNode*>(node) ||
        dynamic_cast<IfNode*>(node) || dynamic_cast<FunctionDefNode*>(node) ||
        dynamic_cast<ClassDefNode*>(node) || dynamic_cast<WhileNode*>(node) ||
        dynamic_cast<ImportNode*>(node) || dynamic_cast<GlobalNode*>(node) ||
        dynamic_cast<ReturnNode*>(node) || dynamic_cast<DeleteNode*>(node) ||
        dynamic_cast<TryNode*>(node) || dynamic_cast<WithNode*>(node) ||
        dynamic_cast<AssertNode*>(node)) return false;
    return true;
}

static void collectNonlocalsFromNode(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* n = dynamic_cast<NonlocalNode*>(node)) {
        for (const auto& name : n->names) out.insert(name);
        return;
    }
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        collectNonlocalsFromNode(a->target.get(), out);
        collectNonlocalsFromNode(a->value.get(), out);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectNonlocalsFromNode(st.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectNonlocalsFromNode(f->target.get(), out);
        collectNonlocalsFromNode(f->iter.get(), out);
        collectNonlocalsFromNode(f->body.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectNonlocalsFromNode(iff->test.get(), out);
        collectNonlocalsFromNode(iff->body.get(), out);
        if (iff->orelse) collectNonlocalsFromNode(iff->orelse.get(), out);
        return;
    }
    if (auto* tr = dynamic_cast<TryNode*>(node)) {
        collectNonlocalsFromNode(tr->body.get(), out);
        for (auto& h : tr->handlers) collectNonlocalsFromNode(h.body.get(), out);
        if (tr->finalbody) collectNonlocalsFromNode(tr->finalbody.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WithNode*>(node)) {
        collectNonlocalsFromNode(w->body.get(), out);
        return;
    }
    if (auto* wh = dynamic_cast<WhileNode*>(node)) {
        collectNonlocalsFromNode(wh->test.get(), out);
        collectNonlocalsFromNode(wh->body.get(), out);
        if (wh->orelse) collectNonlocalsFromNode(wh->orelse.get(), out);
        return;
    }
}

static void collectGlobalsFromNode(ASTNode* node, std::unordered_set<std::string>& globalsOut) {
    if (!node) return;
    if (auto* g = dynamic_cast<GlobalNode*>(node)) {
        for (const auto& name : g->names) globalsOut.insert(name);
        return;
    }
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        collectGlobalsFromNode(a->target.get(), globalsOut);
        collectGlobalsFromNode(a->value.get(), globalsOut);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectGlobalsFromNode(st.get(), globalsOut);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectGlobalsFromNode(f->target.get(), globalsOut);
        collectGlobalsFromNode(f->iter.get(), globalsOut);
        collectGlobalsFromNode(f->body.get(), globalsOut);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectGlobalsFromNode(iff->test.get(), globalsOut);
        collectGlobalsFromNode(iff->body.get(), globalsOut);
        if (iff->orelse) collectGlobalsFromNode(iff->orelse.get(), globalsOut);
        return;
    }
    if (auto* tr = dynamic_cast<TryNode*>(node)) {
        collectGlobalsFromNode(tr->body.get(), globalsOut);
        for (auto& h : tr->handlers) {
            if (h.type) collectGlobalsFromNode(h.type.get(), globalsOut);
            collectGlobalsFromNode(h.body.get(), globalsOut);
        }
        if (tr->orelse) collectGlobalsFromNode(tr->orelse.get(), globalsOut);
        if (tr->finalbody) collectGlobalsFromNode(tr->finalbody.get(), globalsOut);
        return;
    }
    if (auto* r = dynamic_cast<RaiseNode*>(node)) {
        if (r->exc) collectGlobalsFromNode(r->exc.get(), globalsOut);
        if (r->cause) collectGlobalsFromNode(r->cause.get(), globalsOut);
        return;
    }
    if (auto* w = dynamic_cast<WithNode*>(node)) {
        for (auto& item : w->items) {
            collectGlobalsFromNode(item.context_expr.get(), globalsOut);
            if (item.optional_vars) collectGlobalsFromNode(item.optional_vars.get(), globalsOut);
        }
        collectGlobalsFromNode(w->body.get(), globalsOut);
        return;
    }
    if (auto* c = dynamic_cast<CallNode*>(node)) {
        collectGlobalsFromNode(c->func.get(), globalsOut);
        for (auto& arg : c->args) collectGlobalsFromNode(arg.get(), globalsOut);
        return;
    }
    if (auto* att = dynamic_cast<AttributeNode*>(node)) {
        collectGlobalsFromNode(att->value.get(), globalsOut);
        return;
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(node)) {
        collectGlobalsFromNode(sub->value.get(), globalsOut);
        collectGlobalsFromNode(sub->index.get(), globalsOut);
        return;
    }
    if (auto* sl = dynamic_cast<SliceNode*>(node)) {
        if (sl->start) collectGlobalsFromNode(sl->start.get(), globalsOut);
        if (sl->stop) collectGlobalsFromNode(sl->stop.get(), globalsOut);
        if (sl->step) collectGlobalsFromNode(sl->step.get(), globalsOut);
        return;
    }
    if (auto* b = dynamic_cast<BinOpNode*>(node)) {
        collectGlobalsFromNode(b->left.get(), globalsOut);
        collectGlobalsFromNode(b->right.get(), globalsOut);
        return;
    }
    if (auto* u = dynamic_cast<UnaryOpNode*>(node)) {
        collectGlobalsFromNode(u->operand.get(), globalsOut);
        return;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) collectGlobalsFromNode(e.get(), globalsOut);
        return;
    }
    if (auto* d = dynamic_cast<DictLiteralNode*>(node)) {
        for (size_t i = 0; i < d->keys.size(); ++i) {
            collectGlobalsFromNode(d->keys[i].get(), globalsOut);
            collectGlobalsFromNode(d->values[i].get(), globalsOut);
        }
        return;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectGlobalsFromNode(e.get(), globalsOut);
        return;
    }
}

void Compiler::collectLocalsFromBody(ASTNode* body,
    std::unordered_set<std::string>& globalsOut,
    std::unordered_set<std::string>& nonlocalsOut,
    std::vector<std::string>& localsOrdered) {
    globalsOut.clear();
    nonlocalsOut.clear();
    localsOrdered.clear();
    
    // 1. First pass: find all 'global' and 'nonlocal' declarations
    collectGlobalsFromNode(body, globalsOut);
    collectNonlocalsFromNode(body, nonlocalsOut);
    
    // 2. Second pass: find all names that are assigned/stored (implicitly local)
    std::unordered_set<std::string> defined;
    collectDefinedNames(body, defined);
    
    // Filter out those explicitly declared global
    std::unordered_set<std::string> seen;
    // We want a stable order, so ideally we'd traverse the AST once more 
    // to see the order of appearance of these 'defined' names that are NOT global.
    // However, collectDefinedNames doesn't give us order.
    // For now, let's just add them.
    for (const auto& name : defined) {
        if (globalsOut.find(name) == globalsOut.end() && nonlocalsOut.find(name) == nonlocalsOut.end()) {
            if (seen.find(name) == seen.end()) {
                localsOrdered.push_back(name);
                seen.insert(name);
            }
        }
    }
}

/** Collect names used (read or written) in node into out. */
static void collectUsedNames(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* nm = dynamic_cast<NameNode*>(node)) {
        out.insert(nm->id);
        return;
    }
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        collectUsedNames(a->target.get(), out);
        collectUsedNames(a->value.get(), out);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectUsedNames(st.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectUsedNames(f->target.get(), out);
        collectUsedNames(f->iter.get(), out);
        collectUsedNames(f->body.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectUsedNames(iff->test.get(), out);
        collectUsedNames(iff->body.get(), out);
        if (iff->orelse) collectUsedNames(iff->orelse.get(), out);
        return;
    }
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        for (const auto& p : fn->parameters) out.insert(p);
        collectUsedNames(fn->body.get(), out);
        return;
    }
    if (auto* c = dynamic_cast<CallNode*>(node)) {
        collectUsedNames(c->func.get(), out);
        for (auto& arg : c->args) collectUsedNames(arg.get(), out);
        return;
    }
    if (auto* att = dynamic_cast<AttributeNode*>(node)) {
        collectUsedNames(att->value.get(), out);
        return;
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(node)) {
        collectUsedNames(sub->value.get(), out);
        collectUsedNames(sub->index.get(), out);
        return;
    }
    if (auto* b = dynamic_cast<BinOpNode*>(node)) {
        collectUsedNames(b->left.get(), out);
        collectUsedNames(b->right.get(), out);
        return;
    }
    if (auto* u = dynamic_cast<UnaryOpNode*>(node)) {
        collectUsedNames(u->operand.get(), out);
        return;
    }
    if (auto* ret = dynamic_cast<ReturnNode*>(node)) {
        collectUsedNames(ret->value.get(), out);
        return;
    }
    if (auto* y = dynamic_cast<YieldNode*>(node)) {
        collectUsedNames(y->value.get(), out);
        return;
    }
    if (auto* t = dynamic_cast<TryNode*>(node)) {
        collectUsedNames(t->body.get(), out);
        for (auto& h : t->handlers) {
            collectUsedNames(h.body.get(), out);
            collectUsedNames(h.type.get(), out);
        }
        if (t->orelse) collectUsedNames(t->orelse.get(), out);
        if (t->finalbody) collectUsedNames(t->finalbody.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WhileNode*>(node)) {
        collectUsedNames(w->test.get(), out);
        collectUsedNames(w->body.get(), out);
        if (w->orelse) collectUsedNames(w->orelse.get(), out);
        return;
    }
    if (auto* cond = dynamic_cast<CondExprNode*>(node)) {
        collectUsedNames(cond->body.get(), out);
        collectUsedNames(cond->cond.get(), out);
        collectUsedNames(cond->orelse.get(), out);
        return;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) collectUsedNames(e.get(), out);
        return;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectUsedNames(e.get(), out);
        return;
    }
    if (auto* d = dynamic_cast<DictLiteralNode*>(node)) {
        for (size_t i = 0; i < d->keys.size(); ++i) {
            collectUsedNames(d->keys[i].get(), out);
            collectUsedNames(d->values[i].get(), out);
        }
        return;
    }
    if (auto* lam = dynamic_cast<LambdaNode*>(node)) {
        for (const auto& p : lam->parameters) out.insert(p);
        collectUsedNames(lam->body.get(), out);
        return;
    }
}

/** Collect names defined in node (assigned or are params of a nested def). */
static void collectDefinedNames(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        collectDefinedNames(a->target.get(), out);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectDefinedNames(st.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectDefinedNames(f->target.get(), out);
        collectDefinedNames(f->body.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectDefinedNames(iff->body.get(), out);
        if (iff->orelse) collectDefinedNames(iff->orelse.get(), out);
        return;
    }
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        out.insert(fn->name);
        // Note: parameters are handled separately in compileFunctionDef, 
        // but adding them here doesn't hurt if we want a complete set of locals.
        for (const auto& p : fn->parameters) out.insert(p);
        // We do NOT recurse into fn->body because those are local to the NESTED function.
        return;
    }
    if (auto* cl = dynamic_cast<ClassDefNode*>(node)) {
        out.insert(cl->name);
        // We do NOT recurse into cl->body.
        return;
    }
    if (auto* imp = dynamic_cast<ImportNode*>(node)) {
        if (imp->isAs) {
            out.insert(imp->alias);
        } else {
            // For 'import a.b.c', 'a' is defining in the local scope.
            std::string root = imp->moduleName;
            size_t dot = root.find('.');
            if (dot != std::string::npos) root = root.substr(0, dot);
            out.insert(root);
        }
        return;
    }
    if (auto* t = dynamic_cast<TryNode*>(node)) {
        collectDefinedNames(t->body.get(), out);
        for (auto& h : t->handlers) {
            collectDefinedNames(h.body.get(), out);
        }
        if (t->orelse) collectDefinedNames(t->orelse.get(), out);
        if (t->finalbody) collectDefinedNames(t->finalbody.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WithNode*>(node)) {
        for (auto& item : w->items) {
            if (item.optional_vars) collectDefinedNames(item.optional_vars.get(), out);
        }
        collectDefinedNames(w->body.get(), out);
        return;
    }
    if (auto* nm = dynamic_cast<NameNode*>(node)) {
        out.insert(nm->id);
        return;
    }
    if (auto* lam = dynamic_cast<LambdaNode*>(node)) {
        for (const auto& p : lam->parameters) out.insert(p);
        collectDefinedNames(lam->body.get(), out);
        return;
    }
}

/** Returns non-empty string if body has dynamic locals access; reason for slot fallback. */
static std::string getDynamicLocalsReason(ASTNode* node) {
    if (!node) return "";
    if (auto* call = dynamic_cast<CallNode*>(node)) {
        if (call->func) {
            if (auto* nm = dynamic_cast<NameNode*>(call->func.get())) {
                if (nm->id == "locals") return "locals";
                if (nm->id == "exec") return "exec";
                if (nm->id == "eval") return "eval";
            }
        }
    }
    if (auto* suite = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : suite->statements) {
            std::string r = getDynamicLocalsReason(st.get());
            if (!r.empty()) return r;
        }
        return "";
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        std::string r = getDynamicLocalsReason(iff->body.get());
        if (!r.empty()) return r;
        if (iff->orelse) return getDynamicLocalsReason(iff->orelse.get());
        return "";
    }
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        return getDynamicLocalsReason(fn->body.get());
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        return getDynamicLocalsReason(f->body.get());
    }
    return "";
}

bool Compiler::hasDynamicLocalsAccess(ASTNode* node) {
    return !getDynamicLocalsReason(node).empty();
}

void Compiler::collectCapturedNames(ASTNode* node,
    const std::unordered_set<std::string>& globalsInScope,
    std::unordered_set<std::string>& capturedOut) {
    if (!node) return;
    if (std::getenv("PROTO_ENV_DIAG")) {
        std::cerr << "[proto-compiler] collectCapturedNames node type: " << typeid(*node).name() << "\n";
    }

    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        std::unordered_set<std::string> used, defined, nonlocals;
        for (const auto& p : fn->parameters) defined.insert(p);
        collectUsedNames(fn->body.get(), used);
        collectDefinedNames(fn->body.get(), defined);
        collectNonlocalsFromNode(fn->body.get(), nonlocals);
        
        if (std::getenv("PROTO_ENV_DIAG")) {
            std::cerr << "[proto-compiler] Analyzing nested function: " << fn->name << "\n";
            for (auto& u : used) std::cerr << "  uses: " << u << "\n";
            for (auto& d : defined) std::cerr << "  defines: " << d << "\n";
            for (auto& nl : nonlocals) std::cerr << "  nonlocals: " << nl << "\n";
        }

        for (const auto& name : nonlocals) {
            capturedOut.insert(name);
        }
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name) && !nonlocals.count(name)) {
                if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-compiler]   FOUND CAPTURE: " << name << "\n";
                capturedOut.insert(name);
            }
        }
        // Grandchildren can capture from this level too
        collectCapturedNames(fn->body.get(), globalsInScope, capturedOut);
        return;
    }

    if (auto* lam = dynamic_cast<LambdaNode*>(node)) {
        std::unordered_set<std::string> used, defined, nonlocals;
        for (const auto& p : lam->parameters) defined.insert(p);
        collectUsedNames(lam->body.get(), used);
        collectDefinedNames(lam->body.get(), defined);
        collectNonlocalsFromNode(lam->body.get(), nonlocals);
        
        for (const auto& name : nonlocals) {
            capturedOut.insert(name);
        }
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name) && !nonlocals.count(name)) {
                capturedOut.insert(name);
            }
        }
        collectCapturedNames(lam->body.get(), globalsInScope, capturedOut);
        return;
    }

    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectCapturedNames(st.get(), globalsInScope, capturedOut);
    } else if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectCapturedNames(iff->body.get(), globalsInScope, capturedOut);
        if (iff->orelse) collectCapturedNames(iff->orelse.get(), globalsInScope, capturedOut);
    } else if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectCapturedNames(f->body.get(), globalsInScope, capturedOut);
    } else if (auto* w = dynamic_cast<WhileNode*>(node)) {
        collectCapturedNames(w->body.get(), globalsInScope, capturedOut);
    } else if (auto* t = dynamic_cast<TryNode*>(node)) {
        collectCapturedNames(t->body.get(), globalsInScope, capturedOut);
        for (auto& h : t->handlers) collectCapturedNames(h.body.get(), globalsInScope, capturedOut);
        if (t->orelse) collectCapturedNames(t->orelse.get(), globalsInScope, capturedOut);
        if (t->finalbody) collectCapturedNames(t->finalbody.get(), globalsInScope, capturedOut);
    } else if (auto* a = dynamic_cast<AssignNode*>(node)) {
        collectCapturedNames(a->value.get(), globalsInScope, capturedOut);
    } else if (auto* b = dynamic_cast<BinOpNode*>(node)) {
        collectCapturedNames(b->left.get(), globalsInScope, capturedOut);
        collectCapturedNames(b->right.get(), globalsInScope, capturedOut);
    } else if (auto* c = dynamic_cast<CallNode*>(node)) {
        collectCapturedNames(c->func.get(), globalsInScope, capturedOut);
        for (auto& arg : c->args) collectCapturedNames(arg.get(), globalsInScope, capturedOut);
    } else if (auto* ret = dynamic_cast<ReturnNode*>(node)) {
        collectCapturedNames(ret->value.get(), globalsInScope, capturedOut);
    }
}

bool Compiler::compileSuite(SuiteNode* n) {
    if (!n || n->statements.empty()) return false;
    for (size_t i = 0; i < n->statements.size(); ++i) {
        if (!compileNode(n->statements[i].get())) return false;
        if (statementLeavesValue(n->statements[i].get()))
            emit(OP_POP_TOP, 0);
    }
    return true;
}

bool Compiler::compileFunctionDef(FunctionDefNode* n) {
    if (!n) return false;
    std::unordered_set<std::string> bodyGlobals;
    std::unordered_set<std::string> bodyNonlocals;
    std::vector<std::string> localsOrdered;
    collectLocalsFromBody(n->body.get(), bodyGlobals, bodyNonlocals, localsOrdered);
    std::vector<std::string> params;
    if (!n->parameters.empty()) {
        for (const auto& p : n->parameters) params.push_back(p);
    }
    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), bodyGlobals, captured);
    if (std::getenv("PROTO_ENV_DIAG")) {
        std::cerr << "[proto-compiler] Function " << n->name << " capture count: " << captured.size() << std::endl;
        for (const auto& c : captured) std::cerr << "  captured: " << c << std::endl;
    }

    std::string dynamicReason = getDynamicLocalsReason(n->body.get());
    const bool forceMapped = !dynamicReason.empty() || !captured.empty();
    if (std::getenv("PROTO_ENV_DIAG") && forceMapped)
        std::cerr << "[proto-compiler] forceMapped " << n->name << " reason: " << dynamicReason << "\n";

    std::vector<std::string> varnamesOrdered;
    // Parameters first
    for (const auto& p : params) {
        varnamesOrdered.push_back(p);
    }
    // Then vararg and kwarg (to match ExecutionEngine's expectation of index nparams and nparams+1)
    if (!n->vararg.empty()) varnamesOrdered.push_back(n->vararg);
    if (!n->kwarg.empty()) varnamesOrdered.push_back(n->kwarg);

    // Then other locals
    for (const auto& loc : localsOrdered) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == loc) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(loc);
    }

    std::unordered_map<std::string, int> slotMap;
    int automatic_count = 0;
    if (!forceMapped) {
        for (size_t i = 0; i < varnamesOrdered.size(); ++i) {
            slotMap[varnamesOrdered[i]] = static_cast<int>(i);
        }
        automatic_count = static_cast<int>(varnamesOrdered.size());
    } else {
        if (std::getenv("PROTO_ENV_DIAG")) {
            std::cerr << "[protoPython] Slot fallback: function=" << n->name << " reason=" << (dynamicReason.empty() ? "capture" : dynamicReason);
            if (!captured.empty()) {
                std::cerr << "(";
                for(const auto& c : captured) std::cerr << c << " ";
                std::cerr << ")";
            }
            std::cerr << std::endl;
        }
    }
    int nparams = static_cast<int>(params.size());

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = bodyGlobals;
    bodyCompiler.nonlocalNames_ = bodyNonlocals;
    bodyCompiler.localSlotMap_ = slotMap;
    if (!bodyCompiler.compileNode(n->body.get())) return false;
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
    int noneIdx = bodyCompiler.addConstant(env ? env->getNonePrototype() : PROTO_NONE);
    bodyCompiler.emit(OP_LOAD_CONST, noneIdx);
    bodyCompiler.emit(OP_RETURN_VALUE);
    
    bodyCompiler.applyPatches();

    const proto::ProtoList* co_varnames = ctx_->newList();
    for (const auto& name : varnamesOrdered)
        co_varnames = co_varnames->appendLast(ctx_, ctx_->fromUTF8String(name.c_str()));
        
    int co_flags = 0;
    if (!n->vararg.empty()) co_flags |= CO_VARARGS;
    if (!n->kwarg.empty()) co_flags |= CO_VARKEYWORDS;
    
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), ctx_->fromUTF8String(filename_.c_str())->asString(ctx_), co_varnames, nparams, automatic_count, co_flags, bodyCompiler.isGenerator_);
    if (!codeObj) return false;
    int idx = addConstant(codeObj);
    emit(OP_LOAD_CONST, idx);
    emit(OP_BUILD_FUNCTION, co_flags);
    
    // Apply decorators Bottom-to-Top
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
            if (!compileNode(it->get())) return false;
            emit(OP_ROT_TWO, 0);
            emit(OP_CALL_FUNCTION, 1);
        }
    }
    
    return emitNameOp(n->name, TargetCtx::Store);
}

bool Compiler::compileLambda(LambdaNode* n) {
    if (!n) return false;
    
    std::unordered_set<std::string> bodyGlobals;
    std::vector<std::string> localsOrdered;
    std::vector<std::string> params = n->parameters;
    
    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), bodyGlobals, captured);
    
    const bool forceMapped = !captured.empty();
    std::vector<std::string> varnamesOrdered = params;
    if (!n->vararg.empty()) varnamesOrdered.push_back(n->vararg);
    if (!n->kwarg.empty()) varnamesOrdered.push_back(n->kwarg);
    
    std::unordered_map<std::string, int> slotMap;
    int automatic_count = 0;
    if (!forceMapped) {
        for (size_t i = 0; i < varnamesOrdered.size(); ++i) {
            slotMap[varnamesOrdered[i]] = static_cast<int>(i);
        }
        automatic_count = static_cast<int>(varnamesOrdered.size());
    }
    
    int nparams = static_cast<int>(params.size());

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = bodyGlobals;
    bodyCompiler.localSlotMap_ = slotMap;
    
    if (!bodyCompiler.compileNode(n->body.get())) return false;
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();

    const proto::ProtoList* co_varnames = ctx_->newList();
    for (const auto& name : varnamesOrdered)
        co_varnames = co_varnames->appendLast(ctx_, ctx_->fromUTF8String(name.c_str()));
        
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), ctx_->fromUTF8String(filename_.c_str())->asString(ctx_), co_varnames, nparams, automatic_count, 0, false);
    if (!codeObj) return false;
    int idx = addConstant(codeObj);
    emit(OP_LOAD_CONST, idx);
    emit(OP_BUILD_FUNCTION, 0);
    
    return true;
}

bool Compiler::compileClassDef(ClassDefNode* n) {
    if (!n) return false;
    
    // 1. Name
    int nameIdx = addConstant(proto::ProtoString::fromUTF8String(ctx_, n->name.c_str())->asObject(ctx_));
    emit(OP_LOAD_CONST, nameIdx);
    
    // 2. Bases
    for (auto& b : n->bases) {
        if (!compileNode(b.get())) return false;
    }
    emit(OP_BUILD_TUPLE, static_cast<int>(n->bases.size()));
    
    // 3. Body
    Compiler bodyCompiler(ctx_, filename_);
    if (!bodyCompiler.compileNode(n->body.get())) return false;
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), ctx_->fromUTF8String(filename_.c_str())->asString(ctx_), nullptr, 0, 0, 0, bodyCompiler.isGenerator_);
    int coIdx = addConstant(codeObj);
    emit(OP_LOAD_CONST, coIdx);
    emit(OP_BUILD_FUNCTION, 0);
    
    // 4. Build
    emit(OP_BUILD_CLASS);
    
    // Apply decorators Bottom-to-Top
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
            if (!compileNode(it->get())) return false;
            emit(OP_ROT_TWO, 0);
            emit(OP_CALL_FUNCTION, 1);
        }
    }
    
    // 5. Store
    return emitNameOp(n->name, TargetCtx::Store);
}

bool Compiler::compileCondExpr(CondExprNode* n) {
    if (!n) return false;
    if (!compileNode(n->cond.get())) return false;
    emit(OP_POP_JUMP_IF_FALSE, 0);
    int elseSlot = bytecodeOffset() - 1;
    if (!compileNode(n->body.get())) return false;
    emit(OP_JUMP_ABSOLUTE, 0);
    int endSlot = bytecodeOffset() - 1;
    addPatch(elseSlot, bytecodeOffset());
    if (!compileNode(n->orelse.get())) return false;
    addPatch(endSlot, bytecodeOffset());
    return true;
}

bool Compiler::compileNode(ASTNode* node) {
    if (!node) return false;
    if (dynamic_cast<PassNode*>(node)) return true;
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) return compileFunctionDef(fn);
    if (auto* cl = dynamic_cast<ClassDefNode*>(node)) return compileClassDef(cl);
    if (auto* ce = dynamic_cast<CondExprNode*>(node)) return compileCondExpr(ce);
    if (auto* c = dynamic_cast<ConstantNode*>(node)) return compileConstant(c);
    if (auto* nm = dynamic_cast<NameNode*>(node)) return compileName(nm);
    if (auto* b = dynamic_cast<BinOpNode*>(node)) return compileBinOp(b);
    if (auto* u = dynamic_cast<UnaryOpNode*>(node)) return compileUnaryOp(u);
    if (auto* cl = dynamic_cast<CallNode*>(node)) return compileCall(cl);
    if (auto* att = dynamic_cast<AttributeNode*>(node)) return compileAttribute(att);
    if (auto* sub = dynamic_cast<SubscriptNode*>(node)) return compileSubscript(sub);
    if (auto* sl = dynamic_cast<SliceNode*>(node)) return compileSlice(sl);
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) return compileListLiteral(lst);
    if (auto* d = dynamic_cast<DictLiteralNode*>(node)) return compileDictLiteral(d);
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) return compileTupleLiteral(tup);
    if (auto* set = dynamic_cast<SetLiteralNode*>(node)) return compileSetLiteral(set);
    if (auto* a = dynamic_cast<AssignNode*>(node)) return compileAssign(a);
    if (auto* aa = dynamic_cast<AugAssignNode*>(node)) return compileAugAssign(aa);
    if (auto* an = dynamic_cast<AssertNode*>(node)) return compileAssert(an);
    if (auto* lc = dynamic_cast<ListCompNode*>(node)) return compileListComp(lc);
    if (auto* dc = dynamic_cast<DictCompNode*>(node)) return compileDictComp(dc);
    if (auto* sc = dynamic_cast<SetCompNode*>(node)) return compileSetComp(sc);
    if (auto* ge = dynamic_cast<GeneratorExpNode*>(node)) return compileGeneratorExp(ge);
    if (auto* lam = dynamic_cast<LambdaNode*>(node)) return compileLambda(lam);
    if (auto* js = dynamic_cast<JoinedStrNode*>(node)) return compileJoinedStr(js);
    if (auto* fv = dynamic_cast<FormattedValueNode*>(node)) return compileFormattedValue(fv);
    if (auto* d = dynamic_cast<DeleteNode*>(node)) return compileDeleteNode(d);
    if (auto* w = dynamic_cast<WhileNode*>(node)) return compileWhile(w);
    if (auto* f = dynamic_cast<ForNode*>(node)) return compileFor(f);
    if (dynamic_cast<BreakNode*>(node)) return compileBreak(static_cast<BreakNode*>(node));
    if (dynamic_cast<ContinueNode*>(node)) return compileContinue(static_cast<ContinueNode*>(node));
    if (auto* iff = dynamic_cast<IfNode*>(node)) return compileIf(iff);
    if (auto* g = dynamic_cast<GlobalNode*>(node)) return compileGlobal(g);
    if (auto* nl = dynamic_cast<NonlocalNode*>(node)) return compileNonlocal(nl);
    if (auto* r = dynamic_cast<ReturnNode*>(node)) return compileReturn(r);
    if (auto* y = dynamic_cast<YieldNode*>(node)) return compileYield(y);
    if (auto* imp = dynamic_cast<ImportNode*>(node)) return compileImport(imp);
    if (auto* imf = dynamic_cast<ImportFromNode*>(node)) return compileImportFrom(imf);
    if (auto* t = dynamic_cast<TryNode*>(node)) return compileTry(t);
    if (auto* r = dynamic_cast<RaiseNode*>(node)) return compileRaise(r);
    if (auto* w = dynamic_cast<WithNode*>(node)) return compileWith(w);
    if (auto* s = dynamic_cast<SuiteNode*>(node)) return compileSuite(s);
    return false;
}

bool Compiler::compileExpression(ASTNode* expr) {
    if (!compileNode(expr)) return false;
    emit(OP_RETURN_VALUE, 0);
    applyPatches();
    return true;
}

bool Compiler::compileModule(ModuleNode* mod) {
    if (!mod || mod->body.empty()) return false;
    globalNames_.clear();
    
    for (size_t i = 0; i < mod->body.size(); ++i) {
        if (!compileNode(mod->body[i].get())) return false;
        if (statementLeavesValue(mod->body[i].get()))
            emit(OP_POP_TOP, 0);
    }
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
    int noneIdx = addConstant(env ? env->getNonePrototype() : PROTO_NONE);
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_RETURN_VALUE);
    applyPatches();
    return true;
}

const proto::ProtoObject* makeCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* names,
    const proto::ProtoList* bytecode,
    const proto::ProtoString* filename,
    const proto::ProtoList* varnames,
    int nparams,
    int automatic_count,
    int flags,
    bool isGenerator) {
    if (!ctx) return PROTO_NONE;
    if (std::getenv("PROTO_ENV_DIAG")) {
        std::cerr << "[proto-compiler-diag] makeCodeObject constants=" << constants << " size=" << (constants ? constants->getSize(ctx) : 0) << "\n";
        if (constants && constants->getSize(ctx) > 0) {
            std::cerr << "[proto-compiler-diag] makeCodeObject constants[0]=" << constants->getAt(ctx, 0) << "\n";
        }
    }
    const proto::ProtoObject* code = ctx->newObject(true);
    // Optional: add a 'code_proto' if we want to share methods like .exec()
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_consts"), reinterpret_cast<const proto::ProtoObject*>(constants));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_names"), names ? reinterpret_cast<const proto::ProtoObject*>(names) : reinterpret_cast<const proto::ProtoObject*>(ctx->newList()));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_code"), reinterpret_cast<const proto::ProtoObject*>(bytecode));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_filename"), filename ? reinterpret_cast<const proto::ProtoObject*>(filename) : reinterpret_cast<const proto::ProtoObject*>(ctx->fromUTF8String("<stdin>")));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_varnames"), varnames ? reinterpret_cast<const proto::ProtoObject*>(varnames) : reinterpret_cast<const proto::ProtoObject*>(ctx->newList()));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_nparams"), ctx->fromInteger(nparams));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_automatic_count"), ctx->fromInteger(automatic_count));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_flags"), ctx->fromInteger(flags));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_is_generator"), ctx->fromBoolean(isGenerator));
    return code;
}

namespace {
struct CodeObjectScope {
    CodeObjectScope(const proto::ProtoObject* code) : oldCode(PythonEnvironment::getCurrentCodeObject()) {
        PythonEnvironment::setCurrentCodeObject(code);
    }
    ~CodeObjectScope() {
        PythonEnvironment::setCurrentCodeObject(oldCode);
    }
    const proto::ProtoObject* oldCode;
};
}

const proto::ProtoObject* runCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoObject* codeObj,
    proto::ProtoObject*& frame) {
    if (!ctx || !codeObj || !frame) return PROTO_NONE;
    CodeObjectScope cscope(codeObj);

    const proto::ProtoObject* co_consts = codeObj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_consts"));
    const proto::ProtoObject* co_names = codeObj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_names"));
    const proto::ProtoObject* co_code = codeObj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_code"));

    if (!co_consts || !co_consts->asList(ctx) || !co_code || !co_code->asList(ctx))
        return PROTO_NONE;

    return executeBytecodeRange(ctx, co_consts->asList(ctx), co_code->asList(ctx),
        co_names ? co_names->asList(ctx) : nullptr, frame, 0, co_code->asList(ctx)->getSize(ctx));
}

bool Compiler::compileJoinedStr(JoinedStrNode* n) {
    if (!n) return false;
    for (const auto& val : n->values) {
        if (!compileNode(val.get())) return false;
    }
    emit(OP_BUILD_STRING, static_cast<int>(n->values.size()));
    return true;
}

bool Compiler::compileFormattedValue(FormattedValueNode* n) {
    if (!n) return false;
    return compileNode(n->value.get());
}

bool Compiler::compileNonlocal(NonlocalNode* n) {
    // Nonlocals are handled in collectLocalsFromBody / emitNameOp
    return true;
}

} // namespace protoPython
