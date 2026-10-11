#include <protoPython/CppGenerator.h>
#include <algorithm>

namespace protoPython {

namespace {

// Helpers every generated file defines (file-local).
//
// Function storage.  Every compiled callable is a method object whose bound
// `self` is an immutable storage object holding `__module_ns__`, the
// namespace of the module that defined it (the current globals at its
// definition), and the values of its free variables.  The callable enters
// that namespace while it runs (protoPython::ModuleNamespaceScope), so its
// module-level names resolve in its own module whoever calls it, as an
// interpreted function's do through __globals__.
//
// Calling conventions.  Called as a function (`f(a)`, or `C.m(obj, a)`, or a
// method call from compiled code, env->callMethod), the callable receives its
// storage as `self` and the arguments in `args`.  Looked up on an instance by
// the interpreter (`obj.m(a)`), it is rebound as a native method: `self` is
// then the receiver and its storage is not available.  __functionStorageOf
// tells the two apart (a storage owns `__module_ns__`), moves a receiver to
// the front of `args`, where the prologue expects it, and the namespace then
// falls back to __moduleNamespace, the namespace of the module this file
// initialised; free variables are not available in that case.
//
// Generator unwinding.  Generated code reports an exception by leaving it
// pending in the PythonEnvironment.  Inside a generator body, a statement
// that leaves an exception pending throws __PyUnwind, which leaves the
// statements that follow it; a `try` statement catches it to run its
// handlers and its `finally` clause, and the generator's continuation
// catches it to finish the generator with the exception pending.
const char* const kGeneratedPreamble = R"PREAMBLE(namespace {
struct __PyUnwind {};

// The namespace of the module proto_module_init initialised.  It stays alive
// while any callable that can fall back to it does: each one's storage
// references it.
const proto::ProtoObject* __moduleNamespace = nullptr;

// Interned strings are perennial: the key is looked up once.
const proto::ProtoString* __moduleNsKeyCache = nullptr;

const proto::ProtoString* __moduleNsKey(proto::ProtoContext* ctx) {
    if (!__moduleNsKeyCache) __moduleNsKeyCache = protoPython::PythonEnvironment::getInternedString(ctx, "__module_ns__");
    return __moduleNsKeyCache;
}

bool __isFunctionStorage(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    return self && self->hasOwnAttribute(ctx, __moduleNsKey(ctx)) == PROTO_TRUE;
}

const proto::ProtoList* __withReceiver(proto::ProtoContext* ctx, const proto::ProtoObject* receiver, const proto::ProtoList* args) {
    const proto::ProtoList* all = ctx->newList()->appendLast(ctx, receiver);
    const proto::proto_ulong n = args ? args->getSize(ctx) : 0;
    for (proto::proto_ulong i = 0; i < n; ++i) all = all->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
    return all;
}

const proto::ProtoObject* __functionStorageOf(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ProtoList*& args) {
    if (!self) return nullptr;
    if (__isFunctionStorage(ctx, self)) return self;
    args = __withReceiver(ctx, self, args);
    return nullptr;
}

const proto::ProtoObject* __moduleNamespaceOf(proto::ProtoContext* ctx, const proto::ProtoObject* fs) {
    const proto::ProtoObject* ns = fs ? fs->getAttribute(ctx, __moduleNsKey(ctx)) : nullptr;
    return (ns && ns != PROTO_NONE) ? ns : __moduleNamespace;
}

const proto::ProtoObject* __newFunctionStorage(proto::ProtoContext* ctx) {
    const proto::ProtoObject* ns = protoPython::PythonEnvironment::getCurrentGlobals();
    return ctx->newObject(false)->setAttribute(ctx, __moduleNsKey(ctx), ns ? ns : PROTO_NONE);
}

const proto::ProtoObject* __withFreeVar(proto::ProtoContext* ctx, const proto::ProtoObject* fs, const char* name, const proto::ProtoObject* value) {
    return fs->setAttribute(ctx, protoPython::PythonEnvironment::getInternedString(ctx, name), value ? value : PROTO_NONE);
}

const proto::ProtoObject* __freeVar(proto::ProtoContext* ctx, const proto::ProtoObject* fs, const char* name) {
    const proto::ProtoObject* v = fs ? fs->getAttribute(ctx, protoPython::PythonEnvironment::getInternedString(ctx, name)) : nullptr;
    return v ? v : PROTO_NONE;
}
} // namespace

)PREAMBLE";

} // namespace

bool CppGenerator::generate(ModuleNode* module, const std::string& filename) {
    header_.str("");
    header_.clear();
    body_.str("");
    body_.clear();

    // 1. Module initialisation.  The statements are generated first: the
    // slot count the module's context declares is known only afterwards.
    const SlotFrame moduleSlots = enterSlotFrame(0);
    std::ostringstream moduleBody;
    out_ = &moduleBody;
    for (auto& stmt : module->body) {
        if (stmt->line > 0) emitLineDirective(stmt->line, filename);
        beginStatement();
        if (!generateNode(stmt.get())) return false;
        *out_ << ";\n";
    }
    out_ = &body_;
    // The module runs in a context of its own, as a function does, so the
    // safepoints of its loops submit only what it allocated, never the
    // importer's young generation (see enterSlotFrame).
    *out_ << "extern \"C\" void* proto_module_init() {\n";
    *out_ << "    auto* __callerCtx = protoPython::PythonEnvironment::getCurrentContext();\n";
    *out_ << openContextScope("__callerCtx");
    *out_ << "    auto* env = protoPython::PythonEnvironment::get(ctx);\n";
    *out_ << "    __moduleNamespace = protoPython::PythonEnvironment::getCurrentGlobals();\n";
    *out_ << "    (void)__moduleNsKey(ctx);\n";
    *out_ << "    if (protoPython::diagEnabled()) {\n";
    *out_ << "        fprintf(stderr, \"[mod] proto_module_init ctx=%p\\n\", (void*)ctx);\n";
    *out_ << "        const proto::ProtoObject* p = env->lookupName(\"print\");\n";
    *out_ << "        fprintf(stderr, \"[mod] lookupName('print')=%p\\n\", (void*)p);\n";
    *out_ << "        fflush(stderr);\n";
    *out_ << "    }\n\n";
    *out_ << moduleBody.str();
    leaveSlotFrame(moduleSlots);
    *out_ << "    return nullptr;\n";
    *out_ << "}\n";

    // 2. Final assembly
    finalOut_ << "// Generated by protopyc from " << filename << "\n";
    finalOut_ << "#include <thread>\n";
    finalOut_ << "#include <protoCore.h>\n";
    finalOut_ << "#include <protoPython/PythonEnvironment.h>\n";
    // protoPython::ContextScope and promote: each compiled callable runs in
    // its own context.
    finalOut_ << "#include <protoPython/MemoryManager.hpp>\n";
    finalOut_ << "#include <protoPython/Tokenizer.h>\n";
    // Fixed-width integers and portable overflow checks: `long` is 32 bits on
    // Windows, and __builtin_mul_overflow is GCC/Clang only.
    finalOut_ << "#include <protoPython/CheckedArith.h>\n";
    finalOut_ << "#include <cstdint>\n";
    finalOut_ << "#include <algorithm>\n\n";
    finalOut_ << kGeneratedPreamble;

    finalOut_ << header_.str();
    finalOut_ << body_.str();
    
    return true;
}

void CppGenerator::emitLineDirective(int line, const std::string& filename) {
    *out_ << "#line " << line << " \"" << filename << "\"\n";
}

// ---------------------------------------------------------------------------
// GC rooting of generated code (protoPython issue #2; protoCore
// docs/EMBEDDER-CONFORMANCE.md rule 3).
//
// protoCore protects a cell held only in a C++ variable while the cell is in
// its context's young generation; ProtoContext::safepoint() hands that
// generation to the collector, and from then on only the roots (automatic
// slots, the mutables table, pinned structures, the young chains not yet
// submitted) keep a cell alive.  Generated code therefore runs each callable
// in its own context and keeps what it still uses in that context's slots.
// The layout is described in CppGenerator.h.
// ---------------------------------------------------------------------------

CppGenerator::SlotFrame CppGenerator::enterSlotFrame(int reserved) {
    SlotFrame saved{anchorTop_, tempTop_, slotHigh_};
    anchorTop_ = reserved;
    tempTop_ = reserved;
    slotHigh_ = reserved;
    return saved;
}

void CppGenerator::leaveSlotFrame(const SlotFrame& saved) {
    anchorTop_ = saved.anchorTop;
    tempTop_ = saved.tempTop;
    slotHigh_ = saved.high;
}

int CppGenerator::allocTempSlot() {
    const int slot = tempTop_++;
    slotHigh_ = std::max(slotHigh_, tempTop_);
    return slot;
}

int CppGenerator::pushAnchor() {
    // Anchors are taken where no temporary of the current statement is
    // still needed (a statement's start, or after the body it guards), so
    // the slot may be one a finished temporary used.
    const int slot = anchorTop_++;
    tempTop_ = anchorTop_;
    slotHigh_ = std::max(slotHigh_, anchorTop_);
    return slot;
}

void CppGenerator::popAnchor() {
    --anchorTop_;
    tempTop_ = anchorTop_;
}

std::string CppGenerator::openContextScope(const std::string& callerCtx) const {
    // The context is chained to the thread's current context (chainParent),
    // so the collector's root scan reaches its slots; the slot array lives
    // on the C++ stack for up to ContextScope::SBO_SLOTS slots.
    std::ostringstream o;
    const int nSlots = std::max(slotHigh_, 1);
    o << "    protoPython::ContextScope __scope(" << callerCtx << "->space, " << callerCtx
      << ", nullptr, nullptr, nullptr, nullptr, " << nSlots << ");\n";
    o << "    proto::ProtoContext* ctx = __scope.context();\n";
    o << "    const proto::ProtoObject** __slot = ctx->getAutomaticLocals();\n";
    o << "    constexpr unsigned __nslots = " << nSlots << ";\n";
    o << "    (void)__slot; (void)__nslots;\n";
    return o.str();
}

void CppGenerator::emitLoopSafepoint() {
    // No temporary is live at a statement-level loop head: drop the values
    // the previous iteration left in the temporary slots, so they are not
    // retained, then submit this context's young generation.  Locals and
    // anchors are in slots, so everything still in use is reachable.
    *out_ << "    std::fill(__slot + " << anchorTop_ << ", __slot + __nslots, PROTO_NONE);\n";
    *out_ << "    ctx->safepoint();\n";
}

bool CppGenerator::needsTempSlot(ASTNode* node) const {
    // Constants are tagged values or fresh cells of this context's young
    // generation, and a function local is its own slot.  A yield or await
    // lowers to statements, not to an expression.
    if (dynamic_cast<ConstantNode*>(node)) return false;
    if (auto* nm = dynamic_cast<NameNode*>(node)) {
        return std::find(orderedLocalVars_.begin(), orderedLocalVars_.end(), nm->id)
               == orderedLocalVars_.end();
    }
    return dynamic_cast<CallNode*>(node) || dynamic_cast<BinOpNode*>(node) ||
           dynamic_cast<UnaryOpNode*>(node) || dynamic_cast<AttributeNode*>(node) ||
           dynamic_cast<SubscriptNode*>(node) || dynamic_cast<ListLiteralNode*>(node) ||
           dynamic_cast<DictLiteralNode*>(node) || dynamic_cast<TupleLiteralNode*>(node) ||
           dynamic_cast<SetLiteralNode*>(node) || dynamic_cast<NamedExprNode*>(node) ||
           dynamic_cast<ConditionalExprNode*>(node) || dynamic_cast<ListCompNode*>(node) ||
           dynamic_cast<DictCompNode*>(node) || dynamic_cast<SetCompNode*>(node) ||
           dynamic_cast<GeneratorExpNode*>(node) || dynamic_cast<JoinedStrNode*>(node) ||
           dynamic_cast<SliceNode*>(node) || dynamic_cast<LambdaNode*>(node);
}

bool CppGenerator::generateNode(ASTNode* node) {
    if (!needsTempSlot(node)) return generateNodeUnrooted(node);
    // Store the value in a temporary slot as it is produced.  The slot is
    // taken before the children's, which therefore get higher ones.
    const int slot = allocTempSlot();
    *out_ << "(__slot[" << slot << "] = (";
    if (!generateNodeUnrooted(node)) return false;
    *out_ << "))";
    return true;
}

bool CppGenerator::generateNodeUnrooted(ASTNode* node) {
    if (auto* n = dynamic_cast<ConstantNode*>(node)) {
        return generateConstant(n);
    } else if (auto* n = dynamic_cast<NameNode*>(node)) {
        return generateName(n);
    } else if (auto* n = dynamic_cast<CallNode*>(node)) {
        return generateCall(n);
    } else if (auto* n = dynamic_cast<SuiteNode*>(node)) {
        if (inStateMachine_) {
            std::vector<ASTNode*> stmts;
            for (auto& stmt : n->statements) stmts.push_back(stmt.get());
            return emitStatementList(stmts);
        }
        for (auto& stmt : n->statements) {
            if (stmt->line > 0) emitLineDirective(stmt->line, "source.py"); // TODO
            beginStatement();
            if (!generateNode(stmt.get())) return false;
            *out_ << ";\n";
        }
        return true;
    } else if (auto* n = dynamic_cast<AssignNode*>(node)) {
        return generateAssign(n);
    } else if (auto* n = dynamic_cast<IfNode*>(node)) {
        return generateIf(n);
    } else if (auto* n = dynamic_cast<BinOpNode*>(node)) {
        return generateBinOp(n);
    } else if (auto* n = dynamic_cast<PassNode*>(node)) {
        return generatePass(n);
    } else if (auto* n = dynamic_cast<BreakNode*>(node)) {
        return generateBreak(n);
    } else if (auto* n = dynamic_cast<ContinueNode*>(node)) {
        return generateContinue(n);
    } else if (auto* n = dynamic_cast<FunctionDefNode*>(node)) {
        return generateFunctionDef(n);
    } else if (auto* n = dynamic_cast<ReturnNode*>(node)) {
        return generateReturn(n);
    } else if (auto* n = dynamic_cast<AttributeNode*>(node)) {
        return generateAttribute(n);
    } else if (auto* n = dynamic_cast<SubscriptNode*>(node)) {
        return generateSubscript(n);
    } else if (auto* n = dynamic_cast<ListLiteralNode*>(node)) {
        return generateListLiteral(n);
    } else if (auto* n = dynamic_cast<DictLiteralNode*>(node)) {
        return generateDictLiteral(n);
    } else if (auto* n = dynamic_cast<TupleLiteralNode*>(node)) {
        return generateTupleLiteral(n);
    } else if (auto* n = dynamic_cast<WhileNode*>(node)) {
        return generateWhile(n);
    } else if (auto* n = dynamic_cast<UnaryOpNode*>(node)) {
        return generateUnaryOp(n);
    } else if (auto* n = dynamic_cast<AugAssignNode*>(node)) {
        return generateAugAssign(n);
    } else if (auto* n = dynamic_cast<ForNode*>(node)) {
        return generateFor(n);
    } else if (auto* n = dynamic_cast<TryNode*>(node)) {
        return generateTry(n);
    } else if (auto* n = dynamic_cast<RaiseNode*>(node)) {
        return generateRaise(n);
    } else if (auto* n = dynamic_cast<WithNode*>(node)) {
        return generateWith(n);
    } else if (auto* n = dynamic_cast<ImportNode*>(node)) {
        return generateImport(n);
    } else if (auto* n = dynamic_cast<ImportFromNode*>(node)) {
        return generateImportFrom(n);
    } else if (auto* n = dynamic_cast<ClassDefNode*>(node)) {
        return generateClassDef(n);
    } else if (auto* n = dynamic_cast<LambdaNode*>(node)) {
        return generateLambda(n);
    } else if (auto* n = dynamic_cast<JoinedStrNode*>(node)) {
        return generateJoinedStr(n);
    } else if (auto* n = dynamic_cast<FormattedValueNode*>(node)) {
        return generateFormattedValue(n);
    } else if (auto* n = dynamic_cast<SliceNode*>(node)) {
        return generateSlice(n);
    } else if (auto* n = dynamic_cast<DeleteNode*>(node)) {
        return generateDeleteNode(n);
    } else if (auto* n = dynamic_cast<AsyncFunctionDefNode*>(node)) {
        return generateAsyncFunctionDef(n);
    } else if (auto* n = dynamic_cast<AssertNode*>(node)) {
        return generateAssert(n);
    } else if (auto* n = dynamic_cast<YieldNode*>(node)) {
        return generateYield(n);
    } else if (auto* n = dynamic_cast<AwaitNode*>(node)) {
        return generateAwait(n);
    } else if (auto* n = dynamic_cast<StarredNode*>(node)) {
        return generateStarred(n);
    } else if (auto* n = dynamic_cast<AnnAssignNode*>(node)) {
        return generateAnnAssign(n);
    } else if (auto* n = dynamic_cast<NamedExprNode*>(node)) {
        return generateNamedExpr(n);
    } else if (auto* n = dynamic_cast<SetLiteralNode*>(node)) {
        return generateSetLiteral(n);
    } else if (auto* n = dynamic_cast<ListCompNode*>(node)) {
        return generateListComp(n);
    } else if (auto* n = dynamic_cast<DictCompNode*>(node)) {
        return generateDictComp(n);
    } else if (auto* n = dynamic_cast<SetCompNode*>(node)) {
        return generateSetComp(n);
    } else if (auto* n = dynamic_cast<GeneratorExpNode*>(node)) {
        return generateGeneratorExp(n);
    } else if (auto* n = dynamic_cast<ConditionalExprNode*>(node)) {
        return generateConditionalExpr(n);
    }

    *out_ << "/* Unsupported node: " << typeid(*node).name() << " */";
    return true;
}

