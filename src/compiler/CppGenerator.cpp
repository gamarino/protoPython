#include <protoPython/CppGenerator.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

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
// then the receiver.  The entry point tells the two apart (a storage owns
// `__module_ns__`) and moves a receiver to the front of `args`, where the
// prologue expects it.  A `def` in a class body also binds its storage in
// the class namespace under a key of its own, so a method finds its storage
// through the receiver's class (__methodStorage) and keeps its module
// namespace and free variables.  Without a storage (a callable attached to
// a class from outside it, or a lambda) the namespace falls back to
// __moduleNamespace, the namespace of the module this file initialised, and
// free variables read as None.
//
// Exceptions.  A Python exception in flight is a C++ exception,
// protoPython::PyException (PythonEnvironment.h), as in the runtime: every
// operation that raises throws it, so nothing after it in the statement or
// in its statement list runs, and a compiled callable lets it propagate to
// its caller.  A Python `try` statement is a C++ try: the exception a clause
// raises is caught, kept in an anchor slot (where the collector sees it, and
// where a generator saves it across a yield) and handled after the C++ try,
// by the except clauses, the finally clause or __exit__, which rethrow it
// when it is not handled.  Handlers never run inside a C++ catch block: a
// generator suspended in one must be able to re-enter it.  `return`, `break`
// and `continue` inside a `try` statement with a finally clause, or inside a
// `with` statement, record what they do in a slot and jump to the finally
// clause (or the __exit__ call), which does it afterwards.
// MSVC limits one string literal to 16380 bytes (C2026): the preamble is
// several adjacent raw literals.
const char* const kGeneratedPreamble =
R"PREAMBLE(namespace {
// True when a slot that records a caught exception holds one (it holds
// PROTO_NONE, or nullptr, otherwise).
inline bool __inFlight(const proto::ProtoObject* exc) {
    return exc && exc != PROTO_NONE;
}

inline bool __truth(protoPython::PythonEnvironment* env, const proto::ProtoObject* v) {
    // A comparison's result needs no call.
    if (v == PROTO_TRUE) return true;
    if (v == PROTO_FALSE) return false;
    return env->isTrue(v);
}

// The exception an except clause (or an __exit__ call) handles: sys.exc_info()
// and a bare `raise` see it until the clause is left, however it is left.
// It is kept in the callable's context (PythonEnvironment::kActivationSlots).
struct __Handling : protoPython::PythonEnvironment::HandledExceptionScope {
    __Handling(protoPython::PythonEnvironment* env, proto::ProtoContext* ctx, const proto::ProtoObject* exc)
        : HandledExceptionScope(env, ctx, exc) {}
};

// The same for a finally clause, when it runs for an exception (`exc` is
// neither null nor None).
struct __HandlingIf : protoPython::PythonEnvironment::HandledExceptionScope {
    __HandlingIf(protoPython::PythonEnvironment* env, proto::ProtoContext* ctx, const proto::ProtoObject* exc)
        : HandledExceptionScope(__inFlight(exc) ? env->handledExceptionSlot(ctx) : nullptr, exc) {}
};

// A class body binds names in its namespace, the current frame while it
// runs; the previous frame is restored however the body is left.
struct __FrameScope {
    const proto::ProtoObject* previous;
    explicit __FrameScope(const proto::ProtoObject* frame)
        : previous(protoPython::PythonEnvironment::getCurrentFrame()) {
        protoPython::PythonEnvironment::setCurrentFrame(frame);
    }
    ~__FrameScope() { protoPython::PythonEnvironment::setCurrentFrame(previous); }
    __FrameScope(const __FrameScope&) = delete;
    __FrameScope& operator=(const __FrameScope&) = delete;
};

// A class: an interpreter class, or one protopyc built (an object whose own
// __class__ is `type`).
bool __isClass(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    if (env->isActuallyAClass(ctx, obj)) return true;
    const proto::ProtoString* classS = env->getClassString();
    return obj->isCell(ctx) && obj->hasOwnAttribute(ctx, classS) == PROTO_TRUE &&
           obj->getAttribute(ctx, classS) == env->getTypePrototype();
}

// `raise exc` and `raise exc from cause` (not named `__raise`, an MSVC
// keyword).  A class is instantiated with no arguments, as the interpreter's
// RAISE_VARARGS does.  The raise helpers of the runtime (raiseTypeError, ...)
// throw too.
[[noreturn]] void __pyRaise(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env,
                          const proto::ProtoObject* exc, bool hasCause, const proto::ProtoObject* cause) {
    if (__isClass(ctx, env, exc)) exc = env->callObject(exc, {});
    if (!exc || exc == PROTO_NONE) env->raiseTypeError(ctx, "exceptions must derive from BaseException");
    if (hasCause) {
        if (__isClass(ctx, env, cause)) cause = env->callObject(cause, {});
        env->setAttr(exc, "__cause__", cause ? cause : PROTO_NONE);
        env->setAttr(exc, "__suppress_context__", PROTO_TRUE);
    }
    throw protoPython::PyException(env, exc);
}

// Zero-argument super() in a method: the class of the method, from the class
// cell its storage keeps as `__class__` (filled once the class is built).
const proto::ProtoObject* __classFromCell(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env,
                                          const proto::ProtoObject* fs) {
    const proto::ProtoObject* cell =
        fs ? fs->getAttribute(ctx, protoPython::PythonEnvironment::getInternedString(ctx, "__class__")) : nullptr;
    const proto::ProtoObject* cls = (cell && cell != PROTO_NONE) ? env->cellGet(ctx, cell) : nullptr;
    if (!cls || cls == PROTO_NONE) env->raiseRuntimeError(ctx, "super(): __class__ cell not found");
    return cls;
}

// `f(*it)`: appends the values of `iterable` to the list in `listSlot`.
void __extendArgs(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env,
                  const proto::ProtoObject** listSlot, const proto::ProtoObject* iterable) {
    const proto::ProtoObject* it = env->iter(iterable);
    if (!it) {
        env->raiseTypeError(ctx, "argument after * must be an iterable");
        return;
    }
    protoPython::PythonEnvironment::TransientPin pin(env, it);
    // A StopIteration that reaches here ends the iteration.
    try {
        while (const proto::ProtoObject* v = env->next(it)) {
            *listSlot = ::protoPython::listStorage(ctx, (*listSlot))->appendLast(ctx, v)->asObject(ctx);
        }
    } catch (const proto::ProtoException& e) {
        if (!env->isStopIteration(ctx, e.value())) throw;
    }
}

// `f(**a, **b)`: merges the mapping into the dict in `dictSlot` (created by
// the first call).
void __mergeKwargs(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env,
                   const proto::ProtoObject** dictSlot, const proto::ProtoObject* mapping) {
    (void)ctx;
    if (!*dictSlot || *dictSlot == PROTO_NONE) *dictSlot = env->callObject(env->lookupName("dict"), {});
    env->callMethod(*dictSlot, "update", {mapping});
}

// An exception no handler selected leaves unchanged (protoCore's rule E3):
// the C++ exception object the clause caught, held in the activation's frame,
// is thrown again, so a runtime's ProtoException subclass keeps its extras.
// Only when the frame no longer holds it (a generator resumed inside the
// clause: C++ locals do not survive a yield) is a new exception thrown with
// the same value.
[[noreturn]] void __pyRethrow(protoPython::PythonEnvironment* env, const std::exception_ptr& caught,
                              const proto::ProtoObject* exc) {
    if (caught) std::rethrow_exception(caught);
    throw protoPython::PyException(env, exc);
}

// A bare `raise`: the exception the innermost running except clause handles.
// Inside the clause that caught it, `caught` is that clause's C++ exception
// and `handled` its value.
[[noreturn]] void __pyReraise(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env,
                              const std::exception_ptr& caught = nullptr,
                              const proto::ProtoObject* handled = nullptr) {
    const proto::ProtoObject* exc = env->getActiveException(ctx);
    if (!exc) env->raiseRuntimeError(ctx, "No active exception to reraise");
    if (caught && exc == handled) std::rethrow_exception(caught);
    throw protoPython::PyException(env, exc);
}

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

// The cell a free variable closes over, from the function storage (an
// empty cell when there is no storage: the variable reads as unbound).
const proto::ProtoObject* __freeVar(proto::ProtoContext* ctx, const proto::ProtoObject* fs, const char* name) {
    const proto::ProtoObject* v = fs ? fs->getAttribute(ctx, protoPython::PythonEnvironment::getInternedString(ctx, name)) : nullptr;
    if (v && v != PROTO_NONE) return v;
    return protoPython::PythonEnvironment::get(ctx)->newCell(ctx, nullptr);
}

)PREAMBLE"
R"PREAMBLE(// `a, b = value` and `a, *rest, b = value`: the n values of the iterable, the
// one at `star` a list of the rest, into out[0..n); ValueError as CPython's
// when the counts do not match.
void __unpack(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env, const proto::ProtoObject* value,
              unsigned n, int star, const proto::ProtoObject** out) {
    const proto::ProtoObject* it = env->iter(value);
    if (!it) {
        env->raiseTypeError(ctx, "cannot unpack non-iterable object");
        return;
    }
    protoPython::PythonEnvironment::TransientPin pin(env, it);
    // The values are collected in a list kept in out[0], a slot, so the
    // collector sees them while the iteration runs Python code.  A
    // StopIteration that reaches here ends the iteration.
    out[0] = ctx->newList()->asObject(ctx);
    size_t got = 0;
    try {
        while (const proto::ProtoObject* v = env->next(it)) {
            out[0] = ::protoPython::listStorage(ctx, out[0])->appendLast(ctx, v)->asObject(ctx);
            if (star < 0 && ++got > n) break;
        }
    } catch (const proto::ProtoException& e) {
        if (!env->isStopIteration(ctx, e.value())) throw;
    }
    const proto::ProtoList* collected = ::protoPython::listStorage(ctx, out[0]);
    got = collected->getSize(ctx);
    std::vector<const proto::ProtoObject*> values(got);
    for (size_t i = 0; i < got; ++i) values[i] = collected->getAt(ctx, static_cast<int>(i));
    out[0] = PROTO_NONE;
    if (star < 0) {
        if (got != n) {
            const proto::ProtoObject* valueError = env->lookupName("ValueError");
            std::string msg = got > n ? "too many values to unpack (expected " + std::to_string(n) + ")"
                                      : "not enough values to unpack (expected " + std::to_string(n) + ", got " + std::to_string(got) + ")";
            throw protoPython::PyException(env, env->callObject(valueError, {ctx->fromUTF8String(msg.c_str())}));
        }
        for (unsigned i = 0; i < n; ++i) out[i] = values[i];
        return;
    }
    if (got + 1 < n) {
        const proto::ProtoObject* valueError = env->lookupName("ValueError");
        std::string msg = "not enough values to unpack (expected at least " + std::to_string(n - 1) + ", got " + std::to_string(got) + ")";
        throw protoPython::PyException(env, env->callObject(valueError, {ctx->fromUTF8String(msg.c_str())}));
    }
    const size_t after = n - static_cast<size_t>(star) - 1;
    for (int i = 0; i < star; ++i) out[i] = values[static_cast<size_t>(i)];
    for (size_t i = 0; i < after; ++i) out[static_cast<size_t>(star) + 1 + i] = values[got - after + i];
    // The starred target: a list of the middle values (the collected list
    // still holds them while it is built).
    out[star] = collected->asObject(ctx);
    const proto::ProtoObject* restData = ctx->newList(static_cast<unsigned>(got - after - static_cast<size_t>(star)),
                                                      values.data() + star)->asObject(ctx);
    out[star] = restData;
    const proto::ProtoObject* rest = env->callObject(env->lookupName("list"), {});
    // The new list is held only here until it is in its slot: no collection
    // may start in between (protoCore embedder rule 3).
    proto::ProtoContext::CriticalSection holdYoung(ctx);
    rest->setAttribute(ctx, env->getDataString(), restData);
    out[star] = rest;
}

// Reading a cell or free variable: an empty cell raises as in CPython.
inline const proto::ProtoObject* __cellLoad(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env,
                                            const proto::ProtoObject* cell, const char* name, bool isFree) {
    const proto::ProtoObject* v = env->cellGet(ctx, cell);
    if (v) return v;
    if (isFree) env->raiseFreeVariableError(ctx, name);
    else env->raiseUnboundLocalError(ctx, std::string("cannot access local variable '") + name +
                                          "' where it is not associated with a value");
    return nullptr;
}

// A class body's read of a free variable: the class namespace's binding when
// it has one (a __prepare__ namespace or code that bound it), else the cell.
const proto::ProtoObject* __classDeref(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env,
                                       const char* name, const proto::ProtoObject* cell) {
    const proto::ProtoObject* ns = protoPython::PythonEnvironment::getCurrentFrame();
    const proto::ProtoString* key = protoPython::PythonEnvironment::getInternedString(ctx, name);
    if (ns && ns->hasOwnAttribute(ctx, key) == PROTO_TRUE) return ns->getOwnAttributeDirect(ctx, key);
    return __cellLoad(ctx, env, cell, name, true);
}

// The default values of a function's last positional parameters, stored in
// its storage at the definition (a list, not a tuple: protoCore interns
// tuples and an interned tuple is never collected).
const proto::ProtoObject* __withDefaults(proto::ProtoContext* ctx, const proto::ProtoObject* fs,
                                         std::initializer_list<const proto::ProtoObject*> values) {
    const proto::ProtoList* list = ctx->newList(static_cast<unsigned>(values.size()), values.begin());
    return fs->setAttribute(ctx, protoPython::PythonEnvironment::getInternedString(ctx, "__defaults__"), list->asObject(ctx));
}

