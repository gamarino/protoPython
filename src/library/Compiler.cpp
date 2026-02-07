#include <protoPython/Compiler.h>
#include <protoPython/ExecutionEngine.h>
#include <iostream>

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
                   op == OP_LOAD_FAST || op == OP_STORE_FAST ||
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
    else if (n->constType == ConstantNode::ConstType::None)
        obj = PROTO_NONE;
    else if (n->constType == ConstantNode::ConstType::Bool)
        obj = n->intVal ? PROTO_TRUE : PROTO_FALSE;
    if (!obj) return false;
    int idx = addConstant(obj);
    emit(OP_LOAD_CONST, idx);
    return true;
}

bool Compiler::compileName(NameNode* n) {
    if (!n) return false;
    if (globalNames_.count(n->id)) {
        int idx = addName(n->id);
        emit(OP_LOAD_GLOBAL, idx);
        return true;
    }
    auto it = localSlotMap_.find(n->id);
    if (it != localSlotMap_.end()) {
        emit(OP_LOAD_FAST, it->second);
        return true;
    }
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

bool Compiler::compileCall(CallNode* n) {
    if (!n || !compileNode(n->func.get())) return false;
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
        const proto::ProtoTuple* nameTuple = ctx_->newTupleFromList(kwList);
        
        int idx = addConstant(nameTuple->asObject(ctx_));
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
        if (globalNames_.count(nm->id)) {
            int idx = addName(nm->id);
            if (isStore) emit(OP_STORE_GLOBAL, idx);
            else emit(OP_LOAD_GLOBAL, idx);
            return true;
        }
        auto it = localSlotMap_.find(nm->id);
        if (it != localSlotMap_.end()) {
            if (isStore) emit(OP_STORE_FAST, it->second);
            else emit(OP_LOAD_FAST, it->second);
            return true;
        }
        int idx = addName(nm->id);
        if (isStore) emit(OP_STORE_NAME, idx);
        else emit(OP_LOAD_NAME, idx);
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

bool Compiler::compileImport(ImportNode* n) {
    // Load __import__
    int idxImport = addName("__import__");
    emit(OP_LOAD_NAME, idxImport);
    // Load module name string
    int idxMod = addConstant(ctx_->fromUTF8String(n->moduleName.c_str()));
    emit(OP_LOAD_CONST, idxMod);
    // Call with 1 arg
    emit(OP_CALL_FUNCTION, 1);
    // Store in alias
    int idxAlias = addName(n->alias);
    emit(OP_STORE_NAME, idxAlias);
    return true;
}

bool Compiler::compileTry(TryNode* n) {
    // Simple pass-through: compile body, skip handlers for now (benchmarks usually don't fail)
    if (!compileNode(n->body.get())) return false;
    if (n->finalbody) {
        if (!compileNode(n->finalbody.get())) return false;
    }
    return true;
}

bool Compiler::statementLeavesValue(ASTNode* node) {
    if (!node) return false;
    if (dynamic_cast<AssignNode*>(node) || dynamic_cast<ForNode*>(node) ||
        dynamic_cast<IfNode*>(node) || dynamic_cast<GlobalNode*>(node) ||
        dynamic_cast<PassNode*>(node) || dynamic_cast<FunctionDefNode*>(node) ||
        dynamic_cast<ReturnNode*>(node) || dynamic_cast<ImportNode*>(node) ||
        dynamic_cast<TryNode*>(node) ||
        dynamic_cast<SuiteNode*>(node))
        return false;
    return true;
}

static void collectLocalsFromNode(ASTNode* node,
    std::unordered_set<std::string>& globalsOut,
    std::unordered_set<std::string>& seenLocals,
    std::vector<std::string>& localsOrdered) {
    if (!node) return;
    if (auto* g = dynamic_cast<GlobalNode*>(node)) {
        for (const auto& name : g->names) globalsOut.insert(name);
        return;
    }
    if (auto* nm = dynamic_cast<NameNode*>(node)) {
        if (!globalsOut.count(nm->id) && !seenLocals.count(nm->id)) {
            seenLocals.insert(nm->id);
            localsOrdered.push_back(nm->id);
        }
        return;
    }
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        collectLocalsFromNode(a->target.get(), globalsOut, seenLocals, localsOrdered);
        collectLocalsFromNode(a->value.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements)
            collectLocalsFromNode(st.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectLocalsFromNode(f->target.get(), globalsOut, seenLocals, localsOrdered);
        collectLocalsFromNode(f->iter.get(), globalsOut, seenLocals, localsOrdered);
        collectLocalsFromNode(f->body.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectLocalsFromNode(iff->test.get(), globalsOut, seenLocals, localsOrdered);
        collectLocalsFromNode(iff->body.get(), globalsOut, seenLocals, localsOrdered);
        if (iff->orelse) collectLocalsFromNode(iff->orelse.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
    if (dynamic_cast<FunctionDefNode*>(node)) {
        return;
    }
    if (auto* c = dynamic_cast<CallNode*>(node)) {
        collectLocalsFromNode(c->func.get(), globalsOut, seenLocals, localsOrdered);
        for (auto& arg : c->args) collectLocalsFromNode(arg.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
    if (auto* att = dynamic_cast<AttributeNode*>(node)) {
        collectLocalsFromNode(att->value.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(node)) {
        collectLocalsFromNode(sub->value.get(), globalsOut, seenLocals, localsOrdered);
        collectLocalsFromNode(sub->index.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
    if (auto* b = dynamic_cast<BinOpNode*>(node)) {
        collectLocalsFromNode(b->left.get(), globalsOut, seenLocals, localsOrdered);
        collectLocalsFromNode(b->right.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) collectLocalsFromNode(e.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
    if (auto* d = dynamic_cast<DictLiteralNode*>(node)) {
        for (size_t i = 0; i < d->keys.size(); ++i) {
            collectLocalsFromNode(d->keys[i].get(), globalsOut, seenLocals, localsOrdered);
            collectLocalsFromNode(d->values[i].get(), globalsOut, seenLocals, localsOrdered);
        }
        return;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectLocalsFromNode(e.get(), globalsOut, seenLocals, localsOrdered);
        return;
    }
}

void Compiler::collectLocalsFromBody(ASTNode* body,
    std::unordered_set<std::string>& globalsOut,
    std::vector<std::string>& localsOrdered) {
    globalsOut.clear();
    localsOrdered.clear();
    std::unordered_set<std::string> seenLocals;
    collectLocalsFromNode(body, globalsOut, seenLocals, localsOrdered);
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
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) collectUsedNames(e.get(), out);
        return;
    }
    if (auto* d = dynamic_cast<DictLiteralNode*>(node)) {
        for (size_t i = 0; i < d->keys.size(); ++i) {
            collectUsedNames(d->keys[i].get(), out);
            collectUsedNames(d->values[i].get(), out);
        }
        return;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectUsedNames(e.get(), out);
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
        for (const auto& p : fn->parameters) out.insert(p);
        collectDefinedNames(fn->body.get(), out);
        return;
    }
    if (auto* nm = dynamic_cast<NameNode*>(node)) {
        out.insert(nm->id);
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

void Compiler::collectCapturedNames(ASTNode* body,
    const std::unordered_set<std::string>& globalsInScope,
    const std::vector<std::string>& paramsInScope,
    std::unordered_set<std::string>& capturedOut) {
    capturedOut.clear();
    if (!body) return;
    if (auto* s = dynamic_cast<SuiteNode*>(body)) {
        for (auto& st : s->statements) {
            if (auto* fn = dynamic_cast<FunctionDefNode*>(st.get())) {
                std::unordered_set<std::string> used, defined;
                collectUsedNames(fn->body.get(), used);
                collectDefinedNames(fn->body.get(), defined);
                for (const auto& name : used) {
                    if (!defined.count(name) && !globalsInScope.count(name))
                        capturedOut.insert(name);
                }
                collectCapturedNames(fn->body.get(), globalsInScope, fn->parameters, capturedOut);
            }
        }
        return;
    }
    if (auto* fn = dynamic_cast<FunctionDefNode*>(body)) {
        std::unordered_set<std::string> used, defined;
        collectUsedNames(fn->body.get(), used);
        collectDefinedNames(fn->body.get(), defined);
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name))
                capturedOut.insert(name);
        }
        collectCapturedNames(fn->body.get(), globalsInScope, fn->parameters, capturedOut);
    }
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
    std::unordered_set<std::string> bodyGlobals;
    std::vector<std::string> localsOrdered;
    collectLocalsFromBody(n->body.get(), bodyGlobals, localsOrdered);
    std::vector<std::string> params;
    if (!n->parameters.empty()) {
        for (const auto& p : n->parameters) params.push_back(p);
    }
    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), bodyGlobals, params, captured);

    std::string dynamicReason = getDynamicLocalsReason(n->body.get());
    const bool forceMapped = !dynamicReason.empty();

    std::vector<std::string> automaticNames;
    if (!forceMapped) {
        for (const auto& p : params)
            automaticNames.push_back(p);
        for (const auto& loc : localsOrdered) {
            if (!bodyGlobals.count(loc) && !captured.count(loc))
                automaticNames.push_back(loc);
        }
    }
    if (forceMapped) {
        std::cerr << "[protoPython] Slot fallback: function=" << n->name << " reason=" << dynamicReason << std::endl;
    } else if (!captured.empty()) {
        std::cerr << "[protoPython] Slot fallback: function=" << n->name << " reason=capture" << std::endl;
    }
    std::unordered_map<std::string, int> slotMap;
    for (size_t i = 0; i < automaticNames.size(); ++i)
        slotMap[automaticNames[i]] = static_cast<int>(i);
    int nparams = static_cast<int>(params.size());
    int automatic_count = static_cast<int>(automaticNames.size());

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = bodyGlobals;
    bodyCompiler.localSlotMap_ = slotMap;
    if (!bodyCompiler.compileNode(n->body.get())) return false;
    bodyCompiler.bytecode_ = bodyCompiler.bytecode_->appendLast(ctx_, ctx_->fromInteger(static_cast<int>(OP_RETURN_VALUE)));
    bodyCompiler.applyPatches();

    const proto::ProtoList* co_varnames = ctx_->newList();
    for (const auto& name : automaticNames)
        co_varnames = co_varnames->appendLast(ctx_, ctx_->fromUTF8String(name.c_str()));
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), co_varnames, nparams, automatic_count);
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
    if (auto* ret = dynamic_cast<ReturnNode*>(node)) return compileReturn(ret);
    if (auto* imp = dynamic_cast<ImportNode*>(node)) return compileImport(imp);
    if (auto* t = dynamic_cast<TryNode*>(node)) return compileTry(t);
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
    const proto::ProtoList* bytecode,
    const proto::ProtoList* varnames,
    int nparams,
    int automatic_count) {
    if (!ctx || !constants || !bytecode) return nullptr;
    const proto::ProtoObject* code = ctx->newObject(true);
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_consts"), reinterpret_cast<const proto::ProtoObject*>(constants));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_names"), names ? reinterpret_cast<const proto::ProtoObject*>(names) : reinterpret_cast<const proto::ProtoObject*>(ctx->newList()));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_code"), reinterpret_cast<const proto::ProtoObject*>(bytecode));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_varnames"), varnames ? reinterpret_cast<const proto::ProtoObject*>(varnames) : reinterpret_cast<const proto::ProtoObject*>(ctx->newList()));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_nparams"), ctx->fromInteger(nparams));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_automatic_count"), ctx->fromInteger(automatic_count));
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