bool CppGenerator::generateConditionalExpr(ConditionalExprNode* n) {
    // Python `body if test else orelse`.  C++ ternary short-circuits the
    // same way Python does (only one of body/orelse is evaluated), so the
    // straight lowering works as long as test goes through env->isTrue
    // to honour Python truthiness on arbitrary objects.
    *out_ << "(env->isTrue(";
    if (!generateNode(n->test.get())) return false;
    *out_ << ") ? (";
    if (!generateNode(n->body.get())) return false;
    *out_ << ") : (";
    if (!generateNode(n->orelse.get())) return false;
    *out_ << "))";
    return true;
}

static std::string escapeCppString(const std::string& s) {
    // Escape a Python string for emission inside a C++ "..." literal.
    // Without this docstrings containing newlines or stray quotes would
    // close the C++ string literal early and break compilation; non-ASCII
    // UTF-8 bytes are passed through unchanged (the C++ compiler is
    // configured for UTF-8 source).
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\0': out += "\\0";  break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

bool CppGenerator::generateConstant(ConstantNode* n) {
    switch (n->constType) {
        case ConstantNode::ConstType::Int:
            *out_ << "ctx->fromInteger(" << n->intVal << ")";
            break;
        case ConstantNode::ConstType::Float:
            *out_ << "ctx->fromDouble(" << n->floatVal << ")";
            break;
        case ConstantNode::ConstType::Str:
            *out_ << "ctx->fromUTF8String(\"" << escapeCppString(n->strVal) << "\")";
            break;
        case ConstantNode::ConstType::Bool:
            *out_ << (n->intVal ? "PROTO_TRUE" : "PROTO_FALSE");
            break;
        case ConstantNode::ConstType::None:
            *out_ << "PROTO_NONE";
            break;
    }
    return true;
}

bool CppGenerator::generateName(NameNode* n) {
    // Use the function-local C++ variable for any name that
    // generateFunctionInternal declared as `local_<id>`.  Previously
    // this gating was tied to `inStateMachine_` (generators only),
    // so `def f(x, y): return x + y` lowered to
    //   throw env->binaryOp(env->lookupName("x"), Plus, env->lookupName("y"))
    // — and lookupName had no way to reach a function-local binding
    // because `storeName` lived on the module frame.  Result: every
    // function returned PROTO_NONE / raised NameError on first
    // argument reference.  Recognising `orderedLocalVars_` regardless
    // of state-machine mode fixes both regular and generator
    // functions uniformly.
    auto it = std::find(orderedLocalVars_.begin(), orderedLocalVars_.end(), n->id);
    if (it != orderedLocalVars_.end()) {
        *out_ << "local_" << n->id;
        return true;
    }
    *out_ << "env->lookupName(\"" << n->id << "\")";
    return true;
}

bool CppGenerator::generateCall(CallNode* n) {
    bool hasUnpacking = false;
    for (auto& a : n->args) if (dynamic_cast<StarredNode*>(a.get())) { hasUnpacking = true; break; }
    if (!hasUnpacking) {
        for (auto& kw : n->keywords) if (kw.first.empty()) { hasUnpacking = true; break; }
    }

    if (!hasUnpacking) {
        if (n->keywords.empty()) {
            // For `obj.attr(args)` route through env->callMethod, which uses
            // LOAD_METHOD-style attribute resolution and prepends `self` to
            // args when the resolved attribute is a raw function on the
            // class chain — matching the prologue protopyc emits for every
            // generated function (`local_self = args->getAt(0)`).
            if (auto* att = dynamic_cast<AttributeNode*>(n->func.get())) {
                *out_ << "env->callMethod(";
                if (!generateNode(att->value.get())) return false;
                *out_ << ", \"" << att->attr << "\", {";
                for (size_t i = 0; i < n->args.size(); ++i) {
                    if (i > 0) *out_ << ", ";
                    if (!generateNode(n->args[i].get())) return false;
                }
                *out_ << "})";
                return true;
            }
            // Self-recursion fast path: when the call target is the bare
            // name of the function currently being emitted, skip the whole
            // env->lookupName → env->callObject → invokeCallable polymorphic
            // chain and call the C++ symbol directly.  We forward our own
            // `self` (closure storage) so nested closures keep working.
            // Assumes the function name has not been rebound by the body —
            // breaking that assumption would require a runtime guard, which
            // costs more than the optimisation buys on tight recursion.
            //
            // Build the args list through protoCore's bulk-construct
            // factory: ctx->newList(n, items) returns a single inline-
            // storage cell when n ≤ 5, replacing 1 + N appendLast
            // allocations.  fib(25) with two args per frame goes from
            // 3 cells per call to 1.
            if (auto* nm = dynamic_cast<NameNode*>(n->func.get())) {
                if (!currentFuncName_.empty() && nm->id == currentFuncName_) {
                    *out_ << "([&]() -> const proto::ProtoObject* {\n";
                    *out_ << "        const proto::ProtoObject* __argv[] = {";
                    for (size_t i = 0; i < n->args.size(); ++i) {
                        if (i > 0) *out_ << ", ";
                        if (!generateNode(n->args[i].get())) return false;
                    }
                    // Zero-arg case: emit a single nullptr placeholder
                    // so the array is non-empty; newList(0, items) ignores
                    // items anyway, but C++ forbids zero-size arrays.
                    if (n->args.empty()) *out_ << "nullptr";
                    *out_ << "};\n";
                    *out_ << "        auto* __args = ctx->newList(" << n->args.size()
                          << "u, __argv);\n";
                    *out_ << "        return " << currentFuncCppName_
                          << "_body(ctx, __fs, nullptr, __args);\n";
                    *out_ << "    })()";
                    return true;
                }
            }
            *out_ << "env->callObject(";
            if (!generateNode(n->func.get())) return false;
            *out_ << ", {";
            for (size_t i = 0; i < n->args.size(); ++i) {
                if (i > 0) *out_ << ", ";
                if (!generateNode(n->args[i].get())) return false;
            }
            *out_ << "})";
        } else {
            *out_ << "env->callObjectEx(";
            if (!generateNode(n->func.get())) return false;
            *out_ << ", {";
            for (size_t i = 0; i < n->args.size(); ++i) {
                if (i > 0) *out_ << ", ";
                if (!generateNode(n->args[i].get())) return false;
            }
            *out_ << "}, {";
            for (size_t i = 0; i < n->keywords.size(); ++i) {
                if (i > 0) *out_ << ", ";
                *out_ << "{\"" << n->keywords[i].first << "\", ";
                if (!generateNode(n->keywords[i].second.get())) return false;
                *out_ << "}";
            }
            *out_ << "})";
        }
    } else {
        // PEP 448 multiple unpacking. 
        // Build them using helpers or blocks.
        *out_ << "([&]() {\n";
        *out_ << "        auto* __f = "; if (!generateNode(n->func.get())) return false; *out_ << ";\n";
        *out_ << "        auto* __alst = ctx->newList();\n";
        for (auto& a : n->args) {
            if (auto* s = dynamic_cast<StarredNode*>(a.get())) {
                *out_ << "        __alst = ctx->listExtend(ctx, __alst, (const proto::ProtoList*)env->getAttr("; 
                if (!generateNode(s->value.get())) return false;
                *out_ << ", env->getDataString()));\n";
            } else {
                *out_ << "        __alst = __alst->appendLast(ctx, ";
                if (!generateNode(a.get())) return false;
                *out_ << ");\n";
            }
        }
        *out_ << "        bool __hasKw = false;\n";
        *out_ << "        auto* __kmap = ctx->newSparseList();\n";
        for (auto& kw : n->keywords) {
            *out_ << "        __hasKw = true;\n";
            if (kw.first.empty()) {
                *out_ << "        __kmap = ctx->dictUpdate(ctx, __kmap, (const proto::ProtoSparseList*)env->getAttr(";
                if (!generateNode(kw.second.get())) return false;
                *out_ << ", env->getDataString()));\n";
            } else {
                *out_ << "        __kmap = __kmap->setAt(ctx, ctx->fromUTF8String(\"" << kw.first << "\")->getHash(ctx), ";
                if (!generateNode(kw.second.get())) return false;
                *out_ << ");\n";
            }
        }
        *out_ << "        return env->callObjectEx(__f, {}, {}, ctx->newTupleFromList(__alst)->asObject(ctx), __hasKw ? __kmap->asObject(ctx) : nullptr);\n";
        *out_ << "    })()";
    }
    return true;
}

bool CppGenerator::generateStarred(StarredNode* n) {
    return generateNode(n->value.get());
}

bool CppGenerator::generateAnnAssign(AnnAssignNode* n) {
    if (n->value) {
        std::ostringstream valStream;
        std::ostream* oldOut = out_;
        out_ = &valStream;
        if (!generateNode(n->value.get())) return false;
        out_ = oldOut;
        return generateAssignToTarget(n->target.get(), valStream.str());
    }
    return true;
}

bool CppGenerator::generateAssignToTarget(ASTNode* target, const std::string& valueExpr) {
    if (auto* nameNode = dynamic_cast<NameNode*>(target)) {
        // Same rule as generateName: use the C++ local for any name
        // declared as such by generateFunctionInternal, regardless of
        // state-machine vs regular-function mode.  Module-level
        // bindings (orderedLocalVars_ empty) still route through
        // env->storeName so they hit the module's globals.
        emitAssignName(nameNode->id, valueExpr);
        return true;
    } else if (auto* attrNode = dynamic_cast<AttributeNode*>(target)) {
        *out_ << "env->setAttr(";
        if (!generateNode(attrNode->value.get())) return false;
        *out_ << ", \"" << attrNode->attr << "\", " << valueExpr << ")";
        return true;
    } else if (auto* subNode = dynamic_cast<SubscriptNode*>(target)) {
        *out_ << "env->setItem(";
        if (!generateNode(subNode->value.get())) return false;
        *out_ << ", ";
        if (!generateNode(subNode->index.get())) return false;
        *out_ << ", " << valueExpr << ")";
        return true;
    }
    *out_ << "/* Unsupported assignment target */";
    return true;
}

void CppGenerator::emitAssignName(const std::string& name, const std::string& valueExpr) {
    // A function local is its slot; any other binding goes through
    // env->storeName to the current frame: the module namespace, or the
    // class namespace while a class body runs.
    const bool isLocal = !inClassBody_ &&
        std::find(orderedLocalVars_.begin(), orderedLocalVars_.end(), name) != orderedLocalVars_.end();
    if (isLocal) {
        *out_ << "local_" << name << " = " << valueExpr;
    } else {
        *out_ << "env->storeName(\"" << name << "\", " << valueExpr << ")";
    }
}

bool CppGenerator::generateAssign(AssignNode* n) {
    if (n->targets.empty()) return true;

    if (n->targets.size() == 1) {
        std::ostringstream valStream;
        std::ostream* oldOut = out_;
        out_ = &valStream;
        if (!generateNode(n->value.get())) return false;
        out_ = oldOut;
        return generateAssignToTarget(n->targets[0].get(), valStream.str());
    } else {
        *out_ << "{\n";
        *out_ << "const proto::ProtoObject* _gen_tmp = ";
        if (!generateNode(n->value.get())) return false;
        *out_ << ";\n";
        for (auto& t : n->targets) {
            if (!generateAssignToTarget(t.get(), "_gen_tmp")) return false;
            *out_ << ";\n";
        }
        *out_ << "}\n";
        return true;
    }
}

bool CppGenerator::generateIf(IfNode* n) {
    // In a generator, a resumption re-enters the branch that holds the
    // suspended yield without evaluating the test again.
    const int base = stateCount_;
    const int nBody = inStateMachine_ ? countYields(n->body.get()) : 0;
    const int nElse = inStateMachine_ ? countYields(n->orelse.get()) : 0;
    if (nBody + nElse > 0) {
        *out_ << "if (__resume != 0 ? " << resumeIn(base + 1, nBody) << " : env->isTrue(";
    } else {
        *out_ << "if (env->isTrue(";
    }
    if (!generateNode(n->test.get())) return false;
    *out_ << ")) {\n";
    if (!generateBody(n->body.get())) return false;
    *out_ << ";\n}";
    if (n->orelse) {
        *out_ << " else {\n";
        if (!generateBody(n->orelse.get())) return false;
        *out_ << ";\n}";
    }
    return true;
}

bool CppGenerator::generateWhile(WhileNode* n) {
    // Cooperative GC safepoint at the top of each iteration.  The
    // interpreter polls ctx->safepoint() every 64 opcodes inside the
    // bytecode dispatch loop; without this, a CPU-bound compiled thread
    // that never allocates will keep running indefinitely and the GC
    // thread (and every other thread parked waiting for STW) stalls.
    // The check is a single relaxed atomic load on the fast path.
    //
    // In a generator, a resumption inside the body enters the loop without
    // evaluating the test; one inside the else clause goes straight to it.
    const int base = stateCount_;
    const int nBody = inStateMachine_ ? countYields(n->body.get()) : 0;
    const int nElse = inStateMachine_ ? countYields(n->orelse.get()) : 0;
    const bool resumable = nBody + nElse > 0;
    if (n->orelse) {
        // The else clause runs after the loop, outside it: a `break` or
        // `continue` there acts on the enclosing loop, so it is emitted after
        // the C++ loop, guarded by a flag set when the condition turns false
        // (not by a `break` of this loop).
        static int whileElseId = 0;
        const int id = whileElseId++;
        *out_ << "{\n";
        *out_ << "bool __while_done_" << id << " = false;\n";
        *out_ << "while (true) {\n";
        emitLoopSafepoint();
        if (resumable) {
            *out_ << "    if (__resume != 0 ? " << resumeIn(base + nBody + 1, nElse) << " : !env->isTrue(";
        } else {
            *out_ << "    if (!env->isTrue(";
        }
        if (!generateNode(n->test.get())) return false;
        *out_ << ")) {\n";
        *out_ << "        __while_done_" << id << " = true;\n";
        *out_ << "        break;\n";
        *out_ << "    }\n";
        if (!generateBody(n->body.get())) return false;
        *out_ << ";\n}\n";
        *out_ << "if (__while_done_" << id << ") {\n";
        if (!generateBody(n->orelse.get())) return false;
        *out_ << ";\n}\n";
        *out_ << "}\n";
    } else {
        *out_ << (resumable ? "while (__resume != 0 || env->isTrue(" : "while (env->isTrue(");
        if (!generateNode(n->test.get())) return false;
        *out_ << ")) {\n";
        emitLoopSafepoint();
        if (!generateBody(n->body.get())) return false;
        *out_ << ";\n}\n";
    }
    return true;
}

bool CppGenerator::generateUnaryOp(UnaryOpNode* n) {
    *out_ << "env->unaryOp(";
    switch (n->op) {
        case TokenType::Plus: *out_ << "protoPython::TokenType::Plus"; break;
        case TokenType::Minus: *out_ << "protoPython::TokenType::Minus"; break;
        case TokenType::Not: *out_ << "protoPython::TokenType::Not"; break;
        case TokenType::Tilde: *out_ << "protoPython::TokenType::Tilde"; break;
        default: *out_ << "protoPython::TokenType::Plus /* Unsupported uop */"; break;
    }
    *out_ << ", ";
    if (!generateNode(n->operand.get())) return false;
    *out_ << ")";
    return true;
}

bool CppGenerator::generateBinOp(BinOpNode* n) {
    // SmallInt inline fast path for the operations that dominate tight
    // numeric loops (arithmetic + comparison).  If both operands are
    // protoCore tagged SmallIntegers at runtime, the result is computed
    // inline with two getAt-style reads and one C++ arithmetic op,
    // skipping the full env->binaryOp dispatch (which routes through
    // typed __add__ / __mul__ / __cmp__ lookups and method calls per
    // call).  Anything else (LargeInteger, str, list, mixed types, …)
    // falls back to env->binaryOp so semantics stay identical.
    //
    // 64-bit C++ overflow is impossible for the additive ops here:
    // protoCore SmallInts hold a 56-bit signed payload, so the sum or
    // difference of two of them fits in std::int64_t. A product can need
    // ~112 bits and goes through protoPython::checkedMul64 (CheckedArith.h,
    // portable to MSVC). The emitted code never uses `long`, which is 32
    // bits on Windows.  ctx->fromInteger transparently promotes back to a
    // LargeInteger when the result steps outside the SmallInt range.
    const char* arithOp = nullptr;
    const char* cmpOp = nullptr;
    bool needsOverflowGuard = false;
    // Identity ops: `is` / `is not` lower to pointer equality.  Tagged
    // primitives (SmallInt, embedded values) compare by tagged-pointer
    // identity too, which matches Python's `is` semantics for small ints
    // and interned strings via protoCore's intern table.  Emitted directly
    // here as a C++ pointer compare so the dispatch never reaches
    // env->binaryOp at all.
    bool emitIs = false;
    bool emitIsNot = false;
    switch (n->op) {
        case TokenType::Plus:         arithOp = "+";  break;
        case TokenType::Minus:        arithOp = "-";  break;
        case TokenType::Star:         arithOp = "*";  needsOverflowGuard = true; break;
        case TokenType::EqEqual:      cmpOp   = "=="; break;
        case TokenType::NotEqual:     cmpOp   = "!="; break;
        case TokenType::Less:         cmpOp   = "<";  break;
        case TokenType::LessEqual:    cmpOp   = "<="; break;
        case TokenType::Greater:      cmpOp   = ">";  break;
        case TokenType::GreaterEqual: cmpOp   = ">="; break;
        case TokenType::Is:           emitIs    = true; break;
        case TokenType::IsNot:        emitIsNot = true; break;
        default: break;
    }

    if (emitIs || emitIsNot) {
        *out_ << "(((";
        if (!generateNode(n->left.get())) return false;
        *out_ << ") " << (emitIs ? "==" : "!=") << " (";
        if (!generateNode(n->right.get())) return false;
        *out_ << ")) ? PROTO_TRUE : PROTO_FALSE)";
        return true;
    }

    // Short-circuit boolean operators.  The parser puts `and` / `or`
    // into a BinOpNode (rather than a separate BoolOpNode), so the
    // switch above leaves arithOp / cmpOp null for them.  Without this
    // branch the generator fell through to env->binaryOp with a
    // synthesised `Plus` token (`/* Unsupported op */`), so
    // `x and y` was lowered to `x + y` — `True and 0` evaluated as
    // `True + 0 = 1` → True.  multithread_cpu's spin-wait
    // `_done[0] != 0 and _done[1] != 0` therefore broke on a partial
    // result, which masqueraded as a setItem race; the real fix is
    // here, not in the runtime.
    if (n->op == TokenType::And || n->op == TokenType::Or) {
        const bool isAnd = (n->op == TokenType::And);
        *out_ << "([&]() -> const proto::ProtoObject* {\n";
        *out_ << "        const proto::ProtoObject* __a = ";
        if (!generateNode(n->left.get())) return false;
        *out_ << ";\n";
        // `and`: short-circuit on falsy left → return left.
        // `or` : short-circuit on truthy left → return left.
        // Otherwise evaluate and return the right-hand side.
        *out_ << "        if (" << (isAnd ? "!" : "")
              << "env->isTrue(__a)) return __a;\n";
        *out_ << "        return ";
        if (!generateNode(n->right.get())) return false;
        *out_ << ";\n";
        *out_ << "    })()";
        return true;
    }

    auto emitFallback = [&]() -> bool {
        *out_ << "env->binaryOp(__a, ";
        switch (n->op) {
            case TokenType::Plus:         *out_ << "protoPython::TokenType::Plus"; break;
            case TokenType::Minus:        *out_ << "protoPython::TokenType::Minus"; break;
            case TokenType::Star:         *out_ << "protoPython::TokenType::Star"; break;
            case TokenType::EqEqual:      *out_ << "protoPython::TokenType::EqEqual"; break;
            case TokenType::NotEqual:     *out_ << "protoPython::TokenType::NotEqual"; break;
            case TokenType::Less:         *out_ << "protoPython::TokenType::Less"; break;
            case TokenType::LessEqual:    *out_ << "protoPython::TokenType::LessEqual"; break;
            case TokenType::Greater:      *out_ << "protoPython::TokenType::Greater"; break;
            case TokenType::GreaterEqual: *out_ << "protoPython::TokenType::GreaterEqual"; break;
            default: break;
        }
        *out_ << ", __b)";
        return true;
    };

    if (arithOp) {
        *out_ << "([&]() -> const proto::ProtoObject* {\n";
        *out_ << "        const proto::ProtoObject* __a = ";
        if (!generateNode(n->left.get())) return false;
        *out_ << ";\n";
        *out_ << "        const proto::ProtoObject* __b = ";
        if (!generateNode(n->right.get())) return false;
        *out_ << ";\n";
        *out_ << "        if (__a && __b && proto::isSmallInt(__a) && proto::isSmallInt(__b)) {\n";
        *out_ << "            std::int64_t __va = proto::asSmallInt(__a);\n";
        *out_ << "            std::int64_t __vb = proto::asSmallInt(__b);\n";
        if (needsOverflowGuard) {
            *out_ << "            std::int64_t __vr;\n";
            *out_ << "            if (!protoPython::checkedMul64(__va, __vb, &__vr)) return ctx->fromInteger(__vr);\n";
        } else {
            *out_ << "            return ctx->fromInteger(__va " << arithOp << " __vb);\n";
        }
        *out_ << "        }\n";
        *out_ << "        return ";
        emitFallback();
        *out_ << ";\n";
        *out_ << "    })()";
        return true;
    }
    if (cmpOp) {
        *out_ << "([&]() -> const proto::ProtoObject* {\n";
        *out_ << "        const proto::ProtoObject* __a = ";
        if (!generateNode(n->left.get())) return false;
        *out_ << ";\n";
        *out_ << "        const proto::ProtoObject* __b = ";
        if (!generateNode(n->right.get())) return false;
        *out_ << ";\n";
        *out_ << "        if (__a && __b && proto::isSmallInt(__a) && proto::isSmallInt(__b)) {\n";
        *out_ << "            return (proto::asSmallInt(__a) " << cmpOp << " proto::asSmallInt(__b)) ? PROTO_TRUE : PROTO_FALSE;\n";
        *out_ << "        }\n";
        *out_ << "        return ";
        emitFallback();
        *out_ << ";\n";
        *out_ << "    })()";
        return true;
    }

    // No SmallInt specialisation for this op — emit the generic dispatch.
    *out_ << "env->binaryOp(";
    if (!generateNode(n->left.get())) return false;
    *out_ << ", ";
    switch (n->op) {
        case TokenType::Plus: *out_ << "protoPython::TokenType::Plus"; break;
        case TokenType::Minus: *out_ << "protoPython::TokenType::Minus"; break;
        case TokenType::Star: *out_ << "protoPython::TokenType::Star"; break;
        case TokenType::Slash: *out_ << "protoPython::TokenType::Slash"; break;
        case TokenType::Modulo: *out_ << "protoPython::TokenType::Modulo"; break;
        case TokenType::EqEqual: *out_ << "protoPython::TokenType::EqEqual"; break;
        case TokenType::NotEqual: *out_ << "protoPython::TokenType::NotEqual"; break;
        case TokenType::Less: *out_ << "protoPython::TokenType::Less"; break;
        case TokenType::LessEqual: *out_ << "protoPython::TokenType::LessEqual"; break;
        case TokenType::Greater: *out_ << "protoPython::TokenType::Greater"; break;
        case TokenType::GreaterEqual: *out_ << "protoPython::TokenType::GreaterEqual"; break;
        case TokenType::BitAnd: *out_ << "protoPython::TokenType::BitAnd"; break;
        case TokenType::BitOr: *out_ << "protoPython::TokenType::BitOr"; break;
        case TokenType::BitXor: *out_ << "protoPython::TokenType::BitXor"; break;
        case TokenType::LShift: *out_ << "protoPython::TokenType::LShift"; break;
        case TokenType::RShift: *out_ << "protoPython::TokenType::RShift"; break;
        case TokenType::DoubleStar: *out_ << "protoPython::TokenType::DoubleStar"; break;
        case TokenType::DoubleSlash: *out_ << "protoPython::TokenType::DoubleSlash"; break;
        case TokenType::At: *out_ << "protoPython::TokenType::At"; break;
        default: *out_ << "protoPython::TokenType::Plus /* Unsupported op */"; break;
    }
    *out_ << ", ";
    if (!generateNode(n->right.get())) return false;
    *out_ << ")";
    return true;
}

bool CppGenerator::generatePass(PassNode* n) {
    *out_ << "/* pass */";
    return true;
}

bool CppGenerator::generateBreak(BreakNode* n) {
    *out_ << "break";
    return true;
}

bool CppGenerator::generateContinue(ContinueNode* n) {
    *out_ << "continue";
    return true;
}

bool CppGenerator::generateFunctionDef(FunctionDefNode* n) {
    return generateFunctionInternal(n->name, n->parameters, n->vararg, n->kwarg, n->body.get(), n->decorator_list, false, &n->defaults);
}

bool CppGenerator::generateAsyncFunctionDef(AsyncFunctionDefNode* n) {
    return generateFunctionInternal(n->name, n->parameters, n->vararg, n->kwarg, n->body.get(), n->decorator_list, true, &n->defaults);
}

bool CppGenerator::generateFunctionInternal(const std::string& name,
                                         const std::vector<std::string>& parameters,
                                         const std::string& vararg,
                                         const std::string& kwarg,
                                         ASTNode* body,
                                         const std::vector<std::unique_ptr<ASTNode>>& decorator_list,
                                         bool isAsync,
                                         const std::vector<std::unique_ptr<ASTNode>>* defaults) {
    (void)isAsync;
    // Save state for nested functions
    bool oldInStateMachine = inStateMachine_;
    int oldStateCount = stateCount_;
    bool oldInClassBody = inClassBody_;
    auto oldOrderedLocals = orderedLocalVars_;
    auto oldLocalVars = localVars_;

    // 1. Identify locals and check for yield/await
    localVars_.clear();
    orderedLocalVars_.clear();
    collectLocals(body, localVars_);
    // Parameters are also locals — collectLocals only walks the body,
    // so without this insertion, references like `def f(x): return x + 1`
    // emitted `env->lookupName("x")` and missed the C++ `local_x`
    // declared by the function prologue.  Add params so generateName /
    // generateAssignToTarget / generateAugAssign use `local_<param>`.
    for (const auto& p : parameters) localVars_.insert(p);
    if (!vararg.empty()) localVars_.insert(vararg);
    if (!kwarg.empty()) localVars_.insert(kwarg);

    // Sort locals for consistent state management
    std::vector<std::string> orderedLocals(localVars_.begin(), localVars_.end());
    std::sort(orderedLocals.begin(), orderedLocals.end());
    orderedLocalVars_ = orderedLocals;

    // Compute free variables: names referenced in body that are not local
    // here but ARE local in some enclosing function scope.  These are the
    // closure-captured variables; their current values must be snapshotted
    // at the def site and read back as `local_<name>` in the prologue.
    auto oldFreeVars = freeVars_;
    freeVars_.clear();
    {
        std::unordered_set<std::string> bodyRefs;
        collectNameRefs(body, bodyRefs);
        for (const auto& nm : bodyRefs) {
            if (localVars_.count(nm)) continue;
            for (auto it = enclosingLocals_.rbegin(); it != enclosingLocals_.rend(); ++it) {
                if (it->count(nm)) { freeVars_.insert(nm); break; }
            }
        }
    }
    std::vector<std::string> orderedFreeVars(freeVars_.begin(), freeVars_.end());
    std::sort(orderedFreeVars.begin(), orderedFreeVars.end());
    // Free vars become local_<name> in the prologue, so add them to
    // localVars_/orderedLocalVars_ — generateName needs to see them as
    // "use local_<id>, not env->lookupName".
    for (const auto& fv : orderedFreeVars) {
        localVars_.insert(fv);
        orderedLocalVars_.push_back(fv);
    }

    const bool isGenerator = containsYieldOrAwait(body) || countYields(body) > 0;
    inStateMachine_ = isGenerator;
    stateCount_ = 0;
    inClassBody_ = false;

    // Use a unique suffix for top-level C++ function names
    static int functionCounter = 0;
    std::string uniqueSuffix = "_" + std::to_string(++functionCounter);
    std::string cppFuncName = "py_func_" + name + uniqueSuffix;
    std::string cppContName = "py_cont_" + name + uniqueSuffix;

    // Publish the current function identity so generateCall can detect
    // self-recursion and emit a direct C++ call instead of routing the
    // recursive site through env->lookupName + env->callObject.  Only the
    // standard (non-generator) path benefits: calling a generator function
    // builds a generator object, and its body runs in the continuation.
    std::string oldCurrentFuncName = currentFuncName_;
    std::string oldCurrentFuncCppName = currentFuncCppName_;
    // Inside a method, the bare name is the module-level binding, not the
    // method itself.
    if (!isGenerator && !oldInClassBody) {
        currentFuncName_ = name;
        currentFuncCppName_ = cppFuncName;
    } else {
        currentFuncName_.clear();
        currentFuncCppName_.clear();
    }

    std::ostream* oldOut = out_;
    std::ostringstream funcStream;
    out_ = &funcStream;

    // Every compiled callable runs in a context of its own, with its locals
    // in that context's automatic slots (see enterSlotFrame), and in the
    // namespace of the module that defined it (see kGeneratedPreamble).  Its
    // body is generated first, into `inner`, because the slot count is known
    // only afterwards; it then runs as a lambda, so a Python `return` leaves
    // the lambda and the result is promoted to the caller's context before
    // the scope closes.
    const std::vector<std::string> slotLocals = orderedLocalVars_;
    auto emitLocalRefs = [&]() {
        for (size_t i = 0; i < slotLocals.size(); ++i) {
            *out_ << "    const proto::ProtoObject*& local_" << slotLocals[i]
                  << " = __slot[" << i << "];\n";
        }
    };
    // Each callable is an entry point and its body.  The entry point tells
    // the calling conventions apart and enters the module namespace (both
    // only read); the body opens the callable's context and runs.  A
    // self-recursive call runs the body directly: it is already in the
    // namespace, with the same storage.
    auto emitScopedCall = [&](const std::string& cppName, const std::string& innerText) {
        *out_ << "\nstatic const proto::ProtoObject* " << cppName << "_body(proto::ProtoContext* __callerCtx, const proto::ProtoObject* __fs, const proto::ProtoObject* __receiver, const proto::ProtoList* args) {\n";
        *out_ << openContextScope("__callerCtx");
        *out_ << "    auto* env = protoPython::PythonEnvironment::get(ctx);\n";
        *out_ << "    if (__receiver) args = __withReceiver(ctx, __receiver, args);\n";
        emitLocalRefs();
        *out_ << "    const proto::ProtoObject* __result = nullptr;\n";
        *out_ << "    try {\n";
        *out_ << "        __result = [&]() -> const proto::ProtoObject* {\n";
        *out_ << innerText;
        *out_ << "        }();\n";
        *out_ << "    } catch (const proto::ProtoObject* __thrown) {\n";
        *out_ << "        protoPython::promote(ctx, __thrown);\n";
        *out_ << "        throw;\n";
        *out_ << "    }\n";
        *out_ << "    protoPython::promote(ctx, __result);\n";
        *out_ << "    return __result;\n";
        *out_ << "}\n";
        *out_ << "\nconst proto::ProtoObject* " << cppName << "(proto::ProtoContext* __callerCtx, const proto::ProtoObject* self, const proto::ParentLink* pl, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {\n";
        *out_ << "    (void)pl; (void)kwargs;\n";
        *out_ << "    const bool __isStorage = __isFunctionStorage(__callerCtx, self);\n";
        *out_ << "    const proto::ProtoObject* __fs = __isStorage ? self : nullptr;\n";
        *out_ << "    protoPython::ModuleNamespaceScope __ns(__moduleNamespaceOf(__callerCtx, __fs));\n";
        *out_ << "    return " << cppName << "_body(__callerCtx, __fs, __isStorage ? nullptr : self, args);\n";
        *out_ << "}\n";
    };
    // Bind the parameters (trailing ones may carry default values: defaults
    // align to the LAST N positional parameters) and read the closure-captured
    // variables out of the function storage (`self`).  Every other local
    // starts as PROTO_NONE, the value ContextScope gives a fresh slot.
    auto emitBindings = [&]() -> bool {
        *out_ << "    proto::proto_ulong nPos = args ? args->getSize(ctx) : 0;\n";
        const size_t numDefaults = defaults ? defaults->size() : 0;
        const size_t defaultStart = parameters.size() > numDefaults
                                        ? parameters.size() - numDefaults
                                        : 0;
        for (size_t i = 0; i < parameters.size(); ++i) {
            *out_ << "    local_" << parameters[i]
                  << " = (nPos > " << i << ") ? args->getAt(ctx, " << i << ") : ";
            if (i >= defaultStart && defaults) {
                if (!generateNode((*defaults)[i - defaultStart].get())) return false;
            } else {
                *out_ << "PROTO_NONE";
            }
            *out_ << ";\n";
        }
        for (const auto& fv : orderedFreeVars) {
            *out_ << "    local_" << fv << " = __freeVar(ctx, __fs, \"" << fv << "\");\n";
        }
        return true;
    };

    // Push our own scope onto enclosingLocals_ so any nested function can
    // recognise OUR locals (including free vars promoted to locals by the
    // closure read) when computing its own free vars.
    enclosingLocals_.push_back(localVars_);
    for (const auto& fv : freeVars_) enclosingLocals_.back().insert(fv);

    bool ok = true;
    if (isGenerator) {
        // A generator is two callables.  The function builds the generator
        // object; the continuation (its gi_native_callback) runs the body
        // up to the next yield each time the generator is resumed
        // (py_generator_send_impl calls it with the generator, the sent
        // value and, for throw() and close(), the exception to raise at the
        // suspended yield).
        //
        // State across yields lives in the generator object, which the
        // collector sees: gi_pc holds the yield to resume at (0 before the
        // first resumption, -1 once finished) and gi_locals a list of the
        // continuation's slots -- the locals, and the anchors of the
        // statements the yield is inside (a for loop's iterator, a caught
        // exception).  A resumption restores the slots and re-enters the
        // body; every statement on the path to the suspended yield is
        // re-entered without re-evaluating what it evaluated before the
        // yield (see emitStatementList).
        const SlotFrame contSlots = enterSlotFrame(static_cast<int>(slotLocals.size()));
        std::ostringstream contBody;
        out_ = &contBody;
        *out_ << "    auto* gen = const_cast<proto::ProtoObject*>(args->getAt(ctx, 0));\n";
        *out_ << "    const proto::ProtoObject* __sent = args->getSize(ctx) > 1 ? args->getAt(ctx, 1) : PROTO_NONE;\n";
        *out_ << "    const proto::ProtoObject* __throw = args->getSize(ctx) > 2 ? args->getAt(ctx, 2) : nullptr;\n";
        *out_ << "    if (__throw == PROTO_NONE) __throw = nullptr;\n";
        *out_ << "    (void)__sent;\n";
        *out_ << "    const proto::ProtoObject* __pcObj = env->getAttribute(ctx, gen, env->getGiPCString());\n";
        *out_ << "    int __resume = (__pcObj && __pcObj->isInteger(ctx)) ? static_cast<int>(__pcObj->asLong(ctx)) : -1;\n";
        *out_ << "    auto __finish = [&]() {\n";
        *out_ << "        gen->setAttribute(ctx, env->getGiPCString(), ctx->fromInteger(-1));\n";
        *out_ << "        gen->setAttribute(ctx, env->getGiLocalsString(), PROTO_NONE);\n";
        *out_ << "    };\n";
        *out_ << "    if (__resume < 0) {\n";
        *out_ << "        if (__throw) env->raiseException(__throw);\n";
        *out_ << "        else env->raiseStopIteration(ctx, PROTO_NONE);\n";
        *out_ << "        return nullptr;\n";
        *out_ << "    }\n";
        // throw() or close() before the first resumption: the body never runs.
        *out_ << "    if (__resume == 0 && __throw) {\n";
        *out_ << "        __finish();\n";
        *out_ << "        env->raiseException(__throw);\n";
        *out_ << "        return nullptr;\n";
        *out_ << "    }\n";
        *out_ << "    {\n";
        *out_ << "        const proto::ProtoObject* __saved = env->getAttribute(ctx, gen, env->getGiLocalsString());\n";
        *out_ << "        const proto::ProtoList* __savedList = (__saved && __saved != PROTO_NONE) ? __saved->asList(ctx) : nullptr;\n";
        *out_ << "        const proto::proto_ulong __n = __savedList ? std::min<proto::proto_ulong>(__savedList->getSize(ctx), __nslots) : 0;\n";
        *out_ << "        for (proto::proto_ulong __i = 0; __i < __n; ++__i) __slot[__i] = __savedList->getAt(ctx, static_cast<int>(__i));\n";
        *out_ << "    }\n";
        *out_ << "    try {\n";
        beginStatement();
        ok = generateBody(body);
        if (ok) {
            *out_ << ";\n";
            *out_ << "    } catch (const __PyUnwind&) {}\n";
            // The body ran to its end, or an exception left it.
            *out_ << "    __finish();\n";
            *out_ << "    if (!env->hasPendingException()) env->raiseStopIteration(ctx, PROTO_NONE);\n";
            *out_ << "    return nullptr;\n";
            out_ = &funcStream;
            emitScopedCall(cppContName, contBody.str());
        }
        leaveSlotFrame(contSlots);

        // The generator function: binds its arguments into its slots, which
        // become the continuation's initial gi_locals (same slot layout).
        if (ok) {
            const SlotFrame wrapSlots = enterSlotFrame(static_cast<int>(slotLocals.size()));
            std::ostringstream wrapBody;
            out_ = &wrapBody;
            ok = emitBindings();
            if (ok) {
                *out_ << "    auto* gen = const_cast<proto::ProtoObject*>(ctx->newObject(true));\n";
                *out_ << "    if (env->getGeneratorPrototype()) gen = const_cast<proto::ProtoObject*>(gen->addParent(ctx, env->getGeneratorPrototype()));\n";
                *out_ << "    gen = const_cast<proto::ProtoObject*>(gen->setAttribute(ctx, env->getGiNativeCallbackString(), ctx->fromMethod(const_cast<proto::ProtoObject*>(__fs), " << cppContName << ")));\n";
                *out_ << "    gen = const_cast<proto::ProtoObject*>(gen->setAttribute(ctx, env->getGiPCString(), ctx->fromInteger(0)));\n";
                *out_ << "    gen = const_cast<proto::ProtoObject*>(gen->setAttribute(ctx, env->getGiRunningString(), PROTO_FALSE));\n";
                *out_ << "    gen = const_cast<proto::ProtoObject*>(gen->setAttribute(ctx, env->getGiLocalsString(), ctx->newList("
                      << slotLocals.size() << "u, __slot)->asObject(ctx)));\n";
                *out_ << "    return gen;\n";
                out_ = &funcStream;
                emitScopedCall(cppFuncName, wrapBody.str());
            }
            leaveSlotFrame(wrapSlots);
        }
    } else {
        // Standard function logic.
        const SlotFrame funcSlots = enterSlotFrame(static_cast<int>(slotLocals.size()));
        std::ostringstream funcBody;
        out_ = &funcBody;
        ok = emitBindings();
        if (ok) {
            *out_ << "    try {\n";
            beginStatement();
            ok = generateNode(body);
        }
        if (ok) {
            *out_ << "    } catch (const proto::ProtoObject* retVal) { return retVal; }\n";
            *out_ << "    return PROTO_NONE;\n";
            out_ = &funcStream;
            emitScopedCall(cppFuncName, funcBody.str());
        }
        leaveSlotFrame(funcSlots);
    }
    enclosingLocals_.pop_back();

    // Restore the enclosing scope's state: the registration below (the
    // decorators and the binding of the name) runs in the enclosing scope.
    inStateMachine_ = oldInStateMachine;
    stateCount_ = oldStateCount;
    inClassBody_ = oldInClassBody;
    orderedLocalVars_ = oldOrderedLocals;
    localVars_ = oldLocalVars;
    freeVars_ = oldFreeVars;
    currentFuncName_ = oldCurrentFuncName;
    currentFuncCppName_ = oldCurrentFuncCppName;
    out_ = oldOut;
    if (!ok) return false;

    // Append to header
    header_ << funcStream.str();

    // Registration in module init / enclosing scope: the function object is
    // a method bound to its storage (module namespace and a snapshot of the
    // free variables' current values; see kGeneratedPreamble).
    *out_ << "{\n";
    *out_ << "    const proto::ProtoObject* __fs = __newFunctionStorage(ctx);\n";
    for (const auto& fv : orderedFreeVars) {
        *out_ << "    __fs = __withFreeVar(ctx, __fs, \"" << fv << "\", local_" << fv << ");\n";
    }
    *out_ << "    const proto::ProtoObject* __fn = ctx->fromMethod(const_cast<proto::ProtoObject*>(__fs), "
          << cppFuncName << ");\n";
    for (auto it = decorator_list.rbegin(); it != decorator_list.rend(); ++it) {
        *out_ << "    __fn = env->callObject(";
        if (!generateNode(it->get())) return false;
        *out_ << ", {__fn});\n";
    }
    *out_ << "    ";
    emitAssignName(name, "__fn");
    *out_ << ";\n}";
    return true;
}

bool CppGenerator::generateClassDef(ClassDefNode* n) {
    *out_ << "\n    // Class definition: " << n->name << "\n";
    *out_ << "    const proto::ProtoObject* class_name_" << n->name << " = ctx->fromUTF8String(\"" << n->name << "\");\n";
    
    // Bases.  They and the namespace are used after the class body, which
    // may loop: each is kept in an anchor slot.
    *out_ << "    std::vector<const proto::ProtoObject*> bases_" << n->name << ";\n";
    std::vector<int> classAnchors;
    for (auto& b : n->bases) {
        const int baseSlot = pushAnchor();
        classAnchors.push_back(baseSlot);
        *out_ << "    __slot[" << baseSlot << "] = ";
        if (!generateNode(b.get())) return false;
        *out_ << ";\n";
        *out_ << "    bases_" << n->name << ".push_back(__slot[" << baseSlot << "]);\n";
    }

    // Namespace and Body
    const int nsSlot = pushAnchor();
    classAnchors.push_back(nsSlot);
    *out_ << "    auto* ns_" << n->name << " = const_cast<proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "    __slot[" << nsSlot << "] = ns_" << n->name << ";\n";
    *out_ << "    ns_" << n->name << "->setAttribute(ctx, env->getNameString(), class_name_" << n->name << ");\n";
    *out_ << "    {\n";
    *out_ << "        auto* oldFrame = protoPython::PythonEnvironment::getCurrentFrame();\n";
    *out_ << "        protoPython::PythonEnvironment::setCurrentFrame(ns_" << n->name << ");\n";
    // Names the class body binds go to the class namespace (the current
    // frame), not to the enclosing function's locals.
    const bool oldInClassBody = inClassBody_;
    inClassBody_ = true;
    const bool bodyOk = generateNode(n->body.get());
    inClassBody_ = oldInClassBody;
    if (!bodyOk) return false;
    *out_ << "        protoPython::PythonEnvironment::setCurrentFrame(oldFrame);\n";
    *out_ << "    }\n";

    // Create Class Object
    *out_ << "    auto* cls_" << n->name << " = const_cast<proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "    if (env->getTypePrototype()) cls_" << n->name << "->setAttribute(ctx, env->getClassString(), env->getTypePrototype());\n";
    *out_ << "    for (auto* b : bases_" << n->name << ") cls_" << n->name << "->addParent(ctx, b);\n";
    
    // Copy attributes from namespace to class
    *out_ << "    {\n";
    *out_ << "        const proto::ProtoSparseList* keys = ns_" << n->name << "->getAttributes(ctx);\n";
    *out_ << "        if (keys) {\n";
    *out_ << "            auto it = keys->getIterator(ctx);\n";
    *out_ << "            while (it && it->hasNext(ctx)) {\n";
    *out_ << "                proto::proto_ulong key = it->nextKey(ctx);\n";
    *out_ << "                const proto::ProtoObject* keyObj = reinterpret_cast<const proto::ProtoObject*>(static_cast<std::uintptr_t>(key));\n";
    *out_ << "                if (keyObj && keyObj->isString(ctx)) {\n";
    *out_ << "                    const proto::ProtoString* k = keyObj->asString(ctx);\n";
    *out_ << "                    cls_" << n->name << "->setAttribute(ctx, k, ns_" << n->name << "->getAttribute(ctx, k));\n";
    *out_ << "                }\n";
    *out_ << "                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);\n";
    *out_ << "            }\n";
    *out_ << "        }\n";
    *out_ << "    }\n";
    
    // Set __call__ for instantiation
    *out_ << "    cls_" << n->name << "->setAttribute(ctx, env->getCallString(), ctx->fromMethod(cls_" << n->name << ", [](proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* pl, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) -> const proto::ProtoObject* {\n";
    *out_ << "        auto* env = protoPython::PythonEnvironment::get(ctx);\n";
    *out_ << "        auto* obj = const_cast<proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "        obj->addParent(ctx, self);\n";
    *out_ << "        const proto::ProtoObject* init = env->getAttribute(ctx, obj, env->getInitString());\n";
    *out_ << "        if (init && init != PROTO_NONE) {\n";
    *out_ << "            std::vector<const proto::ProtoObject*> vargs;\n";
    *out_ << "            for (proto::proto_ulong i = 0; i < args->getSize(ctx); ++i) vargs.push_back(args->getAt(ctx, i));\n";
    *out_ << "            env->callObject(init, vargs);\n";
    *out_ << "        }\n";
    *out_ << "        return obj;\n";
    *out_ << "    }));\n";

    *out_ << "    auto var_cls_" << n->name << " = static_cast<const proto::ProtoObject*>(cls_" << n->name << ");\n";

    // Apply decorators Bottom-to-Top
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
            *out_ << "    var_cls_" << n->name << " = env->callObject(";
            if (!generateNode(it->get())) return false;
            *out_ << ", {var_cls_" << n->name << "});\n";
        }
    }

    *out_ << "    ";
    emitAssignName(n->name, "var_cls_" + n->name);
    *out_ << ";\n";
    for (size_t i = 0; i < classAnchors.size(); ++i) popAnchor();
    return true;
}