// The default value of a keyword-only parameter, stored likewise (a sparse
// list keyed by the name's hash).
const proto::ProtoObject* __withKwDefault(proto::ProtoContext* ctx, const proto::ProtoObject* fs,
                                          const char* name, const proto::ProtoObject* value) {
    const proto::ProtoString* key = protoPython::PythonEnvironment::getInternedString(ctx, "__kwdefaults__");
    const proto::ProtoObject* cur = fs->getAttribute(ctx, key);
    const proto::ProtoSparseList* map = (cur && cur != PROTO_NONE) ? cur->asSparseList(ctx) : nullptr;
    if (!map) map = ctx->newSparseList();
    map = map->setAt(ctx, protoPython::PythonEnvironment::getInternedString(ctx, name)->getHash(ctx), value);
    return fs->setAttribute(ctx, key, map->asObject(ctx));
}

// The storage of a method called with an instance as `self` (the
// interpreter binds a native method found on a class to the instance): a
// `def` in a class body also binds its storage in the class namespace under
// a key of its own (__protopyc_fs_<function>__), which the instance reaches
// through its class.  nullptr when it is not there (the method was attached
// to the class from outside it).
const proto::ProtoObject* __methodStorage(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                          const proto::ProtoString* key) {
    if (!receiver || !key) return nullptr;
    const proto::ProtoObject* fs = receiver->getAttribute(ctx, key);
    return (fs && fs != PROTO_NONE && __isFunctionStorage(ctx, fs)) ? fs : nullptr;
}

// A special method that a lookup on the type returns still bound to its
// function storage (a method of a class protopyc compiled): bound to the
// instance instead, as the interpreter binds methods found on a class.
const proto::ProtoObject* __bindMethod(proto::ProtoContext* ctx, const proto::ProtoObject* receiver,
                                       const proto::ProtoObject* method) {
    if (method && method != PROTO_NONE && method->isMethod(ctx) && __isFunctionStorage(ctx, method->asMethodSelf(ctx))) {
        return ctx->fromMethod(const_cast<proto::ProtoObject*>(receiver), method->asMethod(ctx));
    }
    return method;
}

// Entering a `with` item: looks up __enter__ and __exit__ on the type of the
// context manager (as the interpreter does), stores __exit__ in its slot and
// returns what __enter__ returns.
const proto::ProtoObject* __withEnter(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env,
                                      const proto::ProtoObject* manager, const proto::ProtoObject** exitSlot) {
    const proto::ProtoObject* enter = __bindMethod(ctx, manager, env->lookupBoundSpecialMethod(ctx, manager, env->getEnterString()));
    *exitSlot = enter ? __bindMethod(ctx, manager, env->lookupBoundSpecialMethod(ctx, manager, env->getExitString())) : nullptr;
    if (!enter || !*exitSlot) {
        *exitSlot = PROTO_NONE;
        std::string type = "object";
        const proto::ProtoObject* cls = env->getType(ctx, manager);
        const proto::ProtoObject* nm = cls ? cls->getAttribute(ctx, env->getNameString()) : nullptr;
        if (nm && nm->isString(ctx)) { type.clear(); nm->asString(ctx)->toUTF8String(ctx, type); }
        env->raiseTypeError(ctx, "'" + type + "' object does not support the context manager protocol");
        return nullptr;
    }
    return env->callObject(enter, {});
}

// Leaving a `with` item: calls __exit__ with the exception the body raised
// (its type, the exception and its traceback), or with three None when
// `exc` is not in flight.  Returns when the body completed or __exit__
// suppressed the exception; otherwise throws the exception again.  An
// exception __exit__ raises propagates instead, with the body's exception as
// its __context__.  The caller keeps `exc` in a slot while __exit__ runs.
void __withExit(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env,
                const proto::ProtoObject* exit, const proto::ProtoObject* exc,
                const std::exception_ptr& caught = nullptr) {
    if (!__inFlight(exc)) {
        env->callObject(exit, {PROTO_NONE, PROTO_NONE, PROTO_NONE});
        return;
    }
    const proto::ProtoObject* tb = exc->getAttribute(ctx, protoPython::PythonEnvironment::getInternedString(ctx, "__traceback__"));
    const proto::ProtoObject* suppress;
    {
        __Handling handling(env, ctx, exc);
        try {
            suppress = env->callObject(exit, {env->getType(ctx, exc), exc, tb ? tb : PROTO_NONE});
        } catch (const proto::ProtoException& e) {
            env->chainException(ctx, e.value(), exc);
            throw;
        }
    }
    if (!env->isTrue(suppress)) __pyRethrow(env, caught, exc);
}

} // namespace

)PREAMBLE";

} // namespace

// ---------------------------------------------------------------------------
// Scopes: protopyc's symbol table (CppGenerator.h, "Scopes").
//
// One pass over the module builds a ScopeInfo per module, def, lambda and
// class body, with the names each binds, declares global or nonlocal, and
// reads; a second resolves, bottom-up, each scope's free variables (names it
// reads, or a scope nested in it reads, that an enclosing function binds)
// and each function's cell variables (its locals a nested scope reads).
// Class scopes are skipped when a nested scope resolves a name, as in
// Python.  Comprehensions are part of the scope that contains them: protopyc
// emits them inline.
// ---------------------------------------------------------------------------

namespace {

struct ScopeBuilder {
    std::vector<std::unique_ptr<CppGenerator::ScopeInfo>>& store;
    std::unordered_map<const ASTNode*, CppGenerator::ScopeInfo*>& index;
    int loopDepth = 0;

    CppGenerator::ScopeInfo* newScope(CppGenerator::ScopeInfo::Kind kind, const ASTNode* node,
                                      CppGenerator::ScopeInfo* parent, const std::string& name) {
        store.push_back(std::make_unique<CppGenerator::ScopeInfo>());
        CppGenerator::ScopeInfo* s = store.back().get();
        s->kind = kind;
        s->node = node;
        s->parent = parent;
        s->name = name;
        if (parent) parent->children.push_back(s);
        if (node) index[node] = s;
        return s;
    }

    static void bind(CppGenerator::ScopeInfo* s, const std::string& name) {
        s->bound.insert(name);
        ++s->bindCount[name];
    }

    void target(ASTNode* t, CppGenerator::ScopeInfo* s) {
        if (!t) return;
        if (auto* nm = dynamic_cast<NameNode*>(t)) { bind(s, nm->id); return; }
        if (auto* tup = dynamic_cast<TupleLiteralNode*>(t)) { for (auto& e : tup->elements) target(e.get(), s); return; }
        if (auto* lst = dynamic_cast<ListLiteralNode*>(t)) { for (auto& e : lst->elements) target(e.get(), s); return; }
        if (auto* st = dynamic_cast<StarredNode*>(t)) { target(st->value.get(), s); return; }
        visit(t, s);  // an attribute or subscript target reads its operands
    }

    void pattern(MatchPatternNode* p, CppGenerator::ScopeInfo* s) {
        if (!p) return;
        if (auto* v = dynamic_cast<MatchValuePatternNode*>(p)) { visit(v->value.get(), s); return; }
        if (auto* st = dynamic_cast<MatchStarPatternNode*>(p)) { if (!st->name.empty() && st->name != "_") bind(s, st->name); return; }
        if (auto* sq = dynamic_cast<MatchSequencePatternNode*>(p)) { for (auto& e : sq->patterns) pattern(e.get(), s); return; }
        if (auto* mp = dynamic_cast<MatchMappingPatternNode*>(p)) {
            for (auto& k : mp->keys) visit(k.get(), s);
            for (auto& e : mp->patterns) pattern(e.get(), s);
            if (!mp->rest.empty()) bind(s, mp->rest);
            return;
        }
        if (auto* cp = dynamic_cast<MatchClassPatternNode*>(p)) {
            visit(cp->cls.get(), s);
            for (auto& e : cp->args) pattern(e.get(), s);
            for (auto& kv : cp->kwargs) pattern(kv.second.get(), s);
            return;
        }
        if (auto* as = dynamic_cast<MatchAsPatternNode*>(p)) {
            pattern(as->pattern.get(), s);
            if (!as->name.empty() && as->name != "_") bind(s, as->name);
            return;
        }
        if (auto* orp = dynamic_cast<MatchOrPatternNode*>(p)) { for (auto& e : orp->alternatives) pattern(e.get(), s); return; }
    }

    template <class Fn>
    void function(Fn* fn, CppGenerator::ScopeInfo* s) {
        bind(s, fn->name);
        for (auto& d : fn->decorator_list) visit(d.get(), s);
        for (auto& d : fn->defaults) visit(d.get(), s);
        for (auto& d : fn->kw_defaults) visit(d.get(), s);
        for (auto& a : fn->parameter_annotations) visit(a.second.get(), s);
        visit(fn->returns.get(), s);
        CppGenerator::ScopeInfo* f = newScope(CppGenerator::ScopeInfo::Kind::Function, fn, s, fn->name);
        f->isDef = true;
        f->decorated = !fn->decorator_list.empty() || !fn->type_params.empty();
        f->inLoop = loopDepth > 0;
        for (const auto& p : fn->parameters) bind(f, p);
        for (const auto& p : fn->kwonlyargs) bind(f, p);
        if (!fn->vararg.empty()) bind(f, fn->vararg);
        if (!fn->kwarg.empty()) bind(f, fn->kwarg);
        const int savedLoops = loopDepth;
        loopDepth = 0;
        visit(fn->body.get(), f);
        loopDepth = savedLoops;
    }

    void comprehension(std::vector<Comprehension>& gens, CppGenerator::ScopeInfo* s) {
        for (auto& g : gens) {
            visit(g.iter.get(), s);
            target(g.target.get(), s);
            for (auto& c : g.ifs) visit(c.get(), s);
        }
    }

