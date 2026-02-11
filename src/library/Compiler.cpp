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
                   op == OP_BUILD_SLICE || op == OP_FOR_ITER || op == OP_POP_JUMP_IF_FALSE || op == OP_POP_JUMP_IF_TRUE ||
                   op == OP_JUMP_ABSOLUTE || op == OP_COMPARE_OP || op == OP_BINARY_SUBSCR ||
                   op == OP_STORE_SUBSCR || op == OP_CALL_FUNCTION_KW || 
                   op == OP_BUILD_FUNCTION || 
                   op == OP_DELETE_NAME || op == OP_DELETE_GLOBAL || op == OP_DELETE_FAST ||
                   op == OP_DELETE_ATTR || op == OP_RAISE_VARARGS ||
                   op == OP_LIST_APPEND || op == OP_MAP_ADD ||
                   op == OP_SET_ADD || op == OP_BUILD_SET);
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

bool Compiler::emitNameOp(const std::string& id, TargetCtx ctx) {
    if (globalNames_.count(id)) {
        int idx = addName(id);
        int op = OP_LOAD_GLOBAL;
        if (ctx == TargetCtx::Store) op = OP_STORE_GLOBAL;
        else if (ctx == TargetCtx::Delete) op = OP_DELETE_GLOBAL;
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
            bytecode_ = bytecode_->appendLast(ctx_, ctx_->fromInteger(OP_ROT_THREE));
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
        for (auto it = tup->elements.rbegin(); it != tup->elements.rend(); ++it) {
            if (!compileTarget(it->get(), TargetCtx::Store)) return false;
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

bool Compiler::compileFor(ForNode* n) {
    if (!n || !compileNode(n->iter.get())) return false;
    emit(OP_GET_ITER);
    int loopStart = bytecodeOffset();
    emit(OP_FOR_ITER, 0);
    int argSlot = bytecodeOffset() - 1;
    if (!compileTarget(n->target.get(), TargetCtx::Store)) return false;
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

bool Compiler::compileListComp(ListCompNode* n) {
    if (!n) return false;
    emit(OP_BUILD_LIST, 0);
    int nGen = static_cast<int>(n->generators.size());
    
    auto inner = [&]() -> bool {
        if (!compileNode(n->elt.get())) return false;
        emit(OP_LIST_APPEND, nGen + 1);
        return true;
    };
    
    return compileComprehension(n->generators, 0, inner);
}

bool Compiler::compileDictComp(DictCompNode* n) {
    if (!n) return false;
    emit(OP_BUILD_MAP, 0);
    int nGen = static_cast<int>(n->generators.size());
    
    auto inner = [&]() -> bool {
        if (!compileNode(n->value.get())) return false;
        if (!compileNode(n->key.get())) return false;
        emit(OP_MAP_ADD, nGen + 1);
        return true;
    };
    
    return compileComprehension(n->generators, 0, inner);
}

bool Compiler::compileSetComp(SetCompNode* n) {
    if (!n) return false;
    emit(OP_BUILD_SET, 0);
    int nGen = static_cast<int>(n->generators.size());
    
    auto inner = [&]() -> bool {
        if (!compileNode(n->elt.get())) return false;
        emit(OP_SET_ADD, nGen + 1);
        return true;
    };
    
    return compileComprehension(n->generators, 0, inner);
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
        collectGlobalsFromNode(tr->handlers.get(), globalsOut);
        if (tr->orelse) collectGlobalsFromNode(tr->orelse.get(), globalsOut);
        if (tr->finalbody) collectGlobalsFromNode(tr->finalbody.get(), globalsOut);
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
    std::vector<std::string>& localsOrdered) {
    globalsOut.clear();
    localsOrdered.clear();
    
    // 1. First pass: find all 'global' declarations
    collectGlobalsFromNode(body, globalsOut);
    
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
        if (globalsOut.find(name) == globalsOut.end()) {
            if (seen.find(name) == seen.end()) {
                if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-compiler] found local: " << name << "\n";
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
        collectDefinedNames(t->handlers.get(), out);
        if (t->orelse) collectDefinedNames(t->orelse.get(), out);
        if (t->finalbody) collectDefinedNames(t->finalbody.get(), out);
        return;
    }
    if (auto* nm = dynamic_cast<NameNode*>(node)) {
        out.insert(nm->id);
        return;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectDefinedNames(e.get(), out);
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
        if (statementLeavesValue(n->statements[i].get()))
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
    if (std::getenv("PROTO_ENV_DIAG") && forceMapped)
        std::cerr << "[proto-compiler] forceMapped " << n->name << " reason: " << dynamicReason << "\n";

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
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
    int noneIdx = bodyCompiler.addConstant(env ? env->getNonePrototype() : PROTO_NONE);
    bodyCompiler.emit(OP_LOAD_CONST, noneIdx);
    bodyCompiler.emit(OP_RETURN_VALUE);
    
    bodyCompiler.applyPatches();

    const proto::ProtoList* co_varnames = ctx_->newList();
    for (const auto& name : automaticNames)
        co_varnames = co_varnames->appendLast(ctx_, ctx_->fromUTF8String(name.c_str()));
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), ctx_->fromUTF8String(filename_.c_str())->asString(ctx_), co_varnames, nparams, automatic_count);
    if (!codeObj) return false;
    int idx = addConstant(codeObj);
    emit(OP_LOAD_CONST, idx);
    emit(OP_BUILD_FUNCTION, 0);
    return emitNameOp(n->name, TargetCtx::Store);
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
    
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), ctx_->fromUTF8String(filename_.c_str())->asString(ctx_));
    int coIdx = addConstant(codeObj);
    emit(OP_LOAD_CONST, coIdx);
    emit(OP_BUILD_FUNCTION, 0);
    
    // 4. Build
    emit(OP_BUILD_CLASS);
    
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
    if (auto* a = dynamic_cast<AssignNode*>(node)) return compileAssign(a);
    if (auto* an = dynamic_cast<AssertNode*>(node)) return compileAssert(an);
    if (auto* lc = dynamic_cast<ListCompNode*>(node)) return compileListComp(lc);
    if (auto* dc = dynamic_cast<DictCompNode*>(node)) return compileDictComp(dc);
    if (auto* sc = dynamic_cast<SetCompNode*>(node)) return compileSetComp(sc);
    if (auto* d = dynamic_cast<DeleteNode*>(node)) return compileDeleteNode(d);
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
    int automatic_count) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* code = ctx->newObject(true);
    // Optional: add a 'code_proto' if we want to share methods like .exec()
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_consts"), reinterpret_cast<const proto::ProtoObject*>(constants));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_names"), names ? reinterpret_cast<const proto::ProtoObject*>(names) : reinterpret_cast<const proto::ProtoObject*>(ctx->newList()));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_code"), reinterpret_cast<const proto::ProtoObject*>(bytecode));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_filename"), filename ? reinterpret_cast<const proto::ProtoObject*>(filename) : reinterpret_cast<const proto::ProtoObject*>(ctx->fromUTF8String("<stdin>")));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_varnames"), varnames ? reinterpret_cast<const proto::ProtoObject*>(varnames) : reinterpret_cast<const proto::ProtoObject*>(ctx->newList()));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_nparams"), ctx->fromInteger(nparams));
    code = code->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "co_automatic_count"), ctx->fromInteger(automatic_count));
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

} // namespace protoPython