bool CppGenerator::generateAugAssign(AugAssignNode* n) {
    if (auto* nameNode = dynamic_cast<NameNode*>(n->target.get())) {
        // Function-local case: lower to a C++ assignment using the
        // existing `local_<name>` and env->binaryOp, so the new value
        // updates the C++ local in place (no env round-trip).  Module
        // / global case still goes through env->augAssignName.
        auto it = std::find(orderedLocalVars_.begin(), orderedLocalVars_.end(), nameNode->id);
        if (it != orderedLocalVars_.end()) {
            // SmallInt fast path on `local += rhs`-style augmented arithmetic.
            // Mirrors generateBinOp: tag-check both operands, inline the
            // arithmetic, fall back to env->binaryOp otherwise.  Covers the
            // common `total += i` accumulator loop that dominates many
            // benchmarks.
            const char* arithOp = nullptr;
            bool overflow = false;
            switch (n->op) {
                case TokenType::PlusAssign:  arithOp = "+"; break;
                case TokenType::MinusAssign: arithOp = "-"; break;
                case TokenType::StarAssign:  arithOp = "*"; overflow = true; break;
                default: break;
            }
            if (arithOp) {
                *out_ << "local_" << nameNode->id
                      << " = ([&]() -> const proto::ProtoObject* {\n"
                      << "        const proto::ProtoObject* __a = local_" << nameNode->id << ";\n"
                      << "        const proto::ProtoObject* __b = ";
                if (!generateNode(n->value.get())) return false;
                *out_ << ";\n"
                      << "        if (__a && __b && proto::isSmallInt(__a) && proto::isSmallInt(__b)) {\n"
                      << "            std::int64_t __va = proto::asSmallInt(__a);\n"
                      << "            std::int64_t __vb = proto::asSmallInt(__b);\n";
                if (overflow) {
                    *out_ << "            std::int64_t __vr;\n"
                          << "            if (!protoPython::checkedMul64(__va, __vb, &__vr)) return ctx->fromInteger(__vr);\n";
                } else {
                    *out_ << "            return ctx->fromInteger(__va " << arithOp << " __vb);\n";
                }
                *out_ << "        }\n"
                      << "        return env->binaryOp(__a, protoPython::TokenType::";
                switch (n->op) {
                    case TokenType::PlusAssign:  *out_ << "Plus"; break;
                    case TokenType::MinusAssign: *out_ << "Minus"; break;
                    case TokenType::StarAssign:  *out_ << "Star"; break;
                    default: *out_ << "Plus"; break;
                }
                *out_ << ", __b);\n"
                      << "    })()";
                return true;
            }
            *out_ << "local_" << nameNode->id << " = env->binaryOp(local_"
                  << nameNode->id << ", protoPython::TokenType::";
            switch (n->op) {
                case TokenType::PlusAssign:        *out_ << "Plus"; break;
                case TokenType::MinusAssign:       *out_ << "Minus"; break;
                case TokenType::StarAssign:        *out_ << "Star"; break;
                case TokenType::SlashAssign:       *out_ << "Slash"; break;
                case TokenType::ModuloAssign:      *out_ << "Modulo"; break;
                case TokenType::DoubleSlashAssign: *out_ << "DoubleSlash"; break;
                case TokenType::DoubleStarAssign:  *out_ << "DoubleStar"; break;
                case TokenType::AndAssign:         *out_ << "BitAnd"; break;
                case TokenType::OrAssign:          *out_ << "BitOr"; break;
                case TokenType::XorAssign:         *out_ << "BitXor"; break;
                case TokenType::LShiftAssign:      *out_ << "LShift"; break;
                case TokenType::RShiftAssign:      *out_ << "RShift"; break;
                case TokenType::AtAssign:          *out_ << "At"; break;
                default:                            *out_ << "Plus"; break;
            }
            *out_ << ", ";
            if (!generateNode(n->value.get())) return false;
            *out_ << ")";
            return true;
        }
        *out_ << "env->augAssignName(\"" << nameNode->id << "\", ";
    } else if (auto* attrNode = dynamic_cast<AttributeNode*>(n->target.get())) {
        *out_ << "env->augAssignAttr(";
        if (!generateNode(attrNode->value.get())) return false;
        *out_ << ", \"" << attrNode->attr << "\", ";
    } else if (auto* subNode = dynamic_cast<SubscriptNode*>(n->target.get())) {
        *out_ << "env->augAssignItem(";
        if (!generateNode(subNode->value.get())) return false;
        *out_ << ", ";
        if (!generateNode(subNode->index.get())) return false;
        *out_ << ", ";
    } else {
        *out_ << "/* Unsupported augAssign target */";
        return true;
    }
    
    switch (n->op) {
        case TokenType::PlusAssign:        *out_ << "protoPython::TokenType::PlusAssign"; break;
        case TokenType::MinusAssign:       *out_ << "protoPython::TokenType::MinusAssign"; break;
        case TokenType::StarAssign:        *out_ << "protoPython::TokenType::StarAssign"; break;
        case TokenType::SlashAssign:       *out_ << "protoPython::TokenType::SlashAssign"; break;
        case TokenType::ModuloAssign:      *out_ << "protoPython::TokenType::ModuloAssign"; break;
        case TokenType::DoubleSlashAssign: *out_ << "protoPython::TokenType::DoubleSlashAssign"; break;
        case TokenType::DoubleStarAssign:  *out_ << "protoPython::TokenType::DoubleStarAssign"; break;
        case TokenType::AndAssign:         *out_ << "protoPython::TokenType::AndAssign"; break;
        case TokenType::OrAssign:          *out_ << "protoPython::TokenType::OrAssign"; break;
        case TokenType::XorAssign:         *out_ << "protoPython::TokenType::XorAssign"; break;
        case TokenType::LShiftAssign:      *out_ << "protoPython::TokenType::LShiftAssign"; break;
        case TokenType::RShiftAssign:      *out_ << "protoPython::TokenType::RShiftAssign"; break;
        case TokenType::AtAssign:          *out_ << "protoPython::TokenType::AtAssign"; break;
        default:                            *out_ << "protoPython::TokenType::PlusAssign"; break;
    }
    *out_ << ", ";
    if (!generateNode(n->value.get())) return false;
    *out_ << ")";
    return true;
}