    void visit(ASTNode* node, CppGenerator::ScopeInfo* s) {
        if (!node) return;
        auto all = [&](const std::vector<std::unique_ptr<ASTNode>>& v) { for (auto& e : v) visit(e.get(), s); };
        if (auto* nm = dynamic_cast<NameNode*>(node)) {
            s->used.insert(nm->id);
            s->nonCallUsed.insert(nm->id);
            return;
        }
        if (auto* call = dynamic_cast<CallNode*>(node)) {
            bool plain = call->keywords.empty();
            for (auto& a : call->args) if (dynamic_cast<StarredNode*>(a.get())) plain = false;
            if (auto* fnName = dynamic_cast<NameNode*>(call->func.get()); fnName && plain) {
                s->used.insert(fnName->id);  // a call by name: not a value use
            } else {
                visit(call->func.get(), s);
            }
            all(call->args);
            for (auto& kw : call->keywords) visit(kw.second.get(), s);
            return;
        }
        if (auto* suite = dynamic_cast<SuiteNode*>(node)) { all(suite->statements); return; }
        if (auto* m = dynamic_cast<ModuleNode*>(node)) { all(m->body); return; }
        if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) { function(fn, s); return; }
        if (auto* fn = dynamic_cast<AsyncFunctionDefNode*>(node)) { function(fn, s); return; }
        if (auto* lam = dynamic_cast<LambdaNode*>(node)) {
            all(lam->defaults);
            all(lam->kw_defaults);
            CppGenerator::ScopeInfo* f = newScope(CppGenerator::ScopeInfo::Kind::Function, lam, s, "<lambda>");
            for (const auto& p : lam->parameters) bind(f, p);
            for (const auto& p : lam->kwonlyargs) bind(f, p);
            if (!lam->vararg.empty()) bind(f, lam->vararg);
            if (!lam->kwarg.empty()) bind(f, lam->kwarg);
            const int savedLoops = loopDepth;
            loopDepth = 0;
            visit(lam->body.get(), f);
            loopDepth = savedLoops;
            return;
        }
        if (auto* cd = dynamic_cast<ClassDefNode*>(node)) {
            bind(s, cd->name);
            all(cd->bases);
            all(cd->decorator_list);
            for (auto& kw : cd->keywords) visit(kw.second.get(), s);
            CppGenerator::ScopeInfo* c = newScope(CppGenerator::ScopeInfo::Kind::Class, cd, s, cd->name);
            const int savedLoops = loopDepth;
            loopDepth = 0;
            visit(cd->body.get(), c);
            loopDepth = savedLoops;
            return;
        }
        if (auto* g = dynamic_cast<GlobalNode*>(node)) {
            for (const auto& n : g->names) { s->globals.insert(n); ++s->bindCount[n]; }
            return;
        }
        if (auto* nl = dynamic_cast<NonlocalNode*>(node)) {
            for (const auto& n : nl->names) { s->nonlocals.insert(n); ++s->bindCount[n]; }
            return;
        }
        if (auto* as = dynamic_cast<AssignNode*>(node)) {
            visit(as->value.get(), s);
            for (auto& t : as->targets) target(t.get(), s);
            return;
        }
        if (auto* aug = dynamic_cast<AugAssignNode*>(node)) {
            if (auto* nm = dynamic_cast<NameNode*>(aug->target.get())) {
                s->used.insert(nm->id);
                s->nonCallUsed.insert(nm->id);
            }
            target(aug->target.get(), s);
            visit(aug->value.get(), s);
            return;
        }
        if (auto* ann = dynamic_cast<AnnAssignNode*>(node)) {
            visit(ann->annotation.get(), s);
            visit(ann->value.get(), s);
            if (auto* nm = dynamic_cast<NameNode*>(ann->target.get())) {
                if (!nm->parenthesized) bind(s, nm->id);
            } else {
                target(ann->target.get(), s);
            }
            return;
        }
        if (auto* ne = dynamic_cast<NamedExprNode*>(node)) { visit(ne->value.get(), s); target(ne->target.get(), s); return; }
        if (auto* f = dynamic_cast<ForNode*>(node)) {
            visit(f->iter.get(), s);
            target(f->target.get(), s);
            ++loopDepth;
            visit(f->body.get(), s);
            --loopDepth;
            visit(f->orelse.get(), s);
            return;
        }
        if (auto* f = dynamic_cast<AsyncForNode*>(node)) {
            visit(f->iter.get(), s);
            target(f->target.get(), s);
            ++loopDepth;
            visit(f->body.get(), s);
            --loopDepth;
            visit(f->orelse.get(), s);
            return;
        }
        if (auto* w = dynamic_cast<WhileNode*>(node)) {
            ++loopDepth;
            visit(w->test.get(), s);
            visit(w->body.get(), s);
            --loopDepth;
            visit(w->orelse.get(), s);
            return;
        }
        if (auto* iff = dynamic_cast<IfNode*>(node)) { visit(iff->test.get(), s); visit(iff->body.get(), s); visit(iff->orelse.get(), s); return; }
        if (auto* w = dynamic_cast<WithNode*>(node)) {
            for (auto& it : w->items) { visit(it.context_expr.get(), s); target(it.optional_vars.get(), s); }
            visit(w->body.get(), s);
            return;
        }
        if (auto* w = dynamic_cast<AsyncWithNode*>(node)) {
            for (auto& it : w->items) { visit(it.context_expr.get(), s); target(it.optional_vars.get(), s); }
            visit(w->body.get(), s);
            return;
        }
        if (auto* t = dynamic_cast<TryNode*>(node)) {
            visit(t->body.get(), s);
            for (auto& h : t->handlers) {
                visit(h.type.get(), s);
                if (!h.name.empty()) bind(s, h.name);
                visit(h.body.get(), s);
            }
            visit(t->orelse.get(), s);
            visit(t->finalbody.get(), s);
            return;
        }
        if (auto* d = dynamic_cast<DeleteNode*>(node)) { for (auto& t : d->targets) target(t.get(), s); return; }
        if (auto* imp = dynamic_cast<ImportNode*>(node)) {
            bind(s, imp->isAs ? imp->alias : imp->moduleName.substr(0, imp->moduleName.find('.')));
            return;
        }
        if (auto* imp = dynamic_cast<ImportFromNode*>(node)) {
            for (const auto& item : imp->names) {
                if (item.first == "*") continue;
                bind(s, item.second.empty() ? item.first : item.second);
            }
            return;
        }
        if (auto* m = dynamic_cast<MatchNode*>(node)) {
            visit(m->subject.get(), s);
            for (auto& c : m->cases) {
                if (!c) continue;
                pattern(c->pattern.get(), s);
                visit(c->guard.get(), s);
                visit(c->body.get(), s);
            }
            return;
        }
        if (auto* ta = dynamic_cast<TypeAliasNode*>(node)) { bind(s, ta->name); visit(ta->value.get(), s); return; }
        if (auto* lc = dynamic_cast<ListCompNode*>(node)) { comprehension(lc->generators, s); visit(lc->elt.get(), s); return; }
        if (auto* sc = dynamic_cast<SetCompNode*>(node)) { comprehension(sc->generators, s); visit(sc->elt.get(), s); return; }
        if (auto* ge = dynamic_cast<GeneratorExpNode*>(node)) { comprehension(ge->generators, s); visit(ge->elt.get(), s); return; }
        if (auto* dc = dynamic_cast<DictCompNode*>(node)) { comprehension(dc->generators, s); visit(dc->key.get(), s); visit(dc->value.get(), s); return; }
        if (auto* b = dynamic_cast<BinOpNode*>(node)) { visit(b->left.get(), s); visit(b->right.get(), s); return; }
        if (auto* u = dynamic_cast<UnaryOpNode*>(node)) { visit(u->operand.get(), s); return; }
        if (auto* c = dynamic_cast<CondExprNode*>(node)) { visit(c->body.get(), s); visit(c->cond.get(), s); visit(c->orelse.get(), s); return; }
        if (auto* c = dynamic_cast<ConditionalExprNode*>(node)) { visit(c->body.get(), s); visit(c->test.get(), s); visit(c->orelse.get(), s); return; }
        if (auto* st = dynamic_cast<StarredNode*>(node)) { visit(st->value.get(), s); return; }
        if (auto* a = dynamic_cast<AttributeNode*>(node)) { visit(a->value.get(), s); return; }
        if (auto* sb = dynamic_cast<SubscriptNode*>(node)) { visit(sb->value.get(), s); visit(sb->index.get(), s); return; }
        if (auto* sl = dynamic_cast<SliceNode*>(node)) { visit(sl->start.get(), s); visit(sl->stop.get(), s); visit(sl->step.get(), s); return; }
        if (auto* l = dynamic_cast<ListLiteralNode*>(node)) { all(l->elements); return; }
        if (auto* t = dynamic_cast<TupleLiteralNode*>(node)) { all(t->elements); return; }
        if (auto* st = dynamic_cast<SetLiteralNode*>(node)) { all(st->elements); return; }
        if (auto* d = dynamic_cast<DictLiteralNode*>(node)) { for (auto& k : d->keys) visit(k.get(), s); all(d->values); return; }
        if (auto* fv = dynamic_cast<FormattedValueNode*>(node)) { visit(fv->value.get(), s); return; }
        if (auto* js = dynamic_cast<JoinedStrNode*>(node)) { all(js->values); return; }
        if (auto* aw = dynamic_cast<AwaitNode*>(node)) { visit(aw->value.get(), s); return; }
        if (auto* y = dynamic_cast<YieldNode*>(node)) { visit(y->value.get(), s); return; }
        if (auto* r = dynamic_cast<ReturnNode*>(node)) { visit(r->value.get(), s); return; }
        if (auto* r = dynamic_cast<RaiseNode*>(node)) { visit(r->exc.get(), s); visit(r->cause.get(), s); return; }
        if (auto* a = dynamic_cast<AssertNode*>(node)) { visit(a->test.get(), s); visit(a->msg.get(), s); return; }
        // Constants, pass, break, continue: nothing to record.
    }
};

// Names a function scope can close over from its enclosing functions.
using NameSet = std::unordered_set<std::string>;

void resolveScope(CppGenerator::ScopeInfo* s, const NameSet& visible) {
    using Kind = CppGenerator::ScopeInfo::Kind;
    // `__class__` and `super` in a method are the class cell protopyc keeps
    // in the method's storage, never a closure.
    NameSet childVisible = visible;
    if (s->kind == Kind::Function) {
        for (const auto& b : s->bound) if (!s->globals.count(b)) childVisible.insert(b);
        for (const auto& n : s->nonlocals) childVisible.insert(n);
    }
    for (auto* c : s->children) resolveScope(c, childVisible);
    auto isLocal = [&](const std::string& n) {
        return s->kind == Kind::Function && s->bound.count(n) && !s->globals.count(n) && !s->nonlocals.count(n);
    };
    for (const auto& n : s->nonlocals) if (visible.count(n)) s->frees.insert(n);
    for (auto* c : s->children) {
        for (const auto& f : c->frees) {
            if (isLocal(f)) s->cells.insert(f);
            else if (!s->globals.count(f) && (visible.count(f) || s->nonlocals.count(f))) s->frees.insert(f);
        }
    }
    if (s->kind != Kind::Module) {
        for (const auto& u : s->used) {
            if (u == "__class__") continue;
            if (s->globals.count(u)) continue;
            if (s->kind == Kind::Function ? isLocal(u) : (s->bound.count(u) && !s->nonlocals.count(u))) continue;
            if (visible.count(u)) s->frees.insert(u);
        }
    }
    // A recursive def whose name only the def binds in its scope (not
    // decorated, not in a loop) calls itself directly: its body runs as the
    // function, with no capture of the name (which would close the cycle
    // cell -> function -> storage -> cell).  The name is still captured
    // when the body uses it other than as a plain call, or a scope nested
    // in it does.
    if (s->isDef && !s->decorated && !s->inLoop && s->parent) {
        const CppGenerator::ScopeInfo* p = s->parent;
        auto bc = p->bindCount.find(s->name);
        const bool onlyBinding = bc != p->bindCount.end() && bc->second == 1 && !p->globals.count(s->name)
            && !p->nonlocals.count(s->name);
        bool nestedRebinds = false;
        std::vector<const CppGenerator::ScopeInfo*> stack(p->children.begin(), p->children.end());
        while (!stack.empty()) {
            const CppGenerator::ScopeInfo* q = stack.back();
            stack.pop_back();
            if (q->nonlocals.count(s->name) || q->globals.count(s->name)) nestedRebinds = true;
            for (auto* qc : q->children) stack.push_back(qc);
        }
        if (p->kind != Kind::Class && onlyBinding && !nestedRebinds && !s->bound.count(s->name)) {
            s->selfCalls = true;
            bool childNeeds = false;
            for (auto* c : s->children) if (c->frees.count(s->name)) childNeeds = true;
            if (!s->nonCallUsed.count(s->name) && !childNeeds) s->frees.erase(s->name);
        }
    }
}

}  // namespace

void CppGenerator::buildScopes(ModuleNode* module) {
    scopeStore_.clear();
    scopeOf_.clear();
    ScopeBuilder builder{scopeStore_, scopeOf_};
    ScopeInfo* root = builder.newScope(ScopeInfo::Kind::Module, module, nullptr, "<module>");
    builder.visit(module, root);
    // A global declared anywhere is a module binding too.
    for (const auto& s : scopeStore_)
        for (const auto& g : s->globals) ++root->bindCount[g];
    resolveScope(root, {});
}

CppGenerator::NameKind CppGenerator::nameKind(const std::string& name) const {
    const ScopeInfo* s = scope_;
    if (!s || s->kind == ScopeInfo::Kind::Module) return NameKind::Global;
    if (s->globals.count(name)) return NameKind::Global;
    if (s->kind == ScopeInfo::Kind::Class) {
        if (s->nonlocals.count(name)) return NameKind::Free;
        if (s->bound.count(name)) return NameKind::ClassLocal;
        if (s->frees.count(name)) return NameKind::ClassFree;
        return NameKind::Global;
    }
    if (s->cells.count(name)) return NameKind::Cell;
    if (s->frees.count(name)) return NameKind::Free;
    if (s->bound.count(name)) return NameKind::Local;
    return NameKind::Global;
}