bool CppGenerator::generateReturn(ReturnNode* n) {
    if (inStateMachine_) {
        // A generator's return finishes it: the value travels in the
        // StopIteration that the resumer sees.
        *out_ << "{\n";
        *out_ << "    const proto::ProtoObject* __rv = ";
        if (n->value) {
            if (!generateNode(n->value.get())) return false;
        } else {
            *out_ << "PROTO_NONE";
        }
        *out_ << ";\n";
        *out_ << "    if (env->hasPendingException()) throw __PyUnwind{};\n";
        *out_ << "    __finish();\n";
        *out_ << "    env->raiseStopIteration(ctx, __rv);\n";
        *out_ << "    return nullptr;\n";
        *out_ << "}";
        return true;
    }
    // Plain C++ return.  Earlier revisions used `throw X` so that the
    // function-wrapper try/catch caught the value uniformly; the cost
    // was a full stack unwind per return (frame walking, possible
    // exception payload allocation, destructor runs).  On fib(25) that
    // path alone added ~hundreds of ms across 242k recursive returns.
    // A plain `return` exits the C++ function immediately — exactly
    // the same shape as Python `return`, and the wrapper try/catch
    // still catches the few throws we still emit for control flow
    // sentinels.  No semantic change versus the previous throw-based
    // scheme for the standard "return at end of function body" case.
    *out_ << "return ";
    if (n->value) {
        if (!generateNode(n->value.get())) return false;
    } else {
        *out_ << "PROTO_NONE";
    }
    return true;
}

bool CppGenerator::generateAttribute(AttributeNode* n) {
    *out_ << "env->getAttr(";
    if (!generateNode(n->value.get())) return false;
    *out_ << ", \"" << n->attr << "\")";
    return true;
}

bool CppGenerator::generateSubscript(SubscriptNode* n) {
    *out_ << "env->getItem(";
    if (!generateNode(n->value.get())) return false;
    *out_ << ", ";
    if (!generateNode(n->index.get())) return false;
    *out_ << ")";
    return true;
}

bool CppGenerator::generateListLiteral(ListLiteralNode* n) {
    *out_ << "([&]() {\n";
    *out_ << "        auto* listObj = static_cast<const proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "        auto* lst = ctx->newList();\n";
    for (auto& elt : n->elements) {
        if (auto* s = dynamic_cast<StarredNode*>(elt.get())) {
            *out_ << "        {\n";
            *out_ << "            auto* unpacked = "; if (!generateNode(s->value.get())) return false; *out_ << ";\n";
            *out_ << "            auto* i = env->iter(unpacked);\n";
            *out_ << "            while (true) {\n";
            *out_ << "                auto* item = env->next(i);\n";
            *out_ << "                if (!item || item == PROTO_NONE) break;\n";
            *out_ << "                lst = lst->appendLast(ctx, item);\n";
            *out_ << "            }\n";
            *out_ << "        }\n";
        } else {
            *out_ << "        lst = lst->appendLast(ctx, ";
            if (!generateNode(elt.get())) return false;
            *out_ << ");\n";
        }
    }
    *out_ << "        listObj->setAttribute(ctx, env->getDataString(), (const proto::ProtoObject*)lst);\n";
    *out_ << "        if (env->getListPrototype()) listObj->addParent(ctx, env->getListPrototype());\n";
    *out_ << "        return (const proto::ProtoObject*)listObj;\n";
    *out_ << "    })()";
    return true;
}

bool CppGenerator::generateDictLiteral(DictLiteralNode* n) {
    *out_ << "([&]() {\n";
    *out_ << "        auto* mapObj = static_cast<const proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "        auto* data = ctx->newSparseList();\n";
    *out_ << "        auto* keys = ctx->newList();\n";
    for (size_t i = 0; i < n->keys.size(); ++i) {
        if (n->keys[i] == nullptr) {
            // Unpacking: **v
            *out_ << "        {\n";
            *out_ << "            auto* unpacked = "; 
            if (auto* starred = dynamic_cast<protoPython::StarredNode*>(n->values[i].get())) {
                if (!generateNode(starred->value.get())) return false;
            } else {
                if (!generateNode(n->values[i].get())) return false;
            }
            *out_ << ";\n";
            *out_ << "            auto* i = env->iter(unpacked);\n";
            *out_ << "            while (true) {\n";
            *out_ << "                auto* k = env->next(i);\n";
            *out_ << "                if (!k || k == PROTO_NONE) break;\n";
            *out_ << "                auto* v = env->getItem(unpacked, k);\n";
            *out_ << "                data = data->setAt(ctx, k->getHash(ctx), v);\n";
            *out_ << "                keys = keys->appendLast(ctx, k);\n";
            *out_ << "            }\n";
            *out_ << "        }\n";
        } else {
            *out_ << "        {\n";
            *out_ << "            auto* k = "; if (!generateNode(n->keys[i].get())) return false; *out_ << ";\n";
            *out_ << "            auto* v = "; if (!generateNode(n->values[i].get())) return false; *out_ << ";\n";
            *out_ << "            data = data->setAt(ctx, k->getHash(ctx), v);\n";
            *out_ << "            keys = keys->appendLast(ctx, k);\n";
            *out_ << "        }\n";
        }
    }
    *out_ << "        mapObj->setAttribute(ctx, env->getDataString(), (const proto::ProtoObject*)data);\n";
    *out_ << "        mapObj->setAttribute(ctx, env->getKeysString(), (const proto::ProtoObject*)keys);\n";
    *out_ << "        if (env->getDictPrototype()) mapObj->addParent(ctx, env->getDictPrototype());\n";
    *out_ << "        return (const proto::ProtoObject*)mapObj;\n";
    *out_ << "    })()";
    return true;
}

bool CppGenerator::generateTupleLiteral(TupleLiteralNode* n) {
    *out_ << "([&]() {\n";
    *out_ << "        auto* tupObj = static_cast<const proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "        auto* lst = ctx->newList();\n";
    for (auto& elt : n->elements) {
        if (auto* s = dynamic_cast<StarredNode*>(elt.get())) {
            *out_ << "        {\n";
            *out_ << "            auto* unpacked = "; if (!generateNode(s->value.get())) return false; *out_ << ";\n";
            *out_ << "            auto* i = env->iter(unpacked);\n";
            *out_ << "            while (true) {\n";
            *out_ << "                auto* item = env->next(i);\n";
            *out_ << "                if (!item || item == PROTO_NONE) break;\n";
            *out_ << "                lst = lst->appendLast(ctx, item);\n";
            *out_ << "            }\n";
            *out_ << "        }\n";
        } else {
            *out_ << "        lst = lst->appendLast(ctx, ";
            if (!generateNode(elt.get())) return false;
            *out_ << ");\n";
        }
    }
    *out_ << "        auto* tup = ctx->newTupleFromList(lst);\n";
    *out_ << "        tupObj->setAttribute(ctx, env->getDataString(), (const proto::ProtoObject*)tup);\n";
    *out_ << "        if (env->getTuplePrototype()) tupObj->addParent(ctx, env->getTuplePrototype());\n";
    *out_ << "        return (const proto::ProtoObject*)tupObj;\n";
    *out_ << "    })()";
    return true;
}

bool CppGenerator::generateSetLiteral(SetLiteralNode* n) {
    *out_ << "([&]() {\n";
    *out_ << "        auto* setObj = static_cast<const proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "        auto* data = ctx->newSparseList();\n";
    for (auto& elt : n->elements) {
        if (auto* s = dynamic_cast<StarredNode*>(elt.get())) {
            *out_ << "        {\n";
            *out_ << "            auto* unpacked = "; if (!generateNode(s->value.get())) return false; *out_ << ";\n";
            *out_ << "            auto* i = env->iter(unpacked);\n";
            *out_ << "            while (true) {\n";
            *out_ << "                auto* item = env->next(i);\n";
            *out_ << "                if (!item || item == PROTO_NONE) break;\n";
            *out_ << "                data = data->setAt(ctx, item->getHash(ctx), item);\n";
            *out_ << "            }\n";
            *out_ << "        }\n";
        } else {
            *out_ << "        {\n";
            *out_ << "            auto* e = "; if (!generateNode(elt.get())) return false; *out_ << ";\n";
            *out_ << "            data = data->setAt(ctx, e->getHash(ctx), e);\n";
            *out_ << "        }\n";
        }
    }
    *out_ << "        setObj->setAttribute(ctx, env->getDataString(), (const proto::ProtoObject*)data);\n";
    *out_ << "        if (env->getSetPrototype()) setObj->addParent(ctx, env->getSetPrototype());\n";
    *out_ << "        return (const proto::ProtoObject*)setObj;\n";
    *out_ << "    })()";
    return true;
}

bool CppGenerator::generateNamedExpr(NamedExprNode* n) {
    *out_ << "([&]() {\n";
    *out_ << "        const proto::ProtoObject* val = ";
    if (!generateNode(n->value.get())) return false;
    *out_ << ";\n";
    if (!generateAssignToTarget(n->target.get(), "val")) return false;
    *out_ << ";\n";
    *out_ << "        return val;\n";
    *out_ << "    })()";
    return true;
}