bool CppGenerator::generate(ModuleNode* module, const std::string& filename) {
    header_.str("");
    header_.clear();
    body_.str("");
    body_.clear();

    // 1. Module initialisation.  The statements are generated first: the
    // slot count the module's context declares is known only afterwards.
    filename_ = filename;
    buildScopes(module);
    scope_ = scopeOf_.count(module) ? scopeOf_[module] : nullptr;
    const SlotFrame moduleSlots = enterSlotFrame(0);
    ctl_.clear();
    returnSlot_ = -1;
    std::ostringstream moduleBody;
    out_ = &moduleBody;
    for (auto& stmt : module->body) {
        if (!emitStatement(stmt.get())) return false;
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
    // An exception the module body raises propagates to the importer
    // (CompiledModuleProvider): the import fails with it.
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
    finalOut_ << "#include <exception>\n";
    finalOut_ << "#include <limits>\n";
    finalOut_ << "#include <initializer_list>\n";
    finalOut_ << "#include <string>\n";
    finalOut_ << "#include <vector>\n";
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
    SlotFrame saved{anchorTop_, tempTop_, slotHigh_, handlerCaught_.size()};
    // A new callable: the clauses around it are another C++ function's.
    handlerCaught_.push_back({std::string(), std::string()});
    anchorTop_ = reserved;
    tempTop_ = reserved;
    slotHigh_ = reserved;
    return saved;
}

void CppGenerator::leaveSlotFrame(const SlotFrame& saved) {
    handlerCaught_.resize(saved.handlerDepth);
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

std::string CppGenerator::openContextScope(const std::string& callerCtx, bool /*unused*/) const {
    // The context is chained to the thread's current context (chainParent),
    // so the collector's root scan reaches its slots; the slot array lives
    // on the C++ stack for up to ContextScope::SBO_SLOTS slots.  The
    // callable's own slots are followed by the activation slots
    // (PythonEnvironment::kActivationSlots), which hold the exception its
    // except or finally clause handles; __nslots counts only its own.
    std::ostringstream o;
    const int nSlots = std::max(slotHigh_, 1);
    o << "    protoPython::ContextScope __scope(" << callerCtx << "->space, " << callerCtx
      << ", nullptr, nullptr, nullptr, nullptr, " << nSlots
      << " + protoPython::PythonEnvironment::kActivationSlots);\n";
    o << "    proto::ProtoContext* ctx = __scope.context();\n";
    o << "    protoPython::PythonEnvironment::get(ctx)->markActivation(ctx);\n";
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
        // A local is its slot and a cell's contents are held by the cell.
        const NameKind k = nameKind(nm->id);
        return k != NameKind::Local && k != NameKind::Cell && k != NameKind::Free;
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
    // An operation that raises throws, so nothing after it in the statement
    // runs.
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
            if (!emitStatement(stmt.get())) return false;
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
    // straight lowering works as long as test goes through __truth (that
    // is, env->isTrue) to honour Python truthiness on arbitrary objects.
    *out_ << "(__truth(env, ";
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
            // Every digit of the literal: a stream's default precision (6
            // significant digits) compiled 12.566370614359172 as 12.5664.
            // A literal too large for a double (1e999) is infinity.
            if (std::isinf(n->floatVal)) {
                *out_ << "ctx->fromDouble(std::numeric_limits<double>::infinity())";
            } else {
                std::ostringstream literal;
                literal.precision(std::numeric_limits<double>::max_digits10);
                literal << n->floatVal;
                std::string text = literal.str();
                if (text.find_first_of(".eE") == std::string::npos) text += ".0";
                *out_ << "ctx->fromDouble(" << text << ")";
            }
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
    *out_ << nameRead(n->id);
    return true;
}

std::string CppGenerator::nameRead(const std::string& name) const {
    // `__class__` in a method: the class, from the cell its storage keeps.
    if (name == "__class__" && inMethod_ && nameKind(name) == NameKind::Global)
        return "__classFromCell(ctx, env, __fs)";
    // A name is read as the current scope resolves it (buildScopes): a
    // function's local is its slot (`local_<name>`), a cell or free variable
    // the contents of the cell in its slot (an empty one raises), a class
    // body's free variable the class namespace's binding or else the cell,
    // anything else through the current frame and the module
    // (env->lookupName: the class namespace in a class body, the module
    // namespace, then the builtins).
    switch (nameKind(name)) {
        case NameKind::Local:
            return "local_" + name;
        case NameKind::Cell:
            return "__cellLoad(ctx, env, local_" + name + ", \"" + name + "\", false)";
        case NameKind::Free:
            return "__cellLoad(ctx, env, local_" + name + ", \"" + name + "\", true)";
        case NameKind::ClassFree:
            return "__classDeref(ctx, env, \"" + name + "\", local_" + name + ")";
        case NameKind::ClassLocal:
        case NameKind::Global:
            break;
    }
    return "env->lookupName(\"" + name + "\")";
}

void CppGenerator::emitDeleteName(const std::string& name) {
    switch (nameKind(name)) {
        case NameKind::Local:
            *out_ << "local_" << name << " = PROTO_NONE";
            return;
        case NameKind::Cell:
        case NameKind::Free:
            *out_ << "(void)__cellLoad(ctx, env, local_" << name << ", \"" << name << "\", "
                  << (nameKind(name) == NameKind::Free ? "true" : "false") << "), env->cellClear(ctx, local_" << name << ")";
            return;
        default:
            *out_ << "env->delName(\"" << name << "\")";
            return;
    }
}

bool CppGenerator::generateCall(CallNode* n) {
    bool hasUnpacking = false;
    for (auto& a : n->args) if (dynamic_cast<StarredNode*>(a.get())) { hasUnpacking = true; break; }
    if (!hasUnpacking) {
        for (auto& kw : n->keywords) if (kw.first.empty()) { hasUnpacking = true; break; }
    }

    if (!hasUnpacking && n->args.empty() && n->keywords.empty() && !methodSelfParam_.empty()) {
        // Zero-argument super() in a method: super(<the class>, <first
        // parameter>), the class read from the method's class cell.
        auto* nm = dynamic_cast<NameNode*>(n->func.get());
        if (nm && nm->id == "super" && nameKind("super") == NameKind::Global) {
            *out_ << "env->callObject(env->lookupName(\"super\"), {__classFromCell(ctx, env, __fs), "
                  << nameRead(methodSelfParam_) << "})";
            return true;
        }
    }
    if (!hasUnpacking) {
        if (n->keywords.empty()) {
            // For `obj.attr(args)` route through env->callMethod, which uses
            // LOAD_METHOD-style attribute resolution and prepends `self` to
            // args when the resolved attribute is a raw function on the
            // class chain — matching the prologue protopyc emits for every
            // generated function (`local_self = args->getAt(0)`).
            if (auto* att = dynamic_cast<AttributeNode*>(n->func.get())) {
                // The name is interned once (internedKey) and the arguments
                // stay on the C++ stack.
                *out_ << "env->callMethod(ctx, ";
                if (!generateNode(att->value.get())) return false;
                *out_ << ", " << internedKey(att->attr) << ", {";
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
                if (!currentFuncName_.empty() && nm->id == currentFuncName_ &&
                        nameKind(nm->id) != NameKind::Local && nameKind(nm->id) != NameKind::Cell) {
                    *out_ << "([&]() -> const proto::ProtoObject* {\n";
                    // Not `__argv`: the MSVC runtime defines it as a macro.
                    *out_ << "        const proto::ProtoObject* __selfArgs[] = {";
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
                          << "u, __selfArgs);\n";
                    *out_ << "        return " << currentFuncCppName_
                          << "_body(ctx, __fs, nullptr, __args, nullptr);\n";
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
        // f(a, *it, k=v, **m) (PEP 448): the positional values, starred
        // iterables expanded, go into a list kept in a temporary slot (the
        // iteration can run Python code and collect); keywords are named or
        // come from the `**` mappings (merged into one dict when there are
        // several).
        const int listSlot = allocTempSlot();
        *out_ << "([&]() -> const proto::ProtoObject* {\n";
        *out_ << "        const proto::ProtoObject* __f = ";
        if (!generateNode(n->func.get())) return false;
        *out_ << ";\n";
        *out_ << "        __slot[" << listSlot << "] = ctx->newList()->asObject(ctx);\n";
        for (auto& a : n->args) {
            if (auto* s = dynamic_cast<StarredNode*>(a.get())) {
                *out_ << "        __extendArgs(ctx, env, &__slot[" << listSlot << "], ";
                if (!generateNode(s->value.get())) return false;
                *out_ << ");\n";
            } else {
                *out_ << "        { const proto::ProtoObject* __v = ";
                if (!generateNode(a.get())) return false;
                *out_ << "; __slot[" << listSlot << "] = __slot[" << listSlot
                      << "]->asList(ctx)->appendLast(ctx, __v)->asObject(ctx); }\n";
            }
        }
        *out_ << "        std::vector<std::pair<std::string, const proto::ProtoObject*>> __kws;\n";
        size_t nMaps = 0;
        for (auto& kw : n->keywords) if (kw.first.empty()) ++nMaps;
        const int mapSlot = nMaps > 1 ? allocTempSlot() : -1;
        *out_ << "        const proto::ProtoObject* __kwmap = nullptr;\n";
        if (nMaps > 1) *out_ << "        __slot[" << mapSlot << "] = PROTO_NONE;\n";
        for (auto& kw : n->keywords) {
            if (kw.first.empty()) {
                if (nMaps == 1) {
                    *out_ << "        __kwmap = ";
                    if (!generateNode(kw.second.get())) return false;
                    *out_ << ";\n";
                } else {
                    *out_ << "        __mergeKwargs(ctx, env, &__slot[" << mapSlot << "], ";
                    if (!generateNode(kw.second.get())) return false;
                    *out_ << ");\n";
                }
            } else {
                *out_ << "        __kws.push_back({\"" << kw.first << "\", ";
                if (!generateNode(kw.second.get())) return false;
                *out_ << "});\n";
            }
        }
        if (nMaps > 1) *out_ << "        __kwmap = __slot[" << mapSlot << "];\n";
        *out_ << "        return env->callObjectEx(__f, {}, __kws, env->makeTuple(ctx, __slot["
              << listSlot << "]->asList(ctx)), __kwmap);\n";
        *out_ << "    })()";
    }
    return true;
}

bool CppGenerator::generateStarred(StarredNode* n) {
    return generateNode(n->value.get());
}

bool CppGenerator::generateAnnAssign(AnnAssignNode* n) {
    // `name: annotation [= value]` in a class body records the annotation
    // in the class namespace's __annotations__ (created with the namespace,
    // see generateClassDef), as the interpreter does; dataclasses read it.
    auto* annName = dynamic_cast<NameNode*>(n->target.get());
    if (inClassBody_ && annName && n->annotation) {
        *out_ << "env->setItem(env->lookupName(\"__annotations__\"), "
              << "protoPython::PythonEnvironment::getInternedString(ctx, \"" << annName->id << "\")->asObject(ctx), ";
        if (!generateNode(n->annotation.get())) return false;
        *out_ << ")";
        if (!n->value) return true;
        *out_ << ";\n    ";
    }
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
        // Python's attribute write (data descriptors, __setattr__).
        *out_ << "env->storeAttr(ctx, ";
        if (!generateNode(attrNode->value.get())) return false;
        *out_ << ", " << internedKey(attrNode->attr) << ", " << valueExpr << ")";
        return true;
    } else if (auto* subNode = dynamic_cast<SubscriptNode*>(target)) {
        *out_ << "env->setItem(";
        if (!generateNode(subNode->value.get())) return false;
        *out_ << ", ";
        if (!generateNode(subNode->index.get())) return false;
        *out_ << ", " << valueExpr << ")";
        return true;
    }
    const std::vector<std::unique_ptr<ASTNode>>* elements = nullptr;
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(target)) elements = &tup->elements;
    else if (auto* lst = dynamic_cast<ListLiteralNode*>(target)) elements = &lst->elements;
    if (elements) {
        // `a, b = v` / `a, *rest = v`: the values, unpacked as CPython does
        // (__unpack), are kept in temporary slots while the targets bind.
        const size_t n = elements->size();
        int star = -1;
        for (size_t i = 0; i < n; ++i) if (dynamic_cast<StarredNode*>((*elements)[i].get())) star = static_cast<int>(i);
        const int first = allocTempSlot();
        for (size_t i = 1; i < n; ++i) allocTempSlot();
        *out_ << "__unpack(ctx, env, " << valueExpr << ", " << n << "u, " << star << ", &__slot[" << first << "]);\n";
        for (size_t i = 0; i < n; ++i) {
            ASTNode* t = (*elements)[i].get();
            if (auto* st = dynamic_cast<StarredNode*>(t)) t = st->value.get();
            *out_ << "    ";
            if (!generateAssignToTarget(t, "__slot[" + std::to_string(first + static_cast<int>(i)) + "]")) return false;
            *out_ << ";\n";
        }
        return true;
    }
    *out_ << "/* Unsupported assignment target */";
    return true;
}

void CppGenerator::emitAssignName(const std::string& name, const std::string& valueExpr) {
    // A function local is its slot, a cell or free variable its cell (the
    // write is seen by every scope sharing the cell); any other binding goes
    // through env->storeName to the current frame: the module namespace, or
    // the class namespace while a class body runs.  An internal name (a
    // method's storage key) is stored in the class namespace.
    if (inClassBody_ && name.rfind("__protopyc_fs_", 0) == 0) {
        *out_ << "env->storeName(\"" << name << "\", " << valueExpr << ")";
        return;
    }
    switch (nameKind(name)) {
        case NameKind::Local:
            *out_ << "local_" << name << " = " << valueExpr;
            return;
        case NameKind::Cell:
        case NameKind::Free:
            *out_ << "env->cellSet(ctx, local_" << name << ", " << valueExpr << ")";
            return;
        default:
            *out_ << "env->storeName(\"" << name << "\", " << valueExpr << ")";
            return;
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
        *out_ << "if (__resume != 0 ? " << resumeIn(base + 1, nBody) << " : __truth(env, ";
    } else {
        *out_ << "if (__truth(env, ";
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
            *out_ << "    if (__resume != 0 ? " << resumeIn(base + nBody + 1, nElse) << " : !__truth(env, ";
        } else {
            *out_ << "    if (!__truth(env, ";
        }
        if (!generateNode(n->test.get())) return false;
        *out_ << ")) {\n";
        *out_ << "        __while_done_" << id << " = true;\n";
        *out_ << "        break;\n";
        *out_ << "    }\n";
        ctl_.push_back({true, 0, -1});
        const bool bodyOk = generateBody(n->body.get());
        ctl_.pop_back();
        if (!bodyOk) return false;
        *out_ << ";\n}\n";
        *out_ << "if (__while_done_" << id << ") {\n";
        if (!generateBody(n->orelse.get())) return false;
        *out_ << ";\n}\n";
        *out_ << "}\n";
    } else {
        *out_ << (resumable ? "while (__resume != 0 || __truth(env, " : "while (__truth(env, ");
        if (!generateNode(n->test.get())) return false;
        *out_ << ")) {\n";
        emitLoopSafepoint();
        ctl_.push_back({true, 0, -1});
        const bool bodyOk = generateBody(n->body.get());
        ctl_.pop_back();
        if (!bodyOk) return false;
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
    // `a in b` / `a not in b`: the interpreter's COMPARE_OP 6 / 7
    // (__contains__, else iteration).  They fell through to binaryOp,
    // which concatenated: `"a" in "abc"` was "aabc".
    if (n->op == TokenType::In || n->op == TokenType::NotIn) {
        *out_ << "env->compareOperation(ctx, ";
        if (!generateNode(n->left.get())) return false;
        *out_ << ", ";
        if (!generateNode(n->right.get())) return false;
        *out_ << ", " << (n->op == TokenType::In ? 6 : 7) << ")";
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
              << "__truth(env, __a)) return __a;\n";
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
    (void)n;
    emitExit(2, std::string());
    return true;
}

bool CppGenerator::generateContinue(ContinueNode* n) {
    (void)n;
    emitExit(3, std::string());
    return true;
}

// ---------------------------------------------------------------------------
// Exceptions and non-local exits.
//
// An operation that raises throws protoPython::PyException, which leaves
// the statement and its statement list (kGeneratedPreamble).  A `try`
// statement catches it around its body and around its other clauses, keeps
// it in a slot and runs its handlers or its finally clause after the C++
// try, outside the catch block; an exception that is not handled is thrown
// again.  `return`, `break` and `continue` inside a `try` statement with a
// finally clause, or inside a `with` statement, store their kind (and a
// return value, in the callable's return slot) and jump to the finally
// clause, or the __exit__ call; once it has run, the exit carries on
// outwards from there (emitExitDispatch), unless the clause raised or left
// by its own exit.
// ---------------------------------------------------------------------------

bool CppGenerator::emitStatement(ASTNode* stmt) {
    if (stmt->line > 0) emitLineDirective(stmt->line, filename_);
    beginStatement();
    if (!generateNode(stmt)) return false;
    *out_ << ";\n";
    return true;
}

void CppGenerator::emitExit(int kind, const std::string& returnValue) {
    for (auto it = ctl_.rbegin(); it != ctl_.rend(); ++it) {
        if (it->isLoop) {
            // break / continue reach their loop with no finally clause in
            // between; a return passes loops.
            if (kind == 2) { *out_ << "break"; return; }
            if (kind == 3) { *out_ << "continue"; return; }
            continue;
        }
        if (kind == 1) it->exitReturn = true;
        if (kind == 2) it->exitBreak = true;
        if (kind == 3) it->exitContinue = true;
        *out_ << "{\n";
        if (kind == 1) *out_ << "    __slot[" << returnSlot_ << "] = " << returnValue << ";\n";
        *out_ << "    __slot[" << it->exitSlot << "] = ctx->fromInteger(" << kind << ");\n";
        *out_ << "    goto __finally_" << it->id << ";\n";
        *out_ << "}";
        return;
    }
    if (kind == 1) { emitTerminalReturn(returnValue); return; }
    *out_ << (kind == 2 ? "break" : "continue");
}

void CppGenerator::emitTerminalReturn(const std::string& valueExpr) {
    if (inStateMachine_) {
        // A generator's return finishes it: the value travels in the
        // StopIteration that the resumer sees.  The StopIteration is raised
        // after the body (generateDef, `__gen_return`), not here: raised here
        // it would reach the except clauses of the try statements around the
        // return.
        *out_ << "{\n";
        *out_ << "    __genReturn = " << valueExpr << ";\n";
        *out_ << "    goto __gen_return;\n";
        *out_ << "}";
        return;
    }
    *out_ << "return " << valueExpr;
}

void CppGenerator::emitExitDispatch(const ControlEntry& entry) {
    // Only the exits that jumped to this finally clause are dispatched: a
    // `break` emitted where there is no loop would not compile.  The code
    // runs only when the finally clause (or __exit__) did not raise.
    if (!entry.exitReturn && !entry.exitBreak && !entry.exitContinue) return;
    *out_ << "{\n";
    const std::string why = "__slot[" + std::to_string(entry.exitSlot) + "]";
    const std::pair<int, bool> kinds[] = {{1, entry.exitReturn}, {2, entry.exitBreak}, {3, entry.exitContinue}};
    for (const auto& k : kinds) {
        if (!k.second) continue;
        *out_ << "    if (" << why << " == ctx->fromInteger(" << k.first << ")) ";
        emitExit(k.first, "__slot[" + std::to_string(returnSlot_) + "]");
        *out_ << ";\n";
    }
    *out_ << "}\n";
}

bool CppGenerator::needsReturnSlot(ASTNode* node) const {
    // A `try` statement with a finally clause, or a `with` statement, in the
    // callable's own body (nested callables have their own).
    if (!node) return false;
    if (dynamic_cast<WithNode*>(node)) return true;
    if (auto* n = dynamic_cast<TryNode*>(node)) {
        if (n->finalbody) return true;
        if (needsReturnSlot(n->body.get()) || needsReturnSlot(n->orelse.get())) return true;
        for (auto& h : n->handlers) if (needsReturnSlot(h.body.get())) return true;
        return false;
    }
    if (auto* n = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : n->statements) if (needsReturnSlot(st.get())) return true;
        return false;
    }
    if (auto* n = dynamic_cast<IfNode*>(node)) return needsReturnSlot(n->body.get()) || needsReturnSlot(n->orelse.get());
    if (auto* n = dynamic_cast<WhileNode*>(node)) return needsReturnSlot(n->body.get()) || needsReturnSlot(n->orelse.get());
    if (auto* n = dynamic_cast<ForNode*>(node)) return needsReturnSlot(n->body.get()) || needsReturnSlot(n->orelse.get());
    return false;
}

bool CppGenerator::generateFunctionDef(FunctionDefNode* n) {
    return generateDef(n, false);
}

bool CppGenerator::generateAsyncFunctionDef(AsyncFunctionDefNode* n) {
    return generateDef(n, true);
}

std::vector<std::string> CppGenerator::slotLayout(const ScopeInfo* s, const std::vector<std::string>& params) const {
    std::vector<std::string> layout = params;
    std::unordered_set<std::string> placed(params.begin(), params.end());
    std::vector<std::string> others;
    if (s) {
        for (const auto& b : s->bound)
            if (!placed.count(b) && !s->globals.count(b) && !s->nonlocals.count(b) && !s->frees.count(b)) others.push_back(b);
    }
    std::sort(others.begin(), others.end());
    for (const auto& o : others) { layout.push_back(o); placed.insert(o); }
    if (s) for (const auto& f : s->frees) if (!placed.count(f)) layout.push_back(f);
    return layout;
}

// The prologue of a compiled callable: binds the arguments as CPython does
// (PythonEnvironment::bindArguments: positional, keyword, defaults stored
// in the function storage at the definition, keyword-only, *args, **kwargs,
// and CPython's TypeErrors), makes the cell variables' cells and loads the
// free variables' cells from the function storage.
bool CppGenerator::emitPrologue(const ScopeInfo* s, const std::vector<std::string>& params,
                                const std::vector<std::string>& layout, const std::string& qualname,
                                size_t nPositional, int nPosOnly, size_t nKwOnly,
                                bool hasVarargs, bool hasKwargs, const std::string& indent) {
    static int sigCounter = 0;
    const int id = ++sigCounter;
    // The parameter names, interned once (an interned string is perennial;
    // a function-local static is initialised once, thread-safely).
    *out_ << indent << "static const proto::ProtoString* const __pnames_" << id << "[] = {";
    for (size_t i = 0; i < params.size(); ++i)
        *out_ << (i ? ", " : "") << "protoPython::PythonEnvironment::getInternedString(ctx, \"" << params[i] << "\")";
    if (params.empty()) *out_ << "nullptr";
    *out_ << "};\n";
    *out_ << indent << "static const protoPython::PythonEnvironment::CompiledSignature __sig_" << id << "{\""
          << escapeCppString(qualname) << "\", " << nPositional << "u, " << nPosOnly << "u, " << nKwOnly << "u, "
          << (hasVarargs ? "true" : "false") << ", " << (hasKwargs ? "true" : "false") << ", __pnames_" << id << "};\n";
    const bool simple = nKwOnly == 0 && !hasVarargs && !hasKwargs;
    if (simple) {
        // A call with exactly the positional parameters (the common case)
        // binds them directly; anything else takes bindArguments, which
        // raises CPython's TypeError when the call does not match.
        *out_ << indent << "if ((!kwargs || kwargs->getSize(ctx) == 0) && args && args->getSize(ctx) == " << nPositional << "u) {\n";
        for (size_t i = 0; i < nPositional; ++i)
            *out_ << indent << "    __slot[" << i << "] = args->getAt(ctx, " << i << ");\n";
        *out_ << indent << "} else {\n";
        *out_ << indent << "    (void)env->bindArguments(ctx, __sig_" << id << ", args, kwargs, __fs, __slot);\n";
        *out_ << indent << "}\n";
    } else {
        *out_ << indent << "(void)env->bindArguments(ctx, __sig_" << id << ", args, kwargs, __fs, __slot);\n";
    }
    for (size_t i = 0; i < layout.size(); ++i) {
        const std::string& v = layout[i];
        if (s && s->cells.count(v)) {
            // The cell holds the parameter's value; a local's starts empty.
            const bool isParam = i < params.size();
            *out_ << indent << "__slot[" << i << "] = env->newCell(ctx, " << (isParam ? "__slot[" + std::to_string(i) + "]" : std::string("nullptr")) << ");\n";
        } else if (s && s->frees.count(v)) {
            *out_ << indent << "__slot[" << i << "] = __freeVar(ctx, __fs, \"" << v << "\");\n";
        }
    }
    return true;
}

template <class DefNode>
bool CppGenerator::generateDef(DefNode* n, bool isAsync) {
    (void)isAsync;
    const std::string& name = n->name;
    ASTNode* body = n->body.get();
    ScopeInfo* const fscope = scopeOf_.count(n) ? scopeOf_[n] : nullptr;
    // Save state for nested functions
    bool oldInStateMachine = inStateMachine_;
    int oldStateCount = stateCount_;
    bool oldInClassBody = inClassBody_;
    auto oldOrderedLocals = orderedLocalVars_;
    auto oldLocalVars = localVars_;
    ScopeInfo* const oldScope = scope_;
    // Loops and finally clauses do not extend into a nested callable.
    auto oldCtl = std::move(ctl_);
    ctl_.clear();
    const int oldReturnSlot = returnSlot_;
    const bool needsReturn = needsReturnSlot(body);

    // 1. The slots: the parameters in signature order (positional,
    // keyword-only, *args, **kwargs), the other locals, the free variables.
    std::vector<std::string> params = n->parameters;
    for (const auto& k : n->kwonlyargs) params.push_back(k);
    if (!n->vararg.empty()) params.push_back(n->vararg);
    if (!n->kwarg.empty()) params.push_back(n->kwarg);
    const std::vector<std::string> slotLocals = slotLayout(fscope, params);
    orderedLocalVars_ = slotLocals;
    localVars_ = std::unordered_set<std::string>(slotLocals.begin(), slotLocals.end());
    scope_ = fscope;

    const bool isGenerator = containsYieldOrAwait(body) || countYields(body) > 0;
    inStateMachine_ = isGenerator;
    stateCount_ = 0;
    inClassBody_ = false;

    // Use a unique suffix for top-level C++ function names
    static int functionCounter = 0;
    std::string uniqueSuffix = "_" + std::to_string(++functionCounter);
    std::string cppFuncName = "py_func_" + name + uniqueSuffix;
    std::string cppContName = "py_cont_" + name + uniqueSuffix;
    // A def in a class body is a method: the interpreter calls it with the
    // instance as `self`, and its storage is found through the instance's
    // class under this key (__methodStorage).
    const bool isMethod = oldInClassBody;
    const std::string methodStorageKey = "__protopyc_fs_" + cppFuncName + "__";
    // Zero-argument super() in the body binds to the first parameter of a
    // method; the qualified name of what the body defines continues this one.
    const std::string oldMethodSelfParam = methodSelfParam_;
    methodSelfParam_ = (isMethod && !n->parameters.empty()) ? n->parameters[0] : std::string();
    const bool oldInMethod = inMethod_;
    inMethod_ = isMethod;
    const std::string oldQualnamePrefix = qualnamePrefix_;
    const std::string qualname = qualnamePrefix_.empty() ? name : qualnamePrefix_ + "." + name;
    qualnamePrefix_ = qualname + ".<locals>";

    // A recursive def that only this def binds (ScopeInfo::selfCalls): a call
    // by its name runs the body directly (generateCall), without looking the
    // name up -- and without capturing it, which would close a cycle.
    std::string oldCurrentFuncName = currentFuncName_;
    std::string oldCurrentFuncCppName = currentFuncCppName_;
    if (fscope && fscope->selfCalls && !isMethod) {
        currentFuncName_ = name;
        currentFuncCppName_ = cppFuncName;
    } else {
        currentFuncName_.clear();
        currentFuncCppName_.clear();
    }

    std::ostream* oldOut = out_;
    std::ostringstream funcStream;
    out_ = &funcStream;
    // A generator's continuation calls the generator function's body (a
    // recursive call by name), which is emitted after it.
    *out_ << "\nstatic const proto::ProtoObject* " << cppFuncName << "_body(proto::ProtoContext*, const proto::ProtoObject*, const proto::ProtoObject*, const proto::ProtoList*, const proto::ProtoSparseList*);\n";

    // Every compiled callable runs in a context of its own, with its locals
    // in that context's automatic slots (see enterSlotFrame), and in the
    // namespace of the module that defined it (see kGeneratedPreamble).  Its
    // body is generated first, into `inner`, because the slot count is known
    // only afterwards; it then runs as a lambda, so a Python `return` leaves
    // the lambda and the result is promoted to the caller's context before
    // the scope closes.
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
        *out_ << "\nstatic const proto::ProtoObject* " << cppName << "_body(proto::ProtoContext* __callerCtx, const proto::ProtoObject* __fs, const proto::ProtoObject* __receiver, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {\n";
        *out_ << openContextScope("__callerCtx");
        *out_ << "    auto* env = protoPython::PythonEnvironment::get(ctx);\n";
        *out_ << "    (void)kwargs;\n";
        *out_ << "    if (__receiver) args = __withReceiver(ctx, __receiver, args);\n";
        emitLocalRefs();
        *out_ << "    const proto::ProtoObject* __result = nullptr;\n";
        *out_ << "    try {\n";
        *out_ << "        __result = [&]() -> const proto::ProtoObject* {\n";
        *out_ << innerText;
        *out_ << "        }();\n";
        // A Python exception (protoPython::PyException) propagates to the
        // caller as it is.
        *out_ << "    } catch (const proto::ProtoObject* __thrown) {\n";
        *out_ << "        protoPython::promote(ctx, __thrown);\n";
        *out_ << "        throw;\n";
        *out_ << "    }\n";
        *out_ << "    protoPython::promote(ctx, __result);\n";
        *out_ << "    return __result;\n";
        *out_ << "}\n";
        *out_ << "\nconst proto::ProtoObject* " << cppName << "(proto::ProtoContext* __callerCtx, const proto::ProtoObject* self, const proto::ParentLink* pl, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs) {\n";
        *out_ << "    (void)pl;\n";
        *out_ << "    const bool __isStorage = __isFunctionStorage(__callerCtx, self);\n";
        if (isMethod && cppName == cppFuncName) {
            // Called with an instance as `self`: its class holds the storage
            // (kGeneratedPreamble, __methodStorage).  Interned strings are
            // perennial: the key is looked up once.
            *out_ << "    static const proto::ProtoString* __fsKey = nullptr;\n";
            *out_ << "    if (!__isStorage && !__fsKey) __fsKey = protoPython::PythonEnvironment::getInternedString(__callerCtx, \""
                  << methodStorageKey << "\");\n";
            *out_ << "    const proto::ProtoObject* __fs = __isStorage ? self : __methodStorage(__callerCtx, self, __fsKey);\n";
        } else {
            *out_ << "    const proto::ProtoObject* __fs = __isStorage ? self : nullptr;\n";
        }
        *out_ << "    protoPython::ModuleNamespaceScope __ns(__moduleNamespaceOf(__callerCtx, __fs));\n";
        *out_ << "    return " << cppName << "_body(__callerCtx, __fs, __isStorage ? nullptr : self,\n"
              << "        args ? args->asList(__callerCtx) : nullptr, kwargs ? kwargs->asSparseList(__callerCtx) : nullptr);\n";
        *out_ << "}\n";
    };
    auto emitBindings = [&]() -> bool {
        return emitPrologue(fscope, params, slotLocals, qualname, n->parameters.size(), n->posonlyargcount,
                            n->kwonlyargs.size(), !n->vararg.empty(), !n->kwarg.empty(), "    ");
    };

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
        // continuation's slots -- the locals (cells included), and the
        // anchors of the statements the yield is inside (a for loop's
        // iterator, a caught exception).  A resumption restores the slots and
        // re-enters the body; every statement on the path to the suspended
        // yield is re-entered without re-evaluating what it evaluated before
        // the yield (see emitStatementList).
        // A return value in transit through finally clauses has a slot
        // above the locals.
        const SlotFrame contSlots = enterSlotFrame(static_cast<int>(slotLocals.size()) + (needsReturn ? 1 : 0));
        returnSlot_ = needsReturn ? static_cast<int>(slotLocals.size()) : -1;
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
        // A finished generator raises the exception throw() passes, or
        // StopIteration.
        *out_ << "    if (__resume < 0) {\n";
        *out_ << "        if (__throw) throw protoPython::PyException(env, __throw);\n";
        *out_ << "        env->raiseStopIteration(ctx, PROTO_NONE);\n";
        *out_ << "        return nullptr;\n";
        *out_ << "    }\n";
        // throw() or close() before the first resumption: the body never runs.
        *out_ << "    if (__resume == 0 && __throw) {\n";
        *out_ << "        __finish();\n";
        *out_ << "        throw protoPython::PyException(env, __throw);\n";
        *out_ << "    }\n";
        *out_ << "    {\n";
        *out_ << "        const proto::ProtoObject* __saved = env->getAttribute(ctx, gen, env->getGiLocalsString());\n";
        *out_ << "        const proto::ProtoList* __savedList = (__saved && __saved != PROTO_NONE) ? __saved->asList(ctx) : nullptr;\n";
        *out_ << "        const proto::proto_ulong __n = __savedList ? std::min<proto::proto_ulong>(__savedList->getSize(ctx), __nslots) : 0;\n";
        *out_ << "        for (proto::proto_ulong __i = 0; __i < __n; ++__i) __slot[__i] = __savedList->getAt(ctx, static_cast<int>(__i));\n";
        *out_ << "    }\n";
        // The body finishes the generator when it raises, returns
        // (emitTerminalReturn jumps to __gen_return with the value) or runs
        // to its end; a return and the end raise StopIteration, the
        // completion that py_generator_send_impl passes to the resumer.
        *out_ << "    const proto::ProtoObject* __genReturn = PROTO_NONE;\n";
        *out_ << "    try {\n";
        beginStatement();
        ok = generateBody(body);
        if (ok) {
            *out_ << ";\n";
            *out_ << "    } catch (const proto::ProtoException&) {\n";
            *out_ << "        __finish();\n";
            *out_ << "        throw;\n";
            *out_ << "    }\n";
            // The label only when a return jumps to it (an unused label is
            // a warning).
            if (contBody.str().find("goto __gen_return;") != std::string::npos) *out_ << "__gen_return:\n";
            *out_ << "    __finish();\n";
            *out_ << "    env->raiseStopIteration(ctx, __genReturn);\n";
            *out_ << "    return nullptr;\n";
            out_ = &funcStream;
            emitScopedCall(cppContName, contBody.str());
        }
        leaveSlotFrame(contSlots);

        // The generator function: binds its arguments into its slots, which
        // become the continuation's initial gi_locals (same slot layout).
        if (ok) {
            returnSlot_ = -1;
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
        const SlotFrame funcSlots = enterSlotFrame(static_cast<int>(slotLocals.size()) + (needsReturn ? 1 : 0));
        returnSlot_ = needsReturn ? static_cast<int>(slotLocals.size()) : -1;
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

    // Restore the enclosing scope's state: the registration below (the
    // defaults, the decorators and the binding of the name) runs in the
    // enclosing scope.
    inStateMachine_ = oldInStateMachine;
    stateCount_ = oldStateCount;
    inClassBody_ = oldInClassBody;
    orderedLocalVars_ = oldOrderedLocals;
    localVars_ = oldLocalVars;
    scope_ = oldScope;
    currentFuncName_ = oldCurrentFuncName;
    currentFuncCppName_ = oldCurrentFuncCppName;
    methodSelfParam_ = oldMethodSelfParam;
    inMethod_ = oldInMethod;
    qualnamePrefix_ = oldQualnamePrefix;
    ctl_ = std::move(oldCtl);
    returnSlot_ = oldReturnSlot;
    out_ = oldOut;
    if (!ok) return false;

    // Append to header
    header_ << funcStream.str();

    // Registration in the enclosing scope: the function object is a method
    // bound to its storage -- the module namespace, the cells of its free
    // variables (the enclosing scope's `local_<name>` is that cell) and its
    // default values, evaluated here as CPython evaluates them at the
    // definition (see kGeneratedPreamble).  The storage is kept in an anchor
    // slot while it is built.
    const int fsSlot = pushAnchor();
    *out_ << "{\n";
    *out_ << "    __slot[" << fsSlot << "] = __newFunctionStorage(ctx);\n";
    if (fscope) {
        for (const auto& fv : fscope->frees) {
            *out_ << "    __slot[" << fsSlot << "] = __withFreeVar(ctx, __slot[" << fsSlot << "], \"" << fv << "\", local_" << fv << ");\n";
        }
    }
    if (isMethod && classCellSlot_ >= 0) {
        // The class cell, for zero-argument super() (see generateClassDef).
        *out_ << "    __slot[" << fsSlot << "] = __withFreeVar(ctx, __slot[" << fsSlot << "], \"__class__\", __slot[" << classCellSlot_ << "]);\n";
    }
    if (!n->defaults.empty()) {
        beginStatement();
        *out_ << "    __slot[" << fsSlot << "] = __withDefaults(ctx, __slot[" << fsSlot << "], {";
        for (size_t i = 0; i < n->defaults.size(); ++i) {
            if (i) *out_ << ", ";
            if (!generateNode(n->defaults[i].get())) return false;
        }
        *out_ << "});\n";
    }
    for (size_t i = 0; i < n->kw_defaults.size() && i < n->kwonlyargs.size(); ++i) {
        if (!n->kw_defaults[i]) continue;
        beginStatement();
        *out_ << "    __slot[" << fsSlot << "] = __withKwDefault(ctx, __slot[" << fsSlot << "], \"" << n->kwonlyargs[i] << "\", ";
        if (!generateNode(n->kw_defaults[i].get())) return false;
        *out_ << ");\n";
    }
    beginStatement();
    *out_ << "    const proto::ProtoObject* __fn = ctx->fromMethod(const_cast<proto::ProtoObject*>(__slot[" << fsSlot << "]), "
          << cppFuncName << ");\n";
    if (isMethod) {
        // The class namespace also keeps the storage under the method's own
        // key (see __methodStorage).
        *out_ << "    ";
        emitAssignName(methodStorageKey, "__slot[" + std::to_string(fsSlot) + "]");
        *out_ << ";\n";
    }
    for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
        *out_ << "    __fn = env->callObject(";
        if (!generateNode(it->get())) return false;
        *out_ << ", {__fn});\n";
    }
    *out_ << "    ";
    emitAssignName(name, "__fn");
    *out_ << ";\n}";
    popAnchor();
    return true;
}

bool CppGenerator::generateClassDef(ClassDefNode* n) {
    // A compiled class is built as the interpreter builds one
    // (PythonEnvironment::buildClass): the metaclass is chosen and
    // __prepare__ called, the body fills the namespace, and the metaclass
    // call makes the class (its __mro__, __init_subclass__, __set_name__).
    *out_ << "\n    // Class definition: " << n->name << "\n";
    *out_ << "    {\n";
    // Bases, keywords, the namespace (then the class) and the class cell are
    // used across the class body, which may loop: each is kept in an anchor
    // slot.
    std::vector<int> classAnchors;
    const int basesSlot = pushAnchor();
    classAnchors.push_back(basesSlot);
    *out_ << "    __slot[" << basesSlot << "] = ctx->newList()->asObject(ctx);\n";
    for (auto& b : n->bases) {
        if (auto* st = dynamic_cast<StarredNode*>(b.get())) {
            *out_ << "    __extendArgs(ctx, env, &__slot[" << basesSlot << "], ";
            if (!generateNode(st->value.get())) return false;
            *out_ << ");\n";
        } else {
            *out_ << "    { const proto::ProtoObject* __b = ";
            if (!generateNode(b.get())) return false;
            *out_ << "; __slot[" << basesSlot << "] = __slot[" << basesSlot
                  << "]->asList(ctx)->appendLast(ctx, __b)->asObject(ctx); }\n";
        }
        beginStatement();
    }
    *out_ << "    __slot[" << basesSlot << "] = env->makeTuple(ctx, __slot[" << basesSlot
          << "]->asList(ctx));\n";
    int kwdsSlot = -1;
    if (!n->keywords.empty()) {
        // class C(metaclass=M, **kw): a dict of the keywords.
        kwdsSlot = pushAnchor();
        classAnchors.push_back(kwdsSlot);
        *out_ << "    __slot[" << kwdsSlot << "] = env->callObject(env->lookupName(\"dict\"), {});\n";
        for (auto& kw : n->keywords) {
            if (kw.first.empty()) {
                *out_ << "    env->callMethod(__slot[" << kwdsSlot << "], \"update\", {";
                if (!generateNode(kw.second.get())) return false;
                *out_ << "});\n";
            } else {
                *out_ << "    env->setItem(__slot[" << kwdsSlot << "], ctx->fromUTF8String(\""
                      << escapeCppString(kw.first) << "\"), ";
                if (!generateNode(kw.second.get())) return false;
                *out_ << ");\n";
            }
            beginStatement();
        }
    }
    const int nsSlot = pushAnchor();
    classAnchors.push_back(nsSlot);
    // The methods' `__class__` cell (zero-argument super()): left in the
    // namespace as `__classcell__`, which type.__new__ fills with the class
    // before __init_subclass__ runs, as in CPython.
    const int cellSlot = pushAnchor();
    classAnchors.push_back(cellSlot);
    *out_ << "    __slot[" << cellSlot << "] = env->newCell(ctx, nullptr);\n";

    const std::string qualname = qualnamePrefix_.empty() ? n->name : qualnamePrefix_ + "." + n->name;
    *out_ << "    const proto::ProtoObject* var_cls_" << n->name << " = env->buildClass(ctx, "
          << "protoPython::PythonEnvironment::getInternedString(ctx, \"" << n->name << "\")->asObject(ctx), "
          << "__slot[" << basesSlot << "], "
          << (kwdsSlot >= 0 ? "__slot[" + std::to_string(kwdsSlot) + "]" : std::string("PROTO_NONE")) << ",\n";
    *out_ << "        [&](proto::ProtoObject*& __ns) -> bool {\n";
    // An exception the body raises propagates through buildClass.
    *out_ << "        __FrameScope __classFrame(__ns);\n";
    *out_ << "        env->storeName(\"__qualname__\", protoPython::PythonEnvironment::getInternedString(ctx, \""
          << qualname << "\")->asObject(ctx));\n";
    bool hasAnnotations = false;
    if (auto* suite = dynamic_cast<SuiteNode*>(n->body.get())) {
        for (auto& st : suite->statements) {
            if (dynamic_cast<AnnAssignNode*>(st.get())) { hasAnnotations = true; break; }
        }
    }
    if (hasAnnotations) {
        *out_ << "        env->storeName(\"__annotations__\", env->callObject(env->lookupName(\"dict\"), {}));\n";
    }
    // Names the class body binds go to the class namespace (the current
    // frame), not to the enclosing function's locals.
    const bool oldInClassBody = inClassBody_;
    const int oldClassCellSlot = classCellSlot_;
    const std::string oldQualnamePrefix = qualnamePrefix_;
    ScopeInfo* const oldScope = scope_;
    inClassBody_ = true;
    classCellSlot_ = cellSlot;
    qualnamePrefix_ = qualname;
    if (scopeOf_.count(n)) scope_ = scopeOf_[n];
    beginStatement();
    const bool bodyOk = generateNode(n->body.get());
    inClassBody_ = oldInClassBody;
    classCellSlot_ = oldClassCellSlot;
    qualnamePrefix_ = oldQualnamePrefix;
    scope_ = oldScope;
    if (!bodyOk) return false;
    *out_ << ";\n";
    *out_ << "        env->storeName(\"__classcell__\", __slot[" << cellSlot << "]);\n";
    // The body may have rebound the frame (a namespace that is not mutable).
    *out_ << "        __ns = const_cast<proto::ProtoObject*>(protoPython::PythonEnvironment::getCurrentFrame());\n";
    *out_ << "        return true;\n";
    *out_ << "    }, &__slot[" << nsSlot << "]);\n";
    *out_ << "    __slot[" << nsSlot << "] = var_cls_" << n->name << ";\n";

    // Apply decorators Bottom-to-Top
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
            beginStatement();
            *out_ << "    var_cls_" << n->name << " = env->callObject(";
            if (!generateNode(it->get())) return false;
            *out_ << ", {var_cls_" << n->name << "});\n";
            *out_ << "    __slot[" << nsSlot << "] = var_cls_" << n->name << ";\n";
        }
    }

    *out_ << "    ";
    emitAssignName(n->name, "var_cls_" + n->name);
    *out_ << ";\n";
    *out_ << "    }\n";
    for (size_t i = 0; i < classAnchors.size(); ++i) popAnchor();
    return true;
}