bool CppGenerator::generateFor(ForNode* n) {
    static int forId = 0;
    int id = forId++;
    // In a generator, a resumption inside the body re-enters the loop
    // without creating the iterator again (it is restored with the slots)
    // and without advancing it; one inside the else clause skips the loop.
    const int base = stateCount_;
    const int nBody = inStateMachine_ ? countYields(n->body.get()) : 0;
    const int nElse = inStateMachine_ ? countYields(n->orelse.get()) : 0;
    const bool resumable = nBody + nElse > 0;
    const std::string inElse = resumeIn(base + nBody + 1, nElse);
    // The iterator lives across the whole loop, safepoints included: it is
    // kept in an anchor slot, not only in a C++ variable.
    const int iterSlot = pushAnchor();
    *out_ << "{\n";
    *out_ << "        " << (resumable ? "if (__resume == 0) " : "") << "__slot[" << iterSlot << "] = env->iter(";
    if (!generateNode(n->iter.get())) return false;
    *out_ << ");\n";
    *out_ << "        const proto::ProtoObject* const& __iter_" << id << " = __slot[" << iterSlot << "];\n";
    // The else clause runs after the loop, outside it (see generateWhile):
    // emitted after the C++ loop, when the iterator was exhausted.
    if (n->orelse) *out_ << "        bool __for_done_" << id << " = false;\n";
    if (nElse > 0) *out_ << "        if (!" << inElse << ")\n";
    *out_ << "        while (true) {\n";
    emitLoopSafepoint();
    if (resumable) *out_ << "            if (__resume == 0) {\n";
    // env->next returns nullptr when the iterator is exhausted (None is an
    // ordinary item) or raised; an exception it raised stays pending.
    *out_ << "            auto* __val_" << id << " = env->next(__iter_" << id << ");\n";
    *out_ << "            if (!__val_" << id << ") {\n";
    if (n->orelse) *out_ << "                if (!env->hasPendingException()) __for_done_" << id << " = true;\n";
    *out_ << "                break;\n";
    *out_ << "            }\n";

    // Bind target — route NameNode through generateAssignToTarget so that
    // the for-loop variable lands in the correct scope.  Inside a function
    // body collectLocals() already added the name to orderedLocalVars_,
    // and generateAssignToTarget will emit `local_<id> = __val_<id>`;
    // at module scope (orderedLocalVars_ empty) it falls through to
    // `env->storeName(...)`.  Hard-coding storeName here was the parallel
    // of the WhileNode scope bug fixed in commit d7dfec38 for the
    // bytecode compiler: `for i in range(N): s += i` then wrote `i` to
    // module globals while `s += i` read `local_i` (still PROTO_NONE),
    // producing "Object is not an integer type" inside protopyc-compiled
    // functions.
    if (auto* nameNode = dynamic_cast<NameNode*>(n->target.get())) {
        std::ostringstream valStream;
        valStream << "__val_" << id;
        *out_ << "            ";
        if (!generateAssignToTarget(nameNode, valStream.str())) return false;
        *out_ << ";\n";
    } else if (auto* tupleNode = dynamic_cast<TupleLiteralNode*>(n->target.get())) {
        // Handle unpacking in for target: for x, y in ...
        std::ostringstream targetStream;
        targetStream << "__val_" << id;
        if (!generateAssignToTarget(tupleNode, targetStream.str())) return false;
        *out_ << ";\n";
    }
    if (resumable) *out_ << "            }\n";

    if (!generateBody(n->body.get())) return false;
    *out_ << "        }\n";
    popAnchor();
    if (n->orelse) {
        *out_ << "        if (__for_done_" << id << (nElse > 0 ? " || " + inElse : std::string()) << ") {\n";
        if (!generateBody(n->orelse.get())) return false;
        *out_ << ";\n        }\n";
    }
    *out_ << "    }";
    return true;
}

bool CppGenerator::generateTry(TryNode* n) {
    // Exceptions in generated code are pending in the PythonEnvironment.
    // The body runs; if it left an exception pending, the first handler
    // that matches takes it, and an unmatched one stays pending.  The else
    // clause runs when the body raised nothing.  The finally clause always
    // runs, with the exception that is propagating set aside, and that
    // exception is pending again afterwards unless the clause raised its own.
    //
    // In a generator a statement that raises leaves its statement list
    // (__PyUnwind), so each clause is a C++ try that stops the unwinding
    // here.  A resumption re-enters the clause that holds the suspended
    // yield; the caught exception and the one the finally clause set aside
    // are in anchor slots, which are restored with the others.
    const int base = stateCount_;
    std::vector<int> nHandler;
    int nHandlers = 0;
    const int nBody = inStateMachine_ ? countYields(n->body.get()) : 0;
    for (auto& h : n->handlers) {
        nHandler.push_back(inStateMachine_ ? countYields(h.body.get()) : 0);
        nHandlers += nHandler.back();
    }
    const int nElse = inStateMachine_ ? countYields(n->orelse.get()) : 0;
    const int nFinally = inStateMachine_ ? countYields(n->finalbody.get()) : 0;
    const bool resumable = nBody + nHandlers + nElse + nFinally > 0;
    const std::string resumeIdle = resumable ? "__resume == 0" : "true";

    auto emitClause = [&](ASTNode* clause, int first, int count) -> bool {
        if (resumable) *out_ << "if (__resume == 0 || " << resumeIn(first, count) << ") ";
        *out_ << "{\n";
        if (inStateMachine_) *out_ << "try {\n";
        if (!generateBody(clause)) return false;
        *out_ << ";\n";
        if (inStateMachine_) *out_ << "} catch (const __PyUnwind&) {}\n";
        *out_ << "}\n";
        return true;
    };

    *out_ << "{\n";
    if (!emitClause(n->body.get(), base + 1, nBody)) return false;
    // The caught exception is used across the handlers' bodies, which may
    // loop: it is kept in an anchor slot.
    const int excSlot = pushAnchor();
    int first = base + nBody + 1;
    if (!n->handlers.empty()) {
        *out_ << "        if (" << (nHandlers > 0 ? resumeIn(first, nHandlers) + " || " : std::string())
              << "(" << resumeIdle << " && env->hasPendingException())) {\n";
        *out_ << "            if (" << resumeIdle << ") __slot[" << excSlot << "] = env->takePendingException();\n";
        *out_ << "            const proto::ProtoObject* __exc = __slot[" << excSlot << "];\n";
        *out_ << "            bool __handled = false;\n";
        for (size_t i = 0; i < n->handlers.size(); ++i) {
            auto& h = n->handlers[i];
            *out_ << "            if (" << (nHandler[i] > 0 ? resumeIn(first, nHandler[i]) + " || " : std::string())
                  << "(" << resumeIdle << " && !__handled";
            if (h.type) {
                *out_ << " && env->isException(__exc, ";
                if (!generateNode(h.type.get())) return false;
                *out_ << ")";
            }
            *out_ << ")) {\n";
            *out_ << "                __handled = true;\n";
            if (!h.name.empty()) {
                *out_ << "                if (" << resumeIdle << ") ";
                emitAssignName(h.name, "__exc");
                *out_ << ";\n";
            }
            if (inStateMachine_) *out_ << "try {\n";
            if (!generateBody(h.body.get())) return false;
            *out_ << ";\n";
            if (inStateMachine_) *out_ << "} catch (const __PyUnwind&) {}\n";
            *out_ << "            }\n";
            first += nHandler[i];
        }
        *out_ << "            if (!__handled) env->raiseException(__exc);\n";
        *out_ << "        }";
        if (n->orelse) *out_ << " else ";
        else *out_ << "\n";
    }
    if (n->orelse) {
        // Runs only when the body raised nothing (a resumption inside it
        // has been through the body already).
        *out_ << "if (" << (nElse > 0 ? resumeIn(first, nElse) + " || " : std::string())
              << "(" << resumeIdle << " && !env->hasPendingException())) {\n";
        if (inStateMachine_) *out_ << "try {\n";
        if (!generateBody(n->orelse.get())) return false;
        *out_ << ";\n";
        if (inStateMachine_) *out_ << "} catch (const __PyUnwind&) {}\n";
        *out_ << "}\n";
        first += nElse;
    }
    popAnchor();
    if (n->finalbody) {
        const int pendingSlot = pushAnchor();
        *out_ << "        // finally\n";
        *out_ << "        if (" << resumeIdle << ") __slot[" << pendingSlot
              << "] = env->hasPendingException() ? env->takePendingException() : nullptr;\n";
        if (!emitClause(n->finalbody.get(), first, nFinally)) return false;
        *out_ << "        if (__slot[" << pendingSlot << "] && !env->hasPendingException()) env->raiseException(__slot["
              << pendingSlot << "]);\n";
        popAnchor();
    }
    *out_ << "    }";
    return true;
}

bool CppGenerator::generateRaise(RaiseNode* n) {
    if (n->exc) {
        *out_ << "env->raiseException(";
        if (!generateNode(n->exc.get())) return false;
        *out_ << ")";
    } else {
        *out_ << "/* re-raise unsupported */";
    }
    return true;
}

bool CppGenerator::generateWith(WithNode* n) {
    // In a generator, a resumption inside the body does not evaluate the
    // context expressions again.
    const bool resumable = inStateMachine_ && countYields(n->body.get()) > 0;
    *out_ << "/* with statement simplified */\n";
    if (resumable) *out_ << "if (__resume == 0) {\n";
    for (auto& item : n->items) {
        if (!generateNode(item.context_expr.get())) return false;
        *out_ << ";\n";
    }
    if (resumable) *out_ << "}\n";
    if (!generateBody(n->body.get())) return false;
    return true;
}

bool CppGenerator::generateImport(ImportNode* n) {
    *out_ << "{\n";
    *out_ << "        const proto::ProtoObject* __mod = env->importModule(\"" << n->moduleName << "\");\n";
    if (n->isAs) {
        *out_ << "        env->storeName(\"" << n->alias << "\", __mod);\n";
    } else {
        size_t dot = n->moduleName.find('.');
        std::string root = (dot == std::string::npos) ? n->moduleName : n->moduleName.substr(0, dot);
        *out_ << "        env->storeName(\"" << root << "\", __mod);\n";
    }
    *out_ << "    }";
    return true;
}

bool CppGenerator::generateImportFrom(ImportFromNode* n) {
    *out_ << "{\n";
    *out_ << "        const proto::ProtoObject* __mod = env->importModule(\"" << n->moduleName << "\", " << n->level << ", {";
    for (size_t i = 0; i < n->names.size(); ++i) {
        *out_ << "\"" << n->names[i].first << "\"";
        if (i < n->names.size() - 1) *out_ << ", ";
    }
    *out_ << "});\n";
    if (n->names.size() == 1 && n->names[0].first == "*") {
        *out_ << "        env->importStar(__mod);\n";
    } else {
        for (auto& p : n->names) {
            std::string alias = p.second.empty() ? p.first : p.second;
            *out_ << "        env->storeName(\"" << alias << "\", env->getAttr(__mod, \"" << p.first << "\"));\n";
        }
    }
    *out_ << "    }";
    return true;
}

bool CppGenerator::generateLambda(LambdaNode* n) {
    if (!n) return false;
    // A lambda is compiled like a def (see generateFunctionInternal): it runs
    // in a context of its own, in the namespace of its module, with its
    // parameters in slots and the enclosing functions' locals it uses
    // captured in its function storage.  Its body is generated first because
    // the slot count is known only afterwards.
    std::vector<std::string> lambdaLocals = n->parameters;
    if (!n->vararg.empty()) lambdaLocals.push_back(n->vararg);
    if (!n->kwarg.empty()) lambdaLocals.push_back(n->kwarg);
    std::vector<std::string> lambdaFreeVars;
    {
        std::unordered_set<std::string> refs;
        collectNameRefs(n->body.get(), refs);
        for (const auto& nm : refs) {
            if (std::find(lambdaLocals.begin(), lambdaLocals.end(), nm) != lambdaLocals.end()) continue;
            for (auto it = enclosingLocals_.rbegin(); it != enclosingLocals_.rend(); ++it) {
                if (it->count(nm)) { lambdaFreeVars.push_back(nm); break; }
            }
        }
        std::sort(lambdaFreeVars.begin(), lambdaFreeVars.end());
    }

    const auto oldOrderedLocals = orderedLocalVars_;
    const auto oldLocalVars = localVars_;
    const bool oldInStateMachine = inStateMachine_;
    const bool oldInClassBody = inClassBody_;
    const std::string oldCurrentFuncName = currentFuncName_;
    const std::string oldCurrentFuncCppName = currentFuncCppName_;
    orderedLocalVars_ = lambdaLocals;
    orderedLocalVars_.insert(orderedLocalVars_.end(), lambdaFreeVars.begin(), lambdaFreeVars.end());
    localVars_ = std::unordered_set<std::string>(orderedLocalVars_.begin(), orderedLocalVars_.end());
    inStateMachine_ = false;
    inClassBody_ = false;
    currentFuncName_.clear();
    currentFuncCppName_.clear();
    auto restore = [&]() {
        orderedLocalVars_ = oldOrderedLocals;
        localVars_ = oldLocalVars;
        inStateMachine_ = oldInStateMachine;
        inClassBody_ = oldInClassBody;
        currentFuncName_ = oldCurrentFuncName;
        currentFuncCppName_ = oldCurrentFuncCppName;
    };

    const SlotFrame lambdaSlots = enterSlotFrame(static_cast<int>(orderedLocalVars_.size()));
    std::ostream* outerOut = out_;
    std::ostringstream lambdaBody;
    out_ = &lambdaBody;
    bool ok = true;
    for (size_t i = 0; i < orderedLocalVars_.size(); ++i) {
        *out_ << "        const proto::ProtoObject*& local_" << orderedLocalVars_[i]
              << " = __slot[" << i << "];\n";
    }
    *out_ << "        // Bind parameters\n";
    *out_ << "        proto::proto_ulong nPos = args ? args->getSize(ctx) : 0;\n";
    {
        const size_t numDefaultsLam = n->defaults.size();
        const size_t defaultStartLam = n->parameters.size() > numDefaultsLam
                                            ? n->parameters.size() - numDefaultsLam
                                            : 0;
        for (size_t i = 0; i < n->parameters.size() && ok; ++i) {
            *out_ << "        local_" << n->parameters[i]
                  << " = (nPos > " << i << ") ? args->getAt(ctx, " << i << ") : ";
            if (i >= defaultStartLam) {
                ok = generateNode(n->defaults[i - defaultStartLam].get());
            } else {
                *out_ << "PROTO_NONE";
            }
            *out_ << ";\n";
        }
    }

    if (ok && !n->vararg.empty()) {
        *out_ << "        const proto::ProtoList* vaList = ctx->newList();\n";
        *out_ << "        for (proto::proto_ulong i = " << n->parameters.size() << "; i < nPos; ++i) vaList = vaList->appendLast(ctx, args->getAt(ctx, i));\n";
        *out_ << "        auto* vaTup = static_cast<const proto::ProtoObject*>(ctx->newObject(true));\n";
        *out_ << "        vaTup->setAttribute(ctx, env->getDataString(), (const proto::ProtoObject*)ctx->newTupleFromList(vaList));\n";
        *out_ << "        if (env->getTuplePrototype()) vaTup->addParent(ctx, env->getTuplePrototype());\n";
        *out_ << "        local_" << n->vararg << " = vaTup;\n";
    }

    if (ok && !n->kwarg.empty()) {
        *out_ << "        auto* kaDict = static_cast<const proto::ProtoObject*>(ctx->newObject(true));\n";
        *out_ << "        kaDict->setAttribute(ctx, env->getDataString(), (const proto::ProtoObject*)(kwargs ? kwargs : ctx->newSparseList()));\n";
        *out_ << "        const proto::ProtoTuple* kwNames = env->getCurrentKwNames();\n";
        *out_ << "        if (kwNames) kaDict->setAttribute(ctx, env->getKeysString(), (const proto::ProtoObject*)kwNames->asList(ctx));\n";
        *out_ << "        if (env->getDictPrototype()) kaDict->addParent(ctx, env->getDictPrototype());\n";
        *out_ << "        local_" << n->kwarg << " = kaDict;\n";
    }
    for (const auto& fv : lambdaFreeVars) {
        *out_ << "        local_" << fv << " = __freeVar(ctx, __fs, \"" << fv << "\");\n";
    }

    if (ok) {
        *out_ << "        const proto::ProtoObject* __result = ";
        enclosingLocals_.push_back(localVars_);
        ok = generateNode(n->body.get());
        enclosingLocals_.pop_back();
        *out_ << ";\n";
        *out_ << "        protoPython::promote(ctx, __result);\n";
        *out_ << "        return __result;\n";
    }
    out_ = outerOut;
    restore();
    if (!ok) {
        leaveSlotFrame(lambdaSlots);
        return false;
    }

    // The lambda's slot count is known now; its storage is built in the
    // enclosing scope (the free variables are the enclosing locals).
    std::ostringstream lambdaFn;
    lambdaFn << "[](proto::ProtoContext* __callerCtx, const proto::ProtoObject* self, const proto::ParentLink* pl, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) -> const proto::ProtoObject* {\n";
    lambdaFn << openContextScope("__callerCtx");
    lambdaFn << "        auto* env = protoPython::PythonEnvironment::get(ctx);\n";
    lambdaFn << "        const proto::ProtoObject* __fs = __functionStorageOf(ctx, self, args);\n";
    lambdaFn << "        protoPython::ModuleNamespaceScope __ns(__moduleNamespaceOf(ctx, __fs));\n";
    lambdaFn << lambdaBody.str();
    lambdaFn << "    }";
    leaveSlotFrame(lambdaSlots);

    *out_ << "([&]() -> const proto::ProtoObject* {\n";
    *out_ << "        const proto::ProtoObject* __fs = __newFunctionStorage(ctx);\n";
    for (const auto& fv : lambdaFreeVars) {
        *out_ << "        __fs = __withFreeVar(ctx, __fs, \"" << fv << "\", local_" << fv << ");\n";
    }
    *out_ << "        return ctx->fromMethod(const_cast<proto::ProtoObject*>(__fs), " << lambdaFn.str() << ");\n";
    *out_ << "    })()";
    return true;
}

bool CppGenerator::generateJoinedStr(JoinedStrNode* n) {
    if (!n) return false;
    *out_ << "env->buildString({";
    for (size_t i = 0; i < n->values.size(); ++i) {
        if (!generateNode(n->values[i].get())) return false;
        if (i < n->values.size() - 1) *out_ << ", ";
    }
    *out_ << "})";
    return true;
}

bool CppGenerator::generateFormattedValue(FormattedValueNode* n) {
    if (!n) return false;
    return generateNode(n->value.get());
}

bool CppGenerator::generateSlice(SliceNode* n) {
    *out_ << "env->buildSlice(";
    if (n->start) { if (!generateNode(n->start.get())) return false; } else { *out_ << "PROTO_NONE"; }
    *out_ << ", ";
    if (n->stop) { if (!generateNode(n->stop.get())) return false; } else { *out_ << "PROTO_NONE"; }
    *out_ << ", ";
    if (n->step) { if (!generateNode(n->step.get())) return false; } else { *out_ << "PROTO_NONE"; }
    *out_ << ")";
    return true;
}

bool CppGenerator::generateDeleteNode(DeleteNode* n) {
    for (auto& target : n->targets) {
        if (auto* nm = dynamic_cast<NameNode*>(target.get())) {
            *out_ << "env->delName(\"" << nm->id << "\")";
        } else if (auto* att = dynamic_cast<AttributeNode*>(target.get())) {
            *out_ << "env->delAttr(";
            if (!generateNode(att->value.get())) return false;
            *out_ << ", \"" << att->attr << "\")";
        } else if (auto* sub = dynamic_cast<SubscriptNode*>(target.get())) {
            *out_ << "env->delItem(";
            if (!generateNode(sub->value.get())) return false;
            *out_ << ", ";
            if (!generateNode(sub->index.get())) return false;
            *out_ << ")";
        }
        *out_ << ";\n";
    }
    return true;
}

bool CppGenerator::generateAssert(AssertNode* n) {
    *out_ << "if (!env->isTrue(";
    if (!generateNode(n->test.get())) return false;
    *out_ << ")) {\n";
    *out_ << "        env->raiseAssertionError(ctx, ";
    if (n->msg) { if (!generateNode(n->msg.get())) return false; } else { *out_ << "nullptr"; }
    *out_ << ");\n";
    *out_ << "    }";
    return true;
}

bool CppGenerator::generateYield(YieldNode* n) {
    (void)n;
    if (!inStateMachine_) {
        *out_ << "env->raiseException(ctx->fromUTF8String(\"SyntaxError: 'yield' outside function\"))";
        return true;
    }
    // Statement-level yields are lowered by emitYieldStatement.
    std::cerr << "protopyc: line " << n->line
              << ": a yield is supported as a statement or as the value of an assignment, "
                 "not inside another expression\n";
    return false;
}

bool CppGenerator::generateAwait(AwaitNode* n) {
    if (!inStateMachine_) {
        *out_ << "env->raiseException(ctx->fromUTF8String(\"SyntaxError: 'await' outside function\"))";
        return true;
    }
    std::cerr << "protopyc: line " << n->line
              << ": an await is supported as a statement or as the value of an assignment, "
                 "not inside another expression\n";
    return false;
}

// ---------------------------------------------------------------------------
// Generator bodies.
//
// A generator's continuation re-enters its body at each resumption; the
// yields are numbered 1, 2, ... in source order and `__resume` holds the one
// to resume at (0 when running normally).  Only statement-level yields are
// supported: `yield v`, `yield from it`, `await v` and their use as the value
// of an assignment.  The yields of a statement are therefore numbered
// consecutively, and a statement list re-entered at yield k skips every
// statement whose range does not hold k.  Compound statements re-enter the
// clause holding k without evaluating their tests or creating their iterators
// again (generateIf, generateWhile, generateFor, generateTry, generateWith).
// ---------------------------------------------------------------------------

int CppGenerator::countYields(ASTNode* node) const {
    if (!node) return 0;
    if (dynamic_cast<YieldNode*>(node) || dynamic_cast<AwaitNode*>(node)) return 1;
    if (auto* n = dynamic_cast<AssignNode*>(node)) {
        return (n->targets.size() == 1 &&
                (dynamic_cast<YieldNode*>(n->value.get()) || dynamic_cast<AwaitNode*>(n->value.get())))
                   ? 1 : 0;
    }
    if (auto* n = dynamic_cast<SuiteNode*>(node)) {
        int c = 0;
        for (auto& s : n->statements) c += countYields(s.get());
        return c;
    }
    if (auto* n = dynamic_cast<IfNode*>(node)) return countYields(n->body.get()) + countYields(n->orelse.get());
    if (auto* n = dynamic_cast<WhileNode*>(node)) return countYields(n->body.get()) + countYields(n->orelse.get());
    if (auto* n = dynamic_cast<ForNode*>(node)) return countYields(n->body.get()) + countYields(n->orelse.get());
    if (auto* n = dynamic_cast<WithNode*>(node)) return countYields(n->body.get());
    if (auto* n = dynamic_cast<TryNode*>(node)) {
        int c = countYields(n->body.get()) + countYields(n->orelse.get()) + countYields(n->finalbody.get());
        for (auto& h : n->handlers) c += countYields(h.body.get());
        return c;
    }
    return 0;
}

std::string CppGenerator::resumeIn(int first, int count) const {
    if (count <= 0) return "false";
    std::ostringstream o;
    o << "(__resume >= " << first << " && __resume <= " << (first + count - 1) << ")";
    return o.str();
}

bool CppGenerator::generateBody(ASTNode* body) {
    // In a generator every statement list, including a clause that is a
    // single statement, is emitted by emitStatementList.
    if (inStateMachine_ && body && !dynamic_cast<SuiteNode*>(body)) {
        return emitStatementList({body});
    }
    return generateNode(body);
}

bool CppGenerator::emitStatementList(const std::vector<ASTNode*>& stmts) {
    int total = 0;
    for (auto* s : stmts) total += countYields(s);
    for (auto* s : stmts) {
        if (s->line > 0) emitLineDirective(s->line, "source.py");
        beginStatement();
        const int count = countYields(s);
        if (total > 0) {
            *out_ << "if (__resume == 0";
            if (count > 0) *out_ << " || " << resumeIn(stateCount_ + 1, count);
            *out_ << ") {\n";
        }
        bool ok;
        if (dynamic_cast<YieldNode*>(s) || dynamic_cast<AwaitNode*>(s)) {
            ok = emitYieldStatement(s, nullptr);
        } else if (auto* a = dynamic_cast<AssignNode*>(s); a && count == 1) {
            ok = emitYieldStatement(a->value.get(), a->targets[0].get());
        } else {
            ok = generateNode(s);
        }
        if (!ok) return false;
        *out_ << ";\n";
        if (total > 0) *out_ << "}\n";
        // A statement that raised leaves the statement list.
        *out_ << "if (env->hasPendingException()) throw __PyUnwind{};\n";
    }
    return true;
}

bool CppGenerator::emitYieldStatement(ASTNode* node, ASTNode* target) {
    auto* y = dynamic_cast<YieldNode*>(node);
    auto* aw = dynamic_cast<AwaitNode*>(node);
    ASTNode* value = y ? y->value.get() : aw->value.get();
    const int k = ++stateCount_;
    // Suspending saves every slot in gi_locals and the yield's number in
    // gi_pc, and returns the value from the continuation.
    auto emitSuspend = [&](const std::string& slotExpr) {
        *out_ << "    gen->setAttribute(ctx, env->getGiLocalsString(), ctx->newList(__nslots, __slot)->asObject(ctx));\n";
        *out_ << "    gen->setAttribute(ctx, env->getGiPCString(), ctx->fromInteger(" << k << "));\n";
        *out_ << "    return " << slotExpr << ";\n";
    };

    if (y && y->isFrom) {
        // Delegation, as the interpreter's OP_YIELD_FROM does it: the
        // sub-iterator is in an anchor slot; a resumption sends the value
        // sent to it (with send(), or with __next__ when it has no send()
        // and the value is None) and forwards throw() and close() to its
        // throw(); its StopIteration value is the result.
        const int itSlot = pushAnchor();
        const int valSlot = allocTempSlot();
        *out_ << "{\n";
        *out_ << "if (__resume == 0) {\n";
        *out_ << "    __slot[" << itSlot << "] = env->iter(";
        if (!generateNode(value)) return false;
        *out_ << ");\n";
        *out_ << "    if (env->hasPendingException()) throw __PyUnwind{};\n";
        *out_ << "}\n";
        *out_ << "const proto::ProtoObject* __yf_res_" << k << " = PROTO_NONE;\n";
        *out_ << "while (true) {\n";
        *out_ << "    proto::ProtoObject* __sub = const_cast<proto::ProtoObject*>(__slot[" << itSlot << "]);\n";
        *out_ << "    const proto::ProtoObject* __yf_in = PROTO_NONE;\n";
        *out_ << "    bool __advance = true;\n";
        *out_ << "    if (__resume != 0) {\n";
        *out_ << "        __resume = 0;\n";
        *out_ << "        __yf_in = __sent;\n";
        *out_ << "        if (__throw) {\n";
        *out_ << "            const proto::ProtoObject* __t = __throw;\n";
        *out_ << "            __throw = nullptr;\n";
        *out_ << "            const proto::ProtoObject* __tm = __sub->getAttribute(ctx, protoPython::PythonEnvironment::getInternedString(ctx, \"throw\"));\n";
        *out_ << "            if (!__tm || __tm == PROTO_NONE || !__tm->asMethod(ctx)) {\n";
        *out_ << "                env->raiseException(__t);\n";
        *out_ << "                throw __PyUnwind{};\n";
        *out_ << "            }\n";
        *out_ << "            __slot[" << valSlot << "] = __tm->asMethod(ctx)(ctx, __sub, nullptr, ctx->newList()->appendLast(ctx, __t), nullptr);\n";
        *out_ << "            __advance = false;\n";
        *out_ << "        }\n";
        *out_ << "    }\n";
        *out_ << "    if (__advance) {\n";
        *out_ << "        const proto::ProtoObject* __sm = __sub->getAttribute(ctx, env->getSendString());\n";
        *out_ << "        if (__sm && __sm != PROTO_NONE) {\n";
        *out_ << "            __slot[" << valSlot << "] = __sub->call(ctx, nullptr, env->getSendString(), __sub, ctx->newList()->appendLast(ctx, __yf_in), nullptr);\n";
        *out_ << "        } else if (__yf_in != PROTO_NONE) {\n";
        *out_ << "            env->raiseTypeError(ctx, \"can't send non-None value to a plain iterator\");\n";
        *out_ << "            throw __PyUnwind{};\n";
        *out_ << "        } else {\n";
        *out_ << "            __slot[" << valSlot << "] = __sub->call(ctx, nullptr, env->getNextString(), __sub, ctx->newList(), nullptr);\n";
        *out_ << "        }\n";
        *out_ << "    }\n";
        *out_ << "    if (env->hasPendingException()) {\n";
        *out_ << "        const proto::ProtoObject* __e = env->peekPendingException();\n";
        *out_ << "        if (!env->isStopIteration(ctx, __e)) throw __PyUnwind{};\n";
        *out_ << "        __yf_res_" << k << " = env->getStopIterationValue(ctx, __e);\n";
        *out_ << "        env->clearPendingException();\n";
        *out_ << "        if (!__yf_res_" << k << ") __yf_res_" << k << " = PROTO_NONE;\n";
        *out_ << "        break;\n";
        *out_ << "    }\n";
        *out_ << "    if (!__slot[" << valSlot << "]) break;\n";
        emitSuspend("__slot[" + std::to_string(valSlot) + "]");
        *out_ << "}\n";
        if (target) {
            if (!generateAssignToTarget(target, "__yf_res_" + std::to_string(k))) return false;
            *out_ << ";\n";
        }
        *out_ << "}";
        popAnchor();
        return true;
    }

    const int valSlot = allocTempSlot();
    *out_ << "{\n";
    *out_ << "if (__resume == 0) {\n";
    *out_ << "    __slot[" << valSlot << "] = ";
    if (value) {
        if (!generateNode(value)) return false;
    } else {
        *out_ << "PROTO_NONE";
    }
    *out_ << ";\n";
    *out_ << "    if (env->hasPendingException()) throw __PyUnwind{};\n";
    emitSuspend("__slot[" + std::to_string(valSlot) + "]");
    *out_ << "}\n";
    // Resumed here: throw() and close() raise their exception at the yield;
    // otherwise the yield's value is the value sent (None for next()).
    *out_ << "__resume = 0;\n";
    *out_ << "if (__throw) {\n";
    *out_ << "    const proto::ProtoObject* __t = __throw;\n";
    *out_ << "    __throw = nullptr;\n";
    *out_ << "    env->raiseException(__t);\n";
    *out_ << "    throw __PyUnwind{};\n";
    *out_ << "}\n";
    if (target) {
        if (!generateAssignToTarget(target, "__sent")) return false;
        *out_ << ";\n";
    }
    *out_ << "}";
    return true;
}

void CppGenerator::collectLocals(ASTNode* node, std::unordered_set<std::string>& locals) {
    if (!node) return;
    if (auto* n = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : n->targets) {
            if (auto* name = dynamic_cast<NameNode*>(t.get())) locals.insert(name->id);
            else collectLocals(t.get(), locals);
        }
    }
 else if (auto* n = dynamic_cast<ForNode*>(node)) {
        if (auto* name = dynamic_cast<NameNode*>(n->target.get())) locals.insert(name->id);
        collectLocals(n->body.get(), locals);
    } else if (auto* n = dynamic_cast<SuiteNode*>(node)) {
        for (auto& stmt : n->statements) collectLocals(stmt.get(), locals);
    } else if (auto* n = dynamic_cast<IfNode*>(node)) {
        collectLocals(n->body.get(), locals);
        if (n->orelse) collectLocals(n->orelse.get(), locals);
    } else if (auto* n = dynamic_cast<WhileNode*>(node)) {
        collectLocals(n->body.get(), locals);
        if (n->orelse) collectLocals(n->orelse.get(), locals);
    } else if (auto* n = dynamic_cast<TryNode*>(node)) {
        collectLocals(n->body.get(), locals);
        for (auto& h : n->handlers) {
            if (!h.name.empty()) locals.insert(h.name);
            collectLocals(h.body.get(), locals);
        }
        if (n->orelse) collectLocals(n->orelse.get(), locals);
        if (n->finalbody) collectLocals(n->finalbody.get(), locals);
    } else if (auto* n = dynamic_cast<WithNode*>(node)) {
        collectLocals(n->body.get(), locals);
    } else if (auto* n = dynamic_cast<FunctionDefNode*>(node)) {
        // A def or class statement binds its name in the enclosing scope.
        locals.insert(n->name);
    } else if (auto* n = dynamic_cast<AsyncFunctionDefNode*>(node)) {
        locals.insert(n->name);
    } else if (auto* n = dynamic_cast<ClassDefNode*>(node)) {
        locals.insert(n->name);
    }
}