bool CppGenerator::generateAugAssign(AugAssignNode* n) {
    if (auto* nameNode = dynamic_cast<NameNode*>(n->target.get())) {
        // Function-local case: lower to a C++ assignment using the
        // existing `local_<name>` and env->binaryOp, so the new value
        // updates the C++ local in place (no env round-trip).  Module
        // / global case still goes through env->augAssignName.
        const NameKind kind = nameKind(nameNode->id);
        if (kind == NameKind::Cell || kind == NameKind::Free) {
            // x op= v on a cell: read the cell, compute, write the cell.
            *out_ << "env->cellSet(ctx, local_" << nameNode->id << ", env->binaryOp("
                  << nameRead(nameNode->id) << ", protoPython::TokenType::";
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
            *out_ << "))";
            return true;
        }
        if (kind == NameKind::Local) {
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
    // The value is computed first (an exception it raises propagates), then
    // the return leaves through the enclosing finally clauses, if any
    // (emitExit).  A generator's return finishes it (emitTerminalReturn).
    std::string value = "PROTO_NONE";
    if (n->value) {
        std::ostringstream valueText;
        std::ostream* outer = out_;
        out_ = &valueText;
        const bool ok = generateNode(n->value.get());
        out_ = outer;
        if (!ok) return false;
        value = valueText.str();
    }
    if (inStateMachine_ || returnSlot_ >= 0) {
        emitExit(1, value);
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
    *out_ << "return " << value;
    return true;
}

// An attribute name as an interned string, looked up once per site (interned
// strings are perennial; a function-local static is initialized once, also
// when several threads reach it).
std::string CppGenerator::internedKey(const std::string& name) {
    return "([&]() { static const proto::ProtoString* const __k = "
           "protoPython::PythonEnvironment::getInternedString(ctx, \"" + name + "\"); return __k; }())";
}

bool CppGenerator::generateAttribute(AttributeNode* n) {
    // Python's attribute read (descriptors, __getattr__, AttributeError).
    *out_ << "env->loadAttr(ctx, ";
    if (!generateNode(n->value.get())) return false;
    *out_ << ", " << internedKey(n->attr) << ")";
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
    // A ProtoList-backed tuple (PythonEnvironment::makeTuple): a ProtoTuple
    // is interned and never collected.
    *out_ << "        return env->makeTuple(ctx, lst);\n";
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
    // ordinary item); an exception it raises propagates.
    *out_ << "            auto* __val_" << id << " = env->next(__iter_" << id << ");\n";
    *out_ << "            if (!__val_" << id << ") {\n";
    if (n->orelse) *out_ << "                __for_done_" << id << " = true;\n";
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

    ctl_.push_back({true, 0, -1});
    const bool bodyOk = generateBody(n->body.get());
    ctl_.pop_back();
    if (!bodyOk) return false;
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
    // A Python try is a C++ try.  The body runs in a C++ try whose catch
    // keeps the exception (protoPython::PyException) in an anchor slot; the
    // handlers then run after it, outside the catch block: the first one
    // that matches handles the exception (for sys.exc_info() and a bare
    // `raise`, until it is left), and an unmatched one is thrown again.  An
    // exception raised while matching or by a handler has the handled one as
    // its __context__ (PEP 3134).  The else clause runs when the body raised
    // nothing.  With a finally clause, the body, the handlers and the else
    // clause are inside another C++ try, whose catch sets the propagating
    // exception aside in a slot; the finally clause always runs, and that
    // exception is thrown again afterwards unless the clause raised its own
    // or left by `return`, `break` or `continue`.  A `return`, `break` or
    // `continue` that left the other clauses jumps to the finally clause and
    // carries on after it (emitExit, emitExitDispatch).
    //
    // In a generator a resumption re-enters the clause that holds the
    // suspended yield; no clause runs inside a catch block, so every clause
    // can be re-entered.  The caught exception, the one the finally clause
    // set aside and the pending exit are in anchor slots, which are restored
    // with the others; a slot holds None when there is no exception.
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
    const bool hasHandlers = !n->handlers.empty();

    // A clause runs normally, or when a resumption re-enters it.
    auto openClause = [&](int first, int count) {
        if (resumable) *out_ << "if (__resume == 0 || " << resumeIn(first, count) << ") ";
        *out_ << "{\n";
    };

    *out_ << "{\n";
    // The C++ exceptions the clauses caught, held in this frame so that one
    // no clause selects is thrown again unchanged (__pyRethrow).
    const std::string bodyCaught = "__caught" + std::to_string(caughtCounter_++);
    const std::string finallyCaught = "__caught" + std::to_string(caughtCounter_++);
    if (hasHandlers) *out_ << "std::exception_ptr " << bodyCaught << ";\n";
    if (n->finalbody) *out_ << "std::exception_ptr " << finallyCaught << ";\n";
    // With a finally clause, the exits that leave the other clauses go
    // through it: they are recorded in an anchor slot, and the exception
    // that propagates out of them in another.
    ControlEntry fin{false, 0, -1};
    int propagatingSlot = -1;
    if (n->finalbody) {
        fin.id = finallyCounter_++;
        fin.exitSlot = pushAnchor();
        propagatingSlot = pushAnchor();
        *out_ << "if (" << resumeIdle << ") {\n";
        *out_ << "    __slot[" << fin.exitSlot << "] = PROTO_NONE;\n";
        *out_ << "    __slot[" << propagatingSlot << "] = PROTO_NONE;\n";
        *out_ << "}\n";
        ctl_.push_back(fin);
        *out_ << "try {\n";
    }
    // The caught exception is used across the handlers' bodies, which may
    // loop: it is kept in an anchor slot.
    const int excSlot = hasHandlers ? pushAnchor() : -1;
    const std::string caught = "__slot[" + std::to_string(excSlot) + "]";
    if (hasHandlers) *out_ << "if (" << resumeIdle << ") " << caught << " = PROTO_NONE;\n";
    openClause(base + 1, nBody);
    if (hasHandlers) *out_ << "try {\n";
    bool ok = generateBody(n->body.get());
    *out_ << ";\n";
    if (hasHandlers) {
        *out_ << "} catch (const proto::ProtoException& __e) {\n";
        *out_ << "    " << caught << " = __e.value();\n";
        *out_ << "    " << bodyCaught << " = std::current_exception();\n";
        *out_ << "}\n";
    }
    *out_ << "}\n";
    int first = base + nBody + 1;
    if (ok && hasHandlers) {
        *out_ << "if (" << (nHandlers > 0 ? resumeIn(first, nHandlers) + " || " : std::string())
              << "(" << resumeIdle << " && __inFlight(" << caught << "))) {\n";
        *out_ << "    const proto::ProtoObject* __exc = " << caught << ";\n";
        *out_ << "    bool __handled = false;\n";
        *out_ << "    try {\n";
        for (size_t i = 0; ok && i < n->handlers.size(); ++i) {
            auto& h = n->handlers[i];
            *out_ << "    if (" << (nHandler[i] > 0 ? resumeIn(first, nHandler[i]) + " || " : std::string())
                  << "(" << resumeIdle << " && !__handled";
            if (h.type) {
                // Evaluating the type may raise: that exception replaces
                // the one being matched, as in the interpreter.
                *out_ << " && env->isException(__exc, ";
                if (!generateNode(h.type.get())) { ok = false; break; }
                *out_ << ")";
            }
            *out_ << ")) {\n";
            *out_ << "        __handled = true;\n";
            *out_ << "        __Handling __handling(env, ctx, __exc);\n";
            handlerCaught_.push_back({bodyCaught, "__exc"});
            if (!h.name.empty()) {
                *out_ << "        if (" << resumeIdle << ") ";
                emitAssignName(h.name, "__exc");
                *out_ << ";\n";
            }
            const bool bodyOk = generateBody(h.body.get());
            handlerCaught_.pop_back();
            if (!bodyOk) { ok = false; break; }
            *out_ << ";\n";
            *out_ << "    }\n";
            first += nHandler[i];
        }
        *out_ << "    if (!__handled) __pyRethrow(env, " << bodyCaught << ", __exc);\n";
        // Rethrowing the unmatched exception chains nothing: it is its own
        // context.
        *out_ << "    } catch (const proto::ProtoException& __e) {\n";
        *out_ << "        env->chainException(ctx, __e.value(), __exc);\n";
        *out_ << "        throw;\n";
        *out_ << "    }\n";
        *out_ << "}\n";
    }
    if (ok && n->orelse) {
        // Runs only when the body raised nothing (a resumption inside it
        // has been through the body already).
        *out_ << "if (" << (nElse > 0 ? resumeIn(first, nElse) + " || " : std::string()) << "(" << resumeIdle
              << (hasHandlers ? " && !__inFlight(" + caught + ")" : std::string()) << ")) {\n";
        ok = generateBody(n->orelse.get());
        *out_ << ";\n";
        *out_ << "}\n";
        first += nElse;
    }
    if (hasHandlers) popAnchor();
    if (ok && n->finalbody) {
        // The exits recorded above (exitReturn, ...) are known now.
        fin = ctl_.back();
        ctl_.pop_back();
        const std::string propagating = "__slot[" + std::to_string(propagatingSlot) + "]";
        *out_ << "} catch (const proto::ProtoException& __e) {\n";
        *out_ << "    " << propagating << " = __e.value();\n";
        *out_ << "    " << finallyCaught << " = std::current_exception();\n";
        *out_ << "}\n";
        *out_ << "__finally_" << fin.id << ": ;\n";
        // While the finally clause runs for an exception, that exception is
        // the active one (sys.exc_info()), and the __context__ of one the
        // clause raises.
        *out_ << "{\n";
        *out_ << "    __HandlingIf __finallyHandling(env, ctx, " << propagating << ");\n";
        openClause(first, nFinally);
        *out_ << "try {\n";
        handlerCaught_.push_back({finallyCaught, propagating});
        ok = generateBody(n->finalbody.get());
        handlerCaught_.pop_back();
        *out_ << ";\n";
        *out_ << "} catch (const proto::ProtoException& __e) {\n";
        *out_ << "    if (__inFlight(" << propagating << ")) env->chainException(ctx, __e.value(), " << propagating << ");\n";
        *out_ << "    throw;\n";
        *out_ << "}\n";
        *out_ << "}\n";
        *out_ << "}\n";
        *out_ << "if (__inFlight(" << propagating << ")) __pyRethrow(env, " << finallyCaught << ", " << propagating << ");\n";
        popAnchor();
        emitExitDispatch(fin);
        popAnchor();
    } else if (n->finalbody) {
        ctl_.pop_back();
        popAnchor();
        popAnchor();
    }
    *out_ << "    }";
    return ok;
}

bool CppGenerator::generateRaise(RaiseNode* n) {
    if (!n->exc) {
        // Inside a clause of this callable, the clause's own C++ exception.
        if (!handlerCaught_.empty() && !handlerCaught_.back().first.empty()) {
            *out_ << "__pyReraise(ctx, env, " << handlerCaught_.back().first << ", "
                  << handlerCaught_.back().second << ")";
        } else {
            *out_ << "__pyReraise(ctx, env)";
        }
        return true;
    }
    *out_ << "__pyRaise(ctx, env, ";
    if (!generateNode(n->exc.get())) return false;
    if (n->cause) {
        *out_ << ", true, ";
        if (!generateNode(n->cause.get())) return false;
    } else {
        *out_ << ", false, nullptr";
    }
    *out_ << ")";
    return true;
}

bool CppGenerator::generateWith(WithNode* n) {
    // In a generator, a resumption inside the body does not evaluate the
    // context expressions or call __enter__ again.
    const bool resumable = inStateMachine_ && countYields(n->body.get()) > 0;
    return emitWithItems(n, 0, resumable);
}

bool CppGenerator::emitWithItems(WithNode* n, size_t index, bool resumable) {
    // `with a as x, b as y: body` is `with a as x: with b as y: body`.  Each
    // item: evaluate the context manager, look up __exit__ and call
    // __enter__ (__withEnter), then bind the target and run the rest in a C++
    // try whose catch keeps the exception in an anchor slot, and call
    // __exit__ with that exception, if any (__withExit): an exception
    // __exit__ does not suppress is thrown again.  A `return`, `break` or
    // `continue` leaves through the __exit__ call, like a finally clause.
    if (index == n->items.size()) return generateBody(n->body.get());
    const WithItem& item = n->items[index];
    ControlEntry entry{false, finallyCounter_++, -1};
    const int exitFnSlot = pushAnchor();
    entry.exitSlot = pushAnchor();
    const int excSlot = pushAnchor();
    const std::string withCaught = "__caught" + std::to_string(caughtCounter_++);
    *out_ << "{\n";
    *out_ << "std::exception_ptr " << withCaught << ";\n";
    if (resumable) *out_ << "if (__resume == 0) {\n";
    beginStatement();
    const int managerSlot = allocTempSlot();
    *out_ << "    __slot[" << managerSlot << "] = ";
    if (!generateNode(item.context_expr.get())) return false;
    *out_ << ";\n";
    const int valueSlot = allocTempSlot();
    *out_ << "    __slot[" << valueSlot << "] = __withEnter(ctx, env, __slot[" << managerSlot
          << "], &__slot[" << exitFnSlot << "]);\n";
    *out_ << "    __slot[" << entry.exitSlot << "] = PROTO_NONE;\n";
    *out_ << "    __slot[" << excSlot << "] = PROTO_NONE;\n";
    if (resumable) *out_ << "}\n";
    ctl_.push_back(entry);
    *out_ << "try {\n";
    // A target that cannot be bound raises inside the try: __exit__ is
    // called for it.
    if (item.optional_vars) {
        *out_ << "    " << (resumable ? "if (__resume == 0) {\n    " : "");
        if (!generateAssignToTarget(item.optional_vars.get(), "__slot[" + std::to_string(valueSlot) + "]")) return false;
        *out_ << ";\n";
        if (resumable) *out_ << "    }\n";
    }
    const bool ok = emitWithItems(n, index + 1, resumable);
    *out_ << ";\n";
    *out_ << "} catch (const proto::ProtoException& __e) {\n";
    *out_ << "    __slot[" << excSlot << "] = __e.value();\n";
    *out_ << "    " << withCaught << " = std::current_exception();\n";
    *out_ << "}\n";
    entry = ctl_.back();
    ctl_.pop_back();
    if (!ok) return false;
    *out_ << "__finally_" << entry.id << ": ;\n";
    *out_ << "__withExit(ctx, env, __slot[" << exitFnSlot << "], __slot[" << excSlot << "], " << withCaught << ");\n";
    popAnchor();
    emitExitDispatch(entry);
    popAnchor();
    popAnchor();
    *out_ << "}";
    return true;
}

bool CppGenerator::generateImport(ImportNode* n) {
    *out_ << "{\n";
    *out_ << "        const proto::ProtoObject* __mod = env->importModule(\"" << n->moduleName << "\");\n";
    if (n->isAs) {
        // `import a.b as c` binds the submodule a.b: __import__ answers the
        // top-level package `a`, and the submodule is its attribute chain
        // (CPython's IMPORT_NAME + IMPORT_FROM).
        std::string rest = n->moduleName;
        for (size_t dot = rest.find('.'); dot != std::string::npos; dot = rest.find('.')) {
            rest = rest.substr(dot + 1);
            const std::string part = rest.substr(0, rest.find('.'));
            *out_ << "        __mod = env->getAttr(__mod, \"" << part << "\");\n";
        }
        *out_ << "        ";
        emitAssignName(n->alias, "__mod");
        *out_ << ";\n";
    } else {
        size_t dot = n->moduleName.find('.');
        std::string root = (dot == std::string::npos) ? n->moduleName : n->moduleName.substr(0, dot);
        *out_ << "        ";
        emitAssignName(root, "__mod");
        *out_ << ";\n";
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
            *out_ << "        ";
            emitAssignName(alias, "env->getAttr(__mod, \"" + p.first + "\")");
            *out_ << ";\n";
        }
    }
    *out_ << "    }";
    return true;
}

bool CppGenerator::generateLambda(LambdaNode* n) {
    if (!n) return false;
    // A lambda is compiled like a def (see generateDef): it runs in a context
    // of its own, in the namespace of its module, with its parameters, cell
    // variables and free variables in slots and the cells it closes over in
    // its function storage.  Its body is generated first because the slot
    // count is known only afterwards.
    ScopeInfo* const lscope = scopeOf_.count(n) ? scopeOf_[n] : nullptr;
    std::vector<std::string> params = n->parameters;
    for (const auto& k : n->kwonlyargs) params.push_back(k);
    if (!n->vararg.empty()) params.push_back(n->vararg);
    if (!n->kwarg.empty()) params.push_back(n->kwarg);
    const std::vector<std::string> layout = slotLayout(lscope, params);

    const auto oldOrderedLocals = orderedLocalVars_;
    const auto oldLocalVars = localVars_;
    const bool oldInStateMachine = inStateMachine_;
    const bool oldInClassBody = inClassBody_;
    const std::string oldCurrentFuncName = currentFuncName_;
    const std::string oldCurrentFuncCppName = currentFuncCppName_;
    const std::string oldMethodSelfParam = methodSelfParam_;
    const bool oldInMethod = inMethod_;
    ScopeInfo* const oldScope = scope_;
    const std::string qualname = qualnamePrefix_.empty() ? std::string("<lambda>") : qualnamePrefix_ + ".<lambda>";
    orderedLocalVars_ = layout;
    localVars_ = std::unordered_set<std::string>(layout.begin(), layout.end());
    inStateMachine_ = false;
    inClassBody_ = false;
    currentFuncName_.clear();
    currentFuncCppName_.clear();
    methodSelfParam_.clear();
    inMethod_ = false;
    scope_ = lscope;
    auto restore = [&]() {
        inMethod_ = oldInMethod;
        orderedLocalVars_ = oldOrderedLocals;
        localVars_ = oldLocalVars;
        inStateMachine_ = oldInStateMachine;
        inClassBody_ = oldInClassBody;
        currentFuncName_ = oldCurrentFuncName;
        currentFuncCppName_ = oldCurrentFuncCppName;
        methodSelfParam_ = oldMethodSelfParam;
        scope_ = oldScope;
    };

    const SlotFrame lambdaSlots = enterSlotFrame(static_cast<int>(layout.size()));
    std::ostream* outerOut = out_;
    std::ostringstream lambdaBody;
    out_ = &lambdaBody;
    bool ok = true;
    for (size_t i = 0; i < layout.size(); ++i) {
        *out_ << "        const proto::ProtoObject*& local_" << layout[i]
              << " = __slot[" << i << "];\n";
    }
    // An exception the prologue or the body raises propagates to the caller.
    ok = emitPrologue(lscope, params, layout, qualname, n->parameters.size(), n->posonlyargcount,
                      n->kwonlyargs.size(), !n->vararg.empty(), !n->kwarg.empty(), "        ");
    if (ok) {
        *out_ << "        const proto::ProtoObject* const __result = ";
        ok = generateNode(n->body.get());
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
    // enclosing scope (the cells it closes over are the enclosing scope's).
    std::ostringstream lambdaFn;
    lambdaFn << "[](proto::ProtoContext* __callerCtx, const proto::ProtoObject* self, const proto::ParentLink* pl, const proto::ProtoArguments* __argsD, const proto::ProtoKeywordArguments* __kwargsD) -> const proto::ProtoObject* {\n";
    lambdaFn << "        (void)pl;\n";
    // The body binds from lists: the call's own when it came with them
    // (protoCore 3.0.0 descriptors), otherwise built here.
    lambdaFn << "        const proto::ProtoList* args = __argsD ? __argsD->asList(__callerCtx) : nullptr;\n";
    lambdaFn << "        const proto::ProtoSparseList* kwargs = __kwargsD ? __kwargsD->asSparseList(__callerCtx) : nullptr;\n";
    lambdaFn << openContextScope("__callerCtx");
    lambdaFn << "        auto* env = protoPython::PythonEnvironment::get(ctx);\n";
    lambdaFn << "        const proto::ProtoObject* __fs = __functionStorageOf(ctx, self, args);\n";
    lambdaFn << "        protoPython::ModuleNamespaceScope __ns(__moduleNamespaceOf(ctx, __fs));\n";
    lambdaFn << lambdaBody.str();
    lambdaFn << "    }";
    leaveSlotFrame(lambdaSlots);

    const int fsSlot = allocTempSlot();
    *out_ << "([&]() -> const proto::ProtoObject* {\n";
    *out_ << "        __slot[" << fsSlot << "] = __newFunctionStorage(ctx);\n";
    if (lscope) {
        for (const auto& fv : lscope->frees) {
            *out_ << "        __slot[" << fsSlot << "] = __withFreeVar(ctx, __slot[" << fsSlot << "], \"" << fv << "\", local_" << fv << ");\n";
        }
    }
    if (!n->defaults.empty()) {
        *out_ << "        __slot[" << fsSlot << "] = __withDefaults(ctx, __slot[" << fsSlot << "], {";
        for (size_t i = 0; i < n->defaults.size(); ++i) {
            if (i) *out_ << ", ";
            if (!generateNode(n->defaults[i].get())) return false;
        }
        *out_ << "});\n";
    }
    for (size_t i = 0; i < n->kw_defaults.size() && i < n->kwonlyargs.size(); ++i) {
        if (!n->kw_defaults[i]) continue;
        *out_ << "        __slot[" << fsSlot << "] = __withKwDefault(ctx, __slot[" << fsSlot << "], \"" << n->kwonlyargs[i] << "\", ";
        if (!generateNode(n->kw_defaults[i].get())) return false;
        *out_ << ");\n";
    }
    *out_ << "        return ctx->fromMethod(const_cast<proto::ProtoObject*>(__slot[" << fsSlot << "]), " << lambdaFn.str() << ");\n";
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
            emitDeleteName(nm->id);
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
    *out_ << "if (!__truth(env, ";
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
    if (body && !dynamic_cast<SuiteNode*>(body)) {
        if (inStateMachine_) return emitStatementList({body});
        // A clause that is a single statement is a statement list too.
        return emitStatement(body);
    }
    return generateNode(body);
}

bool CppGenerator::emitStatementList(const std::vector<ASTNode*>& stmts) {
    int total = 0;
    for (auto* s : stmts) total += countYields(s);
    for (auto* s : stmts) {
        if (s->line > 0) emitLineDirective(s->line, filename_);
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
        // throw(); its StopIteration value is the result.  A sub-iterator
        // without throw() gets the exception raised at the yield instead.
        // A native iterator that is exhausted returns nullptr.
        const int itSlot = pushAnchor();
        const int valSlot = allocTempSlot();
        *out_ << "{\n";
        *out_ << "if (__resume == 0) {\n";
        *out_ << "    __slot[" << itSlot << "] = env->iter(";
        if (!generateNode(value)) return false;
        *out_ << ");\n";
        *out_ << "}\n";
        *out_ << "const proto::ProtoObject* __yf_res_" << k << " = PROTO_NONE;\n";
        *out_ << "while (true) {\n";
        *out_ << "    proto::ProtoObject* __sub = const_cast<proto::ProtoObject*>(__slot[" << itSlot << "]);\n";
        *out_ << "    const proto::ProtoObject* __yf_in = PROTO_NONE;\n";
        *out_ << "    const proto::ProtoObject* __t = nullptr;\n";
        *out_ << "    const proto::ProtoObject* __tm = nullptr;\n";
        *out_ << "    if (__resume != 0) {\n";
        *out_ << "        __resume = 0;\n";
        *out_ << "        __yf_in = __sent;\n";
        *out_ << "        if (__throw) {\n";
        *out_ << "            __t = __throw;\n";
        *out_ << "            __throw = nullptr;\n";
        *out_ << "            __tm = __sub->getAttribute(ctx, protoPython::PythonEnvironment::getInternedString(ctx, \"throw\"));\n";
        *out_ << "            if (!__tm || __tm == PROTO_NONE || !__tm->asMethod(ctx)) throw protoPython::PyException(env, __t);\n";
        *out_ << "        }\n";
        *out_ << "    }\n";
        *out_ << "    bool __yf_done = false;\n";
        *out_ << "    try {\n";
        *out_ << "        if (__t) {\n";
        // __t came in the generator's own throw() arguments, which its caller
        // holds: the array call form reads it in place (protoCore 3.0.0).
        *out_ << "            __slot[" << valSlot << "] = __tm->asMethod(ctx)(ctx, __sub, nullptr, protoPython::argsView(proto::ProtoArguments(&__t, 1)), nullptr);\n";
        *out_ << "        } else {\n";
        *out_ << "            const proto::ProtoObject* __sm = __sub->getAttribute(ctx, env->getSendString());\n";
        *out_ << "            if (__sm && __sm != PROTO_NONE) {\n";
        *out_ << "                __slot[" << valSlot << "] = __sub->call(ctx, nullptr, env->getSendString(), __sub, ctx->newList()->appendLast(ctx, __yf_in), nullptr);\n";
        *out_ << "            } else if (__yf_in != PROTO_NONE) {\n";
        *out_ << "                env->raiseTypeError(ctx, \"can't send non-None value to a plain iterator\");\n";
        *out_ << "            } else {\n";
        *out_ << "                __slot[" << valSlot << "] = __sub->call(ctx, nullptr, env->getNextString(), __sub, ctx->newList(), nullptr);\n";
        *out_ << "            }\n";
        *out_ << "        }\n";
        *out_ << "    } catch (const proto::ProtoException& __e) {\n";
        *out_ << "        if (!env->isStopIteration(ctx, __e.value())) throw;\n";
        *out_ << "        __yf_res_" << k << " = env->getStopIterationValue(ctx, __e.value());\n";
        *out_ << "        if (!__yf_res_" << k << ") __yf_res_" << k << " = PROTO_NONE;\n";
        *out_ << "        __yf_done = true;\n";
        *out_ << "    }\n";
        *out_ << "    if (__yf_done || !__slot[" << valSlot << "]) break;\n";
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
    emitSuspend("__slot[" + std::to_string(valSlot) + "]");
    *out_ << "}\n";
    // Resumed here: throw() and close() raise their exception at the yield;
    // otherwise the yield's value is the value sent (None for next()).
    *out_ << "__resume = 0;\n";
    *out_ << "if (__throw) {\n";
    *out_ << "    const proto::ProtoObject* __t = __throw;\n";
    *out_ << "    __throw = nullptr;\n";
    *out_ << "    throw protoPython::PyException(env, __t);\n";
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
    // A nested function or lambda has its own scope: the names it binds
    // (its parameters and locals) are its own, and the others it reads are
    // read from the enclosing scopes, through this one -- they are free
    // variables of this function too when an outer function binds them
    // (Python's closures are transitive).  Default values and decorators
    // are evaluated here.  A class body runs in this scope, and its methods
    // are nested functions.
    auto nestedFunction = [&](const std::vector<std::string>& params, const std::string& vararg,
                              const std::string& kwarg, ASTNode* body,
                              const std::vector<std::unique_ptr<ASTNode>>& defaults,
                              const std::vector<std::unique_ptr<ASTNode>>* decorators) {
        for (auto& d : defaults) collectNameRefs(d.get(), refs);
        if (decorators) for (auto& d : *decorators) collectNameRefs(d.get(), refs);
        std::unordered_set<std::string> inner;
        collectNameRefs(body, inner);
        std::unordered_set<std::string> own(params.begin(), params.end());
        if (!vararg.empty()) own.insert(vararg);
        if (!kwarg.empty()) own.insert(kwarg);
        if (!dynamic_cast<LambdaNode*>(node)) collectLocals(body, own);
        for (const auto& nm : inner) if (!own.count(nm)) refs.insert(nm);
    };
    if (auto* n = dynamic_cast<FunctionDefNode*>(node)) {
        nestedFunction(n->parameters, n->vararg, n->kwarg, n->body.get(), n->defaults, &n->decorator_list);
        return;
    }
    if (auto* n = dynamic_cast<AsyncFunctionDefNode*>(node)) {
        nestedFunction(n->parameters, n->vararg, n->kwarg, n->body.get(), n->defaults, &n->decorator_list);
        return;
    }
    if (auto* n = dynamic_cast<LambdaNode*>(node)) {
        nestedFunction(n->parameters, n->vararg, n->kwarg, n->body.get(), n->defaults, nullptr);
        return;
    }
    if (auto* n = dynamic_cast<ClassDefNode*>(node)) {
        for (auto& b : n->bases) collectNameRefs(b.get(), refs);
        for (auto& d : n->decorator_list) collectNameRefs(d.get(), refs);
        collectNameRefs(n->body.get(), refs);
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
        *out_ << "            ";
        emitAssignName(nm->id, valExpr);
        *out_ << ";\n";
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
    // nullptr: exhausted (an exception the iterator raises propagates).
    *out_ << "                if (!__cval_" << id << ") break;\n";

    std::ostringstream valVar;
    valVar << "__cval_" << id;
    if (!emitComprehensionAssign(g.target.get(), valVar.str())) return false;

    for (auto& f : g.ifs) {
        *out_ << "            if (__truth(env, ";
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