void CppGenerator::collectNameRefs(ASTNode* node, std::unordered_set<std::string>& refs) {
    if (!node) return;
    if (auto* n = dynamic_cast<NameNode*>(node)) {
        refs.insert(n->id);
        return;
    }
    // Nested functions/classes/lambdas create their own scope.  Walking
    // into their bodies here would conflate inner-scope references with
    // the outer function's free variables.  We treat them as opaque.
    if (dynamic_cast<FunctionDefNode*>(node) ||
        dynamic_cast<AsyncFunctionDefNode*>(node) ||
        dynamic_cast<ClassDefNode*>(node) ||
        dynamic_cast<LambdaNode*>(node)) {
        return;
    }
    if (auto* n = dynamic_cast<SuiteNode*>(node)) {
        for (auto& s : n->statements) collectNameRefs(s.get(), refs);
    } else if (auto* n = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : n->targets) collectNameRefs(t.get(), refs);
        collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<AugAssignNode*>(node)) {
        collectNameRefs(n->target.get(), refs);
        collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<AnnAssignNode*>(node)) {
        collectNameRefs(n->target.get(), refs);
        if (n->value) collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<IfNode*>(node)) {
        collectNameRefs(n->test.get(), refs);
        collectNameRefs(n->body.get(), refs);
        if (n->orelse) collectNameRefs(n->orelse.get(), refs);
    } else if (auto* n = dynamic_cast<WhileNode*>(node)) {
        collectNameRefs(n->test.get(), refs);
        collectNameRefs(n->body.get(), refs);
        if (n->orelse) collectNameRefs(n->orelse.get(), refs);
    } else if (auto* n = dynamic_cast<ForNode*>(node)) {
        collectNameRefs(n->target.get(), refs);
        collectNameRefs(n->iter.get(), refs);
        collectNameRefs(n->body.get(), refs);
        if (n->orelse) collectNameRefs(n->orelse.get(), refs);
    } else if (auto* n = dynamic_cast<TryNode*>(node)) {
        collectNameRefs(n->body.get(), refs);
        for (auto& h : n->handlers) {
            if (h.type) collectNameRefs(h.type.get(), refs);
            collectNameRefs(h.body.get(), refs);
        }
        if (n->orelse) collectNameRefs(n->orelse.get(), refs);
        if (n->finalbody) collectNameRefs(n->finalbody.get(), refs);
    } else if (auto* n = dynamic_cast<WithNode*>(node)) {
        for (auto& it : n->items) {
            collectNameRefs(it.context_expr.get(), refs);
            if (it.optional_vars) collectNameRefs(it.optional_vars.get(), refs);
        }
        collectNameRefs(n->body.get(), refs);
    } else if (auto* n = dynamic_cast<ReturnNode*>(node)) {
        if (n->value) collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<YieldNode*>(node)) {
        if (n->value) collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<AwaitNode*>(node)) {
        if (n->value) collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<CallNode*>(node)) {
        collectNameRefs(n->func.get(), refs);
        for (auto& a : n->args) collectNameRefs(a.get(), refs);
        for (auto& kw : n->keywords) collectNameRefs(kw.second.get(), refs);
    } else if (auto* n = dynamic_cast<BinOpNode*>(node)) {
        collectNameRefs(n->left.get(), refs);
        collectNameRefs(n->right.get(), refs);
    } else if (auto* n = dynamic_cast<UnaryOpNode*>(node)) {
        collectNameRefs(n->operand.get(), refs);
    } else if (auto* n = dynamic_cast<AttributeNode*>(node)) {
        collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<SubscriptNode*>(node)) {
        collectNameRefs(n->value.get(), refs);
        collectNameRefs(n->index.get(), refs);
    } else if (auto* n = dynamic_cast<SliceNode*>(node)) {
        if (n->start) collectNameRefs(n->start.get(), refs);
        if (n->stop) collectNameRefs(n->stop.get(), refs);
        if (n->step) collectNameRefs(n->step.get(), refs);
    } else if (auto* n = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : n->elements) collectNameRefs(e.get(), refs);
    } else if (auto* n = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : n->elements) collectNameRefs(e.get(), refs);
    } else if (auto* n = dynamic_cast<SetLiteralNode*>(node)) {
        for (auto& e : n->elements) collectNameRefs(e.get(), refs);
    } else if (auto* n = dynamic_cast<DictLiteralNode*>(node)) {
        for (auto& k : n->keys) if (k) collectNameRefs(k.get(), refs);
        for (auto& v : n->values) collectNameRefs(v.get(), refs);
    } else if (auto* n = dynamic_cast<JoinedStrNode*>(node)) {
        for (auto& v : n->values) collectNameRefs(v.get(), refs);
    } else if (auto* n = dynamic_cast<FormattedValueNode*>(node)) {
        collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<StarredNode*>(node)) {
        collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<NamedExprNode*>(node)) {
        collectNameRefs(n->target.get(), refs);
        collectNameRefs(n->value.get(), refs);
    } else if (auto* n = dynamic_cast<ConditionalExprNode*>(node)) {
        collectNameRefs(n->test.get(), refs);
        collectNameRefs(n->body.get(), refs);
        collectNameRefs(n->orelse.get(), refs);
    } else if (auto* n = dynamic_cast<ListCompNode*>(node)) {
        collectNameRefs(n->elt.get(), refs);
        for (auto& c : n->generators) {
            collectNameRefs(c.iter.get(), refs);
            for (auto& cond : c.ifs) collectNameRefs(cond.get(), refs);
        }
    } else if (auto* n = dynamic_cast<SetCompNode*>(node)) {
        collectNameRefs(n->elt.get(), refs);
        for (auto& c : n->generators) {
            collectNameRefs(c.iter.get(), refs);
            for (auto& cond : c.ifs) collectNameRefs(cond.get(), refs);
        }
    } else if (auto* n = dynamic_cast<GeneratorExpNode*>(node)) {
        collectNameRefs(n->elt.get(), refs);
        for (auto& c : n->generators) {
            collectNameRefs(c.iter.get(), refs);
            for (auto& cond : c.ifs) collectNameRefs(cond.get(), refs);
        }
    } else if (auto* n = dynamic_cast<DictCompNode*>(node)) {
        collectNameRefs(n->key.get(), refs);
        collectNameRefs(n->value.get(), refs);
        for (auto& c : n->generators) {
            collectNameRefs(c.iter.get(), refs);
            for (auto& cond : c.ifs) collectNameRefs(cond.get(), refs);
        }
    } else if (auto* n = dynamic_cast<RaiseNode*>(node)) {
        if (n->exc) collectNameRefs(n->exc.get(), refs);
        if (n->cause) collectNameRefs(n->cause.get(), refs);
    } else if (auto* n = dynamic_cast<DeleteNode*>(node)) {
        for (auto& t : n->targets) collectNameRefs(t.get(), refs);
    } else if (auto* n = dynamic_cast<AssertNode*>(node)) {
        collectNameRefs(n->test.get(), refs);
        if (n->msg) collectNameRefs(n->msg.get(), refs);
    }
}

bool CppGenerator::containsYieldOrAwait(ASTNode* node) {
    if (!node) return false;
    if (dynamic_cast<YieldNode*>(node)) return true;
    if (dynamic_cast<AwaitNode*>(node)) return true;
    
    if (auto* n = dynamic_cast<SuiteNode*>(node)) {
        for (auto& stmt : n->statements) if (containsYieldOrAwait(stmt.get())) return true;
    } else if (auto* n = dynamic_cast<IfNode*>(node)) {
        if (containsYieldOrAwait(n->test.get())) return true;
        if (containsYieldOrAwait(n->body.get())) return true;
        if (n->orelse && containsYieldOrAwait(n->orelse.get())) return true;
    } else if (auto* n = dynamic_cast<WhileNode*>(node)) {
        if (containsYieldOrAwait(n->test.get())) return true;
        if (containsYieldOrAwait(n->body.get())) return true;
        if (n->orelse && containsYieldOrAwait(n->orelse.get())) return true;
    } else if (auto* n = dynamic_cast<ForNode*>(node)) {
        if (containsYieldOrAwait(n->iter.get())) return true;
        if (containsYieldOrAwait(n->body.get())) return true;
        if (n->orelse && containsYieldOrAwait(n->orelse.get())) return true;
    } else if (auto* n = dynamic_cast<WithNode*>(node)) {
        for (auto& item : n->items) if (containsYieldOrAwait(item.context_expr.get())) return true;
        if (containsYieldOrAwait(n->body.get())) return true;
    } else if (auto* n = dynamic_cast<TryNode*>(node)) {
        if (containsYieldOrAwait(n->body.get())) return true;
        for (auto& h : n->handlers) if (containsYieldOrAwait(h.body.get())) return true;
        if (n->orelse && containsYieldOrAwait(n->orelse.get())) return true;
        if (n->finalbody && containsYieldOrAwait(n->finalbody.get())) return true;
    } else if (auto* n = dynamic_cast<CallNode*>(node)) {
        if (containsYieldOrAwait(n->func.get())) return true;
        for (auto& arg : n->args) if (containsYieldOrAwait(arg.get())) return true;
    } else if (auto* n = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : n->targets) if (containsYieldOrAwait(t.get())) return true;
        if (containsYieldOrAwait(n->value.get())) return true;
    } else if (auto* n = dynamic_cast<BinOpNode*>(node)) {
        if (containsYieldOrAwait(n->left.get())) return true;
        if (containsYieldOrAwait(n->right.get())) return true;
    } else if (auto* n = dynamic_cast<UnaryOpNode*>(node)) {
        if (containsYieldOrAwait(n->operand.get())) return true;
    } else if (auto* n = dynamic_cast<ReturnNode*>(node)) {
        if (n->value && containsYieldOrAwait(n->value.get())) return true;
    }
    return false;
}

// =========================================================================
// Comprehensions — list / set / dict comprehensions and generator
// expressions all share a nested for-loop structure that walks each
// generator's iterable (left to right), evaluates each `if` filter,
// and finally evaluates the element expression and appends it to the
// accumulator.  Generator expressions are emitted as eager list
// builds for the compiled form (matches CPython's runtime behaviour
// closely enough for iteration consumers; a future improvement
// could emit a real lazy generator state machine here).
// =========================================================================

bool CppGenerator::emitComprehensionAssign(ASTNode* target, const std::string& valExpr) {
    if (auto* nm = dynamic_cast<NameNode*>(target)) {
        *out_ << "            env->storeName(\"" << nm->id << "\", " << valExpr << ");\n";
        return true;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(target)) {
        // for a, b in ... — unpack via the same helper assignments use.
        if (!generateAssignToTarget(tup, valExpr)) return false;
        *out_ << ";\n";
        return true;
    }
    *out_ << "            /* unsupported comp target */\n";
    return true;
}

bool CppGenerator::emitComprehensionBody(const std::vector<Comprehension>& generators,
                                          size_t depth,
                                          ASTNode* elt,
                                          ASTNode* dictKey,
                                          int kind) {
    static int compNestId = 0;
    int id = compNestId++;
    const Comprehension& g = generators[depth];

    *out_ << "        {\n";
    *out_ << "            auto* __cit_" << id << " = env->iter(";
    if (!generateNode(g.iter.get())) return false;
    *out_ << ");\n";
    *out_ << "            while (true) {\n";
    *out_ << "                auto* __cval_" << id << " = env->next(__cit_" << id << ");\n";
    *out_ << "                if (env->hasPendingException()) { (void)env->takePendingException(); break; }\n";
    *out_ << "                if (!__cval_" << id << ") break;\n";

    std::ostringstream valVar;
    valVar << "__cval_" << id;
    if (!emitComprehensionAssign(g.target.get(), valVar.str())) return false;

    for (auto& f : g.ifs) {
        *out_ << "            if (env->isTrue(";
        if (!generateNode(f.get())) return false;
        *out_ << ")) {\n";
    }

    if (depth + 1 < generators.size()) {
        if (!emitComprehensionBody(generators, depth + 1, elt, dictKey, kind)) return false;
    } else {
        if (kind == 0 || kind == 3) {
            // List / generator-as-list — append to accumulator list.
            *out_ << "                acc = acc->appendLast(ctx, ";
            if (!generateNode(elt)) return false;
            *out_ << ");\n";
        } else if (kind == 1) {
            // Set — append + add via env-aware set semantics.
            *out_ << "                __set_acc = protoPython::PythonEnvironment::setAdd(ctx, __set_acc, ";
            if (!generateNode(elt)) return false;
            *out_ << ");\n";
            *out_ << "                if (!__set_acc) return nullptr;\n";
        } else if (kind == 2) {
            // Dict — set key/value on the accumulator's __data__ / __keys__.
            *out_ << "                {\n";
            *out_ << "                    auto* __k = ";
            if (!generateNode(dictKey)) return false;
            *out_ << ";\n";
            *out_ << "                    auto* __v = ";
            if (!generateNode(elt)) return false;
            *out_ << ";\n";
            *out_ << "                    env->setItem(__dict_acc, __k, __v);\n";
            *out_ << "                }\n";
        }
    }

    for (size_t i = 0; i < g.ifs.size(); ++i) *out_ << "            }\n";
    *out_ << "            }\n";  // while
    *out_ << "        }\n";      // block
    return true;
}

bool CppGenerator::generateListComp(ListCompNode* n) {
    if (!n) return false;
    *out_ << "([&]() -> const proto::ProtoObject* {\n";
    *out_ << "        auto* listObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "        auto* acc = ctx->newList();\n";
    if (!emitComprehensionBody(n->generators, 0, n->elt.get(), nullptr, 0)) return false;
    *out_ << "        listObj->setAttribute(ctx, env->getDataString(), acc->asObject(ctx));\n";
    *out_ << "        if (env->getListPrototype()) listObj->addParent(ctx, env->getListPrototype());\n";
    *out_ << "        return listObj;\n";
    *out_ << "    })()";
    return true;
}

bool CppGenerator::generateGeneratorExp(GeneratorExpNode* n) {
    // Eager list — consumers (sum, max, set(...), tuple(...), etc.)
    // iterate the result the same way.  A lazy real generator state
    // machine is future work.
    if (!n) return false;
    *out_ << "([&]() -> const proto::ProtoObject* {\n";
    *out_ << "        auto* listObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "        auto* acc = ctx->newList();\n";
    if (!emitComprehensionBody(n->generators, 0, n->elt.get(), nullptr, 3)) return false;
    *out_ << "        listObj->setAttribute(ctx, env->getDataString(), acc->asObject(ctx));\n";
    *out_ << "        if (env->getListPrototype()) listObj->addParent(ctx, env->getListPrototype());\n";
    *out_ << "        return listObj;\n";
    *out_ << "    })()";
    return true;
}

bool CppGenerator::generateSetComp(SetCompNode* n) {
    if (!n) return false;
    *out_ << "([&]() -> const proto::ProtoObject* {\n";
    *out_ << "        auto* setObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "        auto* __set_acc = ctx->newSet();\n";
    if (!emitComprehensionBody(n->generators, 0, n->elt.get(), nullptr, 1)) return false;
    *out_ << "        setObj->setAttribute(ctx, env->getDataString(), __set_acc->asObject(ctx));\n";
    *out_ << "        if (env->getSetPrototype()) setObj->addParent(ctx, env->getSetPrototype());\n";
    *out_ << "        return setObj;\n";
    *out_ << "    })()";
    return true;
}

bool CppGenerator::generateDictComp(DictCompNode* n) {
    if (!n) return false;
    *out_ << "([&]() -> const proto::ProtoObject* {\n";
    *out_ << "        auto* __dict_acc = const_cast<proto::ProtoObject*>(ctx->newObject(true));\n";
    *out_ << "        if (env->getDictPrototype()) __dict_acc->addParent(ctx, env->getDictPrototype());\n";
    *out_ << "        __dict_acc->setAttribute(ctx, env->getDataString(), ctx->newSparseList()->asObject(ctx));\n";
    *out_ << "        __dict_acc->setAttribute(ctx, env->getKeysString(), ctx->newList()->asObject(ctx));\n";
    if (!emitComprehensionBody(n->generators, 0, n->value.get(), n->key.get(), 2)) return false;
    *out_ << "        return __dict_acc;\n";
    *out_ << "    })()";
    return true;
}

} // namespace protoPython
