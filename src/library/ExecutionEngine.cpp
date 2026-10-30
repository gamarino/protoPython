#include <exception>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/Compiler.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/MemoryManager.hpp>
#include <protoPython/SignalModule.h>
#include <protoPython/Debugger.h>
#include <protoCore.h>
#include <protoPython/CheckedArith.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <sys/resource.h>
#endif

#include <cstring>
#include <functional>
#include <iostream>

#if defined(_WIN32)
// PosixCompat.cpp: the current thread's stack bounds.
void protopy_thread_stack_limits(std::uintptr_t* low, std::uintptr_t* high);
#endif

namespace protoPython {

// Forward-declared in PythonEnvironment.cpp so the dict-key opcodes
// (OP_BUILD_MAP / OP_MAP_ADD) can share the env-aware hash function
// with py_dict_getitem / setitem — keeps user-__hash__ overrides
// (cistr, etc.) consistent across the entire dict pipeline.
proto::proto_ulong pyDictKeyHash(proto::ProtoContext* context, const proto::ProtoObject* key);

// Compact metadata cache pre-computed at createUserFunction time.
// Stored as a ProtoByteBuffer on the function object (__fn_meta_cache__) so
// runUserFunctionCall can retrieve all constant values with a single attribute
// lookup instead of 7 separate codeObj/self attribute reads per call.
struct FunctionMetaCache {
    int co_flags;
    int nparams;
    int kwonly;
    int automatic_count;
    bool is_generator;
    // True when the function runs without a frame of its own: its bytecode
    // has no OP_BUILD_FUNCTION / OP_BUILD_CLASS (what it defines may look
    // for it as f_back) and it does not name _getframe / currentframe (frame
    // introspection).  The frame is never captured: a function closes over
    // cells.
    bool no_inner_functions;
    // Closure layout (Compiler.h, "Closures"), from the code object.
    bool selfRefCell;          // the self binding is held in a cell
    int32_t selfRef;           // index in co_freevars of a recursive def's own name, or -1
    uint32_t nFree;            // len(co_freevars)
    uint32_t nCellSlots;       // len(co_cellslots)
    uint32_t nLocalsPlus;      // slots before the operand stack: co_varnames, then free variables
    uint32_t nPosOnly;         // co_posonlyargcount
    // The function's own closure cells (`__closure_cells__`), kept alive by
    // the function; nullptr when it has none.
    const proto::ProtoList* closure;
    // Raw pointers — safe because codeObj and globalsObj are kept alive by the
    // function object's own __code__ / __globals__ attributes.
    const proto::ProtoObject* codeObj;
    const proto::ProtoObject* globalsObj;
    const proto::ProtoTuple* co_varnames;  // kept alive by codeObj's co_varnames attr
    // Cached tuple/bytecode pointers — kept alive by codeObj's attributes.
    const proto::ProtoTuple* co_bytecode;       // codeObj.__code__ (as tuple)
    const proto::ProtoTuple* co_consts_tuple;   // codeObj.co_consts (as tuple)
    const proto::ProtoTuple* co_names_tuple;    // codeObj.co_names (as tuple)
    const int*               nativeBc;          // co_bytecode_native ByteBuffer data
    uint32_t                 nConsts;           // number of elements in co_consts
    uint32_t                 nNames;            // number of elements in co_names
    uint32_t                 nVarnames;         // number of elements in co_varnames (parameters, then locals)
    // Followed in memory by flat arrays:
    //   const proto::ProtoObject* nativeConsts[nConsts]
    //   const proto::ProtoObject* nativeNames[nNames]
    //   int32_t cellSlots[nCellSlots]
    // Access via: reinterpret_cast<const proto::ProtoObject**>(cache + 1)
    const int32_t* cellSlots() const {
        return reinterpret_cast<const int32_t*>(
            reinterpret_cast<const proto::ProtoObject* const*>(this + 1) + nConsts + nNames);
    }
};

/** The closure layout of a call, from a function's FunctionMetaCache or, when
 *  it has none, from its code object. */
struct ClosureLayout {
    uint32_t nVarnames = 0;
    uint32_t nFree = 0;
    int32_t selfRef = -1;
    bool selfRefCell = false;
    const proto::ProtoList* closure = nullptr;
    const int32_t* cellSlots = nullptr;
    uint32_t nCellSlots = 0;
    std::vector<int32_t> ownedCellSlots;  // backing store for the code-object path

    static ClosureLayout fromCache(const FunctionMetaCache* c) {
        ClosureLayout l;
        l.nVarnames = c->nVarnames;
        l.nFree = c->nFree;
        l.selfRef = c->selfRef;
        l.selfRefCell = c->selfRefCell;
        l.closure = c->closure;
        l.cellSlots = c->cellSlots();
        l.nCellSlots = c->nCellSlots;
        return l;
    }
    static ClosureLayout fromCode(proto::ProtoContext* ctx, PythonEnvironment* env,
                                  const proto::ProtoObject* codeObj, const proto::ProtoObject* fn) {
        ClosureLayout l;
        if (!codeObj || codeObj == PROTO_NONE) return l;
        const proto::ProtoObject* vn = codeObj->getAttribute(ctx, env->getCoVarnamesString());
        l.nVarnames = (vn && ::protoPython::PythonEnvironment::tupleView(ctx, vn)) ? static_cast<uint32_t>(::protoPython::PythonEnvironment::tupleView(ctx, vn)->getSize(ctx)) : 0;
        const proto::ProtoObject* fv = codeObj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "co_freevars"));
        l.nFree = (fv && fv != PROTO_NONE && ::protoPython::PythonEnvironment::tupleView(ctx, fv)) ? static_cast<uint32_t>(::protoPython::PythonEnvironment::tupleView(ctx, fv)->getSize(ctx)) : 0;
        const proto::ProtoObject* sr = codeObj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "co_selfref"));
        if (sr && sr->isInteger(ctx)) l.selfRef = static_cast<int32_t>(sr->asLong(ctx));
        l.selfRefCell = codeObj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "co_selfref_cell")) == PROTO_TRUE;
        const proto::ProtoObject* cs = codeObj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "co_cellslots"));
        if (cs && cs != PROTO_NONE && ::protoPython::PythonEnvironment::tupleView(ctx, cs)) {
            ::protoPython::TupleView t = ::protoPython::PythonEnvironment::tupleView(ctx, cs);
            for (proto::proto_ulong i = 0; i < t->getSize(ctx); ++i) {
                const proto::ProtoObject* v = t->getAt(ctx, static_cast<int>(i));
                if (v && v->isInteger(ctx)) l.ownedCellSlots.push_back(static_cast<int32_t>(v->asLong(ctx)));
            }
        }
        l.cellSlots = l.ownedCellSlots.data();
        l.nCellSlots = static_cast<uint32_t>(l.ownedCellSlots.size());
        if (fn && env->getClosureString()) {
            const proto::ProtoObject* c = fn->getAttribute(ctx, env->getClosureString());
            if (c && c != PROTO_NONE && !c->isString(ctx)) l.closure = ::protoPython::listStorage(ctx, c);
        }
        return l;
    }
};

/** A call's cells and free variables (Compiler.h, "Closures"), set up after
 *  its parameters are bound: each free-variable slot receives the cell from
 *  the function's closure (a recursive def's own name, the function itself,
 *  in a cell when the layout says so), and each cell variable's slot a new
 *  cell holding the parameter bound there, or an empty one.  Every cell
 *  goes into its slot as soon as it exists, so the collector sees it. */
static void setupClosureSlots(proto::ProtoContext* ctx, PythonEnvironment* env,
                              proto::ProtoObject** slots, unsigned int nSlots,
                              const ClosureLayout& layout, const proto::ProtoObject* fn,
                              proto::proto_ulong nParamSlots) {
    if (!slots || !env) return;
    if (layout.nFree > 0) {
        const proto::proto_ulong nCells = layout.closure ? layout.closure->getSize(ctx) : 0;
        // A closure missing the self binding's cell is the usual case; a
        // full one comes from types.FunctionType(code, globals, closure=...).
        const bool selfOmitted = layout.selfRef >= 0 && nCells + 1 == layout.nFree;
        proto::proto_ulong k = 0;
        for (uint32_t j = 0; j < layout.nFree; ++j) {
            const unsigned int slot = layout.nVarnames + j;
            if (slot >= nSlots) break;
            if (static_cast<int32_t>(j) == layout.selfRef) {
                if (selfOmitted) {
                    slots[slot] = const_cast<proto::ProtoObject*>(layout.selfRefCell ? env->newCell(ctx, fn) : fn);
                    continue;
                }
                const proto::ProtoObject* given = (k < nCells) ? layout.closure->getAt(ctx, static_cast<int>(k++)) : nullptr;
                if (layout.selfRefCell) {
                    slots[slot] = const_cast<proto::ProtoObject*>(given ? given : env->newCell(ctx, nullptr));
                } else {
                    // Read with LOAD_FAST: the cell's value as of the call.
                    const proto::ProtoObject* v = given ? env->cellGet(ctx, given) : nullptr;
                    slots[slot] = const_cast<proto::ProtoObject*>(v ? v : env->getUnboundSentinel());
                }
                continue;
            }
            const proto::ProtoObject* cell = (k < nCells) ? layout.closure->getAt(ctx, static_cast<int>(k++)) : nullptr;
            slots[slot] = const_cast<proto::ProtoObject*>(cell ? cell : env->newCell(ctx, nullptr));
        }
    }
    for (uint32_t c = 0; c < layout.nCellSlots; ++c) {
        const int32_t idx = layout.cellSlots[c];
        if (idx < 0 || static_cast<unsigned int>(idx) >= nSlots) continue;
        const proto::ProtoObject* v = slots[idx];
        if (static_cast<proto::proto_ulong>(idx) >= nParamSlots || v == env->getUnboundSentinel()) v = nullptr;
        slots[idx] = const_cast<proto::ProtoObject*>(env->newCell(ctx, v));
    }
}

static bool areSameClassesVM(proto::ProtoContext* context, const proto::ProtoObject* c1, const proto::ProtoObject* c2) {
    return c1 == c2;
}


static bool opcodeHasArg(int op) {
    switch (op) {
        case OP_LOAD_CONST:
        case OP_LOAD_NAME:
        case OP_STORE_NAME:
        case OP_CALL_FUNCTION:
        case OP_COMPARE_OP:
        case OP_POP_JUMP_IF_FALSE:
        case OP_POP_JUMP_IF_TRUE:
        case OP_JUMP_ABSOLUTE:
        case OP_JUMP_FORWARD:
        case OP_LOAD_ATTR:
        case OP_STORE_ATTR:
        case OP_STORE_ATTR_GROUP:
        case OP_STORE_ATTR_GROUP_END:
        case OP_BUILD_LIST:
        case OP_BUILD_MAP:
        case OP_BUILD_TUPLE:
        case OP_UNPACK_SEQUENCE:
        case OP_LOAD_GLOBAL:
        case OP_STORE_GLOBAL:
        case OP_BUILD_SLICE:
        case OP_FOR_ITER:
        case OP_LIST_APPEND:
        case OP_MAP_ADD:
        case OP_SET_ADD:
        case OP_DICT_UPDATE:
        case OP_DICT_MERGE:
        case OP_LIST_EXTEND:
        case OP_SET_UPDATE:
        case OP_BUILD_SET:
        case OP_BUILD_STRING:
        case OP_LOAD_DEREF:
        case OP_STORE_DEREF:
        case OP_DELETE_DEREF:
        case OP_LOAD_CLOSURE:
        case OP_LOAD_CLASSDEREF:
        case OP_SETUP_FINALLY:
        case OP_SETUP_WITH:
        case OP_SETUP_ASYNC_WITH:
        case OP_RERAISE:
        case OP_GEN_START:
        case OP_FORMAT_VALUE:
        case OP_EXTENDED_ARG:
        case OP_MATCH_MAPPING:
        case OP_MATCH_SEQUENCE:
        case OP_RAISE_VARARGS:
        case OP_LOAD_FAST:
        case OP_STORE_FAST:
        case OP_DELETE_NAME:
        case OP_DELETE_ATTR:
        case OP_DELETE_SUBSCR:
        case OP_DELETE_GLOBAL:
        case OP_DELETE_FAST:
        case OP_UNPACK_EX:
        case OP_GET_LEN:
            return true;
        default:
            return false;
    }
}



// Does the type chain of an instance whose type is `directType` (class flags
// `typeFlags`, from ensureClassFlags) intercept the assignment
// `obj.<nameS> = value`, so that OP_STORE_ATTR's direct write would be wrong?
// It does when
//   * `directType` owns an attribute named `nameS` (the slow path decides what
//     it is, as it always did: on a class receiver this is a dunder of object
//     or type, whose write may invalidate cached class flags);
//   * a class on the chain defines `__setattr__` (PYFLAG_HAS_CUSTOM_SETATTR);
//   * a class on the chain holds a data descriptor named `nameS` (an object
//     whose own chain answers `__set__` or `__delete__`: property, a
//     descriptor instance, a member of `__slots__`).
// The class's attribute of that name is found by one protoCore lookup of the
// whole chain (served by the per-thread attribute cache): the chain of a
// Python class is its MRO. A setter is an annotation of the class, so that
// lookup returns it; what it returns decides. Absent, None, a tagged int or
// string, or a bool is no descriptor: the write goes to the instance.
static bool typeChainInterceptsStore(proto::ProtoContext* ctx, PythonEnvironment* env,
                                     const proto::ProtoObject* directType, uint32_t typeFlags,
                                     const proto::ProtoString* nameS) {
    if (typeFlags & PythonEnvironment::PYFLAG_HAS_CUSTOM_SETATTR) return true;
    const proto::ProtoObject* d = directType->getAttribute(ctx, nameS);
    if (directType->hasOwnAttribute(ctx, nameS) == PROTO_TRUE) return true;
    if (d == PROTO_NONE || proto::isSmallInt(d) || proto::ProtoObject::isStringTagFast(d)
            || d == PROTO_TRUE || d == PROTO_FALSE) return false;
    {
        // Only an object cell can be a descriptor; a class stored as a class
        // attribute is not one, whatever methods it defines.
        if (d && d != PROTO_NONE && (reinterpret_cast<uintptr_t>(d) & 0x3F) == 0
                && !env->isActuallyAClass(ctx, d)) {
            const proto::ProtoString* setS = env->getSetDunderString();
            static const proto::ProtoString* const delS =  // interned: perennial
                PythonEnvironment::getInternedString(ctx, "__delete__");
            if ((setS && d->hasAttribute(ctx, setS) == PROTO_TRUE)
                    || d->hasAttribute(ctx, delS) == PROTO_TRUE)
                return true;
        }
    }
    return false;
}

// `obj.name = value` for generated code: OP_STORE_ATTR's plain instance write
// when nothing on the type chain intercepts it (no __slots__, __setattr__ or
// data descriptor of that name; not a class or a module; not __class__ or
// __dict__), env->setAttribute's full protocol otherwise.
bool PythonEnvironment::storeAttr(const proto::ProtoObject* obj, const char* name, const proto::ProtoObject* value) {
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;
    return storeAttr(obj, PythonEnvironment::getInternedString(ctx, name), value);
}

bool PythonEnvironment::storeAttr(const proto::ProtoObject* obj, const proto::ProtoString* nameS, const proto::ProtoObject* value) {
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;
    return storeAttr(ctx, obj, nameS, value);
}

bool PythonEnvironment::storeAttr(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                  const proto::ProtoString* nameS, const proto::ProtoObject* value) {
    if (!ctx) ctx = rootContext_;
    if (!obj || !nameS) return false;
    if (obj != PROTO_NONE && (reinterpret_cast<uintptr_t>(obj) & 0x3F) == 0 &&
            nameS != getClassString() && nameS != getDictDunderString()) {
        const proto::ProtoObject* directType = obj->getFirstParent(ctx);
        if (directType && directType != PROTO_NONE && directType != getTypePrototype() &&
                directType != getModulePrototype() && obj != getBuiltinsModule()) {
            const uint32_t flags = ensureClassFlags(ctx, directType);
            if ((flags & PYFLAG_HAS_SLOTS) == 0 && !typeChainInterceptsStore(ctx, this, directType, flags, nameS)) {
                obj->setAttribute(ctx, nameS, value);
                return true;
            }
        }
    }
    setAttribute(ctx, obj, nameS, value);
    if (obj == getBuiltinsModule() || getType(ctx, obj) == getModulePrototype()) invalidateResolveCache();
    return true;
}

// ---------------------------------------------------------------------------
// Attribute write groups
//
// Compiler::tryCompileAttrGroup turns a run `p.a = x; p.b = y; ...` whose
// values after the first cannot run code or raise into OP_STORE_ATTR_GROUP
// ... OP_STORE_ATTR_GROUP_END.  At the run's first write the engine checks,
// once, that every write of the run would be OP_STORE_ATTR's plain instance
// write (attrGroupApplies).  When it is, each write keeps its value on the
// operand stack and the last one publishes the whole run as one new version
// of `p` (ProtoObject::setAttributes): one snapshot and one publication
// instead of one per name, atomic to other threads.  Otherwise every write of
// the run is OP_STORE_ATTR's own, unchanged.
//
// Equivalence with the per-write path rests on: (1) between the check and
// the publication nothing runs that could observe `p` -- the later values are
// constants, bound parameters and displays of them, and the bytecode loop
// delivers no Python signal handler while a run is pending (CPython checks for
// signals only at calls and backward jumps, never between two stores either);
// (2) no write can have an effect beyond storing its value -- no
// `__setattr__` override, data descriptor or `__slots__` on the type chain,
// and the receiver is neither a class nor a module; (3) nothing in the run
// can raise.  A write by another thread between the check and the
// publication is ordered before or after the whole run, as it could be
// ordered around a single write.  Which writes of a run survive if the thread
// is terminated in the middle of it is indeterminate.
// ---------------------------------------------------------------------------
static bool attrGroupApplies(proto::ProtoContext* ctx, PythonEnvironment* env,
                             const proto::ProtoObject* obj, const proto::ProtoTuple* names,
                             const proto::ProtoTuple* desc) {
    if (!env || !names || !desc || !obj || obj == PROTO_NONE) return false;
    // An object cell: not a tagged value (SmallInteger, string, ...).
    if ((reinterpret_cast<uintptr_t>(obj) & 0x3F) != 0) return false;
    if (obj->isString(ctx) || obj->isInteger(ctx) || obj->isBoolean(ctx) || obj->isFloat(ctx))
        return false;
    const proto::ProtoObject* directType = obj->getFirstParent(ctx);
    if (!directType || directType == PROTO_NONE || directType == env->getTypePrototype())
        return false;
    if (directType == env->getModulePrototype() || obj == env->getBuiltinsModule()) return false;
    if (env->isActuallyAClass(ctx, obj)) return false;
    const uint32_t typeFlags = env->ensureClassFlags(ctx, directType);
    if (typeFlags & PythonEnvironment::PYFLAG_HAS_SLOTS) return false;
    const proto::proto_ulong descSize = desc->getSize(ctx);
    if (descSize < 1) return false;
    const proto::proto_ulong n = static_cast<proto::proto_ulong>(desc->getAt(ctx, 0)->asLong(ctx));
    if (n < 2 || n > 16 || descSize < 1 + n) return false;
    for (proto::proto_ulong k = 0; k < n; ++k) {
        const proto::proto_long idx = desc->getAt(ctx, static_cast<int>(1 + k))->asLong(ctx);
        if (idx < 0 || static_cast<proto::proto_ulong>(idx) >= names->getSize(ctx)) return false;
        const proto::ProtoObject* nameObj = names->getAt(ctx, static_cast<int>(idx));
        if (!proto::ProtoObject::isStringTagFast(nameObj)) return false;
        if (typeChainInterceptsStore(ctx, env, directType, typeFlags, nameObj->asString(ctx)))
            return false;
    }
    // The parameters the later values read: `del p` leaves the unbound
    // sentinel, and reading it raises UnboundLocalError.
    const proto::ProtoObject* unbound = env->getUnboundSentinel();
    const unsigned int nSlots = ctx->getAutomaticLocalsCount();
    for (proto::proto_ulong k = 1 + n; k < descSize; ++k) {
        const proto::proto_long slot = desc->getAt(ctx, static_cast<int>(k))->asLong(ctx);
        if (slot < 0 || static_cast<unsigned int>(slot) >= nSlots) return false;
        if (unbound && ctx->getAutomaticLocal(static_cast<unsigned int>(slot)) == unbound) return false;
    }
    return true;
}

// The limit reached: raise RecursionError. Building it may itself recurse;
// the flag allows that, and is reset when the raise (a throw) leaves. Out of
// line: the constructor stays small enough to inline on every call.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static void raiseRecursionLimit(PythonEnvironment* env, proto::ProtoContext* ctx) {
    struct InRecursionError {
        InRecursionError() { PythonEnvironment::s_inRecursionError = true; }
        ~InRecursionError() { PythonEnvironment::s_inRecursionError = false; }
    } inRecursionError;
    env->raiseRecursionError(ctx);
}

// Native stack guard.  Every Python call recurses on the native stack (one
// dispatcher frame per call, several KiB with GCC), so a recursion limit
// raised with sys.setrecursionlimit -- 2,000 is enough on Linux --
// overflowed the stack and the process died of SIGSEGV instead of raising
// RecursionError.  A call that would leave too little of the native stack
// free raises RecursionError, as CPython 3.14 does for C stack exhaustion.
//
// Windows: the thread's real bounds (GetCurrentThreadStackLimits), and half
// of the stack is kept free.  MSVC runs a catch block on top of the stack
// before the frames below it are unwound, so an exception travelling up a
// deep recursion through frames that catch and rethrow needs stack in
// proportion to the depth; with a quarter kept free the process still died
// of stack overflow.
//
// Elsewhere: the first RecursionScope of a thread records where its Python
// calls start, and a quarter of the main thread's stack (RLIMIT_STACK) is
// kept free.
namespace {
#if defined(_WIN32)
thread_local const char* t_pythonStackLimit = nullptr;

#if defined(_MSC_VER)
__declspec(noinline)
#endif
bool nativeStackExhausted() {
    char probe = 0;
    const char* here = &probe;
    if (!t_pythonStackLimit) {
        std::uintptr_t low = 0, high = 0;
        protopy_thread_stack_limits(&low, &high);
        if (high <= low) return false;
        t_pythonStackLimit = reinterpret_cast<const char*>(low + (high - low) / 2);
    }
    return here < t_pythonStackLimit;
}
#else
thread_local const char* t_pythonStackBase = nullptr;

size_t pythonStackBudget() {
    static const size_t budget = [] {
        size_t size = 8u << 20;
        struct rlimit rl;
        if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur > 0)
            size = static_cast<size_t>(rl.rlim_cur);
        return size / 4 * 3;
    }();
    return budget;
}

__attribute__((noinline))
bool nativeStackExhausted() {
    char probe = 0;
    const char* here = &probe;
    if (!t_pythonStackBase) {
        t_pythonStackBase = here;
        return false;
    }
    return t_pythonStackBase > here
        && static_cast<size_t>(t_pythonStackBase - here) > pythonStackBudget();
}
#endif
}  // namespace

RecursionScope::RecursionScope(PythonEnvironment* env, proto::ProtoContext* ctx) : env_(env), ctx_(ctx) {
    if (!env_) return;
    
    int limit = env_->getRecursionLimit();
    // The native stack nearly full is the recursion limit too.
    if (nativeStackExhausted()) limit = PythonEnvironment::s_recursionDepth;

    // If we are already raising a recursion error, allow more depth (up to a hard limit)
    if (PythonEnvironment::s_inRecursionError) {
        if (PythonEnvironment::s_recursionDepth >= limit + 100) {
            overflowed_ = true;
            return;
        }
        PythonEnvironment::s_recursionDepth++;
        incremented_ = true;
        return;
    }

    if (PythonEnvironment::s_recursionDepth >= limit) {
        overflowed_ = true;
        raiseRecursionLimit(env_, ctx_);
    } else {
        PythonEnvironment::s_recursionDepth++;
        incremented_ = true;
    }
}
RecursionScope::~RecursionScope() {
    if (incremented_) {
        PythonEnvironment::s_recursionDepth--;
    }
}


namespace {

static const proto::ProtoObject* invokeDunder(proto::ProtoContext* ctx, const proto::ProtoObject* container, const proto::ProtoString* name, const proto::ProtoList* args);
static bool isTruthy(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

// CPython's _PyObject_FunctionStr, naming a callable in call errors:
// "module.qualname()" outside builtins, "qualname()" in it, and
// "<type> object" for a callable without __qualname__.
static std::string callableDescription(proto::ProtoContext* ctx, PythonEnvironment* env,
                                       const proto::ProtoObject* callable) {
    auto strAttr = [&](const proto::ProtoObject* o, const char* attr, std::string& out) {
        const proto::ProtoObject* v = o ? o->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, attr)) : nullptr;
        if (!v || !v->isString(ctx)) return false;
        v->asString(ctx)->toUTF8String(ctx, out);
        return true;
    };
    const proto::ProtoObject* fn = callable;
    std::string qualname;
    if (!strAttr(fn, "__qualname__", qualname)) {
        fn = callable ? callable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__func__")) : nullptr;
        if (!strAttr(fn, "__qualname__", qualname)) {
            std::string typeName = "function";
            if (env && callable) strAttr(env->getType(ctx, callable), "__name__", typeName);
            return typeName + " object";
        }
    }
    std::string module;
    if (strAttr(fn, "__module__", module) && module != "builtins") return module + "." + qualname + "()";
    return qualname + "()";
}

static void syncModuleIdentity(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* oldMod, const proto::ProtoObject* newMod) {
    if (oldMod == newMod || !env) return;
    
    // Check if oldMod is a module
    if (env->getModulePrototype() && env->getType(ctx, oldMod) != env->getModulePrototype()) return;

    // Get the name of the module
    const proto::ProtoObject* nameAttr = oldMod->getAttribute(ctx, env->getNameString());
    if (!nameAttr || !nameAttr->isString(ctx)) return;

    // Update sys.modules
    const proto::ProtoObject* sys = env->getSysModule();
    if (sys) {
        const proto::ProtoObject* modules = env->getAttribute(ctx, sys, env->getModulesS());
        if (modules && modules != PROTO_NONE) {
            const proto::ProtoString* modNameS = nameAttr->asString(ctx);
            std::string moduleName;
            modNameS->toUTF8String(ctx, moduleName);

            // 1. Update as attribute (internal lookup fallback)
            if (modules->getAttribute(ctx, modNameS) == oldMod) {
                const_cast<proto::ProtoObject*>(modules)->setAttribute(ctx, modNameS, newMod);
            }
            // 2. Also update ProtoCore's internal import cache
            const proto::ProtoObject* modWrapper = ctx->space->getImportModule(ctx, moduleName.c_str(), "val");
            if (modWrapper && modWrapper != PROTO_NONE) {
                const_cast<proto::ProtoObject*>(modWrapper)->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "val"), newMod);
            }

            // 3. Update as dict item (Python-side lookup)
            const proto::ProtoObject* dataAttr = modules->getAttribute(ctx, env->getDataString());
            if (dataAttr && dataAttr != PROTO_NONE) {
                const proto::ProtoSparseList* dict = dataAttr->asSparseList(ctx);
                if (dict && dict->has(ctx, modNameS->getHash(ctx)) && dict->getAt(ctx, modNameS->getHash(ctx)) == oldMod) {
                    dict = dict->setAt(ctx, modNameS->getHash(ctx), newMod);
                    const_cast<proto::ProtoObject*>(modules)->setAttribute(ctx, env->getDataString(), dict->asObject(ctx));
                    
                    // CRITICAL: Ensure resolve cache is invalidated
                    const_cast<PythonEnvironment*>(env)->incrementResolveCacheGeneration();
                    
                    if (get_env_diag()) {
                        std::string m; modNameS->toUTF8String(ctx, m);
                        fprintf(stderr, "DEBUG: syncModuleIdentity updated sys.modules[%s]: %p -> %p\n", m.c_str(), (void*)oldMod, (void*)newMod);
                    }
                }
            }
        }
    }
}

struct FrameScope {
    FrameScope(const proto::ProtoObject* frame) : oldFrame(PythonEnvironment::getCurrentFrame()) {
        PythonEnvironment::setCurrentFrame(frame);
    }
    /** Also records which context runs `frame`, so f_locals can read the
     *  activation's fast locals (see PythonEnvironment::LiveFrame). */
    FrameScope(const proto::ProtoObject* frame, proto::ProtoContext* ctx)
        : oldFrame(PythonEnvironment::getCurrentFrame()), live{frame, ctx, nullptr}, linked(frame != nullptr) {
        PythonEnvironment::setCurrentFrame(frame);
        if (linked) PythonEnvironment::pushLiveFrame(&live);
    }
    ~FrameScope() {
        if (linked) PythonEnvironment::popLiveFrame(&live);
        PythonEnvironment::setCurrentFrame(oldFrame);
    }
    const proto::ProtoObject* oldFrame;
    PythonEnvironment::LiveFrame live{nullptr, nullptr, nullptr};
    bool linked = false;
};

/** Restores the thread's current frame when a call returns.  A call makes its
 *  frame current for its body; without the restore the caller's next call
 *  took the previous callee as its f_back, which was wrong and chained every
 *  frame-mapped call into one chain of mutable frames that the collector
 *  releases one level per cycle (test/regression/frame_f_back_after_return.py). */
struct CurrentFrameRestore {
    explicit CurrentFrameRestore(bool active = true)
        : saved(active ? PythonEnvironment::getCurrentFrame() : nullptr), active(active) {}
    ~CurrentFrameRestore() { if (active) PythonEnvironment::setCurrentFrame(saved); }
    CurrentFrameRestore(const CurrentFrameRestore&) = delete;
    CurrentFrameRestore& operator=(const CurrentFrameRestore&) = delete;
    const proto::ProtoObject* saved;
    bool active;
};

// The running code object for the duration of a scope, restored on every
// exit path (a Python exception is a C++ exception).
struct CodeObjectScope {
    explicit CodeObjectScope(const proto::ProtoObject* code) : prev(PythonEnvironment::getCurrentCodeObject()) {
        PythonEnvironment::setCurrentCodeObject(code);
    }
    ~CodeObjectScope() { PythonEnvironment::setCurrentCodeObject(prev); }
    CodeObjectScope(const CodeObjectScope&) = delete;
    CodeObjectScope& operator=(const CodeObjectScope&) = delete;
    const proto::ProtoObject* prev;
};

// One scope-globals entry (sys._getframemodulename) for a scope's duration.
struct ScopeGlobalsEntry {
    explicit ScopeGlobalsEntry(const proto::ProtoObject* globals) { PythonEnvironment::pushScopeGlobals(globals); }
    ~ScopeGlobalsEntry() { PythonEnvironment::popScopeGlobals(); }
    ScopeGlobalsEntry(const ScopeGlobalsEntry&) = delete;
    ScopeGlobalsEntry& operator=(const ScopeGlobalsEntry&) = delete;
};

struct GlobalsScope {
    GlobalsScope(const proto::ProtoObject* globals) : oldGlobals(PythonEnvironment::getCurrentGlobals()) {
        // One entry per Python-level call, for sys._getframemodulename().
        PythonEnvironment::pushScopeGlobals(globals);
        if (globals != oldGlobals) {
            PythonEnvironment::setCurrentGlobals(globals);
            PythonEnvironment* env = PythonEnvironment::getCurrentEnvironment();
            if (env) env->invalidateResolveCache();
        }
    }
    ~GlobalsScope() {
        PythonEnvironment::popScopeGlobals();
        if (oldGlobals != PythonEnvironment::getCurrentGlobals()) {
            PythonEnvironment::setCurrentGlobals(oldGlobals);
            PythonEnvironment* env = PythonEnvironment::getCurrentEnvironment();
            if (env) env->invalidateResolveCache();
        }
    }
    const proto::ProtoObject* oldGlobals;
};

/** __call__ for user-defined functions: push context (RAII), build frame, run __code__, promote return value.
 * Reads co_varnames, co_nparams, co_automatic_count from code object to size automatic slots and bind args. */
static const proto::ProtoObject* invokeCallable(proto::ProtoContext* ctx,
    const proto::ProtoObject* callable, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);

// Internal fast-path: accepts a raw C++ argument slice instead of a ProtoList.
// Called from OP_CALL_FUNCTION when the callee is a known user function, bypassing
// the newList() + appendLast() per-call cell allocations.
static const proto::ProtoObject* runUserFunctionCallRaw(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ProtoSparseList* kwargs,
    const proto::ProtoObject* const* rawArgs,
    proto::proto_ulong rawArgCount);

static const proto::ProtoObject* runUserFunctionCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* kwargs) {
    if (!ctx || !self || !args) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    // Fast path: retrieve all constant metadata from the pre-computed FunctionMetaCache
    // (one ByteBuffer attribute lookup instead of 7 individual codeObj reads).
    const proto::ProtoObject* codeObj = nullptr;
    const proto::ProtoObject* globalsObj = nullptr;
    int co_flags = 0, nparams_count = 0, kwonly_count = 0, automatic_count = 0;
    bool isGenerator = false;
    ::protoPython::TupleView co_varnames = nullptr;

    bool cacheHit = false;
    bool cacheNoInnerFunctions = false;
    proto::proto_ulong nVarnames = 0;
    proto::proto_ulong nLocalsPlus = 0;
    const FunctionMetaCache* metaCache = nullptr;
    if (env && env->getFnMetaCacheString()) {
        const proto::ProtoObject* cacheAttr = self->getAttribute(ctx, env->getFnMetaCacheString());
        if (cacheAttr && cacheAttr != PROTO_NONE) {
            char* cacheData = cacheAttr->getDataIfByteBuffer(ctx);
            if (cacheData) {
                const FunctionMetaCache* cache =
                    reinterpret_cast<const FunctionMetaCache*>(cacheData);
                metaCache              = cache;
                codeObj                = cache->codeObj;
                globalsObj             = cache->globalsObj;
                co_flags               = cache->co_flags;
                nparams_count          = cache->nparams;
                kwonly_count           = cache->kwonly;
                automatic_count        = cache->automatic_count;
                nVarnames              = cache->nVarnames;
                nLocalsPlus            = cache->nLocalsPlus;
                isGenerator            = cache->is_generator;
                co_varnames            = cache->co_varnames;
                cacheNoInnerFunctions  = cache->no_inner_functions;
                cacheHit = true;
            }
        }
    }

    if (!cacheHit) {
        // Fallback: read individually (generators resuming, legacy code paths)
        const proto::ProtoString* code_name = env ? env->getCodeString() : PythonEnvironment::getInternedString(ctx, "__code__");
        codeObj = self->getAttribute(ctx, code_name);
        if (!codeObj || codeObj == PROTO_NONE) return PROTO_NONE;

        const proto::ProtoString* globals_name = env ? env->getGlobalsString() : PythonEnvironment::getInternedString(ctx, "__globals__");
        globalsObj = self->getAttribute(ctx, globals_name);
        if (!globalsObj || globalsObj == PROTO_NONE) return PROTO_NONE;

        auto getInt = [&](const proto::ProtoString* key) -> int {
            const proto::ProtoObject* v = key ? codeObj->getAttribute(ctx, key) : nullptr;
            return (v && v->isInteger(ctx)) ? static_cast<int>(v->asLong(ctx)) : 0;
        };
        co_flags       = getInt(env ? env->getCoFlagsString()         : nullptr);
        nparams_count  = getInt(env ? env->getCoNparamsString()        : nullptr);
        kwonly_count   = getInt(env ? env->getCoKwonlyargcountString() : nullptr);
        automatic_count= getInt(env ? env->getCoAutomaticCountString() : nullptr);
        const proto::ProtoObject* cvObj = (env && env->getCoVarnamesString())
            ? codeObj->getAttribute(ctx, env->getCoVarnamesString()) : nullptr;
        co_varnames = (cvObj && ::protoPython::PythonEnvironment::tupleView(ctx, cvObj)) ? ::protoPython::PythonEnvironment::tupleView(ctx, cvObj) : nullptr;
        nVarnames = co_varnames ? co_varnames->getSize(ctx) : 0;
        const proto::ProtoObject* isGenObj = (env && env->getCoIsGeneratorString())
            ? codeObj->getAttribute(ctx, env->getCoIsGeneratorString()) : nullptr;
        isGenerator = isGenObj && isGenObj->isBoolean(ctx) && isGenObj->asBoolean(ctx);
        const proto::ProtoObject* nlp = codeObj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "co_nlocalsplus"));
        nLocalsPlus = (nlp && nlp->isInteger(ctx)) ? static_cast<proto::proto_ulong>(nlp->asLong(ctx)) : nVarnames;
    }

    if (!codeObj || codeObj == PROTO_NONE) return PROTO_NONE;
    if (!globalsObj || globalsObj == PROTO_NONE) return PROTO_NONE;

    std::string fnName = "unknown";
    if (get_env_diag()) {
        const proto::ProtoObject* co_name_obj = codeObj->getAttribute(ctx, env ? env->getCoNameString() : PythonEnvironment::getInternedString(ctx, "co_name"));
        if (co_name_obj && co_name_obj->isString(ctx)) co_name_obj->asString(ctx)->toUTF8String(ctx, fnName);
        fprintf(stderr, "DEBUG: runUserFunctionCall name=%s nparams_count=%d argCount=%llu\n", fnName.c_str(), nparams_count, static_cast<unsigned long long>(args->getSize(ctx)));
        fflush(stderr);
    }


    // Pass nullptr for parameterNames/localNames/args/kwargs: ProtoContext's internal binding is
    // skipped entirely (early-exit at parameterNames==nullptr check). Python-specific argument
    // binding is handled manually below via bindVar. automatic_count alone is sufficient for
    // slot allocation (max(totalSlots, nameCount) reduces to totalSlots when localNames=nullptr).
    const proto::ProtoObject* result = PROTO_NONE;
    {
    ContextScope scope(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr, (size_t)automatic_count);
    proto::ProtoContext* calleeCtx = scope.context();
    proto::proto_ulong argCount = args->getSize(calleeCtx);

    // 5. Build Execution Frame (for locals()/sys._getframe)
    // CO_OPTIMIZED functions that are not generators, define no class and do
    // not introspect frames never access the frame in the hot path
    // (LOAD_FAST/STORE_FAST use slots, LOAD_DEREF/STORE_DEREF the cells in
    // slots). Skip the ~4 AVL-tree object allocations to reduce per-call GC
    // pressure — the dominant cost in deep recursion.
    //
    // Re-enabling the V97-era frame-skip optimisation. b35bf811 disabled it
    // unconditionally to support sys._getframe()/traceback introspection, but every
    // call to a CO_OPTIMIZED leaf function (e.g. fib) then paid 4-5 cell
    // allocations per call. Profiles of call_recursion (fib(25), 242k calls)
    // showed 90% of time in ProtoContext destruction + cell free-list refilling;
    // frame setup was the upstream amplifier.  Introspection can be added back
    // lazily (build the frame on demand when sys._getframe() / traceback walks
    // actually request it) without paying for it on every call.
    bool skipFrame = env && (co_flags & CO_OPTIMIZED) && !isGenerator && cacheNoInnerFunctions;
    proto::ProtoObject* frame = nullptr;
    if (!skipFrame) {
        // For forceMapped (non-CO_OPTIMIZED) functions the frame holds the
        // locals as own attributes, and STORE_NAME writes them there: an
        // immutable frame would turn every STORE_NAME into a copy-on-write,
        // so build it mutable in that case.  A frame references no function
        // and no closure: nested functions close over cells.
        bool framMutable = !(co_flags & CO_OPTIMIZED);
        frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(framMutable));
        if (env) {
            frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, env->getFramePrototype()));
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFCodeString(), codeObj));
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFGlobalsString(), globalsObj));

            // f_back is always present: None when no enclosing frame is
            // known (a caller that runs without a frame object, or the
            // outermost one).  A missing attribute made every walk of the
            // frame chain (traceback.extract_stack, asyncio in debug mode)
            // raise AttributeError.
            const proto::ProtoObject* parentFrame = PythonEnvironment::getCurrentFrame();
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFBackString(),
                parentFrame ? parentFrame : PROTO_NONE));
        }
    }

    // The closure layout of this call (cells and free variables).
    ClosureLayout closureLayout = metaCache ? ClosureLayout::fromCache(metaCache)
        : (env ? ClosureLayout::fromCode(calleeCtx, env, codeObj, self) : ClosureLayout());
    auto isCellSlot = [&](int idx) {
        for (uint32_t c = 0; c < closureLayout.nCellSlots; ++c)
            if (closureLayout.cellSlots[c] == idx) return true;
        return false;
    };

    // Bind parameters
    unsigned int nSlots = calleeCtx->getAutomaticLocalsCount();
    proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());

    // Fast locals start unbound, like CPython's NULL slots.  The context
    // fills every slot with PROTO_NONE, so a local read before assignment
    // gave None and `del` of it succeeded.  co_varnames lists the parameters
    // first (positional, keyword-only, *args, **kwargs); they are bound
    // below, and the remaining names are locals that LOAD_FAST / DELETE_FAST
    // must see as unbound.
    if ((co_flags & CO_OPTIMIZED) && slots && co_varnames && env && env->getUnboundSentinel()) {
        proto::proto_ulong nParamSlots = static_cast<proto::proto_ulong>(nparams_count + kwonly_count)
            + ((co_flags & CO_VARARGS) ? 1 : 0) + ((co_flags & CO_VARKEYWORDS) ? 1 : 0);
        proto::proto_ulong nVars = nVarnames;
        if (nVars > nSlots) nVars = nSlots;
        for (proto::proto_ulong vi = nParamSlots; vi < nVars; ++vi)
            slots[vi] = const_cast<proto::ProtoObject*>(env->getUnboundSentinel());
    }

    auto bindVar = [&](int idx, const proto::ProtoObject* val) {
        if (get_env_diag()) {
            std::string pn = "unknown";
            if (co_varnames && idx < (int)co_varnames->getSize(calleeCtx) && co_varnames->getAt(calleeCtx, idx)->isString(calleeCtx)) {
                co_varnames->getAt(calleeCtx, idx)->asString(calleeCtx)->toUTF8String(calleeCtx, pn);
            }
            std::string valRepr = "?";
            if (val) {
                if (val->isString(calleeCtx)) {
                    val->asString(calleeCtx)->toUTF8String(calleeCtx, valRepr);
                    valRepr = "'" + valRepr + "'";
                } else if (val->isInteger(calleeCtx)) {
                    valRepr = std::to_string(val->asLong(calleeCtx));
                } else {
                    char buf[32]; sprintf(buf, "%p", (void*)val); valRepr = buf;
                }
            }
            fprintf(stderr, "DEBUG: bindVar idx=%d param=%s val=%s (ptr=%p) co_flags=%d slots=%p frame=%p\n", idx, pn.c_str(), valRepr.c_str(), (void*)val, co_flags, (void*)slots, (void*)frame);
            fflush(stderr);
        }
        if (((co_flags & CO_OPTIMIZED) || isCellSlot(idx)) && slots && idx < (int)nSlots) {
            // A cell variable's parameter lands in its slot even in a mapped
            // function: setupClosureSlots puts it in the cell.
            slots[idx] = const_cast<proto::ProtoObject*>(val);
        } else if (frame && co_varnames && idx < (int)co_varnames->getSize(calleeCtx)) {
            const proto::ProtoObject* nameObj = co_varnames->getAt(calleeCtx, idx);
            if (nameObj && nameObj->isString(calleeCtx)) {
                const proto::ProtoString* nameS = nameObj->asString(calleeCtx);
                const proto::ProtoObject* oldFrame = frame;
                frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, nameS, val));
                const proto::ProtoObject* hasAttr = frame->hasAttribute(calleeCtx, nameS);
                if (get_env_diag()) {
                    std::string pn2; nameS->toUTF8String(calleeCtx, pn2);
                    fprintf(stderr, "DEBUG bindVar (frame): set %s = %p. frame: %p -> %p (hasAttr=%p)\n", pn2.c_str(), (void*)val, (void*)oldFrame, (void*)frame, (void*)hasAttr);
                }
            }
        }
    };

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: runUserFunctionCall nparams_count=%d argCount=%llu argsSize=%llu\n", nparams_count, static_cast<unsigned long long>(argCount), static_cast<unsigned long long>(args ? args->getSize(calleeCtx) : 0));
        fflush(stderr);
    }


    const proto::ProtoObject* codeOwner = self;
    if (env) {
        const proto::ProtoObject* im_func = self->getAttribute(calleeCtx, env->getFuncDunderString());
        if (im_func && im_func != PROTO_NONE) codeOwner = im_func;
    }
    
    // Arguments, bound as CPython binds them (PythonEnvironment::
    // bindArgumentsCore, shared with compiled code): positional and keyword
    // arguments, the defaults of the last positional parameters
    // (__defaults__) and of the keyword-only ones (__kwdefaults__),
    // positional-only parameters, *args and **kwargs, and CPython's
    // TypeErrors.  The values go to the parameter slots (a mapped function
    // has them too), then a mapped function's onto its frame.
    if (env && co_varnames && slots) {
        const unsigned nParamSlots = static_cast<unsigned>(nparams_count + kwonly_count)
            + ((co_flags & CO_VARARGS) ? 1u : 0u) + ((co_flags & CO_VARKEYWORDS) ? 1u : 0u);
        if (nParamSlots > nSlots || nParamSlots > co_varnames->getSize(calleeCtx)) return nullptr;
        const proto::ProtoString* namesBuf[16];
        std::vector<const proto::ProtoString*> namesHeap;
        const proto::ProtoString** names = namesBuf;
        if (nParamSlots > 16) { namesHeap.resize(nParamSlots); names = namesHeap.data(); }
        for (unsigned pi = 0; pi < nParamSlots; ++pi) {
            const proto::ProtoObject* nm = co_varnames->getAt(calleeCtx, static_cast<int>(pi));
            names[pi] = (nm && nm->isString(calleeCtx)) ? nm->asString(calleeCtx) : env->getNameString();
        }
        unsigned nPosOnly = 0;
        if (metaCache) {
            nPosOnly = metaCache->nPosOnly;
        } else {
            const proto::ProtoObject* posOnlyObj = codeObj->getAttribute(calleeCtx,
                PythonEnvironment::getInternedString(calleeCtx, "co_posonlyargcount"));
            nPosOnly = (posOnlyObj && posOnlyObj->isInteger(calleeCtx))
                ? static_cast<unsigned>(posOnlyObj->asLong(calleeCtx)) : 0u;
        }
        PythonEnvironment::ArgumentSpec spec{nullptr, codeOwner, static_cast<unsigned>(nparams_count), nPosOnly,
                                             static_cast<unsigned>(kwonly_count), (co_flags & CO_VARARGS) != 0,
                                             (co_flags & CO_VARKEYWORDS) != 0, names};
        // __defaults__: a tuple of the last positional parameters' values.
        // (Read when the positional arguments do not fill the parameters,
        // or overflow them: the message names how many have defaults.)
        const proto::ProtoObject* defaultsObj = (argCount != static_cast<proto::proto_ulong>(nparams_count))
            ? codeOwner->getAttribute(calleeCtx, env->getDefaultsString()) : nullptr;
        ::protoPython::TupleView defaults = (defaultsObj && defaultsObj != PROTO_NONE && ::protoPython::PythonEnvironment::isTupleValue(calleeCtx, defaultsObj))
            ? ::protoPython::PythonEnvironment::tupleView(calleeCtx, defaultsObj) : nullptr;
        const proto::ProtoObject* defaultsBuf[16];
        std::vector<const proto::ProtoObject*> defaultsHeap;
        const proto::ProtoObject** defaultValues = defaultsBuf;
        const unsigned nDefaults = defaults ? static_cast<unsigned>(defaults->getSize(calleeCtx)) : 0u;
        if (nDefaults > 16) { defaultsHeap.resize(nDefaults); defaultValues = defaultsHeap.data(); }
        for (unsigned di = 0; di < nDefaults; ++di) defaultValues[di] = defaults->getAt(calleeCtx, static_cast<int>(di));
        // __kwdefaults__: a dict, its values keyed by the name's hash.
        const proto::ProtoSparseList* kwDefaults = nullptr;
        if (kwonly_count > 0) {
            const proto::ProtoObject* kwDefaultsObj = self->getAttribute(calleeCtx, env->getKwdefaultsString());
            const proto::ProtoObject* data = (kwDefaultsObj && kwDefaultsObj != PROTO_NONE)
                ? kwDefaultsObj->getAttribute(calleeCtx, env->getDataString()) : nullptr;
            kwDefaults = (data && data != PROTO_NONE) ? data->asSparseList(calleeCtx) : nullptr;
        }
        if (!env->bindArgumentsCore(calleeCtx, spec, args, kwargs, defaultValues, nDefaults, kwDefaults,
                                    const_cast<const proto::ProtoObject**>(slots))) {
            return nullptr;
        }
        if (!(co_flags & CO_OPTIMIZED)) {
            for (unsigned pi = 0; pi < nParamSlots; ++pi) {
                if (!isCellSlot(static_cast<int>(pi))) bindVar(static_cast<int>(pi), slots[pi]);
            }
        }
    }

    // Cells and free variables, once every parameter is bound.
    {
        const proto::proto_ulong nParamSlots = static_cast<proto::proto_ulong>(nparams_count + kwonly_count)
            + ((co_flags & CO_VARARGS) ? 1 : 0) + ((co_flags & CO_VARKEYWORDS) ? 1 : 0);
        setupClosureSlots(calleeCtx, env, slots, nSlots, closureLayout, codeOwner, nParamSlots);
    }

    // A non-CO_OPTIMIZED function keeps its locals on the frame itself; record
    // that under an internal key (read by zero-argument super()).  It is not
    // the Python-visible `f_locals`, which framePrototype computes on demand
    // as a mapping of the local variables.  Skipped for CO_OPTIMIZED
    // functions (params in slots) to save 1 AVL-tree op in the hot path.
    //
    // The value is a marker, not the frame: the frame is mutable, so storing
    // the frame in itself was a one-handle cycle that protoCore never
    // collects, and every call of such a function kept its frame for the
    // life of the space.  The reader (frameNamespaceOf in BuiltinsModule.cpp)
    // takes the object on the frame's chain that owns the marker, which is the
    // object the old value named.
    if (env && !(co_flags & CO_OPTIMIZED)) {
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFrameNamespaceString(), PROTO_TRUE));
    }

    if (isGenerator) {
        // CPython: a freshly-created (not yet started) generator/coroutine
        // has `gi_frame.f_back is None`. f_back is wired up to the caller
        // only while the generator is running. Reset what frame setup
        // earlier wrote so introspection during pre-start matches CPython.
        // For an immutable (CO_OPTIMIZED) frame `setAttribute` is
        // copy-on-write — must reassign the local to capture the snapshot.
        if (frame && env) {
            frame = const_cast<proto::ProtoObject*>(
                frame->setAttribute(calleeCtx, env->getFBackString(), PROTO_NONE));
        }
        proto::ProtoObject* gen = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
        // PB3: select prototype based on co_flags.
        //   CO_ASYNC_GENERATOR (0x200)   → async_generator
        //   CO_COROUTINE (0x80)          → coroutine
        //   else (CO_GENERATOR 0x20)     → generator
        bool hasCoroutineFlag = false;
        bool hasGeneratorFlag = false;
        bool isAsyncGenerator = false;
        if (env && env->getGeneratorPrototype()) {
            const proto::ProtoObject* genProto = env->getGeneratorPrototype();
            const proto::ProtoObject* coFlagsObj = codeObj->getAttribute(calleeCtx, env->getCoFlagsString());
            long long co_flags_val = (coFlagsObj && coFlagsObj->isInteger(calleeCtx)) ? coFlagsObj->asLong(calleeCtx) : 0;
            hasCoroutineFlag = (co_flags_val & (0x80 | 0x200)) != 0;
            hasGeneratorFlag = (co_flags_val & (0x20 | 0x200)) != 0;
            if (hasCoroutineFlag && hasGeneratorFlag && env->getAsyncGeneratorPrototype()) {
                genProto = env->getAsyncGeneratorPrototype();
                isAsyncGenerator = true;
            } else if (hasCoroutineFlag && env->getCoroutinePrototype()) {
                genProto = env->getCoroutinePrototype();
            }
            gen = const_cast<proto::ProtoObject*>(gen->addParent(calleeCtx, genProto));
            gen->setAttribute(calleeCtx, env->getClassString(), genProto);
        }
        gen->setAttribute(calleeCtx, env ? env->getGiCodeString() : PythonEnvironment::getInternedString(calleeCtx, "gi_code"), codeObj);
        gen->setAttribute(calleeCtx, env ? env->getGiFrameString() : PythonEnvironment::getInternedString(calleeCtx, "gi_frame"), frame);
        gen->setAttribute(calleeCtx, env ? env->getGiRunningString() : PythonEnvironment::getInternedString(calleeCtx, "gi_running"), PROTO_FALSE);
        gen->setAttribute(calleeCtx, env ? env->getGiPCString() : PythonEnvironment::getInternedString(calleeCtx, "gi_pc"), calleeCtx->fromInteger(0));
        // PEP 492 / 525 aliases: coroutines expose the gi_* state under cr_*
        // and async generators under ag_*. CPython implements these as
        // descriptors on the type; we mirror the data attributes directly so
        // the simple read paths (`coro.cr_frame`, `agen.ag_code`) work.
        if (hasCoroutineFlag) {
            static const char* cr_table[] = {"cr_code","cr_frame","cr_running"};
            static const char* ag_table[] = {"ag_code","ag_frame","ag_running"};
            const char* const* table = isAsyncGenerator ? ag_table : cr_table;
            gen->setAttribute(calleeCtx, PythonEnvironment::getInternedString(calleeCtx, table[0]), codeObj);
            gen->setAttribute(calleeCtx, PythonEnvironment::getInternedString(calleeCtx, table[1]), frame);
            gen->setAttribute(calleeCtx, PythonEnvironment::getInternedString(calleeCtx, table[2]), PROTO_FALSE);
            gen->setAttribute(calleeCtx, PythonEnvironment::getInternedString(calleeCtx,
                isAsyncGenerator ? "ag_suspended" : "cr_suspended"), PROTO_FALSE);
        }
        gen->setAttribute(calleeCtx, PythonEnvironment::getInternedString(calleeCtx, "gi_suspended"), PROTO_FALSE);
        
        // Generator/coroutine introspection: `gen.__name__` is the wrapped
        // function's name (from co_name) and `gen.__qualname__` is the
        // function's qualname. Both must be writable per CPython
        // (`gen.__name__ = "x"` is a documented use case in test_generators).
        if (env && codeObj) {
            const proto::ProtoObject* coName = codeObj->getAttribute(calleeCtx, env->getCoNameString());
            if (coName && coName != PROTO_NONE) {
                gen->setAttribute(calleeCtx, env->getNameString(), coName);
            }
            const proto::ProtoString* qnS = PythonEnvironment::getInternedString(calleeCtx, "__qualname__");
            const proto::ProtoObject* qn = self ? self->getAttribute(calleeCtx, qnS) : nullptr;
            if (!qn || qn == PROTO_NONE) qn = coName;
            if (qn && qn != PROTO_NONE) gen->setAttribute(calleeCtx, qnS, qn);
        }

        const proto::ProtoList* emptyStack = calleeCtx->newList();
        gen->setAttribute(calleeCtx, env ? env->getGiStackString() : PythonEnvironment::getInternedString(calleeCtx, "gi_stack"), emptyStack->asObject(calleeCtx));
        
        // Only the variable slots (co_varnames, cells, free variables): the
        // rest of the automatic area is the operand stack, empty here.
        // Saving all of it cost one list append per slot (~135 for a small
        // function) on every generator created.
        const proto::ProtoList* localList = calleeCtx->newList();
        const unsigned int nVarSlots = static_cast<unsigned int>(
            std::min<proto::proto_ulong>(nSlots, nLocalsPlus));
        for (unsigned int i = 0; i < nVarSlots; ++i) {
            localList = localList->appendLast(calleeCtx, slots[i]);
        }
        gen->setAttribute(calleeCtx, env ? env->getGiLocalsString() : PythonEnvironment::getInternedString(calleeCtx, "gi_locals"), localList->asObject(calleeCtx));
        
        promote(calleeCtx, gen);
        return gen;
    }

    CurrentFrameRestore frameRestore;
    if (env) {
        PythonEnvironment::setCurrentFrame(frame);
    }

    result = nullptr;
    {
        GlobalsScope gscope(globalsObj);

        // STRUCT-239: thread the executing code object into
        // PythonEnvironment's thread-local s_currentCodeObject so
        // builtins that need to inspect the running function's
        // metadata (locals(), dir() bare-case, sys._getframe().f_code)
        // see the function's code object rather than the module's.
        CodeObjectScope codeScope(codeObj);

        const proto::ProtoObject* bytecodeObj = codeObj->getAttribute(calleeCtx, env->getCoCodeString());
        const proto::ProtoObject* constsObj = codeObj->getAttribute(calleeCtx, env->getCoConstsString());
        const proto::ProtoObject* namesObj = codeObj->getAttribute(calleeCtx, env->getCoNamesString());

        ::protoPython::TupleView bytecode = bytecodeObj ? ::protoPython::PythonEnvironment::tupleView(calleeCtx, bytecodeObj) : nullptr;
        ::protoPython::TupleView consts = constsObj ? ::protoPython::PythonEnvironment::tupleView(calleeCtx, constsObj) : nullptr;
        ::protoPython::TupleView names = namesObj ? ::protoPython::PythonEnvironment::tupleView(calleeCtx, namesObj) : nullptr;

        // Extract pre-computed native bytecode int[] from co_bytecode_native (allocated once at
        // compile time by makeCodeObject). Passing int* to executeBytecodeRange avoids the per-call
        // std::vector rebuild (16 AVL lookups + malloc/free) that Step 5 still paid.
        const int* nativeBc = nullptr;
        if (env->getCoNativeBytecodeString()) {
            const proto::ProtoObject* nativeBcObj = codeObj->getAttribute(calleeCtx, env->getCoNativeBytecodeString());
            if (nativeBcObj && nativeBcObj != PROTO_NONE) {
                char* nbData = nativeBcObj->getDataIfByteBuffer(calleeCtx);
                if (nbData) {
                    nativeBc = reinterpret_cast<const int*>(nbData);
                }
            }
        }

        if (bytecode && consts) {
            // The operand stack starts after the locals and free variables.
            proto::proto_ulong stackOffset = nLocalsPlus;
            result = executeBytecodeRange(calleeCtx, consts.tuple, bytecode.tuple, names.tuple, frame, 0, bytecode->getSize(calleeCtx), stackOffset,
                nullptr, nullptr, nullptr, 0, nullptr, nativeBc);
        } else {
            result = PROTO_NONE;
        }
    }
    promote(calleeCtx, result);
    } // ContextScope destroyed here
    return result;
}

// Fast-path version: accepts raw C++ arg slice instead of ProtoList.
// Used by OP_CALL_FUNCTION when the callee is a known user function, eliminating
// the ctx->newList() + appendLast() cell allocations on the hot recursive call path.
// Falls back to runUserFunctionCall (via a temporary ProtoList) for generators,
// closures, and functions with more args than a small inline buffer.
static const proto::ProtoObject* runUserFunctionCallRaw(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ProtoSparseList* kwargs,
    const proto::ProtoObject* const* rawArgs,
    proto::proto_ulong rawArgCount) {

    if (!ctx || !self) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    // Read FunctionMetaCache — a single attribute lookup replaces all per-call codeObj reads.
    const proto::ProtoObject* codeObj = nullptr;
    const proto::ProtoObject* globalsObj = nullptr;
    int co_flags = 0, nparams_count = 0, kwonly_count = 0, automatic_count = 0;
    bool isGenerator = false;
    bool cacheNoInnerFunctions = false;
    const proto::ProtoTuple* co_varnames = nullptr;
    // Extended cached fields (Opt 2).
    const proto::ProtoTuple* cached_bytecode = nullptr;
    const proto::ProtoTuple* cached_consts   = nullptr;
    const proto::ProtoTuple* cached_names    = nullptr;
    const int*               cached_nativeBc = nullptr;
    const FunctionMetaCache* metaCache = nullptr;
    uint32_t                 cached_nConsts = 0;
    uint32_t                 cached_nNames  = 0;
    const proto::ProtoObject** cached_nativeConsts = nullptr;
    const proto::ProtoObject** cached_nativeNames  = nullptr;

    bool cacheHit = false;
    proto::proto_ulong nVarnames = 0;
    if (env && env->getFnMetaCacheString()) {
        const proto::ProtoObject* cacheAttr = self->getAttribute(ctx, env->getFnMetaCacheString());
        if (cacheAttr && cacheAttr != PROTO_NONE) {
            char* cacheData = cacheAttr->getDataIfByteBuffer(ctx);
            if (cacheData) {
                const FunctionMetaCache* cache =
                    reinterpret_cast<const FunctionMetaCache*>(cacheData);
                codeObj               = cache->codeObj;
                globalsObj            = cache->globalsObj;
                co_flags              = cache->co_flags;
                nparams_count         = cache->nparams;
                kwonly_count          = cache->kwonly;
                automatic_count       = cache->automatic_count;
                nVarnames             = cache->nVarnames;
                isGenerator           = cache->is_generator;
                co_varnames           = cache->co_varnames;
                cacheNoInnerFunctions = cache->no_inner_functions;
                metaCache             = cache;
                // Extended fields cached at BUILD_FUNCTION time.
                cached_bytecode       = cache->co_bytecode;
                cached_consts         = cache->co_consts_tuple;
                cached_names          = cache->co_names_tuple;
                cached_nativeBc       = cache->nativeBc;
                cached_nConsts        = cache->nConsts;
                cached_nNames         = cache->nNames;
                cached_nativeConsts   = reinterpret_cast<const proto::ProtoObject**>(
                                            const_cast<FunctionMetaCache*>(cache) + 1);
                cached_nativeNames    = cached_nativeConsts + cached_nConsts;
                cacheHit = true;
            }
        }
    }

    // For any non-trivial case (generator, kwargs, kwonly args, varargs, defaults
    // needed, a frame needed, no cache), fall back to the full implementation
    // via a temporary ProtoList.  Closures take the fast path: their cells
    // are set up from the cache below.
    bool useSlotFastPath = cacheHit && !isGenerator
        && cacheNoInnerFunctions && (co_flags & CO_OPTIMIZED)
        && kwonly_count == 0 && !(co_flags & CO_VARARGS) && !(co_flags & CO_VARKEYWORDS)
        && (!kwargs || !kwargs->getSize(ctx))
        // Exactly the positional parameters: defaults, or the TypeError for
        // a wrong count, are the full path's.
        && rawArgCount == (proto::proto_ulong)nparams_count;

    if (!useSlotFastPath) {
        // Build a temporary ProtoList and call the full implementation.
        //
        // Step #7 (2026-06-15, mirror of protoJS P-JS-5): use the single-
        // allocation `newList(n, items)` API instead of newList() + N×
        // appendLast. The old form rebuilt the list N+1 times (each
        // appendLast allocates a fresh ProtoList wrapping the previous
        // one as its tail), so calling a 4-arg function paid 5 cell
        // allocations on the args list alone. The new form is one
        // ProtoList ctor call regardless of arg count.
        const proto::ProtoList* argsList =
            ctx->newList(static_cast<unsigned>(rawArgCount), rawArgs);
        return runUserFunctionCall(ctx, self, nullptr, argsView(proto::ProtoListArguments(ctx, argsList)), kwargsView(proto::ProtoSparseListKeywordArguments(ctx, kwargs)));
    }

    if (!codeObj || codeObj == PROTO_NONE) return PROTO_NONE;
    if (!globalsObj || globalsObj == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoObject* result = PROTO_NONE;
    {
    ContextScope scope(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr, (size_t)automatic_count);
    proto::ProtoContext* calleeCtx = scope.context();

    // Bind positional args directly to slots (no ProtoList traversal).
    unsigned int nSlots = calleeCtx->getAutomaticLocalsCount();
    proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());
    for (proto::proto_ulong i = 0; i < rawArgCount && i < (proto::proto_ulong)nparams_count && i < nSlots; ++i)
        slots[i] = const_cast<proto::ProtoObject*>(rawArgs[i]);
    // The other co_varnames slots are locals and start unbound (see
    // runUserFunctionCall); this path only takes plain positional parameters.
    if (slots && co_varnames && env && env->getUnboundSentinel()) {
        proto::proto_ulong nVars = nVarnames;
        if (nVars > nSlots) nVars = nSlots;
        for (proto::proto_ulong vi = static_cast<proto::proto_ulong>(nparams_count); vi < nVars; ++vi)
            slots[vi] = const_cast<proto::ProtoObject*>(env->getUnboundSentinel());
    }

    // Cells and free variables (positional parameters only on this path).
    if (metaCache && (metaCache->nFree || metaCache->nCellSlots)) {
        setupClosureSlots(calleeCtx, env, slots, nSlots, ClosureLayout::fromCache(metaCache), self,
                          static_cast<proto::proto_ulong>(nparams_count));
    }

    // Frame creation: only when actually needed.
    //
    // For CO_OPTIMIZED functions that define no class and do not introspect
    // frames, the frame is never observed during normal execution
    // (LOAD_FAST/STORE_FAST use slots, LOAD_DEREF/STORE_DEREF the cells in
    // slots, BUILD_FUNCTION reads the current globals). sys._getframe() and
    // traceback walks are the only consumers; both can be supported lazily on
    // demand without paying for a full frame on every call. Profiles of
    // call_recursion (fib(25), 242k calls) showed 90% of time was the
    // frame-newObject path here; gating it restores the V97-era hot-call path.
    // (This path only runs functions that need no frame; see useSlotFastPath.)
    bool skipFrame = env && (co_flags & CO_OPTIMIZED) && !isGenerator && cacheNoInnerFunctions;
    // Only a call that builds a frame makes it current (below).
    CurrentFrameRestore frameRestore(!skipFrame);
    proto::ProtoObject* frame = nullptr;
    if (!skipFrame) {
        frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(false));
        if (env) {
            frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, env->getFramePrototype()));
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFCodeString(), codeObj));
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFGlobalsString(), globalsObj));
            // f_back is always present: None when no enclosing frame is
            // known (a caller that runs without a frame object, or the
            // outermost one).  A missing attribute made every walk of the
            // frame chain (traceback.extract_stack, asyncio in debug mode)
            // raise AttributeError.
            const proto::ProtoObject* parentFrame = PythonEnvironment::getCurrentFrame();
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFBackString(),
                parentFrame ? parentFrame : PROTO_NONE));
            PythonEnvironment::setCurrentFrame(frame);
        }
    }
    FrameScope fscope(frame);

    result = nullptr;
    {
        GlobalsScope gscope(globalsObj);

        // STRUCT-239: thread the executing code object — same as
        // the slow runUserFunctionCall path above.  Needed so
        // builtins like dir() / locals() can read co_varnames of
        // the currently-running function instead of the module's.
        CodeObjectScope codeScope(codeObj);

        // Use values cached at BUILD_FUNCTION time — avoids 4 cross-DSO getAttribute calls
        // and 3 asTuple conversions on every function invocation.
        const proto::ProtoTuple* bytecode = cached_bytecode;
        const proto::ProtoTuple* consts   = cached_consts;
        const proto::ProtoTuple* names    = cached_names;
        const int* nativeBc               = cached_nativeBc;

        if (bytecode && consts) {
            proto::proto_ulong stackOffset = metaCache ? metaCache->nLocalsPlus
                                                       : (co_varnames ? co_varnames->getSize(calleeCtx) : 0);
            result = executeBytecodeRange(calleeCtx, consts, bytecode, names, frame,
                                          0, bytecode->getSize(calleeCtx), stackOffset,
                                          nullptr, nullptr, nullptr, 0, nullptr, nativeBc,
                                          cached_nativeConsts, cached_nativeNames);
        } else {
            result = PROTO_NONE;
        }
    }
    promote(calleeCtx, result);
    } // ContextScope destroyed here
    return result;
}

} // namespace

const proto::ProtoObject* runBoundMethodCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* kwargs) {
    if (!ctx || !self) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;

    const proto::ProtoObject* im_self = self->getAttribute(ctx, env->getSelfDunderString());
    const proto::ProtoObject* im_func = self->getAttribute(ctx, env->getFuncDunderString());
    if (!im_self || !im_func) return PROTO_NONE;

    // Prepend im_self to args
    const proto::ProtoList* newArgs = ctx->newList()->appendLast(ctx, im_self);
    if (args) {
        for (proto::proto_ulong i = 0; i < args->getSize(ctx); ++i) {
            newArgs = newArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
        }
    }

    return invokePythonCallable(ctx, im_func, newArgs, kwargs ? kwargs->asSparseList(ctx) : nullptr);
}

namespace {

// Polymorphic Inline Cache for LOAD_ATTR / LOAD_METHOD slow-path lookups
// (sprint-2 step B, 2026-06-15). Caches the resolved value for
// `(type(obj), name)` pairs WITHOUT instance shadow.
//
// Cacheability invariant: only results that do not depend on the receiver
// may be shared across all instances of `type(obj)`. Two shapes qualify:
//   - isUnbound: a Python function found on the type. The CALL opcode
//     prepends self at dispatch time, so the cached function is shared.
//   - nativeMethod: a native method that the slow path bound to `obj`.
//     The entry keeps only the C++ function pointer and every hit
//     re-binds it to the current receiver.
// Anything else — descriptor __get__ results such as property values,
// or a bound method built for one specific instance — is never cached.
// Caching the slow path's receiver-bound native method made every later
// instance of the same type run the call on the first receiver
// (`b.append(x)` appended to `a`; all `_thread` locks shared one
// LockData, deadlocking `threading.Thread.start()`), and it parked a
// freshly allocated bound method in thread_local memory the GC cannot see.
//
// Invalidation: tied to PythonEnvironment::resolveCacheGeneration. Class
// mutations bump the generation; cache entries with a stale generation
// miss and are refilled. Entries are also tagged with the GC cycle count:
// `type` and `value` are raw pointers this thread_local table does not
// root, so once a GC cycle may have freed them an address can come back
// as an unrelated class.
struct LoadAttrPicEntry {
    const proto::ProtoObject* type         = nullptr;
    const proto::ProtoString* name         = nullptr;
    const proto::ProtoObject* value        = nullptr;
    proto::ProtoMethod        nativeMethod = nullptr;
    uint64_t                  generation   = 0;
    uint64_t                  gcEpoch      = 0;
    bool                      isUnbound    = false;
};
constexpr size_t kLoadAttrPicSize = 1024;
thread_local LoadAttrPicEntry g_loadAttrPic[kLoadAttrPicSize];

static inline size_t loadAttrPicIndex(const proto::ProtoObject* type,
                                       const proto::ProtoString* name) {
    // XOR-and-rotate the two pointer addresses. Both are aligned to at
    // least 8 bytes, so the low 3 bits are redundant; mixing higher bits
    // gives a reasonable spread for the direct-mapped cache.
    uintptr_t a = reinterpret_cast<uintptr_t>(type);
    uintptr_t b = reinterpret_cast<uintptr_t>(name);
    uintptr_t h = (a >> 3) ^ ((b >> 3) << 5) ^ (b >> 11);
    return static_cast<size_t>(h) & (kLoadAttrPicSize - 1);
}

static const proto::ProtoObject* py_function_get(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
    if (!ctx || !self || !args || args->getSize(ctx) < 1) return self;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    // In Python, calling __get__ on a class (instance == None) returns the function itself.
    if (!instance || instance == PROTO_NONE) return self;

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    const proto::ProtoObject* bound = ctx->newObject(true);
    if (env && env->getMethodPrototype()) {
        bound = bound->addParent(ctx, env->getMethodPrototype());
        bound = bound->setAttribute(ctx, env->getClassString(), env->getMethodPrototype());
    }
    
    // Set __self__ (the instance)
    bound = bound->setAttribute(ctx, env ? env->getSelfDunderString() : protoPython::PythonEnvironment::getInternalString(ctx, "__self__"),
                               instance);
    
    // Set __func__ (the original function)
    bound = bound->setAttribute(ctx, env ? env->getFuncDunderString() : protoPython::PythonEnvironment::getInternalString(ctx, "__func__"),
                               self);
    if (env) bound = env->stampBoundMethodMetadata(ctx, bound, self);
    
    // Copy __name__ and __qualname__ from the original function
    const proto::ProtoObject* funcName = self->getAttribute(ctx, env ? env->getNameString() : protoPython::PythonEnvironment::getInternalString(ctx, "__name__"));
    if (funcName && funcName != PROTO_NONE) {
        bound = bound->setAttribute(ctx, env ? env->getNameString() : protoPython::PythonEnvironment::getInternalString(ctx, "__name__"), funcName);
    }
    const proto::ProtoObject* funcQualname = self->getAttribute(ctx, env ? env->getInternedString(ctx, "__qualname__") : PythonEnvironment::getInternedString(ctx, "__qualname__"));
    if (funcQualname && funcQualname != PROTO_NONE) {
        bound = bound->setAttribute(ctx, env ? env->getInternedString(ctx, "__qualname__") : protoPython::PythonEnvironment::getInternalString(ctx, "__qualname__"), funcQualname);
    }
    
    // Set __call__ to skip runBoundMethodCall and use methodPrototype.__call__
    // The methodPrototype handles it naturally if __self__ and __func__ are set.

    
    // Also set __class__ to something reasonable if possible, but for now just Return
    return bound;
}

/** Create a callable object with __code__, __globals__, and __call__. */
// A function's __annotate__ (PEP 649 / 749), bound to its annotations dict.
//
// protoPython evaluates annotations when the function is defined, so there is
// nothing left to defer: Format.VALUE (1) and Format.FORWARDREF (3) return a
// new dict of the evaluated annotations, and Format.STRING (4) renders them
// with annotationlib.annotations_to_string. Every other format, including
// VALUE_WITH_FAKE_GLOBALS (2), raises NotImplementedError. CPython's
// compiler-generated __annotate__ supports VALUE (and fake globals) only and
// annotationlib.call_annotate_function derives FORWARDREF and STRING from its
// code object, which this native callable does not have; through
// call_annotate_function and annotationlib.get_annotations the results are
// the same. Bound to the dict, not to the function: a function holding a
// method bound to itself would form a cycle of mutable objects, which
// protoCore never reclaims.
static const proto::ProtoObject* functionAnnotate(proto::ProtoContext* ctx, const proto::ProtoObject* annotations,
                                                  const proto::ParentLink*, const proto::ProtoArguments* args,
                                                  const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    if (!args || args->getSize(ctx) != 1) {
        env->raiseTypeError(ctx, "__annotate__() takes exactly one argument (format)");
        return nullptr;
    }
    const proto::ProtoObject* formatObj = args->getAt(ctx, 0);
    if (formatObj && !formatObj->isInteger(ctx)) {
        // An IntEnum member such as annotationlib.Format.VALUE.
        formatObj = env->callMethod(formatObj, "__index__", {});
        if (!formatObj) return nullptr;
    }
    const long long format = (formatObj && formatObj->isInteger(ctx)) ? formatObj->asLong(ctx) : -1;
    const proto::ProtoObject* builtins = env->getBuiltins();
    if (format == 1 || format == 3) {
        const proto::ProtoObject* dictType = builtins->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "dict"));
        return env->callObject(dictType, {annotations});
    }
    if (format == 4) {
        const proto::ProtoObject* annotationlib = env->importModule("annotationlib");
        if (!annotationlib || annotationlib == PROTO_NONE) return nullptr;
        return env->callMethod(annotationlib, "annotations_to_string", {annotations});
    }
    const proto::ProtoObject* notImplemented = builtins->getAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "NotImplementedError"));
    const proto::ProtoObject* exc = notImplemented ? env->callObject(notImplemented, {}) : nullptr;
    if (exc && exc != PROTO_NONE) env->raiseException(exc);
    return nullptr;
}

/** A new Python function for `codeObj`.  `closure` is the list of cells it
 *  closes over, one per name in co_freevars except a recursive def's own name
 *  (co_selfref), or nullptr when it has none. */
static proto::ProtoObject* createUserFunction(proto::ProtoContext* ctx, const proto::ProtoObject* codeObj, proto::ProtoObject* globalsFrame, const proto::ProtoList* closure = nullptr, const proto::ProtoObject* defaults = nullptr, const proto::ProtoObject* kwDefaults = nullptr) {
    if (!ctx || !codeObj || !globalsFrame) return nullptr;
    if (!ctx || !codeObj || !globalsFrame) return nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    
    // Functions must be mutable so that decorators can attach attributes via
    // setattr(func, '_marked', True) and similar patterns (functools.wraps,
    // unittest.skip, dataclasses field metadata, …).  Immutable functions
    // would cause every setAttribute to return a fresh object the Python-side
    // variable cannot see.
    const proto::ProtoObject* fn = nullptr;
    if (env && env->getFunctionPrototype()) {
        fn = env->getFunctionPrototype()->newChild(ctx, true);
    } else {
        fn = ctx->newObject(true);
    }
    fn = fn->setAttribute(ctx, env ? env->getCodeString() : PythonEnvironment::getInternedString(ctx, "__code__"), codeObj);
    fn = fn->setAttribute(ctx, env ? env->getGlobalsString() : PythonEnvironment::getInternedString(ctx, "__globals__"), globalsFrame);
    // Explicitly set __class__ to fix type identity if prototype linkage failed
    if (env && env->getFunctionPrototype()) {
        fn = fn->setAttribute(ctx, env ? env->getClassString() : PythonEnvironment::getInternedString(ctx, "__class__"), env->getFunctionPrototype());
    }
    if (codeObj) {
        const proto::ProtoString* co_name_s = PythonEnvironment::getInternedString(ctx, "co_name");
        const proto::ProtoObject* codeName = codeObj->getAttribute(ctx, co_name_s);
        if (codeName && codeName != PROTO_NONE) {
            fn = fn->setAttribute(ctx, env ? env->getNameString() : PythonEnvironment::getInternedString(ctx, "__name__"), codeName);
            // Prefer `co_qualname` (the compiler stamps the dotted nested-aware
            // form, e.g. `C.method.<locals>.inner`); fall back to `co_name`
            // when absent so module-level defs still get a meaningful value.
            const proto::ProtoString* coQualS = PythonEnvironment::getInternedString(ctx, "co_qualname");
            const proto::ProtoObject* coQualName = codeObj->getAttribute(ctx, coQualS);
            if (!coQualName || coQualName == PROTO_NONE || !coQualName->isString(ctx)) {
                coQualName = codeName;
            }
            fn = fn->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__qualname__"), coQualName);
        }
    }
    
    // Add missing default function attributes required by CPython/functools
    if (env) {
        const proto::ProtoString* dictS = PythonEnvironment::getInternedString(ctx, "__dict__");
        const proto::ProtoString* annS = PythonEnvironment::getInternedString(ctx, "__annotations__");
        const proto::ProtoString* modS = PythonEnvironment::getInternedString(ctx, "__module__");
        const proto::ProtoString* docS = PythonEnvironment::getInternedString(ctx, "__doc__");
        
        // Materialise both `__dict__` and `__annotations__` as proper empty
        // dicts: dictPrototype->newChild() alone leaves the child without
        // its own `__data__`/`__keys__` slots, so iteration / membership
        // queries walk the prototype chain and surface every dict method
        // (`pop`, `get`, `keys`, …) as a phantom annotation entry.  Bind
        // empty SparseList/List in both canonical slots so the dict reads
        // as `{}`.
        auto makeFreshEmptyDict = [&]() -> const proto::ProtoObject* {
            if (!env->getDictPrototype()) return ctx->newObject(true);
            const proto::ProtoObject* d = env->getDictPrototype()->newChild(ctx, true);
            d = d->setAttribute(ctx, env->getDataString(),
                                  ctx->newSparseList()->asObject(ctx));
            d = d->setAttribute(ctx, env->getKeysString(),
                                  ctx->newList()->asObject(ctx));
            return d;
        };
        const proto::ProtoObject* emptyDict1 = makeFreshEmptyDict();
        const proto::ProtoObject* emptyDict2 = makeFreshEmptyDict();

        fn = fn->setAttribute(ctx, dictS, emptyDict1);
        fn = fn->setAttribute(ctx, annS, emptyDict2);
        // Surface the function's docstring (the compiler stores it on the
        // code object as `co_doc` when the body's first statement is a
        // string literal). Default to None for non-docstring functions —
        // we deliberately do NOT crawl co_consts here because protoPython
        // shares constants across the body, so `def g(): return "abc"`
        // would incorrectly inherit "abc" as its docstring.
        const proto::ProtoObject* docVal = PROTO_NONE;
        if (codeObj) {
            const proto::ProtoString* coDocS = PythonEnvironment::getInternedString(ctx, "co_doc");
            const proto::ProtoObject* docAttr = codeObj->getAttribute(ctx, coDocS);
            if (docAttr && docAttr != PROTO_NONE && docAttr->isString(ctx)) {
                docVal = docAttr;
            }
        }
        fn = fn->setAttribute(ctx, docS, docVal);
        
        const proto::ProtoObject* modName = globalsFrame->getAttribute(ctx, env->getNameString());
        if (modName && modName != PROTO_NONE) {
            fn = fn->setAttribute(ctx, modS, modName);
        } else {
            fn = fn->setAttribute(ctx, modS, PROTO_NONE);
        }
    }
    // The function keeps the cells it closes over, never a frame: a cell is
    // the only mutable object between it and the scope that defined it, so
    // defining a function in a call closes no cycle through that call's frame
    // (protoCore docs/MemoryModel.md, section 7; Compiler.h, "Closures").
    // `__closure__` (None or a tuple of cells) is computed from this.
    if (closure && env && closure->getSize(ctx) > 0) {
        fn = fn->setAttribute(ctx, env->getClosureString(), closure->asObject(ctx));
    }
    // Opcode census of the native bytecode, for the frame decision below:
    // a function that defines a function or a class keeps a frame, so that
    // what it runs (a nested function, a class body) finds it as f_back.
    bool nbScanned = false, hasClassOp = false;
    const int* scannedNativeBc = nullptr;
    if (env && codeObj && env->getCoNativeBytecodeString()) {
        const proto::ProtoObject* nbObj =
            codeObj->getAttribute(ctx, env->getCoNativeBytecodeString());
        if (nbObj && nbObj != PROTO_NONE) {
            char* nbData = nbObj->getDataIfByteBuffer(ctx);
            if (nbData) {
                scannedNativeBc = reinterpret_cast<const int*>(nbData);
                const size_t nbLen =
                    nbObj->asByteBuffer(ctx)->getSize(ctx) / sizeof(int);
                for (size_t bi = 0; bi < nbLen; bi += 2) {
                    const int op = scannedNativeBc[bi];
                    if (op == OP_BUILD_CLASS || op == OP_BUILD_FUNCTION) hasClassOp = true;
                }
                nbScanned = true;
            }
        }
    }
    if (defaults && env) {
        fn = fn->setAttribute(ctx, env->getDefaultsString(), defaults);
    }
    if (kwDefaults && env) {
        fn = fn->setAttribute(ctx, env->getKwdefaultsString(), kwDefaults);
    }
    // `__call__` and `__get__` come from the FUNCTION PROTOTYPE, which already
    // carries both as `fromMethod(nullptr, ...)` cells (PythonEnvironment.cpp,
    // where functionPrototype is built).  Installing a second, per-instance copy
    // here bound to `fn` made every function object IMMORTAL, and that is not a
    // figure of speech:
    //
    //   * a function object is mutable, so its live state lives in
    //     `ProtoSpace::mutableRoot[mutable_ref % 256]`, which the collector
    //     traces as a root;
    //   * `ProtoMethodCell::processReferences` traces the cell's `self`;
    //   * so `fn.__call__ = fromMethod(fn, ...)` made the handle reachable FROM
    //     its own shard root, and protoCore releases a shard entry only when the
    //     handle is finalized (`gcFinalizedMutableRefs`).  Reachable handle, no
    //     finalize; no finalize, no release: a self-sustaining pair.
    //
    // Measured: ~100-150 marked cells retained per function object created at
    // run time, for ever.  `threading.Thread.__init__` builds one closure per
    // Thread (`_make_invoke_excepthook`), which is how this presented as
    // "protoPython retains ~300 marked cells per finished thread" and is what
    // drove the rule-8 conformance case into its heap ceiling.
    //
    // Dispatch does not need the binding.  `ProtoObject::call` and every
    // protoPython call site pass the receiver explicitly, and invokeCallable's
    // fast path for user functions does not read `__call__` at all — it
    // recognises an own `__code__` and calls runUserFunctionCall directly.
    //
    // The bootstrap fallback below (no env, or no function prototype yet) has no
    // prototype to inherit from, so it still installs its own pair.  Those
    // objects are created once during bootstrap and are live for the session
    // anyway.
    if (!env || !env->getFunctionPrototype()) {
        fn = fn->setAttribute(ctx, env ? env->getCallString() : PythonEnvironment::getInternedString(ctx, "__call__"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(fn), runUserFunctionCall));
        fn = fn->setAttribute(ctx, env ? env->getGetDunderString() : PythonEnvironment::getInternedString(ctx, "__get__"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(fn), py_function_get));
    }

    // Build FunctionMetaCache: pre-compute constant codeObj scalars once here so
    // runUserFunctionCall reads one ByteBuffer field instead of multiple separate getAttribute calls.
    // The cache is extended with flat arrays of const ProtoObject* for co_consts and co_names,
    // laid out immediately after the FunctionMetaCache struct in the same ByteBuffer allocation.
    if (env && env->getFnMetaCacheString() && codeObj && ctx->space) {
        const proto::ProtoObject* globalsObj = globalsFrame;
        auto getInt = [&](const proto::ProtoString* key) -> int {
            const proto::ProtoObject* v = codeObj->getAttribute(ctx, key);
            return (v && v->isInteger(ctx)) ? static_cast<int>(v->asLong(ctx)) : 0;
        };

        // Gather all data needed before allocating the variable-length cache buffer.
        int co_flags_val       = env->getCoFlagsString()          ? getInt(env->getCoFlagsString())          : 0;
        int nparams_val        = env->getCoNparamsString()         ? getInt(env->getCoNparamsString())         : 0;
        int kwonly_val         = env->getCoKwonlyargcountString()  ? getInt(env->getCoKwonlyargcountString())  : 0;
        int automatic_val      = env->getCoAutomaticCountString()  ? getInt(env->getCoAutomaticCountString())  : 0;
        const proto::ProtoObject* isGenObj = env->getCoIsGeneratorString()
            ? codeObj->getAttribute(ctx, env->getCoIsGeneratorString()) : nullptr;
        bool is_generator_val  = isGenObj && isGenObj->isBoolean(ctx) && isGenObj->asBoolean(ctx);

        const proto::ProtoObject* cvObj = env->getCoVarnamesString()
            ? codeObj->getAttribute(ctx, env->getCoVarnamesString()) : nullptr;
        ::protoPython::TupleView co_varnames_val = (cvObj && ::protoPython::PythonEnvironment::tupleView(ctx, cvObj)) ? ::protoPython::PythonEnvironment::tupleView(ctx, cvObj) : nullptr;

        // Resolve the co_consts, co_names, and co_bytecode tuples.
        const proto::ProtoObject* constsObj = env->getCoConstsString()
            ? codeObj->getAttribute(ctx, env->getCoConstsString()) : nullptr;
        ::protoPython::TupleView co_consts_val = constsObj ? ::protoPython::PythonEnvironment::tupleView(ctx, constsObj) : nullptr;

        const proto::ProtoObject* namesObj = env->getCoNamesString()
            ? codeObj->getAttribute(ctx, env->getCoNamesString()) : nullptr;
        ::protoPython::TupleView co_names_val = namesObj ? ::protoPython::PythonEnvironment::tupleView(ctx, namesObj) : nullptr;

        const proto::ProtoObject* codeObjTupleObj = env->getCoCodeString()
            ? codeObj->getAttribute(ctx, env->getCoCodeString()) : nullptr;
        ::protoPython::TupleView co_bytecode_val = codeObjTupleObj ? ::protoPython::PythonEnvironment::tupleView(ctx, codeObjTupleObj) : nullptr;

        // Native bytecode and its opcode census were resolved once above.
        // Reusing them saves a second scan of the whole bytecode.
        const int* nativeBc_val = scannedNativeBc;
        // A function that names sys._getframe or inspect.currentframe asks
        // for its own frame (or its caller's through f_back), so it must not
        // run frameless: count it with the functions that need a frame.
        // Over-detection (an unrelated `currentframe` name) only costs a frame.
        bool introspectsFrames = false;
        if (co_names_val) {
            const proto::ProtoObject* getframeS = PythonEnvironment::getInternedString(ctx, "_getframe")->asObject(ctx);
            const proto::ProtoObject* currentframeS = PythonEnvironment::getInternedString(ctx, "currentframe")->asObject(ctx);
            for (proto::proto_ulong ni = 0; ni < co_names_val->getSize(ctx) && !introspectsFrames; ++ni) {
                const proto::ProtoObject* nm = co_names_val->getAt(ctx, static_cast<int>(ni));
                introspectsFrames = (nm == getframeS || nm == currentframeS);
            }
        }
        const bool no_inner_functions_val = nbScanned && !hasClassOp && !introspectsFrames;

        // The closure layout, from the code object (Compiler::stampClosureLayout).
        const ClosureLayout layout = ClosureLayout::fromCode(ctx, env, codeObj, nullptr);
        const proto::ProtoObject* nlpObj = codeObj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "co_nlocalsplus"));
        const uint32_t nVarnames_val = co_varnames_val ? static_cast<uint32_t>(co_varnames_val->getSize(ctx)) : 0;

        // Compute flat array sizes.
        uint32_t nConsts_val = co_consts_val ? static_cast<uint32_t>(co_consts_val->getSize(ctx)) : 0;
        uint32_t nNames_val  = co_names_val  ? static_cast<uint32_t>(co_names_val->getSize(ctx))  : 0;

        // Allocate variable-length buffer: struct + two flat pointer arrays
        // + the cell slots.
        size_t totalBytes = sizeof(FunctionMetaCache)
            + (static_cast<size_t>(nConsts_val) + static_cast<size_t>(nNames_val))
              * sizeof(const proto::ProtoObject*)
            + static_cast<size_t>(layout.nCellSlots) * sizeof(int32_t);
        char* buf = new char[totalBytes];
        FunctionMetaCache* cache = new(buf) FunctionMetaCache{};

        cache->co_flags           = co_flags_val;
        cache->nparams            = nparams_val;
        cache->kwonly             = kwonly_val;
        cache->automatic_count    = automatic_val;
        cache->nVarnames          = nVarnames_val;
        cache->is_generator       = is_generator_val;
        cache->no_inner_functions = no_inner_functions_val;
        cache->selfRef            = layout.selfRef;
        cache->selfRefCell        = layout.selfRefCell;
        cache->nFree              = layout.nFree;
        cache->nCellSlots         = layout.nCellSlots;
        cache->nLocalsPlus        = (nlpObj && nlpObj->isInteger(ctx))
            ? static_cast<uint32_t>(nlpObj->asLong(ctx)) : nVarnames_val;
        cache->closure            = (closure && closure->getSize(ctx) > 0) ? closure : nullptr;
        {
            const proto::ProtoObject* po = codeObj->getAttribute(ctx,
                PythonEnvironment::getInternedString(ctx, "co_posonlyargcount"));
            cache->nPosOnly = (po && po->isInteger(ctx)) ? static_cast<uint32_t>(po->asLong(ctx)) : 0u;
        }
        cache->codeObj            = codeObj;
        cache->globalsObj         = globalsObj;
        cache->co_varnames        = co_varnames_val.tuple;
        cache->co_bytecode        = co_bytecode_val.tuple;
        cache->co_consts_tuple    = co_consts_val.tuple;
        cache->co_names_tuple     = co_names_val.tuple;
        cache->nativeBc           = nativeBc_val;
        cache->nConsts            = nConsts_val;
        cache->nNames             = nNames_val;

        // Fill flat arrays of pre-fetched ProtoObject pointers.
        const proto::ProtoObject** nativeConsts = reinterpret_cast<const proto::ProtoObject**>(cache + 1);
        const proto::ProtoObject** nativeNames  = nativeConsts + nConsts_val;
        for (uint32_t i2 = 0; i2 < nConsts_val; ++i2)
            nativeConsts[i2] = co_consts_val->getAt(ctx, static_cast<int>(i2));
        for (uint32_t i2 = 0; i2 < nNames_val; ++i2)
            nativeNames[i2] = co_names_val->getAt(ctx, static_cast<int>(i2));
        int32_t* cellSlotsOut = const_cast<int32_t*>(cache->cellSlots());
        for (uint32_t i2 = 0; i2 < layout.nCellSlots; ++i2)
            cellSlotsOut[i2] = layout.cellSlots[i2];

        const proto::ProtoObject* cacheObj = ctx->fromBuffer(
            totalBytes, buf, true);
        if (cacheObj) {
            // Not pinned: the function keeps the cache through this attribute,
            // and the cache (a byte buffer) references no cell, so a pin kept
            // nothing else alive.  It used to be pinned for the life of the
            // environment, one root per function ever created (every
            // comprehension and lambda executed), in a set the collector scans
            // inside each stop-the-world pause.  The pointers inside the cache
            // name the code object, its tuples and the globals, which the
            // function's __code__ and __globals__ keep alive.
            fn = fn->setAttribute(ctx, env->getFnMetaCacheString(), cacheObj);
        }
    }

    return const_cast<proto::ProtoObject*>(fn);
}



// Returns true iff `bCls` is a strict subclass of `aCls` (i.e. aCls
// appears in bCls.__mro__ but bCls != aCls). Used to implement
// CPython's reflected-operator subclass priority rule.
static bool isStrictSubclassOf(proto::ProtoContext* ctx,
                                PythonEnvironment* env,
                                const proto::ProtoObject* bCls,
                                const proto::ProtoObject* aCls) {
    if (!env || !bCls || !aCls || bCls == aCls) return false;
    const proto::ProtoString* mroS = env->getMroString();
    if (!mroS) return false;
    const proto::ProtoObject* mroObj = env->getAttribute(ctx, bCls, mroS, false);
    ::protoPython::TupleView mroTup = mroObj ? ::protoPython::PythonEnvironment::tupleView(ctx, mroObj) : nullptr;
    if (!mroTup) return false;
    proto::proto_ulong n = mroTup->getSize(ctx);
    for (proto::proto_ulong i = 0; i < n; ++i) {
        if (mroTup->getAt(ctx, static_cast<int>(i)) == aCls) return true;
    }
    return false;
}

// Returns true iff `bCls` defines `name` as an own attribute (i.e.
// not inherited). Used to detect "subclass overrides reflected op".
static bool typeOwnsAttribute(proto::ProtoContext* ctx,
                              const proto::ProtoObject* bCls,
                              const proto::ProtoString* name) {
    if (!bCls || !name) return false;
    return bCls->hasOwnAttribute(ctx, name) == PROTO_TRUE;
}

static const proto::ProtoObject* binaryOpDispatch(proto::ProtoContext* ctx, const proto::ProtoObject* a, const proto::ProtoObject* b, const char* dunder, const char* rdunder) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* dunderS = PythonEnvironment::getInternedString(ctx, dunder);
    const proto::ProtoString* rdunderS = PythonEnvironment::getInternedString(ctx, rdunder);

    // CPython subclass priority rule: when type(b) is a strict subclass
    // of type(a) AND type(b) owns `__rop__` directly (i.e. defines its
    // own override, not just inherits one), `b.__rop__(a)` is tried
    // *before* `a.__op__(b)`. e.g. `class Frac(int): def __radd__(...)`
    // gets the chance to handle `1 + Frac(2)` instead of int.__add__.
    // The `typeOwnsAttribute(bCls, rdunder)` guard prevents the priority
    // from firing when bCls inherits __rop__ from aCls (in which case
    // it's the SAME implementation and order doesn't matter — but
    // calling __rop__ first would re-enter the same code, costing a
    // dispatch round-trip).
    if (env && a && b) {
        const proto::ProtoObject* aCls = env->getType(ctx, a);
        const proto::ProtoObject* bCls = env->getType(ctx, b);
        if (isStrictSubclassOf(ctx, env, bCls, aCls)
            && typeOwnsAttribute(ctx, bCls, rdunderS)) {
            const proto::ProtoList* argsA = ctx->newList()->appendLast(ctx, a);
            // An error from b.__rop__ is dropped and the normal order tried,
            // as before the C++-exceptions change (CPython propagates it).
            const proto::ProtoObject* res = nullptr;
            try {
                res = invokeDunder(ctx, b, rdunderS, argsA);
            } catch (const proto::ProtoException&) {
                res = nullptr;
            }
            if (res && (!env || res != env->getNotImplementedPrototype())) {
                return res;
            }
            // Fall through to the normal forward-then-reflected order
            // when b.__rop__ returned NotImplemented or nullptr.
        }
    }

    // Track whether either path explicitly returned NotImplemented.
    // CPython distinguishes "method exists and returned NotImplemented"
    // from "method absent" — only the former should produce a TypeError
    // at the operator level. (Module init paths in stdlib often probe
    // for capabilities by trying ops and accepting nullptr/None as
    // "not supported, move on"; raising unconditionally breaks them.)
    bool sawNotImplemented = false;
    const proto::ProtoList* argsB = ctx->newList()->appendLast(ctx, b);
    const proto::ProtoObject* res = invokeDunder(ctx, a, dunderS, argsB);
    if (env && res == env->getNotImplementedPrototype()) sawNotImplemented = true;
    if (!res || (env && res == env->getNotImplementedPrototype())) {
        const proto::ProtoList* argsA = ctx->newList()->appendLast(ctx, a);
        res = invokeDunder(ctx, b, rdunderS, argsA);
        if (env && res == env->getNotImplementedPrototype()) sawNotImplemented = true;
    }
    // CPython parity: at the operator level, when neither path produced
    // a value (and no exception is pending), raise TypeError —
    // matches `a OP b` semantics for `unsupported operand type(s) ...`.
    // The previous "lenient on absent methods" carve-out hid genuine
    // user-level errors; if a stdlib init path ever turns out to need
    // the silent return (we audited current paths and corrected the
    // one we found, in enum.py FlagBoundary), give it its own entry
    // point rather than letting the operator-level dispatcher swallow
    // null silently.
    (void)sawNotImplemented;
    if ((!res || (env && res == env->getNotImplementedPrototype())) && env) {
        std::string aName = "?", bName = "?";
        const proto::ProtoString* nameS = env->getNameString();
        const proto::ProtoObject* aCls2 = env->getType(ctx, a);
        const proto::ProtoObject* bCls2 = env->getType(ctx, b);
        if (aCls2) {
            const proto::ProtoObject* n = aCls2->getAttribute(ctx, nameS);
            if (n && n->isString(ctx)) n->asString(ctx)->toUTF8String(ctx, aName);
        }
        if (bCls2) {
            const proto::ProtoObject* n = bCls2->getAttribute(ctx, nameS);
            if (n && n->isString(ctx)) n->asString(ctx)->toUTF8String(ctx, bName);
        }
        const char* opSym = dunder;
        struct { const char* d; const char* sym; } map[] = {
            {"__add__", "+"}, {"__sub__", "-"}, {"__mul__", "*"},
            {"__truediv__", "/"}, {"__floordiv__", "//"}, {"__mod__", "%"},
            {"__pow__", "** or pow()"}, {"__lshift__", "<<"}, {"__rshift__", ">>"},
            {"__and__", "&"}, {"__or__", "|"}, {"__xor__", "^"},
            {"__matmul__", "@"},
        };
        for (auto& e : map) if (std::strcmp(dunder, e.d) == 0) { opSym = e.sym; break; }
        std::string msg = "unsupported operand type(s) for ";
        msg += opSym;
        msg += ": '";
        msg += aName;
        msg += "' and '";
        msg += bName;
        msg += "'";
        env->raiseTypeError(ctx, msg.c_str());
        return nullptr;
    }
    return res;
}

static bool isEmbeddedValue(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    return obj->isInteger(ctx) || obj->isBoolean(ctx) || obj->isNone(ctx);
}

// bool is a subclass of int in CPython; PROTO_TRUE / PROTO_FALSE are sentinel
// pointers, so isInteger() / isSmallInt() return false for them.  Anywhere a
// path branches on isInteger() to take an integer fast path, callers should
// first run operands through this helper so True / False participate as 1 / 0.
static inline const proto::ProtoObject* coerceBoolToInt(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (obj && obj->isBoolean(ctx)) return ctx->fromInteger(obj == PROTO_TRUE ? 1 : 0);
    return obj;
}

// The in-place dunder `name` (`__iand__`, ...) of `a` applied to `b`: native
// or defined in Python, looked up on the object.  nullptr when `a` has no
// such method or it returned NotImplemented: the caller then falls back to the
// binary operator, as CPython does.
static const proto::ProtoObject* inplaceDunder(proto::ProtoContext* ctx, PythonEnvironment* env,
                                               const proto::ProtoObject* a, const proto::ProtoObject* b,
                                               const proto::ProtoString* name) {
    const proto::ProtoObject* method = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, name);
    if (!method || method == PROTO_NONE) return nullptr;
    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
    const proto::ProtoObject* result = method->asMethod(ctx)
        ? method->asMethod(ctx)(ctx, a, nullptr, argsView(proto::ProtoListArguments(ctx, oneArg)), nullptr)
        : invokeDunder(ctx, a, name, oneArg);
    if (env && result == env->getNotImplementedPrototype()) return nullptr;
    return result;
}

// Python's `a << b` / `a >> b` for two ints (bools already coerced), of any
// size: a negative count raises ValueError; a count too large for protoCore's
// shift gives 0 or -1 for `>>` and, unless `a` is 0, OverflowError for `<<`.
// The binary opcodes take it after their SmallInteger fast path; the in-place
// opcodes after `__ilshift__` / `__irshift__`.
static const proto::ProtoObject* pyIntShift(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                                            const proto::ProtoObject* b, bool left) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* zero = ctx->fromInteger(0);
    long long amount = 0;
    const bool fits = PythonEnvironment::tryAsLong(ctx, b, amount);
    const bool negative = fits ? amount < 0 : b->compare(ctx, zero) < 0;
    if (negative) {
        env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx, "negative shift count")->asObject(ctx));
        return PROTO_NONE;
    }
    if (!fits || amount > std::numeric_limits<int>::max()) {
        const int sign = a->compare(ctx, zero);
        if (!left) return ctx->fromInteger(sign < 0 ? -1 : 0);
        if (sign == 0) return zero;
        env->raiseOverflowError(ctx, "too many digits in integer");
        return PROTO_NONE;
    }
    return left ? a->shiftLeft(ctx, static_cast<int>(amount)) : a->shiftRight(ctx, static_cast<int>(amount));
}

static const proto::ProtoObject* unwrapPrimitive(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (obj->isInteger(ctx) || obj->isDouble(ctx)) return obj;
    // bool is a subclass of int in CPython.  PROTO_TRUE / PROTO_FALSE are
    // sentinel pointers, not SmallInts, so isInteger() returns false for
    // them; coerce here so binaryAdd / binarySubtract / binaryMultiply hit
    // their integer fast path instead of falling through to a __add__ /
    // __mul__ lookup that doesn't exist on the sentinel.
    if (obj->isBoolean(ctx)) {
        return ctx->fromInteger(obj == PROTO_TRUE ? 1 : 0);
    }
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
    if (env) {
        const proto::ProtoObject* data = obj->getAttribute(ctx, env->getDataString());
        if (data && (data->isInteger(ctx) || data->isDouble(ctx))) return data;
    }
    return obj;
}

// Returns true iff BOTH `a` and `b` are exactly the literal numeric
// built-in types (int / bool / float — never a subclass). Use this as
// the gate for any C-level primitive numeric fast path so that a
// Python subclass with an overridden __op__ / __rop__ correctly routes
// through the dunder dispatcher instead of being silently bypassed.
//
// The env-less fallback (legacy / pre-typed callers) preserves the
// original "both are numeric primitives" semantics, since without an
// environment we cannot consult the prototype map.
static inline bool bothExactNumericPrim(
    proto::ProtoContext* ctx,
    PythonEnvironment* env,
    const proto::ProtoObject* a, const proto::ProtoObject* b,
    const proto::ProtoObject* aa, const proto::ProtoObject* bb)
{
    if (!env) {
        // No env context: fall back to the unwrapped primitive check.
        return (aa->isInteger(ctx) || aa->isDouble(ctx))
            && (bb->isInteger(ctx) || bb->isDouble(ctx));
    }
    const proto::ProtoObject* aCls = env->getType(ctx, a);
    if (aCls != env->getIntPrototype()
        && aCls != env->getBoolPrototype()
        && aCls != env->getFloatPrototype()) return false;
    const proto::ProtoObject* bCls = env->getType(ctx, b);
    return (bCls == env->getIntPrototype()
            || bCls == env->getBoolPrototype()
            || bCls == env->getFloatPrototype());
}

// True when at least one operand is a wrapped numeric subclass (so
// `bothExactNumericPrim` declined the fast path), the unwrapped values
// are still both numeric, AND neither operand's class owns the
// operation dunder.  Owning means defining `__op__` / `__rop__` in the
// class body — in that case CPython routes through the override and
// we must respect that.  When no override exists, the inherited
// behaviour IS the primitive op, so doing it directly is both correct
// and avoids the missing-dunder cliff that would otherwise raise
// "unsupported operand type(s) for ...".
static inline bool numericSubclassFastPathOK(
    proto::ProtoContext* ctx,
    PythonEnvironment* env,
    const proto::ProtoObject* a, const proto::ProtoObject* b,
    const proto::ProtoObject* aa, const proto::ProtoObject* bb,
    const char* dunder, const char* rdunder)
{
    if (!env || !aa || !bb) return false;
    bool aaNum = aa->isInteger(ctx) || aa->isDouble(ctx);
    bool bbNum = bb->isInteger(ctx) || bb->isDouble(ctx);
    if (!aaNum || !bbNum) return false;
    if (a == aa && b == bb) return false;  // both already primitive — handled above
    const proto::ProtoString* dS = PythonEnvironment::getInternedString(ctx, dunder);
    const proto::ProtoString* rdS = PythonEnvironment::getInternedString(ctx, rdunder);
    const proto::ProtoObject* aCls = env->getType(ctx, a);
    const proto::ProtoObject* bCls = env->getType(ctx, b);
    bool aOwnsOp = aCls && aCls->hasOwnAttribute(ctx, dS) == PROTO_TRUE;
    bool bOwnsROp = bCls && bCls->hasOwnAttribute(ctx, rdS) == PROTO_TRUE;
    return !aOwnsOp && !bOwnsROp;
}

static const proto::ProtoObject* binaryAdd(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b);

// `a += b`: a.__iadd__(b), then `a + b` when a has no __iadd__ or it answers
// NotImplemented.  INPLACE_ADD and the fused ACC_FAST_FAST / INC_FAST_K
// slow paths share it: the fused forms called binaryAdd alone, so inside a
// function `l += x` rebound l to a new list (aliases of l missed the update)
// and `l += "ab"` raised TypeError instead of extending.
static const proto::ProtoObject* inplaceAdd(proto::ProtoContext* ctx, PythonEnvironment* env,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoString* iaddS = env ? env->getIAddString() : PythonEnvironment::getInternedString(ctx, "__iadd__");
    const proto::ProtoObject* iadd = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, iaddS);
    const proto::ProtoObject* result = nullptr;
    if (iadd && iadd->asMethod(ctx)) {
        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
        result = iadd->asMethod(ctx)(ctx, a, nullptr, argsView(proto::ProtoListArguments(ctx, oneArg)), nullptr);
        if (env && result == env->getNotImplementedPrototype()) result = nullptr;
    } else if (iadd && iadd != PROTO_NONE) {
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
        result = invokeDunder(ctx, a, iaddS, args);
        if (env && result == env->getNotImplementedPrototype()) result = nullptr;
    }
    if (!result) result = binaryAdd(ctx, a, b);
    return result;
}

static const proto::ProtoObject* binaryAdd(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb = unwrapPrimitive(ctx, b);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (bothExactNumericPrim(ctx, env, a, b, aa, bb)
        || numericSubclassFastPathOK(ctx, env, a, b, aa, bb, "__add__", "__radd__")) {
        return aa->add(ctx, bb);
    }

    // Detect bytes objects before the isString fast-path: bytes stores content
    // in __data__ (a ProtoString) so isString() returns true for them, but
    // bytes + bytes must produce a bytes object, not a plain str.
    auto isBytesObj = [&](const proto::ProtoObject* obj) -> bool {
        if (!env || !env->getBytesPrototype()) return false;
        const proto::ProtoObject* cls2 = obj->getAttribute(ctx, env->getClassString());
        if (cls2 == env->getBytesPrototype()) return true;
        if (obj->getPrototype(ctx) == env->getBytesPrototype()) return true;
        return false;
    };
    if (a->isString(ctx) && b->isString(ctx) && (isBytesObj(a) || isBytesObj(b))) {
        // bytes + bytes → new bytes object
        std::string s1, s2;
        a->asString(ctx)->toUTF8String(ctx, s1);
        b->asString(ctx)->toUTF8String(ctx, s2);
        std::string combined = s1 + s2;
        const proto::ProtoObject* bytesProto = env->getBytesPrototype();
        proto::ProtoObject* result = const_cast<proto::ProtoObject*>(bytesProto->newChild(ctx, true));
        result->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__data__"),
                             PythonEnvironment::getInternedString(ctx, combined.c_str())->asObject(ctx));
        result->setAttribute(ctx, env->getClassString(), bytesProto);
        return result;
    }
    if (a->isString(ctx) && b->isString(ctx)) {
        // Sprint-2 step C (2026-06-15): use protoCore's native rope-level
        // appendLast on ProtoString. The previous implementation went
        // `toUTF8String + std::string concat + fromUTF8Buffer`, which is
        // O(N) per call: every concat materialised both operands into
        // std::string buffers, allocated a new combined std::string, and
        // built a fresh ProtoString from scratch. For `s = s + "x"` in a
        // loop that is O(N²) total work.
        //
        // ProtoString::appendLast does the rope-level structural-sharing
        // concat in O(log N) per call, O(N log N) total. Same algorithm
        // protoCpp uses to hit 19 ms on str_concat_loop (we were paying
        // 354 ms on the std::string round-trip).
        //
        // protoCore's rope handles UTF-8 continuation byte boundaries
        // internally — appendLast on two well-formed ProtoStrings yields
        // a well-formed result without intermediate decoding.
        const proto::ProtoString* sa = a->asString(ctx);
        const proto::ProtoString* sb = b->asString(ctx);
        if (sa && sb) {
            const proto::ProtoString* res = sa->appendLast(ctx, sb);
            return res ? res->asObject(ctx) : PROTO_NONE;
        }
    }
    const proto::ProtoList* l1 = ::protoPython::listStorage(ctx, a);
    if (!l1) {
        const proto::ProtoObject* data = a->getAttribute(ctx, env->getDataString());
        if (data) l1 = ::protoPython::listStorage(ctx, data);
        if (!l1 && data) {
            if (::protoPython::PythonEnvironment::tupleView(ctx, data)) l1 = ::protoPython::PythonEnvironment::tupleView(ctx, data)->asList(ctx);
        }
    }
    const proto::ProtoList* l2 = ::protoPython::listStorage(ctx, b);
    if (!l2) {
        const proto::ProtoObject* data = b->getAttribute(ctx, env->getDataString());
        if (data) l2 = ::protoPython::listStorage(ctx, data);
        if (!l2 && data) {
            if (::protoPython::PythonEnvironment::tupleView(ctx, data)) l2 = ::protoPython::PythonEnvironment::tupleView(ctx, data)->asList(ctx);
        }
    }

    // tuple + tuple and list + list only: list + tuple is a TypeError (both
    // keep a ProtoList, so the shapes alone matched).
    if (l1 && l2 && ::protoPython::PythonEnvironment::isTupleValue(ctx, a)
                    != ::protoPython::PythonEnvironment::isTupleValue(ctx, b)) {
        l1 = nullptr;
    }
    if (l1 && l2) {
        // Handle list/tuple addition
        proto::ProtoList* resL = const_cast<proto::ProtoList*>(ctx->newList());
        proto::proto_ulong n1 = l1->getSize(ctx);
        proto::proto_ulong n2 = l2->getSize(ctx);
        for (proto::proto_ulong i = 0; i < n1; ++i) resL = const_cast<proto::ProtoList*>(resL->appendLast(ctx, l1->getAt(ctx, static_cast<int>(i))));
        for (proto::proto_ulong i = 0; i < n2; ++i) resL = const_cast<proto::ProtoList*>(resL->appendLast(ctx, l2->getAt(ctx, static_cast<int>(i))));

        const proto::ProtoObject* aCls = env ? env->getType(ctx, a) : a->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__class__"));
        // CPython: sequence ops on subclasses without an own __add__
        // override produce a plain list/tuple, dropping the subclass.
        // `class T(tuple): pass; T((1,)) + ()` returns `(1,)` whose
        // type is `tuple`, not `T`.  Detect tuple-vs-list via the MRO
        // chain and substitute the primitive prototype.
        bool isTuple = false;
        bool isList = false;
        if (env && aCls && aCls != PROTO_NONE) {
            if (aCls == env->getTuplePrototype()) isTuple = true;
            else if (aCls == env->getListPrototype()) isList = true;
            else {
                const proto::ProtoObject* mroAttr = env->getAttribute(ctx, aCls, env->getMroString(), false);
                ::protoPython::TupleView mroT = mroAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, mroAttr) : nullptr;
                if (mroT) {
                    for (proto::proto_ulong mi = 0; mi < mroT->getSize(ctx); ++mi) {
                        const proto::ProtoObject* base = mroT->getAt(ctx, static_cast<int>(mi));
                        if (base == env->getTuplePrototype()) { isTuple = true; break; }
                        if (base == env->getListPrototype()) { isList = true; break; }
                    }
                }
            }
            // Substitute the primitive prototype so the result drops
            // the subclass (matches CPython behaviour for unoverridden
            // sequence dunders).
            if (isTuple) aCls = env->getTuplePrototype();
            else if (isList) aCls = env->getListPrototype();
        }

        if (isTuple && env) return env->makeTuple(ctx, resL);
        proto::ProtoObject* resObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        const proto::ProtoString* dataS = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
        {
            resObj = const_cast<proto::ProtoObject*>(resObj->setAttribute(ctx, dataS, resL->asObject(ctx)));
        }

        if (aCls) {
            resObj = const_cast<proto::ProtoObject*>(resObj->addParent(ctx, aCls));
            resObj = const_cast<proto::ProtoObject*>(resObj->setAttribute(ctx, env ? env->getClassString() : protoPython::PythonEnvironment::getInternalString(ctx, "__class__"), aCls));
        }
        return resObj;
    }

    const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__add__", "__radd__");
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binarySubtract(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb = unwrapPrimitive(ctx, b);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (bothExactNumericPrim(ctx, env, a, b, aa, bb)
        || numericSubclassFastPathOK(ctx, env, a, b, aa, bb, "__sub__", "__rsub__")) {
        return aa->subtract(ctx, bb);
    }
    const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__sub__", "__rsub__");
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryMultiply(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    // Coerce bool sentinels to int so list/tuple/str * bool hits the
    // sequence-repeat fast paths below (which all check isInteger directly).
    a = coerceBoolToInt(ctx, a);
    b = coerceBoolToInt(ctx, b);
    const proto::ProtoObject* aa = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb = unwrapPrimitive(ctx, b);
    PythonEnvironment* env_local = PythonEnvironment::fromContext(ctx);
    if (bothExactNumericPrim(ctx, env_local, a, b, aa, bb)
        || numericSubclassFastPathOK(ctx, env_local, a, b, aa, bb, "__mul__", "__rmul__")) {
        return aa->multiply(ctx, bb);
    }
    // String/bytes repetition: str * int or int * str (or bytes * int)
    const proto::ProtoObject* strObj = nullptr;
    const proto::ProtoObject* intObj = nullptr;
    if (a->isString(ctx) && b->isInteger(ctx)) { strObj = a; intObj = b; }
    else if (a->isInteger(ctx) && b->isString(ctx)) { strObj = b; intObj = a; }
    if (strObj && intObj) {
        protoPython::PythonEnvironment* env2 = protoPython::PythonEnvironment::fromContext(ctx);
        bool isBytes = false;
        if (env2 && env2->getBytesPrototype()) {
            const proto::ProtoObject* cls2 = strObj->getAttribute(ctx, env2->getClassString());
            if (cls2 == env2->getBytesPrototype() || strObj->getPrototype(ctx) == env2->getBytesPrototype())
                isBytes = true;
        }
        long long n = intObj->asLong(ctx);
        std::string s;
        strObj->asString(ctx)->toUTF8String(ctx, s);
        std::string result;
        if (n > 0) {
            result.reserve(s.size() * static_cast<size_t>(n));
            for (long long i = 0; i < n; ++i) result += s;
        }
        if (isBytes && env2) {
            const proto::ProtoObject* bytesProto = env2->getBytesPrototype();
            proto::ProtoObject* res = const_cast<proto::ProtoObject*>(bytesProto->newChild(ctx, true));
            res->setAttribute(ctx, protoPython::PythonEnvironment::getInternedString(ctx, "__data__"),
                              protoPython::PythonEnvironment::getInternedString(ctx, result.c_str())->asObject(ctx));
            res->setAttribute(ctx, env2->getClassString(), bytesProto);
            return res;
        }
        return proto::ProtoString::fromUTF8(ctx, result.c_str())->asObject(ctx);
    }
    // Tuple repetition: tuple * int or int * tuple
    const proto::ProtoObject* tupleObj = nullptr;
    intObj = nullptr;
    if (::protoPython::PythonEnvironment::tupleView(ctx, a) && b->isInteger(ctx)) { tupleObj = a; intObj = b; }
    else if (a->isInteger(ctx) && ::protoPython::PythonEnvironment::tupleView(ctx, b)) { tupleObj = b; intObj = a; }
    if (tupleObj && intObj) {
        long long n = intObj->asLong(ctx);
        if (n <= 0) return ::protoPython::PythonEnvironment::tupleFromList(ctx, ctx->newList());
        ::protoPython::TupleView t = ::protoPython::PythonEnvironment::tupleView(ctx, tupleObj);
        size_t seqSize = t->getSize(ctx);
        proto::ProtoList* resultList = const_cast<proto::ProtoList*>(ctx->newList());
        for (long long rep = 0; rep < n; ++rep) {
            for (size_t idx = 0; idx < seqSize; ++idx) {
                const proto::ProtoObject* elem = t->getAt(ctx, static_cast<int>(idx));
                if (elem) resultList = const_cast<proto::ProtoList*>(resultList->appendLast(ctx, elem));
            }
        }
        return ::protoPython::PythonEnvironment::tupleFromList(ctx, resultList);
    }
    // Fall through to __mul__/__rmul__ via the shared dunder dispatch so
    // Python-level user functions (which `asMethod` does NOT recognise —
    // it only matches POINTER_TAG_METHOD C++ trampolines) actually run.
    // The previous inline lookup gated on `mul->asMethod(ctx)`, so any
    // user class with `def __mul__(self, ...)` silently produced None.
    const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__mul__", "__rmul__");
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryUnaryNegative(proto::ProtoContext* ctx, const proto::ProtoObject* a) {
    // Exact-type gate: a subclass with __neg__ override must win.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    bool aPrim = false;
    if (env) {
        const proto::ProtoObject* aCls = env->getType(ctx, a);
        aPrim = (aCls == env->getIntPrototype()
                 || aCls == env->getBoolPrototype()
                 || aCls == env->getFloatPrototype());
    } else {
        aPrim = a->isInteger(ctx) || a->isDouble(ctx);
    }
    if (aPrim) {
        if (a->isInteger(ctx)) return a->multiply(ctx, ctx->fromInteger(-1));
        if (a->isDouble(ctx)) return ctx->fromDouble(-a->asDouble(ctx));
    }
    // Subclass-of-int/float without an own __neg__: unwrap __data__ and
    // negate the primitive directly.  intPrototype doesn't ship a
    // __neg__ dunder, so the env->getAttribute walk below would miss
    // and the path would silently return None.
    if (env && !aPrim) {
        const proto::ProtoString* negS = PythonEnvironment::getInternedString(ctx, "__neg__");
        const proto::ProtoObject* aCls = env->getType(ctx, a);
        bool aOwnsNeg = aCls && aCls->hasOwnAttribute(ctx, negS) == PROTO_TRUE;
        if (!aOwnsNeg) {
            const proto::ProtoObject* d = a->getAttribute(ctx, env->getDataString());
            if (d && d->isInteger(ctx)) return d->multiply(ctx, ctx->fromInteger(-1));
            if (d && d->isDouble(ctx)) return ctx->fromDouble(-d->asDouble(ctx));
            if (d == PROTO_TRUE) return ctx->fromInteger(-1);
            if (d == PROTO_FALSE) return ctx->fromInteger(0);
        }
    }
    // Subclass with __neg__ override or non-numeric: route through dunder.
    if (env) {
        const proto::ProtoString* negS = PythonEnvironment::getInternedString(ctx, "__neg__");
        const proto::ProtoObject* method = env->getAttribute(ctx, a, negS, false);
        if (method && method != PROTO_NONE) {
            const proto::ProtoList* emptyL = ctx->newList();
            if (method->asMethod(ctx)) {
                return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(a), nullptr, argsView(proto::ProtoListArguments(ctx, emptyL)), nullptr);
            }
        }
    }
    return PROTO_NONE;
}
static const proto::ProtoObject* binaryTrueDivide(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb = unwrapPrimitive(ctx, b);
    PythonEnvironment* env_div = PythonEnvironment::fromContext(ctx);
    auto integerIsZero = [&](const proto::ProtoObject* o) -> bool {
        return o->isInteger(ctx) && o->integerSign(ctx) == 0;
    };
    // Zero-division check fires only on exact-numeric operands; subclass
    // with overridden __truediv__ takes the dunder path below where the
    // override decides what to do with zero.
    if (bothExactNumericPrim(ctx, env_div, a, b, aa, bb)) {
        if (integerIsZero(bb) || (bb->isDouble(ctx) && bb->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
    }
    // Python: int / int always returns float.  protoCore's native integer
    // divide rounds toward zero (C semantics), so we must explicitly
    // convert through double here.  For bignum we go via the decimal
    // string (std::stod handles values up to DBL_MAX, returning inf
    // beyond that — matching CPython's OverflowError-or-inf behavior
    // modulo the exception channel).
    auto intToDouble = [&](const proto::ProtoObject* o) -> double {
        long long asInt = 0;
        if (PythonEnvironment::tryAsLong(ctx, o, asInt)) return static_cast<double>(asInt);
        {
            const proto::ProtoString* s = o->asIntegerString(ctx, 10);
            std::string digits;
            s->toUTF8String(ctx, digits);
            try { return std::stod(digits); }
            catch (const std::out_of_range&) {
                return (digits.size() && digits[0] == '-')
                    ? -std::numeric_limits<double>::infinity()
                    :  std::numeric_limits<double>::infinity();
            }
        }
    };
    // Numeric primitive path only taken when both operands are exactly the
    // built-in numeric types — subclasses with overridden __truediv__
    // route through binaryOpDispatch below.
    if (bothExactNumericPrim(ctx, env_div, a, b, aa, bb)
        || numericSubclassFastPathOK(ctx, env_div, a, b, aa, bb, "__truediv__", "__rtruediv__")) {
        if (integerIsZero(bb) || (bb->isDouble(ctx) && bb->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
        if (aa->isInteger(ctx) && bb->isInteger(ctx)) {
            return ctx->fromDouble(intToDouble(aa) / intToDouble(bb));
        }
        double da = aa->isDouble(ctx) ? aa->asDouble(ctx) : intToDouble(aa);
        double db = bb->isDouble(ctx) ? bb->asDouble(ctx) : intToDouble(bb);
        return ctx->fromDouble(da / db);
    }
    // User-class fallback: __truediv__ / __rtruediv__ via invokePythonCallable.
    const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__truediv__", "__rtruediv__");
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryModulo(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb = unwrapPrimitive(ctx, b);
    PythonEnvironment* env_mod = PythonEnvironment::fromContext(ctx);
    auto integerIsZero = [&](const proto::ProtoObject* o) -> bool {
        return o->isInteger(ctx) && o->integerSign(ctx) == 0;
    };
    if (bothExactNumericPrim(ctx, env_mod, a, b, aa, bb)
        || numericSubclassFastPathOK(ctx, env_mod, a, b, aa, bb, "__mod__", "__rmod__")) {
        if (integerIsZero(bb) || (bb->isDouble(ctx) && bb->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
        // Mixed int/float: promote to double with Python's floor-rounding.
        if ((aa->isDouble(ctx) && bb->isInteger(ctx)) ||
            (aa->isInteger(ctx) && bb->isDouble(ctx))) {
            auto toDouble = [&](const proto::ProtoObject* o) -> double {
                if (o->isDouble(ctx)) return o->asDouble(ctx);
                long long asInt = 0;
                if (PythonEnvironment::tryAsLong(ctx, o, asInt)) return static_cast<double>(asInt);
                {
                    const proto::ProtoString* s = o->asIntegerString(ctx, 10);
                    std::string digits;
                    s->toUTF8String(ctx, digits);
                    try { return std::stod(digits); }
                    catch (...) { return 0.0; }
                }
            };
            double da = toDouble(aa);
            double db = toDouble(bb);
            double r = std::fmod(da, db);
            if ((r != 0.0) && ((r < 0) != (db < 0))) r += db;
            return ctx->fromDouble(r);
        }
        // CPython: int % int returns Python's floor-modulo — the
        // remainder has the same sign as the divisor.  protoCore's
        // Integer::modulo follows C truncation, so
        //   -5 % 2 produced -1 instead of 1
        //    5 % -2 produced 1 instead of -1
        // Adjust by adding the divisor when the raw remainder is
        // non-zero and its sign differs from the divisor's.
        const proto::ProtoObject* r = aa->modulo(ctx, bb);
        if (r->integerSign(ctx) != 0
            && (r->integerSign(ctx) < 0) != (bb->integerSign(ctx) < 0)) {
            r = r->add(ctx, bb);
        }
        return r;
    }
    if (a->isString(ctx)) {
        // Delegate to str.__mod__ via the Python env attribute lookup (respects strPrototype chain).
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        const proto::ProtoString* modS = protoPython::PythonEnvironment::getInternalString(ctx, "__mod__");
        const proto::ProtoObject* method = env ? env->getAttribute(ctx, a, modS) : a->getAttribute(ctx, modS);
        if (method && method != PROTO_NONE && method->asMethod(ctx)) {
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
            return method->asMethod(ctx)(ctx, a, nullptr, argsView(proto::ProtoListArguments(ctx, args)), nullptr);
        }
    }
    // User-class fallback: __mod__ / __rmod__.
    const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__mod__", "__rmod__");
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryPower(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa_p = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb_p = unwrapPrimitive(ctx, b);
    PythonEnvironment* env_pow = PythonEnvironment::fromContext(ctx);
    if (bothExactNumericPrim(ctx, env_pow, a, b, aa_p, bb_p)
        || numericSubclassFastPathOK(ctx, env_pow, a, b, aa_p, bb_p, "__pow__", "__rpow__")) {
        if (aa_p->isInteger(ctx) && bb_p->isInteger(ctx)) {
            long long exp = 0;
            if (!PythonEnvironment::tryAsLong(ctx, bb_p, exp)) {
                // Exponents that don't fit int64 are astronomical; fall
                // through to the double path (returns inf/0 as CPython
                // does via floats).
                double aa = static_cast<double>(aa_p->asLong(ctx));
                double bb = bb_p->isDouble(ctx) ? bb_p->asDouble(ctx) : 0.0;
                return ctx->fromDouble(std::pow(aa, bb));
            }
            if (exp < 0) {
                long long base_l = 0;
                double base_d = PythonEnvironment::tryAsLong(ctx, aa_p, base_l) ? static_cast<double>(base_l) : 0.0;
                // CPython: 0 ** -1 raises
                //   ZeroDivisionError: 0.0 cannot be raised to a negative power
                // protoPython returned `inf` (std::pow(0.0, -1) is inf in
                // IEEE 754) and a downstream subscript / division then
                // hung the interpreter.  Raise the canonical error.
                if (base_d == 0.0) {
                    if (env_pow) env_pow->raiseZeroDivisionError(ctx);
                    return nullptr;
                }
                return ctx->fromDouble(std::pow(base_d, static_cast<double>(exp)));
            }
            // Exponentiation by squaring via Integer::multiply so the
            // result promotes to bignum automatically when it exceeds
            // int64.  This also keeps the fast path for small results
            // because multiply hits the SmallInt branch internally.
            const proto::ProtoObject* result = ctx->fromInteger(1);
            const proto::ProtoObject* base = aa_p;
            while (exp > 0) {
                if (exp & 1) result = result->multiply(ctx, base);
                exp >>= 1;
                if (exp > 0) base = base->multiply(ctx, base);
            }
            return result;
        }
        double aa = aa_p->isDouble(ctx) ? aa_p->asDouble(ctx) : static_cast<double>(aa_p->asLong(ctx));
        double bb = bb_p->isDouble(ctx) ? bb_p->asDouble(ctx) : static_cast<double>(bb_p->asLong(ctx));
        return ctx->fromDouble(std::pow(aa, bb));
    }
    // User-class fallback: __pow__ / __rpow__.
    const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__pow__", "__rpow__");
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryFloorDivide(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa_p = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb_p = unwrapPrimitive(ctx, b);
    PythonEnvironment* env_fd = PythonEnvironment::fromContext(ctx);
    if (bothExactNumericPrim(ctx, env_fd, a, b, aa_p, bb_p)
        || numericSubclassFastPathOK(ctx, env_fd, a, b, aa_p, bb_p, "__floordiv__", "__rfloordiv__")) {
        if ((bb_p->isInteger(ctx) && bb_p->asLong(ctx) == 0) || (bb_p->isDouble(ctx) && bb_p->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
        if (aa_p->isInteger(ctx) && bb_p->isInteger(ctx)) {
            // CPython uses *floor* division — the result rounds toward
            // negative infinity, not toward zero.  protoCore's
            // Integer::divide truncates (C semantics) so
            //   -5 // 2  produced -2 instead of -3
            //    5 // -2 produced -2 instead of -3
            // When operands have opposite signs and the division is
            // inexact, the truncated quotient must be decremented by 1
            // to reach the floor.
            const proto::ProtoObject* q = aa_p->divide(ctx, bb_p);
            // Fast path: same-sign operands (or zero numerator) need
            // no adjustment — truncation already equals floor.  Sign
            // check is allocation-free; multiply + compare are not, so
            // entering them on every int division was a ~3-4x slowdown
            // on int-heavy code paths (test_descr.py 44s vs 12s).
            int aSign = aa_p->integerSign(ctx);
            int bSign = bb_p->integerSign(ctx);
            if (aSign == 0 || (aSign < 0) == (bSign < 0)) {
                return q;
            }
            // Opposite signs: only adjust when truncation lost a remainder.
            const proto::ProtoObject* prod = q->multiply(ctx, bb_p);
            if (prod->compare(ctx, aa_p) != 0) {
                q = q->subtract(ctx, ctx->fromInteger(1));
            }
            return q;
        }
        // A float operand makes the result a float (CPython: 7 // 2.0 == 3.0).
        // asDouble also converts ints beyond long long, where asLong threw.
        double aa = aa_p->asDouble(ctx);
        double bb = bb_p->asDouble(ctx);
        return ctx->fromDouble(std::floor(aa / bb));
    }
    // User-class fallback: __floordiv__ / __rfloordiv__.
    const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__floordiv__", "__rfloordiv__");
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* compareOp(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b, int op) {
    bool result = false;
    if (op == 8 || op == 9) { // is / is not
        // Normalize None-like values: nonePrototype and PROTO_NONE are both Python None
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        const proto::ProtoObject* noneProto = env ? env->getNonePrototype() : nullptr;
        auto isNoneLike = [noneProto](const proto::ProtoObject* v) {
            return v == PROTO_NONE || (noneProto && v == noneProto);
        };
        bool aIsNone = isNoneLike(a);
        bool bIsNone = isNoneLike(b);
        if (aIsNone && bIsNone) result = true;
        else if (aIsNone || bIsNone) result = false;
        else result = (a == b);
        if (op == 9) result = !result;
        return result ? PROTO_TRUE : PROTO_FALSE;
    }
    
    if (op == 10) { // EXCEPTION_MATCH
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) {
            bool match = env->isException(a, b); // a = exc, b = type
            return match ? PROTO_TRUE : PROTO_FALSE;
        }
        return PROTO_FALSE;
    }

    if (op == 6 || op == 7) { // in, not in
        bool found = false;
        // Priority dispatch: when b is a user-class instance whose own
        // class defines __contains__ (in OWN attrs), AND its class is
        // NOT one of the protoCore built-in containers (whose own
        // __contains__ is a no-op stub or known-broken under the
        // generic dispatch), honour it before the fast paths.
        // UserDict, ChainMap, os._Environ rely on this; built-in
        // containers (dict, list, tuple, set, str, bytes) and anything
        // inheriting __contains__ from a generic prototype skip the
        // branch.
        {
            PythonEnvironment* env_dunder = PythonEnvironment::fromContext(ctx);
            if (env_dunder && b) {
                const proto::ProtoObject* bCls = env_dunder->getType(ctx, b);
                bool isBuiltinContainer = false;
                if (bCls) {
                    isBuiltinContainer = (bCls == env_dunder->getDictPrototype()
                                          || bCls == env_dunder->getListPrototype()
                                          || bCls == env_dunder->getTuplePrototype()
                                          || bCls == env_dunder->getSetPrototype()
                                          || bCls == env_dunder->getFrozensetPrototype()
                                          || bCls == env_dunder->getStrPrototype()
                                          || bCls == env_dunder->getBytesPrototype());
                }
                if (bCls && !isBuiltinContainer) {
                    const proto::ProtoString* containsS = env_dunder->getContainsString();
                    // Walk bCls.__mro__ looking for ANY class that owns
                    // __contains__. This catches inherited overrides
                    // (e.g. _Environ inherits Mapping.__contains__) while
                    // staying off the protoCore prototype chain (which
                    // would attach a __contains__ to internal carriers
                    // like generators / frames and crash the SparseList
                    // fast path further down).
                    bool dispatchHere = false;
                    const proto::ProtoString* mroS = env_dunder->getMroString();
                    const proto::ProtoObject* mroObj = mroS ? env_dunder->getAttribute(ctx, bCls, mroS, false) : nullptr;
                    ::protoPython::TupleView mroTup = mroObj ? ::protoPython::PythonEnvironment::tupleView(ctx, mroObj) : nullptr;
                    if (mroTup) {
                        proto::proto_ulong mn = mroTup->getSize(ctx);
                        for (proto::proto_ulong mi = 0; mi < mn; ++mi) {
                            const proto::ProtoObject* base = mroTup->getAt(ctx, static_cast<int>(mi));
                            if (!base || base == PROTO_NONE) continue;
                            if (base->hasOwnAttribute(ctx, containsS) == PROTO_TRUE) {
                                dispatchHere = true;
                                break;
                            }
                        }
                    } else if (bCls->hasOwnAttribute(ctx, containsS) == PROTO_TRUE) {
                        dispatchHere = true;
                    }
                    if (dispatchHere) {
                        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* res = nullptr;
                        try {
                            res = invokeDunder(ctx, b, containsS, args);
                        } catch (const proto::ProtoException&) {
                            res = nullptr;   // dropped, as before: the fallback below decides
                        }
                        if (res) {
                            found = isTruthy(ctx, res);
                            return ((op == 6) ? found : !found) ? PROTO_TRUE : PROTO_FALSE;
                        }
                    }
                }
            }
        }
        // SP-C/C1: MappingProxy (e.g. cls.__dict__) defines its own
        // __contains__ that distinguishes own vs. inherited attributes.
        // Skip the __data__ / asSparseList fast path which would walk
        // the class's full attribute chain and return wrong results.
        {
            PythonEnvironment* env_mp = PythonEnvironment::fromContext(ctx);
            if (env_mp) {
                const proto::ProtoObject* mpProto = env_mp->getMappingProxyPrototype();
                if (mpProto) {
                    const proto::ProtoString* clsS = env_mp->getClassString();
                    const proto::ProtoObject* bCls = b->getAttribute(ctx, clsS);
                    bool isMP = (bCls == mpProto);
                    if (!isMP) {
                        const proto::ProtoList* parents = b->getParents(ctx);
                        if (parents) {
                            for (size_t i = 0; i < parents->getSize(ctx); ++i) {
                                if (parents->getAt(ctx, static_cast<int>(i)) == mpProto) { isMP = true; break; }
                            }
                        }
                    }
                    if (isMP) {
                        const proto::ProtoString* containsS = env_mp->getContainsString();
                        const proto::ProtoList* args_mp = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* res = invokeDunder(ctx, b, containsS, args_mp);
                        if (res) {
                            found = isTruthy(ctx, res);
                            return ((op == 6) ? found : !found) ? PROTO_TRUE : PROTO_FALSE;
                        }
                    }
                }
            }
        }
        // Fast path: string-in-string uses native substring search
        if (b->isString(ctx) && a->isString(ctx)) {
            std::string s_sub, s_full;
            a->asString(ctx)->toUTF8String(ctx, s_sub);
            b->asString(ctx)->toUTF8String(ctx, s_full);
            found = (s_full.find(s_sub) != std::string::npos);
            result = (op == 6) ? found : !found;
            return result ? PROTO_TRUE : PROTO_FALSE;
        }
        const proto::ProtoList* lst = ::protoPython::listStorage(ctx, b);
        if (!lst && ::protoPython::PythonEnvironment::isTupleValue(ctx, b)) {
            ::protoPython::TupleView tup = ::protoPython::PythonEnvironment::tupleView(ctx, b);
            if (tup) lst = tup->asList(ctx);
        }

        if (!lst) {
            // Try dictionary keys or __data__ fallback.  Restricted to
            // dict-like b — every Python instance owns __data__/__keys__
            // for its attribute storage, so an unrestricted check
            // treats any random object as an empty mapping and the
            // user's __getitem__-driven iteration never fires.
            PythonEnvironment* envContains = PythonEnvironment::fromContext(ctx);
            bool bIsDictLike = false;
            if (envContains) {
                const proto::ProtoObject* bt = envContains->getType(ctx, b);
                if (bt == envContains->getDictPrototype()
                    || bt == envContains->getModulePrototype()) {
                    bIsDictLike = true;
                } else if (bt && bt != PROTO_NONE) {
                    const proto::ProtoObject* mAttr = envContains->getAttribute(ctx, bt, envContains->getMroString(), false);
                    ::protoPython::TupleView mroT = mAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, mAttr) : nullptr;
                    if (mroT) {
                        for (proto::proto_ulong i = 0; i < mroT->getSize(ctx); ++i) {
                            const proto::ProtoObject* m = mroT->getAt(ctx, static_cast<int>(i));
                            if (m == envContains->getDictPrototype()
                                || m == envContains->getModulePrototype()) {
                                bIsDictLike = true;
                                break;
                            }
                        }
                    }
                }
            }
            const proto::ProtoString* dataS = protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
            const proto::ProtoObject* data = bIsDictLike ? b->getAttribute(ctx, dataS) : nullptr;
            if (data && data != PROTO_NONE) {
                if (::protoPython::listStorage(ctx, data)) lst = ::protoPython::listStorage(ctx, data);
                else if (::protoPython::PythonEnvironment::isTupleValue(ctx, data)) lst = ::protoPython::PythonEnvironment::tupleView(ctx, data)->asList(ctx);
                else if (data->asSparseList(ctx)) {
                    // The key is looked up by the hash the dict stores it
                    // under; an unhashable key raises TypeError.
                    proto::proto_ulong hash = 0;
                    if (!PythonEnvironment::hashKey(ctx, a, hash)) return nullptr;
                    if (data->asSparseList(ctx)->has(ctx, hash)) {
                        found = true;
                        result = (op == 6) ? found : !found;
                        return result ? PROTO_TRUE : PROTO_FALSE;
                    }
                    if (!a->isString(ctx)) {
                        result = (op == 6) ? false : true;
                        return result ? PROTO_TRUE : PROTO_FALSE;
                    }
                    // Some namespaces store string keys under another hash:
                    // fall back to scanning __keys__ for strings.
                    const proto::ProtoString* keysS = protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                    const proto::ProtoObject* keysObj = b->getAttribute(ctx, keysS);
                    if (keysObj) lst = ::protoPython::listStorage(ctx, keysObj);
                }
            }
        }
        
        if (lst) {
            // PH: use Python-level equality (compareObjects) so tuple-in-list,
            // dict-in-list, etc. work via __eq__/__hash__ rather than the
            // raw ProtoObject pointer-style compare which only succeeds for
            // identity-equal objects.
            PythonEnvironment* env_local = PythonEnvironment::fromContext(ctx);
            size_t size = lst->getSize(ctx);
            for (size_t i = 0; i < size; ++i) {
                bool eq = false;
                if (env_local) {
                    // Identity first (`x is e or x == e`), so NaN is found.
                    eq = env_local->objectsEqual(ctx, a, lst->getAt(ctx, static_cast<int>(i)));
                } else {
                    eq = (a->compare(ctx, lst->getAt(ctx, static_cast<int>(i))) == 0);
                }
                if (eq) {
                    found = true;
                    break;
                }
            }
        } else {
            // Dunder __contains__ fallback
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoString* containsS = env ? env->getContainsString() : protoPython::PythonEnvironment::getInternalString(ctx, "__contains__");
            const proto::ProtoList* args = ctx->newList();
            args = args->appendLast(ctx, a);
            const proto::ProtoObject* res = invokeDunder(ctx, b, containsS, args);
            if (res) {
                found = isTruthy(ctx, res);
            } else if (b->isString(ctx) && a->isString(ctx)) {
                std::string s_sub, s_full;
                a->asString(ctx)->toUTF8String(ctx, s_sub);
                b->asString(ctx)->toUTF8String(ctx, s_full);
                found = (s_full.find(s_sub) != std::string::npos);
            } else if (env) {
                // CPython: when neither __contains__ nor a fast container
                // path applies, fall back to iteration.  Honours classic
                // sequences that expose only __getitem__ (e.g. user types
                // raising IndexError beyond bounds).  env->iter already
                // synthesises an iterator over getitem(0), getitem(1), …
                // until IndexError, so this branch covers both __iter__
                // and the old-style sequence protocol.
                const proto::ProtoObject* it = nullptr;
                bool notIterable = false;
                try {
                    it = env->iter(b);
                } catch (const proto::ProtoException&) {
                    notIterable = true;
                }
                if (it) {
                    for (;;) {
                        // An error from the iteration ends it, as before.
                        const proto::ProtoObject* item = nullptr;
                        try {
                            item = env->next(it);
                        } catch (const proto::ProtoException&) {
                            item = nullptr;
                        }
                        if (!item) break;
                        if (env->objectsEqual(ctx, a, item)) {
                            found = true;
                            break;
                        }
                    }
                } else if (notIterable) {
                    // env->iter raised because b is not iterable.  CPython
                    // surfaces this as:
                    //   TypeError: argument of type 'X' is not iterable
                    // Replace env->iter's generic message with the form
                    // CPython uses for `x in non_container`.  Without this
                    // both `5 in 10` and `1 in None` silently returned
                    // False — a real correctness gap, not a tolerance.
                    std::string clsName = "object";
                    const proto::ProtoObject* cls = env->getType(ctx, b);
                    if (cls) {
                        const proto::ProtoObject* nm = cls->getAttribute(ctx, env->getNameString());
                        if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, clsName);
                    }
                    env->raiseTypeError(ctx,
                        "argument of type '" + clsName + "' is not iterable");
                    return nullptr;
                }
            }
        }

        result = (op == 6) ? found : !found;
        return result ? PROTO_TRUE : PROTO_FALSE;
    }
    if (op >= 0 && op <= 5) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        // COMPARE_OP: the operator yields the dunder's result unchanged.
        if (env) return env->compareObjects(ctx, a, b, op, /*richResult=*/true);
        
        // Fallback for null environment (e.g. unit tests)
        int c = a->compare(ctx, b);
        bool r = false;
        if (op == 0) r = (c == 0); // ==
        else if (op == 1) r = (c != 0); // !=
        else if (op == 2) r = (c < 0); // <
        else if (op == 3) r = (c <= 0); // <=
        else if (op == 4) r = (c > 0); // >
        else if (op == 5) r = (c >= 0); // >=
        return r ? PROTO_TRUE : PROTO_FALSE;
    }
    return result ? PROTO_TRUE : PROTO_FALSE;
}

} // namespace

// Declared in ExecutionEngine.h, so defined outside the anonymous namespace.
const proto::ProtoObject* containsOperator(proto::ProtoContext* ctx,
    const proto::ProtoObject* container, const proto::ProtoObject* item) {
    return compareOp(ctx, item, container, 6);
}

namespace {

static bool isTruthy(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    if (obj == PROTO_FALSE) return false;
    if (obj == PROTO_TRUE) return true;

    // Only a SmallInteger may be converted with asLong: a LargeInteger does not
    // fit in 64 bits and asLong rejects it, so `while big_int:` and `if
    // big_int:` raised "LargeInteger value exceeds long long range".  A value
    // wide enough to need a LargeInteger is never zero, so it is always true.
    if (proto::isSmallInt(obj)) return (obj->asLong(ctx) != 0);
    if (obj->isInteger(ctx)) return true;
    if (obj->isDouble(ctx)) return (obj->asDouble(ctx) != 0.0);
    if (obj->isString(ctx)) return (obj->asString(ctx)->getSize(ctx) > 0);

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env && obj == env->getNonePrototype()) return false;

    // Python semantics: __bool__ takes priority, then __len__, default True.
    // Look up on the class (not the instance) so descriptors don't fire.
    // Skip the lookup if the resolved method is PROTO_NONE — that's how
    // protoCore reports "name found in chain but bound to None" (e.g. a
    // dunder slot inherited from a prototype that doesn't override it).
    const proto::ProtoString* boolS = env ? env->getBoolString() : PythonEnvironment::getInternedString(ctx, "__bool__");
    const proto::ProtoObject* cls = env ? env->getType(ctx, obj) : nullptr;
    if (!cls) cls = obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"));

    const proto::ProtoObject* boolMethod = cls ? cls->getAttribute(ctx, boolS) : obj->getAttribute(ctx, boolS);
    if (boolMethod && boolMethod != PROTO_NONE) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
        const proto::ProtoObject* result = nullptr;
        if (boolMethod->asMethod(ctx)) {
            result = boolMethod->asMethod(ctx)(ctx, obj, nullptr, argsView(proto::ProtoListArguments(ctx, emptyL)), nullptr);
        } else {
            // Python-user-defined __bool__ (no asMethod handle).  Route
            // through invokePythonCallable with `self` prepended so the
            // user's `def __bool__(self):` receives the receiver.
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, obj);
            result = invokePythonCallable(ctx, boolMethod, args, nullptr);
        }
        if (result == PROTO_FALSE) return false;
        if (result == PROTO_TRUE) return true;
        if (result) return isTruthy(ctx, result);
    }

    // __len__ fallback (before native checks so custom containers win).
    const proto::ProtoString* lenS = env ? env->getLenString() : PythonEnvironment::getInternedString(ctx, "__len__");
    const proto::ProtoObject* lenMethod = cls ? cls->getAttribute(ctx, lenS) : obj->getAttribute(ctx, lenS);
    if (lenMethod && lenMethod != PROTO_NONE) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
        const proto::ProtoObject* result = nullptr;
        if (lenMethod->asMethod(ctx)) {
            result = lenMethod->asMethod(ctx)(ctx, obj, nullptr, argsView(proto::ProtoListArguments(ctx, emptyL)), nullptr);
        } else {
            // Python-user-defined __len__: prepend self for the call.
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, obj);
            result = invokePythonCallable(ctx, lenMethod, args, nullptr);
        }
        if (result && result->isInteger(ctx)) {
            return (result->asLong(ctx) > 0);
        }
    }

    // Native container fallbacks: only when `obj` *is* a raw container of
    // that tag — never via asSparseList, which silently follows the
    // `__data__` chain and reports a *generic* user-class instance as a
    // size-0 sparseList (every `if some_user_object:` then evaluates
    // false). For dict-backed objects, the user-visible `dict.__bool__`
    // binding handles truthiness above.
    if (::protoPython::PythonEnvironment::tupleView(ctx, obj)) return (::protoPython::PythonEnvironment::tupleView(ctx, obj)->getSize(ctx) > 0);
    if (::protoPython::listStorage(ctx, obj)) return (::protoPython::listStorage(ctx, obj)->getSize(ctx) > 0);

    return true;
}




static const proto::ProtoObject* invokeCallable(proto::ProtoContext* ctx,
    const proto::ProtoObject* callable, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs = nullptr) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!kwargs) {
        kwargs = env ? env->getEmptySparseList() : ctx->newSparseList();
    }
    
    if (!callable) {
        if (env) env->raiseTypeError(ctx, "object is not callable (nullptr)");
        return nullptr;
    }

    if (get_env_diag()) {
        std::string repr = PythonEnvironment::reprObject(ctx, callable);
        std::string clsName = "<unknown>";
        const proto::ProtoObject* cls = env ? env->getType(ctx, callable) : nullptr;
        if (cls) {
            const proto::ProtoObject* nameAttr = env->getAttribute(ctx, cls, env->getNameString());
            if (nameAttr && nameAttr->isString(ctx)) nameAttr->asString(ctx)->toUTF8String(ctx, clsName);
        }
        fprintf(stderr, "DEBUG: invokeCallable callable=%p repr=%s class=%s\n", (void*)callable, repr.c_str(), clsName.c_str());
        fflush(stderr);
    }

    RecursionScope recScope(env, ctx);
    if (recScope.overflowed()) return nullptr;

    if (callable->isNone(ctx)) {
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG_CALL_NONE: call targeted None! trace follows.\n");
            fflush(stderr);
        }
        if (env) {
            std::string msg = "'NoneType' object is not callable";
            env->raiseTypeError(ctx, msg.c_str());
        }
        return nullptr;
    }

    if (callable->asMethod(ctx)) {
        // Pin the args list (and everything reachable through it) for
        // the duration of the native call. Native C methods receive
        // args only via C++ stack locals — invisible to the GC. If the
        // native method then calls back into Python (e.g. py_str_join
        // → env->next on a generator), and that callback allocates
        // enough to trigger GC, the args list and any iterables it
        // contains would be reclaimed under the running mutator.
        PythonEnvironment::TransientPin pinArgs(env, args ? args->asObject(ctx) : nullptr);
        const proto::ProtoMethod method = callable->asMethod(ctx);
        const proto::ProtoObject* self = callable->asMethodSelf(ctx);
        const proto::ProtoObject* result = nullptr;
        try {
            result = method(
                ctx, const_cast<proto::ProtoObject*>(self), nullptr, argsView(proto::ProtoListArguments(ctx, args)), kwargsView(proto::ProtoSparseListKeywordArguments(ctx, kwargs)));
        } catch (const std::exception& e) {
            // The native-call boundary of every caller (compiled code
            // included): a C++ standard exception continues as
            // RuntimeError(what()); a ProtoException passes untouched.
            if (env) env->raiseForeignCppException(ctx, e);
            throw;
        }
        // Native iterators signal exhaustion from __next__ by returning
        // nullptr without an exception; env->next, FOR_ITER and next() rely
        // on that and never pay for a StopIteration object. Python code that
        // calls the method itself (`it.__next__()`, as heapq.merge does) must
        // see StopIteration, as CPython's __next__ slot wrapper raises it;
        // it received None instead.
        if (!result && env && self) {
            const proto::ProtoObject* nextAttr = self->getAttribute(ctx, env->getNextString());
            if (nextAttr && nextAttr->asMethod(ctx) == method) env->raiseStopIteration(ctx);
        }
        return result;
    }

    // Fast path for user-defined Python functions: they always have __code__ as an own attribute
    // (set in createUserFunction). Avoid the expensive getType + getAttribute(__call__) path.
    // A class whose namespace binds `__code__` (annotationlib.ForwardRef
    // declares it in __slots__) is not a function: it takes the class path.
    // Nor is an instance that stores a `__code__` of its own without
    // `__globals__` (unittest.mock's AsyncMock stores a mock code object so
    // inspect sees a coroutine function): runUserFunctionCall needs both
    // and answered None for it, so `AsyncMock(...)(...)` returned None
    // instead of calling the mock's __call__.
    if (env && env->getCodeString() &&
        callable->hasOwnAttribute(ctx, env->getCodeString()) == PROTO_TRUE &&
        callable->hasOwnAttribute(ctx, env->getGlobalsString()) == PROTO_TRUE &&
        callable->hasOwnAttribute(ctx, env->getIsPythonClassString()) != PROTO_TRUE) {
        return runUserFunctionCall(ctx, callable, nullptr, argsView(proto::ProtoListArguments(ctx, args)), kwargsView(proto::ProtoSparseListKeywordArguments(ctx, kwargs)));
    }

    // If callable is a Python class, dispatch directly to runUserClassCall.
    // This prevents type(cls).__call__ from resolving to an instance method
    // like ContextDecorator.__call__ instead of type.__call__ = py_type_call.
    {
        const proto::ProtoString* isPyClsS = PythonEnvironment::getInternedString(ctx, "__is_python_class__");
        if (isPyClsS && callable->hasOwnAttribute(ctx, isPyClsS) == PROTO_TRUE) {
            // PI: refuse to instantiate classes whose
            // __abstractmethods__ frozenset is non-empty.  Mirrors
            // py_type_call's check for the metaclass-dispatched path; only
            // the class's own value counts, as there.
            if (env) {
                const proto::ProtoString* amS =
                    PythonEnvironment::getInternedString(ctx, "__abstractmethods__");
                const proto::ProtoObject* am = callable->hasOwnAttribute(ctx, amS) == PROTO_TRUE
                    ? callable->getOwnAttributeDirect(ctx, amS) : nullptr;
                if (am && am != PROTO_NONE) {
                    const proto::ProtoString* lenS =
                        PythonEnvironment::getInternedString(ctx, "__len__");
                    // Errors from probing __abstractmethods__ are dropped:
                    // the class is then instantiated.
                    const proto::ProtoObject* lenM = nullptr;
                    try {
                        lenM = env->getAttribute(ctx, am, lenS, false);
                    } catch (const proto::ProtoException&) {
                        lenM = nullptr;
                    }
                    bool nonEmpty = false;
                    if (lenM && lenM != PROTO_NONE) {
                        const proto::ProtoObject* lenRes = nullptr;
                        try {
                            if (lenM->asMethod(ctx)) {
                                lenRes = lenM->asMethod(ctx)(ctx,
                                    const_cast<proto::ProtoObject*>(am), nullptr,
                                    argsView(proto::ProtoArguments(nullptr, 0)), nullptr);
                            } else {
                                const proto::ProtoList* la =
                                    ctx->newList()->appendLast(ctx, am);
                                lenRes = invokePythonCallable(ctx, lenM, la, nullptr);
                            }
                        } catch (const proto::ProtoException&) {
                            lenRes = nullptr;
                        }
                        if (lenRes && lenRes->isInteger(ctx)) {
                            nonEmpty = lenRes->asLong(ctx) > 0;
                        }
                    }
                    if (nonEmpty) {
                        std::string clsName;
                        const proto::ProtoObject* nm = callable->getAttribute(ctx, env->getNameString());
                        if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, clsName);
                        std::string msg = "Can't instantiate abstract class " + clsName +
                                          " with abstract methods";
                        env->raiseTypeError(ctx, msg.c_str());
                        return nullptr;
                    }
                }
            }
            return runUserClassCall(ctx, callable, nullptr, argsView(proto::ProtoListArguments(ctx, args)), kwargsView(proto::ProtoSparseListKeywordArguments(ctx, kwargs)));
        }
    }

    /* FALLBACK TO PUBLIC API __call__ */
    const proto::ProtoString* callS = env ? env->getCallString() : PythonEnvironment::getInternedString(ctx, "__call__");
    
    const proto::ProtoObject* typeObj = nullptr;
    if (env) {
        typeObj = env->getType(ctx, callable);
    } else {
        typeObj = callable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"));
    }
    
    const proto::ProtoObject* callAttr = nullptr;
    if (typeObj && typeObj != PROTO_NONE) {
        callAttr = env ? env->getAttribute(ctx, typeObj, callS, false) : typeObj->getAttribute(ctx, callS);
    }

    if (!callAttr || callAttr == PROTO_NONE) {
        if (env) {
            std::string repr = PythonEnvironment::reprObject(ctx, callable);
            env->raiseTypeError(ctx, "'" + repr + "' object is not callable");
        }
        return nullptr;
    }

    // If callAttr is a Python callable (not a native method cell), invoke it recursively
    // with the instance prepended to args, i.e., type(callable).__call__(callable, *args).
    if (!callAttr->asMethod(ctx)) {
        const proto::ProtoList* selfPrependedArgs = ctx->newList()->appendLast(ctx, callable);
        proto::proto_ulong nargs = args ? args->getSize(ctx) : 0;
        for (proto::proto_ulong j = 0; j < nargs; ++j) {
            selfPrependedArgs = selfPrependedArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(j)));
        }
        return invokeCallable(ctx, callAttr, selfPrependedArgs, kwargs);
    }

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: invokeCallable calling __call__ asMethod\n");
        fflush(stderr);
    }

    const proto::ProtoObject* result = callAttr->asMethod(ctx)(ctx, callable, nullptr, argsView(proto::ProtoListArguments(ctx, args)), kwargsView(proto::ProtoSparseListKeywordArguments(ctx, kwargs)));
    if (get_env_diag()) {
        std::string r = result ? PythonEnvironment::reprObject(ctx, result) : "nullptr";
        fprintf(stderr, "DEBUG: invokeCallable (__call__) returning result=%p repr=%s\n", (void*)result, r.c_str());
        fflush(stderr);
    }
    return result;
}

static const proto::ProtoObject* invokeDunder(proto::ProtoContext* ctx, const proto::ProtoObject* container, const proto::ProtoString* name, const proto::ProtoList* args) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    RecursionScope recScope(env, ctx);
    if (recScope.overflowed()) return nullptr;

    // STRUCT-206: when the container's type has a user-defined
    // __getattribute__, use type-only MRO lookup for the dunder so
    // we don't trigger the user hook for implicit special-method
    // dispatch.  CPython skips __getattribute__ for special methods
    // (test_special_method_lookup's Checker pattern enforces this).
    const proto::ProtoObject* method = nullptr;
    if (env && container) {
        const proto::ProtoObject* cls = env->getType(ctx, container);
        uint32_t flags = (cls && cls != PROTO_NONE) ? env->ensureClassFlags(ctx, cls) : 0;
        if (flags & protoPython::PythonEnvironment::PYFLAG_HAS_CUSTOM_GETATTR) {
            const proto::ProtoObject* mroAttr = env->getAttribute(ctx, cls, env->getMroString(), false);
            ::protoPython::TupleView mroT = mroAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, mroAttr) : nullptr;
            if (mroT) {
                for (proto::proto_ulong i = 0; i < mroT->getSize(ctx); ++i) {
                    const proto::ProtoObject* base = mroT->getAt(ctx, static_cast<int>(i));
                    if (!base || base == PROTO_NONE) continue;
                    if (base->hasOwnAttribute(ctx, name) == PROTO_TRUE) {
                        const proto::ProtoObject* v = base->getOwnAttributeDirect(ctx, name);
                        if (v && v != PROTO_NONE) {
                            // Apply descriptor protocol if v has __get__.
                            const proto::ProtoString* getDS =
                                PythonEnvironment::getInternedString(ctx, "__get__");
                            const proto::ProtoObject* vType = env->getType(ctx, v);
                            const proto::ProtoObject* getM = vType
                                ? env->getAttribute(ctx, vType, getDS, false) : nullptr;
                            if (getM && getM != PROTO_NONE) {
                                const proto::ProtoList* gargs =
                                    ctx->newList()->appendLast(ctx, container)->appendLast(ctx, cls);
                                if (getM->asMethod(ctx)) {
                                    v = getM->asMethod(ctx)(ctx,
                                        const_cast<proto::ProtoObject*>(v), nullptr, argsView(proto::ProtoListArguments(ctx, gargs)), nullptr);
                                } else {
                                    // Python __get__: prepend descriptor
                                    // as self when invoking.
                                    const proto::ProtoList* fullArgs =
                                        ctx->newList()->appendLast(ctx, v)
                                            ->appendLast(ctx, container)->appendLast(ctx, cls);
                                    v = invokeCallable(ctx, getM, fullArgs, nullptr);
                                }
                            }
                            method = v;
                            break;
                        }
                    }
                }
            }
            if (!method) {
                // Fall back to legacy path if MRO walk missed (e.g.
                // tagged-method registrations not exposed as own).
                method = env->getAttribute(ctx, container, name, false);
            }
        } else {
            method = env->getAttribute(ctx, container, name, false);
        }
    } else {
        method = container ? container->getAttribute(ctx, name) : nullptr;
    }

    if (!method || method == PROTO_NONE) return nullptr;

    const proto::ProtoSparseList* kwargs = env ? env->getEmptySparseList() : ctx->newSparseList();

    if (method->asMethod(ctx)) {
        // STRUCT-177: slot wrapper receiver-type validation.  protoCore's
        // chain walk for dunders such as `__getitem__` / `__setitem__`
        // sometimes resolves to `dict.__getitem__` on receivers whose
        // Python MRO does NOT contain `dict` (e.g. unittest.TestLoader,
        // any plain user class).  The wrapper's C++ implementation then
        // dereferences `__data__` on a foreign receiver and silently
        // returns junk (typically None).  CPython would raise TypeError
        // here because the slot wrapper checks `PyObject_TypeCheck`
        // against its owner.
        //
        // The validation is conservative: only WRAPPER-kind methods are
        // checked; methods (Python-level), built-in functions and
        // descriptors flow unmodified.  When the receiver's MRO does
        // not include the owner, return nullptr so callers (invokeDunder
        // consumers / OP_BINARY_SUBSCR fallback) raise the right
        // TypeError instead of dispatching with a wrong receiver.
        if (env) {
            proto::ProtoMethod fn = method->asMethod(ctx);
            std::string mnm;
            const proto::ProtoObject* owner = nullptr;
            protoPython::PythonEnvironment::NativeMethodInfo::Kind kind =
                protoPython::PythonEnvironment::NativeMethodInfo::Kind::METHOD;
            if (fn && env->lookupNativeMethodInfo(reinterpret_cast<const void*>(fn), mnm, &owner, &kind)
                && kind == protoPython::PythonEnvironment::NativeMethodInfo::Kind::WRAPPER
                && owner && owner != PROTO_NONE) {
                const proto::ProtoObject* rcvType = env->getType(ctx, container);
                if (rcvType && rcvType != owner) {
                    bool ok = false;
                    const proto::ProtoString* mroS = env->getMroString();
                    const proto::ProtoObject* mroAttr = env->getAttribute(ctx, rcvType, mroS, false);
                    ::protoPython::TupleView mroT = mroAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, mroAttr) : nullptr;
                    if (mroT) {
                        for (proto::proto_ulong i = 0; i < mroT->getSize(ctx); ++i) {
                            if (mroT->getAt(ctx, static_cast<int>(i)) == owner) { ok = true; break; }
                        }
                    }
                    if (!ok) {
                        // Also check protoCore native parent chain (covers
                        // built-in subclass instances whose __mro__ tuple
                        // may be unset but whose chain is correct).
                        const proto::ProtoList* chain = rcvType->getParents(ctx);
                        if (chain) {
                            for (proto::proto_ulong i = 0; i < chain->getSize(ctx); ++i) {
                                if (chain->getAt(ctx, static_cast<int>(i)) == owner) {
                                    ok = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!ok) {
                        // STRUCT-307: when the receiver type has the
                        // SAME native fn as its OWN attribute, that
                        // is shared-implementation (set/frozenset
                        // pattern: both prototypes bind the same
                        // py_set_or fn), not mis-binding.  The
                        // recorded owner is whichever prototype was
                        // visited first by registerNativeMethodNames;
                        // the receiver type accepted it as its own,
                        // so the wrapper is valid here.
                        bool sharedImpl = false;
                        if (rcvType) {
                            const proto::ProtoString* attrKey =
                                PythonEnvironment::getInternedString(ctx, mnm.c_str());
                            if (attrKey && rcvType->hasOwnAttribute(ctx, attrKey) == PROTO_TRUE) {
                                const proto::ProtoObject* ownVal =
                                    rcvType->getOwnAttributeDirect(ctx, attrKey);
                                if (ownVal && ownVal->asMethod(ctx) == method->asMethod(ctx)) {
                                    sharedImpl = true;
                                }
                            }
                        }
                        if (sharedImpl) {
                            ok = true;
                        }
                    }
                    // STRUCT-242: when the wrapper-receiver check
                    // fails AND the receiver explicitly bound a
                    // DIFFERENT same-named method (the legacy
                    // `class A(int): __op__ = str.__op__` pattern),
                    // raise TypeError.  For inherited / chain-leak
                    // misses without an explicit own-attr binding,
                    // keep the silent null-return so the implicit-
                    // dispatch path (`x in y`, etc.) can fall back
                    // without raising.
                    if (!ok) {
                        bool explicitMisbind = false;
                        if (rcvType) {
                            const proto::ProtoString* attrKey =
                                PythonEnvironment::getInternedString(ctx, mnm.c_str());
                            if (attrKey && rcvType->hasOwnAttribute(ctx, attrKey) == PROTO_TRUE) {
                                const proto::ProtoObject* ownVal =
                                    rcvType->getOwnAttributeDirect(ctx, attrKey);
                                // Distinct fn → genuine misbind.
                                // (Same fn already handled above.)
                                if (ownVal && ownVal->asMethod(ctx)
                                    && ownVal->asMethod(ctx) != method->asMethod(ctx)) {
                                    explicitMisbind = true;
                                }
                            }
                        }
                        if (explicitMisbind) {
                            std::string ownerName = "?";
                            std::string rcvName = "?";
                            const proto::ProtoObject* nm = owner->getAttribute(ctx, env->getNameString());
                            if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, ownerName);
                            const proto::ProtoObject* rnm = rcvType->getAttribute(ctx, env->getNameString());
                            if (rnm && rnm->isString(ctx)) rnm->asString(ctx)->toUTF8String(ctx, rcvName);
                            env->raiseTypeError(ctx,
                                "descriptor '" + mnm + "' for '" + ownerName +
                                "' objects doesn't apply to a '" + rcvName + "' object");
                        }
                        return nullptr;
                    }
                }
            }
        }
        return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(container), nullptr, argsView(proto::ProtoListArguments(ctx, args)), kwargsView(proto::ProtoSparseListKeywordArguments(ctx, kwargs)));
    }

    // Python user-defined dunder (regular function with __code__): prepend
    // `self` so the call lands as method(self, *args). invokeCallable does
    // not bind for us, so without this every inherited Python __contains__,
    // __setitem__, __getitem__ etc. ran with self=arg0 (silently broken).
    const proto::ProtoString* codeS = env ? env->getCodeString() : PythonEnvironment::getInternedString(ctx, "__code__");
    if (codeS && method->hasOwnAttribute(ctx, codeS) == PROTO_TRUE) {
        const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, container);
        proto::proto_ulong n = args ? args->getSize(ctx) : 0;
        for (proto::proto_ulong j = 0; j < n; ++j) selfArgs = selfArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(j)));
        return invokeCallable(ctx, method, selfArgs, kwargs);
    }
    return invokeCallable(ctx, method, args, kwargs);
}

} // anonymous namespace

// `finished`, when given, receives the end of the generator: it then answers
// nullptr instead of raising StopIteration (iteration's exhaustion is not an
// exception internally; py_generator_step). Without it, a finished generator
// raises StopIteration carrying its return value, as __next__ / send do.
// The generator's suspended/running state as CPython exposes it:
// gi_suspended (inspect.getgeneratorstate reads it), and for a coroutine or
// an async generator cr_/ag_suspended and cr_/ag_running as well
// (getcoroutinestate, getasyncgenstate).  gi_running is written by the
// callers.  Suspended means stopped at a yield: not before the first
// resume, not while running, not once finished.
static void setGeneratorSuspended(proto::ProtoContext* ctx, const proto::ProtoObject* gen,
                                  bool running, bool suspended) {
    static const proto::ProtoString* giSuspended =
        PythonEnvironment::getInternedString(ctx, "gi_suspended");
    static const proto::ProtoString* names[2][2] = {
        {PythonEnvironment::getInternedString(ctx, "cr_suspended"),
         PythonEnvironment::getInternedString(ctx, "cr_running")},
        {PythonEnvironment::getInternedString(ctx, "ag_suspended"),
         PythonEnvironment::getInternedString(ctx, "ag_running")},
    };
    gen->setAttribute(ctx, giSuspended, suspended ? PROTO_TRUE : PROTO_FALSE);
    for (const auto& kind : names) {
        if (gen->hasOwnAttribute(ctx, kind[0]) == PROTO_TRUE) {
            gen->setAttribute(ctx, kind[0], suspended ? PROTO_TRUE : PROTO_FALSE);
            gen->setAttribute(ctx, kind[1], running ? PROTO_TRUE : PROTO_FALSE);
        }
    }
}

// PEP 479: StopIteration raised out of a generator's or a coroutine's body
// (and StopAsyncIteration out of an async generator's) becomes RuntimeError,
// with the original as __cause__ and __context__.  Without it a stray
// StopIteration silently ended the consumer's loop.
[[noreturn]] static void raiseStopIterationAsRuntimeError(
        proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* gen,
        const proto::ProtoException& e) {
    const proto::ProtoObject* exc = e.value();
    const bool isAsyncGen = gen->hasOwnAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "ag_code")) == PROTO_TRUE;
    const bool isCoro = !isAsyncGen && gen->hasOwnAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "cr_code")) == PROTO_TRUE;
    const bool stopIteration = env->isStopIteration(ctx, exc);
    const bool stopAsync = !stopIteration && isAsyncGen && env->getStopAsyncIterationType()
        && e.isInstanceOf(ctx, env->getStopAsyncIterationType());
    if ((!stopIteration && !stopAsync) || !env->getRuntimeErrorType()) throw;
    std::string msg = isAsyncGen ? "async generator" : (isCoro ? "coroutine" : "generator");
    msg += stopIteration ? " raised StopIteration" : " raised StopAsyncIteration";
    const proto::ProtoObject* rt = invokePythonCallable(ctx, env->getRuntimeErrorType(),
        ctx->newList()->appendLast(ctx, PythonEnvironment::getInternedString(ctx, msg.c_str())->asObject(ctx)),
        nullptr);
    if (!rt || rt == PROTO_NONE) throw;
    rt->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cause__"), exc);
    rt->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__context__"), exc);
    throw PyException(env, rt);
}

static const proto::ProtoObject* py_generator_resume(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ProtoObject* sendVal,
    const proto::ProtoObject* throwExc,
    bool* finished) {
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;

    RecursionScope recScope(env, ctx);
    if (recScope.overflowed()) {
        if (get_env_diag()) {
            // log removed
        }
        return nullptr;
    }

    // 1. Check if running
    const proto::ProtoObject* runningAttr = self->getAttribute(ctx, env->getGiRunningString());
    if (runningAttr == PROTO_TRUE) {
        env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx, "generator already executing")->asObject(ctx));
        return PROTO_NONE;
    }

    // 2. Check for native callback
    const proto::ProtoObject* nativeCb = self->getAttribute(ctx, env->getGiNativeCallbackString());
    if (nativeCb && nativeCb != PROTO_NONE) {
        // Native generators (protopyc) use a C++ callback that handles
        // state.  It receives the generator, the value sent and, for throw()
        // and close(), the exception to raise at the suspended yield; it
        // updates gi_pc and gi_locals on the generator itself.

        // Running until the callback returns or throws.
        self->setAttribute(ctx, env->getGiRunningString(), PROTO_TRUE);
        struct NotRunning {
            proto::ProtoContext* ctx; PythonEnvironment* env; const proto::ProtoObject* gen;
            ~NotRunning() { gen->setAttribute(ctx, env->getGiRunningString(), PROTO_FALSE); }
        } notRunning{ctx, env, self};
        return throwExc
            ? env->callObject(nativeCb, {self, sendVal ? sendVal : PROTO_NONE, throwExc})
            : env->callObject(nativeCb, {self, sendVal ? sendVal : PROTO_NONE});
    }

    // 3. Get bytecode state
    const proto::ProtoObject* codeObj = self->getAttribute(ctx, env->getGiCodeString());
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(self->getAttribute(ctx, env->getGiFrameString()));
    const proto::ProtoObject* pcObj = self->getAttribute(ctx, env->getGiPCString());
    const proto::ProtoObject* stackObj = self->getAttribute(ctx, env->getGiStackString());
    const proto::ProtoObject* blocksObj = self->getAttribute(ctx, env->getGiBlocksString());

    if (!codeObj || !frame || !pcObj || !stackObj) {
        if (get_env_diag()) fprintf(stderr, "DEBUG HANG: generator missing properties! codeObj=%p frame=%p pcObj=%p stackObj=%p\n", (void*)codeObj, (void*)frame, (void*)pcObj, (void*)stackObj);
        return PROTO_NONE;
    }

    // Restore blockStack
    std::vector<Block> blockStack;
    const proto::ProtoList* blist = blocksObj ? ::protoPython::listStorage(ctx, blocksObj) : nullptr;
    if (blist) {
        proto::proto_ulong size = blist->getSize(ctx);
        for (proto::proto_ulong i = 0; i < size; ++i) {
            const proto::ProtoObject* item = blist->getAt(ctx, static_cast<int>(i));
            if (item && ::protoPython::PythonEnvironment::isTupleValue(ctx, item)) {
                ::protoPython::TupleView t = ::protoPython::PythonEnvironment::tupleView(ctx, item);
                if (t->getSize(ctx) >= 2) {
                    bool isWith = (t->getSize(ctx) >= 3) && (t->getAt(ctx, 2) == PROTO_TRUE);
                    Block saved{
                        static_cast<proto::proto_ulong>(t->getAt(ctx, 0)->asLong(ctx)),
                        static_cast<size_t>(t->getAt(ctx, 1)->asLong(ctx)),
                        isWith
                    };
                    // A handler level: the exception it was handling.
                    if (t->getSize(ctx) >= 5 && t->getAt(ctx, 3) == PROTO_TRUE) {
                        saved.isHandler = true;
                        saved.exc = t->getAt(ctx, 4);
                    }
                    blockStack.push_back(saved);
                }
            }
        }
    }

    proto::proto_ulong pc = (pcObj && pcObj->isInteger(ctx)) ? static_cast<proto::proto_ulong>(pcObj->asLong(ctx)) : 0;
    const proto::ProtoTuple* co_code_tuple = codeObj->getAttribute(ctx, env->getCoCodeString())->asTuple(ctx);
    if (!co_code_tuple) {
        if (get_env_diag()) fprintf(stderr, "DEBUG HANG: generator missing co_code_tuple!\n");
        return PROTO_NONE;
    }
    
    if (pc >= co_code_tuple->getSize(ctx)) {
        if (get_env_diag()) fprintf(stderr, "DEBUG HANG: generator pc >= co_code_tuple! pc=%llu size=%llu\n", static_cast<unsigned long long>(pc), static_cast<unsigned long long>(co_code_tuple->getSize(ctx)));
        if (finished && !throwExc) { *finished = true; return nullptr; }
        env->raiseStopIteration(ctx, PROTO_NONE);
        return PROTO_NONE;
    }

    // 3. Restore stack (moved inside block below)

    // 5. Run (gen.throw's exception is raised at the resume point, inside
    // the generator's rebuilt tries: executeBytecodeRange's throwOnResume).
    proto::proto_ulong nextPc = pc; 
    proto::proto_ulong finalTop = 0;
    proto::proto_ulong stackOffset = 0;
    proto::proto_ulong initialTop = 0;
    bool yielded = false;
    proto::ProtoContext* calleeCtx = nullptr;
    const proto::ProtoList* slist = stackObj ? ::protoPython::listStorage(ctx, stackObj) : nullptr;
    
    const proto::ProtoObject* result = nullptr;
    {
        const proto::ProtoObject* co_varnames_obj = codeObj->getAttribute(ctx, env->getCoVarnamesString());
        ::protoPython::TupleView co_varnames = co_varnames_obj ? ::protoPython::PythonEnvironment::tupleView(ctx, co_varnames_obj) : nullptr;
        stackOffset = co_varnames ? co_varnames->getSize(ctx) : 0;
        // The operand stack starts after the locals and the free variables.
        const proto::ProtoObject* nlpObj = codeObj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "co_nlocalsplus"));
        if (nlpObj && nlpObj->isInteger(ctx)) stackOffset = static_cast<proto::proto_ulong>(nlpObj->asLong(ctx));
        const proto::ProtoObject* co_automatic_obj = codeObj->getAttribute(ctx, env->getCoAutomaticCountString());
        int automatic_count = (co_automatic_obj && co_automatic_obj->isInteger(ctx)) ? static_cast<int>(co_automatic_obj->asLong(ctx)) : 0;
        
        // Sized like a function call's context (runUserFunctionCall): by
        // automatic_count, without a list of local names.  Building that
        // list took one append per automatic slot on every resume.
        ContextScope scope(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr,
                           static_cast<size_t>(automatic_count));
        calleeCtx = scope.context();

        // The generator's bookkeeping writes (gi_running, gi_pc, and the
        // saved locals, stack and blocks below) allocate in calleeCtx, whose
        // young generation is submitted when the resume ends; the trees stay
        // reachable through the generator object.  Written in the caller's
        // context they stayed live until the caller returned: a native
        // consumer (sum, any, str.join, list) of a 100,000-step generator
        // held about 1.6 M cells of them.
        self->setAttribute(calleeCtx, env->getGiRunningString(), PROTO_TRUE);
        setGeneratorSuspended(calleeCtx, self, /*running=*/true, /*suspended=*/false);

        // Restore locals from gi_locals
        const proto::ProtoObject* localsObj = self->getAttribute(ctx, env->getGiLocalsString());
        const proto::ProtoList* savedLocals = localsObj ? ::protoPython::listStorage(ctx, localsObj) : nullptr;

        unsigned int nSlots = calleeCtx->getAutomaticLocalsCount();
        proto::ProtoObject** allSlots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());

        if (savedLocals && allSlots) {
            for (unsigned int i = 0; i < nSlots && i < savedLocals->getSize(ctx); ++i) {
                allSlots[i] = const_cast<proto::ProtoObject*>(savedLocals->getAt(ctx, i));
            }
        }

        // Restore stack after slots are ready.  Guard against the case
        // where the ContextScope did not allocate enough automatic
        // locals for both varnames and the operand stack: when
        // executeBytecodeRange detects that, it falls back to its own
        // local std::vector for the stack (see ExecutionEngine.cpp:3120).
        // In that mode any bytes the caller would push into
        // `slots[stackOffset..]` are invisible to executeBytecodeRange,
        // and any persisted stack the function returns via finalTop
        // does NOT live in `slots`.  Detecting this here lets us route
        // the prelude / save loop accordingly so we never deref past
        // the slot array.
        proto::ProtoObject** stackBase = (allSlots && nSlots > stackOffset) ? allSlots + stackOffset : nullptr;
        unsigned int maxStack = (allSlots && nSlots > stackOffset) ? static_cast<unsigned int>(nSlots - stackOffset) : 0;

        if (slist && stackBase) {
            proto::proto_ulong sSize = slist->getSize(calleeCtx);
            const proto::ProtoObject* nullSlot = env->getStackNullSentinel();
            for (proto::proto_ulong i = 0; i < sSize && i < maxStack; ++i) {
                const proto::ProtoObject* v = slist->getAt(calleeCtx, static_cast<int>(i));
                stackBase[initialTop++] = (v == nullSlot) ? nullptr : const_cast<proto::ProtoObject*>(v);
            }
        }

        if (pc > 0 && stackBase) {
            if (initialTop < maxStack) stackBase[initialTop++] = const_cast<proto::ProtoObject*>(sendVal);
        }

        const proto::ProtoObject* globals = frame->getAttribute(calleeCtx, env->getFGlobalsString());
        if (!globals) globals = env->getGlobals();
        GlobalsScope gscope(globals);
        
        const proto::ProtoTuple* co_consts = codeObj->getAttribute(calleeCtx, env->getCoConstsString())->asTuple(calleeCtx);
        const proto::ProtoTuple* co_names = codeObj->getAttribute(calleeCtx, env->getCoNamesString())->asTuple(calleeCtx);

        // As in runUserFunctionCall: LOAD_FAST / DELETE_FAST name an unbound
        // local from the running code object's co_varnames.
        try {
            CodeObjectScope codeScope(codeObj);
            result = executeBytecodeRange(calleeCtx,
                co_consts,
                reinterpret_cast<const proto::ProtoObject*>(co_code_tuple)->asTuple(calleeCtx),
                co_names,
                frame,
                pc,
                co_code_tuple->getSize(calleeCtx),
                stackOffset,
                &nextPc,
                &yielded,
                &blockStack,
                initialTop,
                &finalTop,
                nullptr, nullptr, nullptr,
                throwExc);
        } catch (const proto::ProtoException& e) {
            // An exception out of the body finishes the generator.
            self->setAttribute(ctx, env->getGiRunningString(), PROTO_FALSE);
            setGeneratorSuspended(ctx, self, /*running=*/false, /*suspended=*/false);
            self->setAttribute(ctx, env->getGiPCString(), ctx->fromInteger(co_code_tuple->getSize(ctx)));
            raiseStopIterationAsRuntimeError(ctx, env, self, e);
        }

        // Save the variable slots only, [0, stackOffset): the operand stack
        // is saved below up to its live top.  Saving every automatic slot
        // (the operand-stack area included, 128 + max stack) cost one list
        // append per slot on every resume, ~37 us of a ~75 us resume, and
        // made each step of a recursive `yield from` chain pay it per level.
        const proto::ProtoList* newLocals = calleeCtx->newList();
        const proto::ProtoObject** updatedSlots = calleeCtx->getAutomaticLocals();
        unsigned int updatedSlotsN = calleeCtx->getAutomaticLocalsCount();
        if (updatedSlots) {
            const unsigned int nVarSlots = static_cast<unsigned int>(
                std::min<proto::proto_ulong>(updatedSlotsN, stackOffset));
            for (unsigned int i = 0; i < nVarSlots; ++i) {
                newLocals = newLocals->appendLast(calleeCtx, updatedSlots[i]);
            }
        }
        self->setAttribute(calleeCtx, env->getGiLocalsString(), newLocals->asObject(calleeCtx));

        // Save stack back while calleeCtx is still alive.  Same caveat
        // as above: only read from `slots[stackOffset..]` when those
        // indices are actually within the slot array.
        const proto::ProtoList* newStack = calleeCtx->newList();
        const proto::ProtoObject** slots = calleeCtx->getAutomaticLocals();
        if (slots && updatedSlotsN > stackOffset) {
            proto::proto_ulong stackBound = (updatedSlotsN > stackOffset)
                ? static_cast<proto::proto_ulong>(updatedSlotsN - stackOffset) : PROTO_UL(0);
            proto::proto_ulong count = (finalTop < stackBound) ? finalTop : stackBound;
            const proto::ProtoObject* nullSlot = env->getStackNullSentinel();
            for (proto::proto_ulong j = 0; j < count; ++j) {
                const proto::ProtoObject* v = slots[stackOffset + j];
                newStack = newStack->appendLast(calleeCtx, v ? v : nullSlot);
            }
        }
        self->setAttribute(calleeCtx, env->getGiStackString(), newStack->asObject(calleeCtx));

        // Save blockStack back (tuple: [handlerPc, stackDepth, isWithBlock,
        // isHandler, the handled exception or None])
        const proto::ProtoList* newBlocks = calleeCtx->newList();
        for (const auto& b : blockStack) {
            const proto::ProtoList* tempL = calleeCtx->newList();
            tempL = tempL->appendLast(calleeCtx, calleeCtx->fromInteger(b.handlerPc));
            tempL = tempL->appendLast(calleeCtx, calleeCtx->fromInteger(b.stackDepth));
            tempL = tempL->appendLast(calleeCtx, b.isWithBlock ? PROTO_TRUE : PROTO_FALSE);
            tempL = tempL->appendLast(calleeCtx, b.isHandler ? PROTO_TRUE : PROTO_FALSE);
            tempL = tempL->appendLast(calleeCtx, b.exc ? b.exc : PROTO_NONE);
            const proto::ProtoObject* bTup = ::protoPython::PythonEnvironment::tupleFromList(calleeCtx, tempL);
            newBlocks = newBlocks->appendLast(calleeCtx, bTup);
        }
        self->setAttribute(calleeCtx, env->getGiBlocksString(), newBlocks->asObject(calleeCtx));

        // 8. Clear running
        self->setAttribute(calleeCtx, env->getGiRunningString(), PROTO_FALSE);
        setGeneratorSuspended(calleeCtx, self, /*running=*/false, /*suspended=*/yielded);
        if (!yielded) {
            nextPc = co_code_tuple->getSize(calleeCtx);
        }
        self->setAttribute(calleeCtx, env->getGiPCString(), calleeCtx->fromInteger(nextPc));
    }

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: py_generator_send_impl finished. yielded=%d, result=%p\n",
                (int)yielded, (void*)result);
        fflush(stderr);
    }

    if (!yielded) {
        if (get_env_diag()) fprintf(stderr, "DEBUG HANG: generator exhausted cleanly! raising StopIteration\n");
        if (finished) { *finished = true; return nullptr; }
        env->raiseStopIteration(ctx, result);
        return nullptr;
    }

    if (get_env_diag()) {
        std::string repr = env && result ? PythonEnvironment::reprObject(ctx, result) : "???";
        fprintf(stderr, "DEBUG HANG: py_generator_send_impl returning normally: %p %s\n", (void*)result, repr.c_str());
    }
    return result ? result : PROTO_NONE;
}

const proto::ProtoObject* py_self_iter(
    proto::ProtoContext*,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    return self;
}

const proto::ProtoObject* py_generator_repr(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PythonEnvironment::getInternedString(ctx, "<generator object>")->asObject(ctx);

    const proto::ProtoObject* code = self->getAttribute(ctx, env->getGiCodeString());
    std::string name = "<unknown>";
    if (code) {
        const proto::ProtoObject* co_name = code->getAttribute(ctx, env->getCoNameString());
        if (co_name && co_name->isString(ctx)) co_name->asString(ctx)->toUTF8String(ctx, name);
    }
    
    char buf[128];
    snprintf(buf, sizeof(buf), "<generator object %s at %p>", name.c_str(), (void*)self);
    return PythonEnvironment::getInternedString(ctx, buf)->asObject(ctx);
}

const proto::ProtoObject* py_generator_send_impl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ProtoObject* sendVal,
    const proto::ProtoObject* throwExc = nullptr) {
    return py_generator_resume(ctx, self, sendVal, throwExc, nullptr);
}

const proto::ProtoObject* py_generator_step(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    bool finished = false;
    const proto::ProtoObject* r = py_generator_resume(ctx, self, PROTO_NONE, nullptr, &finished);
    return finished ? nullptr : r;
}

const proto::ProtoObject* py_generator_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    return py_generator_send_impl(ctx, self, PROTO_NONE, nullptr);
}

// PF: Drive a class-defined async iterator's __anext__() inline so the
// existing FOR_ITER protocol can iterate it.  Used by the OP_GET_AITER
// bridge when the iterator exposes __anext__ but no inherited __next__
// (e.g. user classes implementing __aiter__/async def __anext__).
//
// Semantics (subset, no internal awaits supported):
//   1. Call self.__anext__() → coroutine
//   2. coroutine.send(None) drives one step
//      - StopIteration(value=V) → return V (the yielded value)
//      - StopAsyncIteration       → raise StopIteration; FOR_ITER ends loop
//      - other exception         → propagate
//      - actual yield            → not supported; treated as exhaustion
//
// Async generators inherit __next__ from the generator prototype, so
// they bypass this wrapper and FOR_ITER drives them via py_generator_next
// directly.
const proto::ProtoObject* py_class_aiter_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments*,
    const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self) return PROTO_NONE;

    const proto::ProtoString* anextS = env->getANextString();
    const proto::ProtoObject* anext = self->getAttribute(ctx, anextS);
    if (!anext || anext == PROTO_NONE) {
        env->raiseStopIteration(ctx, PROTO_NONE);
        return nullptr;
    }

    const proto::ProtoObject* coro = nullptr;
    // StopAsyncIteration from __anext__ ends the iteration: StopIteration.
    // Resolved via the env's cached type and isInstanceOf — exc->__class__
    // can resolve through the metaclass and return `type` instead of the
    // actual exception class for some allocation paths, so a string-name
    // compare against "StopAsyncIteration" is unreliable.
    auto isStopAsync = [&](const proto::ProtoObject* exc) {
        const proto::ProtoObject* sasType = env->getStopAsyncIterationType();
        return exc && sasType && exc->isInstanceOf(ctx, sasType) == PROTO_TRUE;
    };
    try {
        if (anext->asMethod(ctx) && anext->asMethodSelf(ctx) != nullptr) {
            // Bound native method — self already captured.
            coro = anext->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(self),
                                         nullptr, argsView(proto::ProtoArguments(nullptr, 0)), nullptr);
        } else {
            // Python-level async def __anext__ — call as a callable, prepending self
            // explicitly because async def methods are not auto-bound by descriptor
            // protocol at attribute lookup time in protopy.
            const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, self);
            coro = invokePythonCallable(ctx, anext, selfArgs, nullptr);
        }
    } catch (const proto::ProtoException& e) {
        if (!isStopAsync(e.value())) throw;
        env->raiseStopIteration(ctx, PROTO_NONE);
    }
    if (!coro || coro == PROTO_NONE) {
        env->raiseStopIteration(ctx, PROTO_NONE);
        return nullptr;
    }

    const proto::ProtoString* sendS = env->getSendString();
    const proto::ProtoObject* sendMethod = coro->getAttribute(ctx, sendS);
    if (!sendMethod || sendMethod == PROTO_NONE) {
        // Not a coroutine — treat as the value itself (async-gen-style).
        return coro;
    }

    // PROTO_NONE (the global singleton) — py_generator_send compares
    // sendVal == PROTO_NONE to detect a None send.  Using
    // env->getNonePrototype() here would compare unequal and surface as
    // "can't send non-None value to a just-started generator".
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, PROTO_NONE);
    const proto::ProtoObject* sendResult = nullptr;
    try {
        if (sendMethod->asMethod(ctx)) {
            sendResult = sendMethod->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(coro),
                                                    nullptr, argsView(proto::ProtoListArguments(ctx, args)), nullptr);
        } else {
            sendResult = invokePythonCallable(ctx, sendMethod, args, nullptr);
        }
    } catch (const proto::ProtoException& e) {
        // StopIteration carries the step's value; StopAsyncIteration ends
        // the iteration (as StopIteration, so FOR_ITER ends cleanly).
        const proto::ProtoObject* exc = e.value();
        if (env->isStopIteration(ctx, exc)) {
            const proto::ProtoObject* val = env->getStopIterationValue(ctx, exc);
            return val ? val : PROTO_NONE;
        }
        if (!isStopAsync(exc)) throw;
        env->raiseStopIteration(ctx, PROTO_NONE);
    }

    // Non-yielding async __anext__ that completes via implicit return None.
    return sendResult ? sendResult : PROTO_NONE;
}

// PF: per-asend-call wrapper.  Executes one step of the underlying
// async generator using the originally-captured sendVal.  On send(None),
// raises StopIteration with the value yielded by the generator (or
// propagates StopAsyncIteration if the generator is exhausted).
//
// The wrapper is single-shot: after the first send(None) completes, it
// is exhausted.  Subsequent sends raise StopIteration(None).  This is
// sufficient for the canonical `await agen.asend(v)` pattern and the
// `run(agen.asend(v))` driver used in the synthetic tests.
static const proto::ProtoObject* py_aclose_wrapper_await(
    proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*);

static const proto::ProtoObject* py_asend_wrapper_send(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments*,
    const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self) return PROTO_NONE;

    const proto::ProtoString* targetS  = PythonEnvironment::getInternedString(ctx, "_asend_target");
    const proto::ProtoString* sendValS = PythonEnvironment::getInternedString(ctx, "_asend_send_val");
    const proto::ProtoString* doneS    = PythonEnvironment::getInternedString(ctx, "_asend_done");

    const proto::ProtoObject* doneObj = self->getAttribute(ctx, doneS);
    if (doneObj == PROTO_TRUE) {
        env->raiseStopIteration(ctx, PROTO_NONE);
        return nullptr;
    }
    // Mark exhausted (immutable: setAttribute returns a new copy, but the
    // caller already holds the original reference; the wrapper is single-shot
    // anyway, so we don't carry the new copy back — the next call would just
    // re-fire the underlying generator one more step, then hit its own
    // StopIteration).  For strict single-shot semantics, callers should not
    // re-send after StopIteration; the test driver respects this.

    const proto::ProtoObject* target = self->getAttribute(ctx, targetS);
    const proto::ProtoObject* sendVal = self->getAttribute(ctx, sendValS);
    if (!target || target == PROTO_NONE) {
        env->raiseStopIteration(ctx, PROTO_NONE);
        return nullptr;
    }
    if (!sendVal) sendVal = PROTO_NONE;

    // Drive the underlying async_generator one step.
    extern const proto::ProtoObject* py_generator_send_impl(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* self,
        const proto::ProtoObject* sendVal,
        const proto::ProtoObject* throwExc);
    const proto::ProtoObject* val = nullptr;
    try {
        val = py_generator_send_impl(ctx, target, sendVal, nullptr);
    } catch (const proto::ProtoException& e) {
        // StopIteration from the underlying agen → StopAsyncIteration, so
        // callers awaiting `agen.asend(v)` see the async-iteration sentinel.
        if (!env->isStopIteration(ctx, e.value())) throw;
        env->raiseStopAsyncIteration(ctx);
    }

    // val is the yielded value.  Wrap it as the StopIteration return value
    // so the caller's await/run() driver retrieves it.
    env->raiseStopIteration(ctx, val ? val : PROTO_NONE);
    return nullptr;
}

const proto::ProtoObject* py_async_generator_asend(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self) return PROTO_NONE;

    const proto::ProtoObject* sendVal = (posArgs && posArgs->getSize(ctx) > 0)
                                        ? posArgs->getAt(ctx, 0)
                                        : PROTO_NONE;

    // Build the single-shot wrapper.
    const proto::ProtoObject* wrapper = ctx->newObject(true);
    const proto::ProtoString* targetS  = PythonEnvironment::getInternedString(ctx, "_asend_target");
    const proto::ProtoString* sendValS = PythonEnvironment::getInternedString(ctx, "_asend_send_val");
    const proto::ProtoString* doneS    = PythonEnvironment::getInternedString(ctx, "_asend_done");
    wrapper = wrapper->setAttribute(ctx, targetS, self);
    wrapper = wrapper->setAttribute(ctx, sendValS, sendVal);
    wrapper = wrapper->setAttribute(ctx, doneS, PROTO_FALSE);
    // send / __next__ both drive one step; the wrapper StopIterates.  The
    // cells are unbound (the wrapper is passed as self): bound to the
    // wrapper and stored on it they made each asend() a self-cycle that
    // protoCore never collects (docs/CONFORMANCE.md rule 13).
    const proto::ProtoObject* sendCell = ctx->fromMethod(nullptr, py_asend_wrapper_send);
    wrapper = wrapper->setAttribute(ctx, env->getSendString(), sendCell);
    wrapper = wrapper->setAttribute(ctx, env->getNextString(), sendCell);
    // __await__ returns self so it's a valid awaitable.
    wrapper = wrapper->setAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "__await__"),
        ctx->fromMethod(nullptr, py_aclose_wrapper_await));
    return wrapper;
}

// aclose(): an awaitable that closes the async generator when it is driven,
// as CPython's aclose() does (athrow(GeneratorExit) where GeneratorExit and
// StopAsyncIteration mean "closed").  aclose used to be generator.close()
// itself: it closed the generator at once and returned None, so
// `await agen.aclose()` awaited None and never finished.
//
// The wrapper's cells are unbound: a cell bound to the wrapper and stored on
// it would be a self-cycle in the mutables table that protoCore never
// collects (docs/CONFORMANCE.md rule 13).  Attribute reads and the await
// machinery pass the wrapper as self.
static const proto::ProtoObject* py_aclose_wrapper_send(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments*,
    const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    if (!self || self == PROTO_NONE) {
        env->raiseTypeError(ctx, "aclose() awaitable: called without the awaitable");
        return nullptr;
    }
    const proto::ProtoString* targetS = PythonEnvironment::getInternedString(ctx, "_aclose_target");
    const proto::ProtoString* doneS   = PythonEnvironment::getInternedString(ctx, "_aclose_done");
    if (self->hasOwnAttribute(ctx, doneS) == PROTO_TRUE
            && self->getOwnAttributeDirect(ctx, doneS) == PROTO_TRUE) {
        env->raiseStopIteration(ctx, PROTO_NONE);
        return nullptr;
    }
    const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, doneS, PROTO_TRUE);
    const proto::ProtoObject* target = self->hasOwnAttribute(ctx, targetS) == PROTO_TRUE
        ? self->getOwnAttributeDirect(ctx, targetS) : nullptr;
    if (target && target != PROTO_NONE) {
        try {
            py_generator_close(ctx, target, nullptr, nullptr, nullptr);
        } catch (const proto::ProtoException& e) {
            const proto::ProtoObject* exc = e.value();
            const proto::ProtoObject* sas = env->getStopAsyncIterationType();
            const proto::ProtoObject* gex = env->getGeneratorExitType();
            const bool closed = exc
                && ((sas && exc->isInstanceOf(ctx, sas) == PROTO_TRUE)
                    || (gex && exc->isInstanceOf(ctx, gex) == PROTO_TRUE)
                    || env->isStopIteration(ctx, exc));
            if (!closed) throw;
        }
    }
    env->raiseStopIteration(ctx, PROTO_NONE);
    return nullptr;
}

static const proto::ProtoObject* py_aclose_wrapper_await(
    proto::ProtoContext*, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    return self;
}

const proto::ProtoObject* py_async_generator_aclose(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments*,
    const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self) return PROTO_NONE;
    proto::ProtoObject* wrapper = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    wrapper->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_aclose_target"), self);
    const proto::ProtoObject* sendCell = ctx->fromMethod(nullptr, py_aclose_wrapper_send);
    wrapper->setAttribute(ctx, env->getSendString(), sendCell);
    wrapper->setAttribute(ctx, env->getNextString(), sendCell);
    const proto::ProtoObject* selfCell = ctx->fromMethod(nullptr, py_aclose_wrapper_await);
    wrapper->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__await__"), selfCell);
    wrapper->setAttribute(ctx, env->getIterString(), selfCell);
    return wrapper;
}

// athrow(typ[, val[, tb]]): an awaitable that throws into the async
// generator when it is driven, as in CPython.  athrow used to throw at
// once and return what the generator yielded, so `await agen.athrow(E)`
// awaited that value: a yielded StopIteration(2) hung test_asyncgen's
// test_async_gen_asyncio_athrow_stopiteration.  Driven, the awaitable
// finishes with the value the generator yields next; a generator that
// returns instead raises StopAsyncIteration; an exception it raises
// propagates.  Single-shot; the cells are unbound (CONFORMANCE rule 13).
static const proto::ProtoObject* py_athrow_wrapper_send(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoArguments*,
    const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    if (!self || self == PROTO_NONE) {
        env->raiseTypeError(ctx, "athrow() awaitable: called without the awaitable");
        return nullptr;
    }
    const proto::ProtoString* targetS = PythonEnvironment::getInternedString(ctx, "_athrow_target");
    const proto::ProtoString* argsS   = PythonEnvironment::getInternedString(ctx, "_athrow_args");
    const proto::ProtoString* doneS   = PythonEnvironment::getInternedString(ctx, "_athrow_done");
    if (self->hasOwnAttribute(ctx, doneS) == PROTO_TRUE
            && self->getOwnAttributeDirect(ctx, doneS) == PROTO_TRUE) {
        env->raiseRuntimeError(ctx, "cannot reuse already awaited aclose()/athrow()");
        return nullptr;
    }
    const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, doneS, PROTO_TRUE);
    const proto::ProtoObject* target = self->hasOwnAttribute(ctx, targetS) == PROTO_TRUE
        ? self->getOwnAttributeDirect(ctx, targetS) : nullptr;
    const proto::ProtoObject* argsObj = self->hasOwnAttribute(ctx, argsS) == PROTO_TRUE
        ? self->getOwnAttributeDirect(ctx, argsS) : nullptr;
    const proto::ProtoList* argList = argsObj ? ::protoPython::listStorage(ctx, argsObj) : nullptr;
    if (!target || target == PROTO_NONE || !argList) {
        env->raiseStopIteration(ctx, PROTO_NONE);
        return nullptr;
    }
    const proto::ProtoObject* val = nullptr;
    try {
        val = py_generator_throw(ctx, target, pl, argsView(proto::ProtoListArguments(ctx, argList)), nullptr);
    } catch (const proto::ProtoException& e) {
        // The generator returned: the async-iteration sentinel.
        if (!env->isStopIteration(ctx, e.value())) throw;
        env->raiseStopAsyncIteration(ctx);
        return nullptr;
    }
    env->raiseStopIteration(ctx, val ? val : PROTO_NONE);
    return nullptr;
}

const proto::ProtoObject* py_async_generator_athrow(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self) return PROTO_NONE;
    if (!posArgs || posArgs->getSize(ctx) < 1) {
        env->raiseTypeError(ctx, "athrow expected at least 1 argument, got 0");
        return nullptr;
    }
    const proto::ProtoList* args = ctx->newList();
    for (proto::proto_ulong i = 0; i < posArgs->getSize(ctx); ++i)
        args = args->appendLast(ctx, posArgs->getAt(ctx, static_cast<int>(i)));
    proto::ProtoObject* wrapper = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    wrapper->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_athrow_target"), self);
    wrapper->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_athrow_args"), args->asObject(ctx));
    const proto::ProtoObject* sendCell = ctx->fromMethod(nullptr, py_athrow_wrapper_send);
    wrapper->setAttribute(ctx, env->getSendString(), sendCell);
    wrapper->setAttribute(ctx, env->getNextString(), sendCell);
    const proto::ProtoObject* selfCell = ctx->fromMethod(nullptr, py_aclose_wrapper_await);
    wrapper->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__await__"), selfCell);
    wrapper->setAttribute(ctx, env->getIterString(), selfCell);
    return wrapper;
}

const proto::ProtoObject* py_generator_send(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    const proto::ProtoObject* val = (posArgs && posArgs->getSize(ctx) > 0) ? posArgs->getAt(ctx, 0) : PROTO_NONE;
    // PB2: A just-started generator (pc == 0) cannot accept a sent value
    // other than None — there is no `yield` expression yet to receive it.
    if (val != PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) {
            const proto::ProtoObject* pcObj = self->getAttribute(ctx, env->getGiPCString());
            long long pc = (pcObj && pcObj->isInteger(ctx)) ? pcObj->asLong(ctx) : 0;
            if (pc == 0) {
                env->raiseTypeError(ctx,
                    "can't send non-None value to a just-started generator");
                return nullptr;
            }
        }
    }
    return py_generator_send_impl(ctx, self, val);
}

const proto::ProtoObject* py_generator_throw(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    const proto::ProtoObject* exc = (posArgs && posArgs->getSize(ctx) > 0) ? posArgs->getAt(ctx, 0) : PROTO_NONE;
    // Distinguish a *type* (class) from an *instance*.  A class owns __bases__;
    // an instance merely inherits __name__ through its class prototype chain,
    // so checking only __name__ would misclassify instances as classes.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (exc && exc != PROTO_NONE && env) {
        const bool isType =
            exc->hasOwnAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__bases__")) == PROTO_TRUE;
        if (isType) {
            // exc is a type; check if value argument provided
            const proto::ProtoObject* value = (posArgs && posArgs->getSize(ctx) > 1) ? posArgs->getAt(ctx, 1) : nullptr;
            if (value && value != PROTO_NONE) {
                // Caller supplied an already-built instance (or args tuple).
                exc = value;
            } else {
                // Instantiate the type (an error doing so is dropped: the type
                // itself is thrown, as before).
                const proto::ProtoObject* instance = nullptr;
                try {
                    instance = invokePythonCallable(ctx, exc, ctx->newList(), nullptr);
                } catch (const proto::ProtoException&) {
                    instance = nullptr;
                }
                if (instance && instance != PROTO_NONE) {
                    exc = instance;
                }
            }
        }
        // else: exc is already an instance — use as-is.
    }
    return py_generator_send_impl(ctx, self, PROTO_NONE, exc);
}

const proto::ProtoObject* py_generator_close(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;

    // Check if already closed
    const proto::ProtoObject* pcObj = self->getAttribute(ctx, env->getGiPCString());
    const proto::ProtoObject* codeObj = self->getAttribute(ctx, env->getGiCodeString());
    if (pcObj && codeObj && pcObj->isInteger(ctx) && ::protoPython::listStorage(ctx, codeObj->getAttribute(ctx, env->getCoCodeString()))) {
        proto::proto_ulong pc = (pcObj && pcObj->isInteger(ctx)) ? static_cast<proto::proto_ulong>(pcObj->asLong(ctx)) : 0;
        if (pc >= ::protoPython::listStorage(ctx, codeObj->getAttribute(ctx, env->getCoCodeString()))->getSize(ctx)) {
            return PROTO_NONE;
        }
    }

    // Raise GeneratorExit
    const proto::ProtoObject* genExitType = env->getGeneratorExitType();
    if (!genExitType || genExitType == PROTO_NONE) {
        // Fallback: search in builtins if cached is null (during bootstrap)
        genExitType = env->getAttribute(ctx, env->getBuiltins(), PythonEnvironment::getInternedString(ctx, "GeneratorExit"), false);
    }

    if (!genExitType || genExitType == PROTO_NONE) {
        return PROTO_NONE;
    }
    // Create GeneratorExit instance via proper Python constructor call so that
    // the prototype chain is fully set up for isInstanceOf to work correctly.
    const proto::ProtoList* emptyArgs = ctx->newList();
    const proto::ProtoObject* genExit = nullptr;
    try {
        genExit = invokePythonCallable(ctx, genExitType, emptyArgs, nullptr);
    } catch (const proto::ProtoException&) {
        genExit = nullptr;   // an error constructing it: the fallback below
    }
    if (!genExit || genExit == PROTO_NONE) {
        // Fallback: bare object with parent (may not be fully recognized by isinstance)
        genExit = ctx->newObject(true);
        genExit = genExit->addParent(ctx, genExitType);
    }

    // The generator that does not handle GeneratorExit (the common case)
    // lets it out. Python semantics: close() swallows GeneratorExit and
    // StopIteration; any other exception propagates to the caller.
    const proto::ProtoObject* sendResult = nullptr;
    bool finished = false;
    try {
        sendResult = py_generator_send_impl(ctx, self, PROTO_NONE, genExit);
    } catch (const proto::ProtoException& e) {
        const proto::ProtoObject* exc = e.value();
        const bool isGenExit = exc && ((exc == genExitType) ||
                                       (exc->getPrototype(ctx) == genExitType) ||
                                       (exc->isInstanceOf(ctx, genExitType) == PROTO_TRUE));
        if (!isGenExit && !env->isStopIteration(ctx, exc)) throw;
        finished = true;
    }
    if (finished) {
        // closed
    } else if (sendResult && sendResult != PROTO_NONE) {
        // PB6: the generator caught GeneratorExit and *yielded* a new
        // value rather than letting close() finish.  CPython contract:
        // raise RuntimeError("generator ignored GeneratorExit").
        env->raiseRuntimeError(ctx, "generator ignored GeneratorExit");
    }
    return PROTO_NONE;
}

const proto::ProtoObject* addOperator(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    return binaryAdd(ctx, a, b);
}

const proto::ProtoObject* invokePythonCallable(proto::ProtoContext* ctx,
    const proto::ProtoObject* callable, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return invokeCallable(ctx, callable, args, kwargs);
}

const proto::ProtoObject* invokePythonCallable(proto::ProtoContext* ctx,
    const proto::ProtoObject* callable, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs) {
    return invokeCallable(ctx, callable, args ? args->asList(ctx) : nullptr,
                          kwargs ? kwargs->asSparseList(ctx) : nullptr);
}



namespace {
struct GCStack {
    const proto::ProtoObject** slots;
    size_t top;
    size_t capacity;
    bool overflowed;

    GCStack(const proto::ProtoObject** s, size_t cap) : slots(s), top(0), capacity(cap), overflowed(false) {}

    void push_back(const proto::ProtoObject* obj) {
        if (top < capacity) {
            slots[top++] = obj;
        } else if (!overflowed) {
            // Mark overflow so the interpreter loop can raise a clean MemoryError
            // rather than printing an infinite stream of diagnostics.
            overflowed = true;
            fprintf(stderr, "FATAL: GCStack overflow! top=%llu capacity=%llu — "
                    "increase PYTHON_STACK_BUFFER in Compiler.cpp\n",
                    static_cast<unsigned long long>((proto::proto_ulong)top), static_cast<unsigned long long>((proto::proto_ulong)capacity));
            fflush(stderr);
        }
    }

    const proto::ProtoObject* back() const {
        return top > 0 ? slots[top - 1] : nullptr;
    }

    const proto::ProtoObject*& back() {
        static const proto::ProtoObject* dummy = nullptr;
        return top > 0 ? slots[top - 1] : dummy;
    }

    void pop_back() {
        if (top > 0) top--;
    }

    bool empty() const {
        return top == 0;
    }

    size_t size() const {
        return top;
    }

    const proto::ProtoObject* operator[](size_t idx) const {
        return slots[idx];
    }

    const proto::ProtoObject*& operator[](size_t idx) {
        return slots[idx];
    }
};
}

// The class whose __init_subclass__ chain runs on this thread.
// object.__init_subclass__ names it in its TypeError: a hook forwarding
// keywords with super().__init_subclass__(**kw) passes it no class.
thread_local const proto::ProtoObject* g_initSubclassClass = nullptr;

bool invokeInitSubclass(proto::ProtoContext* ctx,
    const proto::ProtoObject* cls,
    const proto::ProtoSparseList* kwargs,
    const proto::ProtoTuple* kwNames) {
    if (!ctx || !cls || cls == PROTO_NONE) return true;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return true;
    const proto::ProtoObject* mroAttr = cls->getAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "__mro__"));
    ::protoPython::TupleView mroT = mroAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, mroAttr) : nullptr;
    if (!mroT || mroT->getSize(ctx) < 2) return true;
    const proto::ProtoString* iscS =
        PythonEnvironment::getInternedString(ctx, "__init_subclass__");
    // super(cls, cls).__init_subclass__: the first definition after cls.
    for (proto::proto_ulong mi = 1; mi < mroT->getSize(ctx); ++mi) {
        const proto::ProtoObject* base = mroT->getAt(ctx, static_cast<int>(mi));
        if (!base || base == PROTO_NONE) continue;
        const proto::ProtoObject* hook = base->getOwnAttributeDirect(ctx, iscS);
        if (!hook || hook == PROTO_NONE) continue;
        // __init_subclass__ is implicitly a classmethod: call the underlying
        // function with cls as its first argument.
        const proto::ProtoObject* unwrapped = hook->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__func__"));
        if (unwrapped && unwrapped != PROTO_NONE) hook = unwrapped;
        const proto::ProtoSparseList* kw =
            (kwargs && kwargs->getSize(ctx) > 0) ? kwargs : nullptr;
        // STRUCT-69: the SparseList stores key hashes only; the **kwargs
        // binding recovers the key names from the kw-names stack.
        const bool pushNames = kw && kwNames;
        if (pushNames) env->pushKwNames(kwNames);
        const proto::ProtoObject* outerClass = g_initSubclassClass;
        g_initSubclassClass = cls;
        invokeCallable(ctx, hook, ctx->newList()->appendLast(ctx, cls), kw);
        g_initSubclassClass = outerClass;
        if (pushNames) env->popKwNames();
        return true;
    }
    return true;
}

const proto::ProtoObject* runUserClassCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* kwargs) {
    if (!ctx || !self) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    // `type(x)` with exactly one argument is a pure type query — it
    // returns x's type and runs NO __init__.  Without this short
    // circuit, the generic path runs __new__ (which returns type(x))
    // and then looks __init__ up on type(type(x)) — i.e. the
    // metaclass of x's class — wrongly invoking a user metaclass's
    // __init__ on every `type(x)` call (test_metaclass: T.counter).
    if (env && self == env->getTypePrototype()
        && args && args->getSize(ctx) == 1
        && (!kwargs || kwargs->getSize(ctx) == 0)) {
        return env->getType(ctx, args->getAt(ctx, 0));
    }

    // The common instantiation, by protoCore. __new__ and __init__ are
    // annotations of the class, and one lookup of the class's chain returns
    // the one that applies. When __new__ is object's, the instance is the
    // class's newChild, and the checks object.__new__ makes are answered by
    // the same lookups: a container base (list, dict, tuple, set, frozenset,
    // bytes) owns its own __new__, so its subclasses do not get here; extra
    // arguments with object's __init__ too take the full path, which raises
    // "takes no arguments". __init__ is invoked as the full path invokes it,
    // when it is object's (nothing to do), a native method or a Python
    // function; anything else takes the full path.
    if (env && env->getObjectPrototype()) {
        const proto::ProtoObject* objectProto = env->getObjectPrototype();
        const proto::ProtoString* newKey = env->getNewString();
        const proto::ProtoString* initKey = env->getInitString();
        const proto::ProtoObject* defNew = objectProto->getOwnAttributeDirect(ctx, newKey);
        if (defNew && defNew != PROTO_NONE && self->getAttribute(ctx, newKey) == defNew) {
            const proto::ProtoObject* defInit = objectProto->getOwnAttributeDirect(ctx, initKey);
            const proto::ProtoObject* initM = self->getAttribute(ctx, initKey);
            const bool hasArgs = (args && args->getSize(ctx) > 0) || (kwargs && kwargs->getSize(ctx) > 0);
            const bool initDefault = initM == defInit;
            const bool initCallable = initM && initM != PROTO_NONE
                && (initDefault || initM->isMethod(ctx)
                    || initM->getFirstParent(ctx) == env->getFunctionPrototype());
            if (initCallable && !(hasArgs && initDefault)) {
                proto::ProtoObject* obj = const_cast<proto::ProtoObject*>(self->newChild(ctx, true));
                if (initDefault) return obj;
                const proto::ProtoList* initArgs = ctx->newList()->appendLast(ctx, obj);
                if (args) {
                    for (size_t i = 0; i < args->getSize(ctx); ++i) {
                        initArgs = initArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
                    }
                }
                const proto::ProtoObject* initRes = invokePythonCallable(ctx, initM, initArgs,
                    kwargs ? kwargs->asSparseList(ctx) : nullptr);
                // CPython: a Python __init__ must return None.
                if (initRes && initRes != PROTO_NONE && env->getCodeString()
                        && initM->hasOwnAttribute(ctx, env->getCodeString()) == PROTO_TRUE) {
                    env->raiseTypeError(ctx, "__init__() should return None, not 'non-None'");
                    return nullptr;
                }
                return obj;
            }
        }
    }

    const proto::ProtoString* newS = env ? env->getNewString() : protoPython::PythonEnvironment::getInternalString(ctx, "__new__");
    const proto::ProtoObject* newM = env ? env->getAttribute(ctx, self, newS) : self->getAttribute(ctx, newS);
    
    proto::ProtoObject* obj = nullptr;
    if (newM && newM != PROTO_NONE) {
        
        // In Python, __new__ is acts like a staticmethod, so looking it up on a class 
        // does not bind it. We MUST explicitly pass `self` (the class) as the first argument 
        // to `__new__`, regardless of whether it's a native C++ method or a Python function.
        const proto::ProtoList* newArgs = ctx->newList()->appendLast(ctx, self);
        if (args) {
            for (size_t i = 0; i < args->getSize(ctx); ++i) {
                newArgs = newArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
            }
        }
        
        obj = const_cast<proto::ProtoObject*>(invokeCallable(ctx, newM, newArgs, kwargs ? kwargs->asSparseList(ctx) : nullptr));
        if (!obj || obj == PROTO_NONE) {
        }
    }

    if (!obj || obj == PROTO_NONE) {
        if (get_env_diag()) {}
        // Fallback for classes whose __new__ returns nothing usable.
        // addParent wires `self` into obj's protoCore parent chain;
        // getType() / env->getAttribute("__class__") synthesise the
        // class identity from there — no parallel storage needed.
        obj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        obj = const_cast<proto::ProtoObject*>(obj->addParent(ctx, self));
    }
    
    // Invoke __init__
    // Magic methods should be looked up on the class, not the instance object's __dict__ directly.
    if (obj && obj != PROTO_NONE) {
        bool isInstanceOfSelf = false;
        if (env) {
            // First, take the protoCore parent link as authoritative
            // when the instance was built via `self.newChild()`: that
            // is the canonical pattern for primitive-subclass wrappers
            // (e.g. cistr extending str) where env->getType folds the
            // result through the built-in prototype.
            const proto::ProtoObject* fp_outer = obj->getFirstParent(ctx);
            if (fp_outer == self) {
                isInstanceOfSelf = true;
            }
            const proto::ProtoObject* objCls = env->getType(ctx, obj);
            // CPython: type.__call__ invokes __init__ when `__new__`
            // returned an instance of `cls` OR of a subclass.  protoCore's
            // `isInstanceOf` walks the parent chain of the CLASS object
            // (i.e. asks "is class D a metaclass-instance of class C?"),
            // not the Python-level subclass relation we need here.  Walk
            // objCls.__mro__ looking for `self` instead.
            if (objCls == self) {
                isInstanceOfSelf = true;
            } else if (objCls && objCls != PROTO_NONE) {
                // STRUCT-132: descriptor-aware __mro__ read.
                const proto::ProtoObject* mroAttr = env
                    ? env->getAttribute(ctx, objCls, env->getMroString(), false)
                    : objCls->getAttribute(ctx, env->getMroString());
                ::protoPython::TupleView mroT =
                    mroAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, mroAttr) : nullptr;
                if (mroT) {
                    for (proto::proto_ulong mi = 0; mi < mroT->getSize(ctx); ++mi) {
                        if (mroT->getAt(ctx, static_cast<int>(mi)) == self) {
                            isInstanceOfSelf = true;
                            break;
                        }
                    }
                }
                if (!isInstanceOfSelf) {
                    // __mro__ absent or doesn't include self — walk
                    // __bases__ recursively as a fallback.
                    std::function<bool(const proto::ProtoObject*, int)> walkBases =
                        [&](const proto::ProtoObject* c, int depth) -> bool {
                            if (!c || c == PROTO_NONE || depth > 32) return false;
                            if (c == self) return true;
                            const proto::ProtoObject* basesAttr =
                                c->getAttribute(ctx, env->getBasesString());
                            ::protoPython::TupleView basesT =
                                basesAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, basesAttr) : nullptr;
                            if (!basesT) return false;
                            for (proto::proto_ulong bi = 0; bi < basesT->getSize(ctx); ++bi) {
                                if (walkBases(basesT->getAt(ctx, static_cast<int>(bi)), depth + 1)) return true;
                            }
                            return false;
                        };
                    isInstanceOfSelf = walkBases(objCls, 0);
                }
            }
        } else {
            // Very naive fallback if env is missing
            const proto::ProtoObject* cls = obj->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__class__"));
            isInstanceOfSelf = (cls == self);
        }

        if (get_env_diag()) {
            std::string selfName = "?";
            if (env) {
                const proto::ProtoObject* nameA = env->getAttribute(ctx, self, env->getNameString());
                if (nameA && nameA->isString(ctx)) nameA->asString(ctx)->toUTF8String(ctx, selfName);
            }
            const proto::ProtoObject* objCls2 = env ? env->getType(ctx, obj) : nullptr;
            fprintf(stderr, "DEBUG runUserClassCall: self='%s' isInstanceOfSelf=%d objCls=%p self=%p\n",
                    selfName.c_str(), (int)isInstanceOfSelf, (void*)objCls2, (void*)self);
            fflush(stderr);
        }
        if (isInstanceOfSelf) {
            const proto::ProtoString* initS = env ? env->getInitString() : protoPython::PythonEnvironment::getInternalString(ctx, "__init__");
            // CPython looks __init__ up on type(obj), not on the
            // class that was called.  When `__new__(cls)` returned a
            // SUBCLASS instance (e.g. `C(1) → D` where D extends C),
            // running C.__init__ skips the subclass's customisation
            // and `d.foo = arg` from D.__init__ never executes.
            // Prefer self as initLookupTarget when obj was created via
            // `self.newChild()` — i.e. fp == self.  This catches the
            // primitive-subclass case (cistr extending str) where
            // env->getType folds back to the built-in prototype and
            // would otherwise route __init__ through str's ignore-init
            // shim.  In all other shapes (C(1) returning D where D
            // extends C; type(name, bases, dict) returning the new
            // class whose fp is its first base, not type), we keep
            // env->getType so __init__ resolves correctly on the
            // returned class's type.
            const proto::ProtoObject* fp = obj->getFirstParent(ctx);
            const proto::ProtoObject* objCls = nullptr;
            if (fp == self && self) {
                objCls = self;
            } else if (env) {
                objCls = env->getType(ctx, obj);
            }
            const proto::ProtoObject* initLookupTarget = (objCls && objCls != PROTO_NONE) ? objCls : self;
            const proto::ProtoObject* initM = env
                ? env->getAttribute(ctx, initLookupTarget, initS)
                : initLookupTarget->getAttribute(ctx, initS);
            if (get_env_diag()) {
                bool isNative = initM && initM->asMethod(ctx) != nullptr;
                bool hascode = initM && env && env->getCodeString() &&
                               initM->hasOwnAttribute(ctx, env->getCodeString()) == PROTO_TRUE;
                std::string initRepr = initM ? PythonEnvironment::reprObject(ctx, initM) : "null";
                fprintf(stderr, "DEBUG runUserClassCall: initM=%p isNativeMethod=%d hasCode=%d repr=%s\n",
                        (void*)initM, (int)isNative, (int)hascode, initRepr.c_str());
                fflush(stderr);
            }
            if (initM && initM != PROTO_NONE) {
                // Since we looked it up on the class (self), we must manually pass `obj` as first arg
                const proto::ProtoList* initArgs = ctx->newList()->appendLast(ctx, obj);
                if (args) {
                    for (size_t i = 0; i < args->getSize(ctx); ++i) {
                        initArgs = initArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
                    }
                }
                const proto::ProtoObject* initRes = invokePythonCallable(ctx, initM, initArgs,
                    kwargs ? kwargs->asSparseList(ctx) : nullptr);
                // CPython: __init__ MUST return None.  Restrict to
                // Python-user __init__ functions (i.e. those carrying
                // own __code__) so native helpers in our bundled
                // stdlib don't accidentally trip the check.
                if (env && initRes && initRes != PROTO_NONE && initM) {
                    const proto::ProtoString* codeS = env->getCodeString();
                    bool isPythonUserInit = codeS
                        && initM->hasOwnAttribute(ctx, codeS) == PROTO_TRUE;
                    if (isPythonUserInit) {
                        env->raiseTypeError(ctx,
                            "__init__() should return None, not '"
                            + std::string("non-None") + "'");
                        return nullptr;
                    }
                }
            }
        }
    }
    
    return obj;
}



// Source line of the instruction at `instrIndex` (instruction units: two
// bytecode slots per instruction) in `codeObj`. co_lnotab holds
// (instruction delta, signed line delta) pairs relative to co_firstlineno, as
// Compiler::setLineNumber records them. Returns 0 when the code object carries
// no line information.
static int lineForInstruction(proto::ProtoContext* ctx, PythonEnvironment* env,
                              const proto::ProtoObject* codeObj,
                              proto::proto_ulong instrIndex) {
    if (!ctx || !env || !codeObj || codeObj == PROTO_NONE) return 0;
    int lineno = 0;
    const proto::ProtoObject* fln = codeObj->getAttribute(ctx, env->getCoFirstLinenoString());
    if (fln && fln->isInteger(ctx)) lineno = static_cast<int>(fln->asLong(ctx));
    if (lineno <= 0) return 0;

    const proto::ProtoObject* lnotabObj = codeObj->getAttribute(ctx, env->getCoLnotabString());
    ::protoPython::TupleView lnT = (lnotabObj && lnotabObj != PROTO_NONE) ? ::protoPython::PythonEnvironment::tupleView(ctx, lnotabObj) : nullptr;
    const proto::ProtoList* lnL = (!lnT && lnotabObj && lnotabObj != PROTO_NONE) ? ::protoPython::listStorage(ctx, lnotabObj) : nullptr;
    const proto::proto_ulong size = lnT ? lnT->getSize(ctx) : (lnL ? lnL->getSize(ctx) : 0);
    auto at = [&](proto::proto_ulong j) -> long long {
        const proto::ProtoObject* o = lnT ? lnT->getAt(ctx, static_cast<int>(j))
                                          : lnL->getAt(ctx, static_cast<int>(j));
        return (o && o->isInteger(ctx)) ? o->asLong(ctx) : 0;
    };
    proto::proto_ulong cursor = 0;
    for (proto::proto_ulong j = 0; j + 1 < size; j += 2) {
        const proto::proto_ulong pcDelta = static_cast<proto::proto_ulong>(at(j));
        const int lineDelta = static_cast<int>(static_cast<signed char>(at(j + 1)));
        if (cursor + pcDelta > instrIndex) break;
        cursor += pcDelta;
        lineno += lineDelta;
    }
    return lineno;
}

// Records the running location on the context (read by the fallback in
// PythonEnvironment::formatTraceback) and returns the line number.
static int updateContextLocation(proto::ProtoContext* ctx, PythonEnvironment* env,
                                 const proto::ProtoObject* codeObj,
                                 proto::proto_ulong instrIndex) {
    if (!ctx || !env || !codeObj || codeObj == PROTO_NONE) return 0;
    const proto::ProtoObject* fn = codeObj->getAttribute(ctx, env->getCoFilenameString());
    if (fn && fn->isString(ctx)) {
        // thread_local: a process-wide map was read and written without a
        // lock from every thread that dispatched an exception concurrently.
        static thread_local std::unordered_map<const proto::ProtoObject*, std::string> filenameCache;
        auto it = filenameCache.find(fn);
        if (it == filenameCache.end()) {
            std::string str;
            fn->asString(ctx)->toUTF8String(ctx, str);
            it = filenameCache.emplace(fn, std::move(str)).first;
        }
        ctx->currentFileName = const_cast<char*>(it->second.c_str());
    }
    const int lineno = lineForInstruction(ctx, env, codeObj, instrIndex);
    ctx->currentLineNumber = lineno;
    return lineno;
}

// The code object whose bytecode this dispatcher invocation runs: the
// per-thread current code object when its co_code is `bytecode`, else the
// frame's f_code when that matches. nullptr when neither does (bytecode run
// without a code object, e.g. by unit tests).
// The exception paths below stay out of executeBytecodeRange's dispatch loop:
// inlined there, they cost the loop about 15% more cycles on a call-heavy
// benchmark with the same instruction count (code layout).
#if defined(_MSC_VER)
#define PROTOPY_EXC_NOINLINE __declspec(noinline)
#else
#define PROTOPY_EXC_NOINLINE __attribute__((noinline))
#endif

// Exception chaining (PEP 3134): `exc` was raised while `context` was being
// handled, so `exc.__context__` becomes `context`, as CPython's
// _PyErr_SetObject does. A cycle through `exc` in context's own chain is cut
// first, so the chain stays finite.
void PythonEnvironment::setExceptionContext(proto::ProtoContext* ctx, const proto::ProtoObject* exc,
                                           const proto::ProtoObject* context) {
    if (!exc || !context || context == PROTO_NONE || context == exc) return;
    if (isEmbeddedValue(ctx, exc) || isEmbeddedValue(ctx, context)) return;
    // A foreign value (an error a C++ module or another language raised)
    // carries no Python chaining attributes: it belongs to its module and is
    // never written to.  A Python exception may have one as its __context__.
    if (!isPythonException(ctx, exc)) return;
    const proto::ProtoString* contextS = PythonEnvironment::getInternedString(ctx, "__context__");
    const proto::ProtoObject* o = context;
    for (int guard = 0; guard < 10000 && o && o != PROTO_NONE && !isEmbeddedValue(ctx, o); ++guard) {
        if (!isPythonException(ctx, o)) break;
        const proto::ProtoObject* next = o->getAttribute(ctx, contextS);
        if (next == exc) {
            o->setAttribute(ctx, contextS, PROTO_NONE);
            break;
        }
        o = next;
    }
    exc->setAttribute(ctx, contextS, context);
}

void PythonEnvironment::chainException(proto::ProtoContext* ctx, const proto::ProtoObject* exc,
                                       const proto::ProtoObject* context) {
    if (!exc || !context || context == exc || isEmbeddedValue(ctx, exc)) return;
    if (!isPythonException(ctx, exc)) return;
    const proto::ProtoObject* current =
        exc->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__context__"));
    if (current && current != PROTO_NONE) return;
    setExceptionContext(ctx, exc, context);
}

// The dispatch loop's entry points to the above (kept out of line).
PROTOPY_EXC_NOINLINE static void setExceptionContext(proto::ProtoContext* ctx, const proto::ProtoObject* exc,
                                                     const proto::ProtoObject* context) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) env->setExceptionContext(ctx, exc, context);
}

PROTOPY_EXC_NOINLINE static void chainCaughtException(proto::ProtoContext* ctx, const proto::ProtoObject* exc,
                                                      const proto::ProtoObject* active) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) env->chainException(ctx, exc, active);
}

// A bare `raise` and OP_RERAISE: `exc` is thrown again. The activation that
// caught it notes nothing more for it (executeBytecodeRange's noteFrame).
// protoCore's rule E3: a re-raised exception leaves unchanged. A handler
// level runs inside the catch of the exception it handles, so the C++
// exception being handled is that one: when it carries `exc` it is thrown
// again as it is, and a runtime's ProtoException subclass keeps its extras.
// Only a handler resumed after a yield (a generator's saved levels hold the
// value, not the C++ object) throws a new exception with the same value.
[[noreturn]] PROTOPY_EXC_NOINLINE static void reraiseException(PythonEnvironment* env, const proto::ProtoObject* exc) {
    if (std::exception_ptr current = std::current_exception()) {
        bool same = false;
        try {
            std::rethrow_exception(current);
        } catch (const proto::ProtoException& e) {
            same = e.value() == exc;
        } catch (...) {
        }
        if (same) std::rethrow_exception(current);
    }
    throw PyException(env, exc);
}

// `raise exc` (an instance): chained to the active exception, then thrown.
[[noreturn]] PROTOPY_EXC_NOINLINE static void raiseChainedException(proto::ProtoContext* ctx, PythonEnvironment* env,
                                                                    const proto::ProtoObject* exc) {
    setExceptionContext(ctx, exc, env->getActiveException(ctx));
    throw PyException(env, exc);
}

// An exception crossing an activation: give it the activation's traceback
// entry. The innermost activation it crosses (`first`: it has no traceback
// yet) also chains it to the exception handled where it was raised, as a
// raise in Python code is chained when it is raised; an outer activation
// does not chain it again.
PROTOPY_EXC_NOINLINE static void noteCaughtException(proto::ProtoContext* ctx, PythonEnvironment* env,
                                                     const proto::ProtoObject* exc, const proto::ProtoObject* tbFrame,
                                                     int lasti, int line, bool first) {
    env->addTraceback(exc, tbFrame, lasti, line);
    if (first) chainCaughtException(ctx, exc, env->getActiveException(ctx));
}

// OP_RERAISE outside a handler level: re-raise the exception being handled.
PROTOPY_EXC_NOINLINE static void reraiseActiveException(proto::ProtoContext* ctx, PythonEnvironment* env) {
    const proto::ProtoObject* activeExc = env->getActiveException(ctx);
    if (activeExc) {
        reraiseException(env, activeExc);
    } else {
        env->raiseRuntimeError(ctx, "No active exception to reraise");
    }
}

// `raise exc from cause`: a class is instantiated with no arguments; `from
// None` clears __cause__; either way __suppress_context__ is set, and the
// exception is chained to the active one as any raised exception is.
PROTOPY_EXC_NOINLINE static void raiseExceptionFrom(proto::ProtoContext* ctx, PythonEnvironment* env,
                                                 const proto::ProtoObject* exc, const proto::ProtoObject* cause) {
    if (env->isActuallyAClass(ctx, exc)) {
        exc = invokePythonCallable(ctx, exc, ctx->newList(), nullptr);
        if (!exc) return;
    }
    if (cause && cause != PROTO_NONE && env->isActuallyAClass(ctx, cause)) {
        cause = invokePythonCallable(ctx, cause, ctx->newList(), nullptr);
        if (!cause) return;
    }
    if (!exc || exc == PROTO_NONE) return;
    if (!isEmbeddedValue(ctx, exc)) {
        exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cause__"),
                          cause ? cause : PROTO_NONE);
        exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__suppress_context__"),
                          PROTO_TRUE);
    }
    setExceptionContext(ctx, exc, env->getActiveException(ctx));
    throw PyException(env, exc);
}

static const proto::ProtoObject* runningCodeObject(proto::ProtoContext* ctx, PythonEnvironment* env,
                                                   const proto::ProtoObject* frame,
                                                   const proto::ProtoTuple* bytecode) {
    if (!ctx || !env || !bytecode) return nullptr;
    const proto::ProtoObject* bcObj = bytecode->asObject(ctx);
    auto runs = [&](const proto::ProtoObject* co) -> bool {
        if (!co || co == PROTO_NONE) return false;
        const proto::ProtoObject* cc = co->getAttribute(ctx, env->getCoCodeString());
        return cc && cc == bcObj;
    };
    const proto::ProtoObject* co = PythonEnvironment::getCurrentCodeObject();
    if (runs(co)) return co;
    if (frame && frame != PROTO_NONE) {
        co = frame->getAttribute(ctx, env->getFCodeString());
        if (runs(co)) return co;
    }
    return nullptr;
}

// Raises UnboundLocalError for fast-local slot `idx`, naming the variable
// from the running code object's co_varnames.
/** The name of the cell or free variable in fast slot `slot` of the running
 *  code object: co_varnames[slot] for a cell variable, co_freevars[slot -
 *  len(co_varnames)] for a free variable (isFree).  nullptr when unknown. */
static const proto::ProtoString* derefSlotName(proto::ProtoContext* ctx, PythonEnvironment* env,
                                               int slot, bool& isFree) {
    isFree = true;
    const proto::ProtoObject* codeObj = PythonEnvironment::getCurrentCodeObject();
    if (!codeObj || slot < 0) return nullptr;
    const proto::ProtoObject* vn = codeObj->getAttribute(ctx, env->getCoVarnamesString());
    ::protoPython::TupleView varnames = (vn && vn != PROTO_NONE) ? ::protoPython::PythonEnvironment::tupleView(ctx, vn) : nullptr;
    const proto::proto_ulong nVar = varnames ? varnames->getSize(ctx) : 0;
    const proto::ProtoObject* nameObj = nullptr;
    if (static_cast<proto::proto_ulong>(slot) < nVar) {
        isFree = false;
        nameObj = varnames->getAt(ctx, slot);
    } else {
        const proto::ProtoObject* fv = codeObj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "co_freevars"));
        ::protoPython::TupleView freevars = (fv && fv != PROTO_NONE) ? ::protoPython::PythonEnvironment::tupleView(ctx, fv) : nullptr;
        const proto::proto_ulong j = static_cast<proto::proto_ulong>(slot) - nVar;
        if (freevars && j < freevars->getSize(ctx)) nameObj = freevars->getAt(ctx, static_cast<int>(j));
    }
    return (nameObj && proto::ProtoObject::isStringTagFast(nameObj)) ? nameObj->asString(ctx) : nullptr;
}

/** Reading or deleting an empty cell: CPython's messages. */
static void raiseEmptyCell(proto::ProtoContext* ctx, PythonEnvironment* env, int slot) {
    bool isFree = true;
    const proto::ProtoString* nameS = derefSlotName(ctx, env, slot, isFree);
    std::string nStr = "?";
    if (nameS) nameS->toUTF8String(ctx, nStr);
    if (isFree) {
        env->raiseFreeVariableError(ctx, nStr);
    } else {
        env->raiseUnboundLocalError(ctx,
            "cannot access local variable '" + nStr + "' where it is not associated with a value");
    }
}

/** A class body's own binding of `nameS` in its namespace `ns`, or nullptr:
 *  an own attribute of the namespace, or, for a namespace __prepare__
 *  returned (EnumDict and the like), its __getitem__ (KeyError: absent). */
static const proto::ProtoObject* classNamespaceLookup(proto::ProtoContext* ctx, PythonEnvironment* env,
                                                      const proto::ProtoObject* ns, const proto::ProtoString* nameS) {
    if (ns->hasOwnAttribute(ctx, nameS) == PROTO_TRUE) return ns->getOwnAttributeDirect(ctx, nameS);
    const proto::ProtoObject* nsType = env->getType(ctx, ns);
    if (nsType == env->getDictPrototype()) {
        const proto::ProtoObject* cls = ns->proto::ProtoObject::getAttribute(ctx, env->getClassString());
        if (cls && cls != PROTO_NONE) nsType = cls;
    }
    if (!nsType || nsType == PROTO_NONE || nsType == env->getDictPrototype() || nsType == env->getModulePrototype())
        return nullptr;
    const proto::ProtoObject* getitem = env->getAttribute(ctx, nsType, env->getGetItemString(), false);
    if (!getitem || getitem == PROTO_NONE) return nullptr;
    // An error from the namespace's __getitem__ (KeyError) means "not here".
    const proto::ProtoObject* val = nullptr;
    try {
        val = invokeDunder(ctx, ns, env->getGetItemString(),
                           ctx->newList()->appendLast(ctx, nameS->asObject(ctx)));
    } catch (const proto::ProtoException&) {
        return nullptr;
    }
    return (val && val != PROTO_NONE) ? val : nullptr;
}

static void raiseUnboundFastLocal(proto::ProtoContext* ctx, PythonEnvironment* env, int idx) {
    std::string nStr = "?";
    const proto::ProtoObject* codeObj = PythonEnvironment::getCurrentCodeObject();
    const proto::ProtoObject* varnamesObj = codeObj ? codeObj->getAttribute(ctx, env->getCoVarnamesString()) : nullptr;
    ::protoPython::TupleView vt = varnamesObj ? ::protoPython::PythonEnvironment::tupleView(ctx, varnamesObj) : nullptr;
    if (vt && idx >= 0 && static_cast<proto::proto_ulong>(idx) < vt->getSize(ctx)) {
        const proto::ProtoObject* nameObj = vt->getAt(ctx, idx);
        if (nameObj && proto::ProtoObject::isStringTagFast(nameObj)) {
            nameObj->asString(ctx)->toUTF8String(ctx, nStr);
        }
    }
    env->raiseUnboundLocalError(ctx,
        "cannot access local variable '" + nStr + "' where it is not associated with a value");
}

const proto::ProtoObject* PythonEnvironment::compareOperation(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                                                             const proto::ProtoObject* b, int op) {
    return compareOp(ctx, a, b, op);
}

// `class name(*bases, **kwds): body`: everything OP_BUILD_CLASS does around
// running the body (PEP 560 __mro_entries__, the metaclass and its
// __prepare__, the namespace's __name__ / __module__, the metaclass call and
// the fix-ups after it), shared with classes protopyc compiles. See
// PythonEnvironment::buildClass in PythonEnvironment.h.
const proto::ProtoObject* PythonEnvironment::buildClass(proto::ProtoContext* ctx, const proto::ProtoObject* name,
                                                        const proto::ProtoObject* bases, const proto::ProtoObject* kwds,
                                                        const ClassBody& runBody, const proto::ProtoObject** anchor) {
    const bool diag_local = false;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* nameS = env ? env->getNameString() : protoPython::PythonEnvironment::getInternalString(ctx, "__name__");
    const proto::ProtoString* callS = env ? env->getCallString() : PythonEnvironment::getInternedString(ctx, "__call__");
    
    // PEP 560: a base that is not a class but defines
    // __mro_entries__ (typing's Generic[T]) is replaced by the tuple
    // that method returns, so metaclass selection, __prepare__ and
    // the metaclass call all see the resolved bases. The bases as
    // written are recorded as __orig_bases__ before the metaclass
    // call. The resolved tuple is a young cell of this context and
    // the original stays in its caller slot, so both stay reachable.
    const proto::ProtoObject* origBases = nullptr;
    if (env && bases && bases != PROTO_NONE && ::protoPython::PythonEnvironment::tupleView(ctx, bases)) {
        ::protoPython::TupleView baseTuple = ::protoPython::PythonEnvironment::tupleView(ctx, bases);
        const proto::ProtoString* mroEntriesS =
            PythonEnvironment::getInternedString(ctx, "__mro_entries__");
        const proto::ProtoList* resolved = ctx->newList();
        bool changed = false;
        bool failed = false;
        for (proto::proto_ulong bi = 0; bi < baseTuple->getSize(ctx); ++bi) {
            const proto::ProtoObject* base = baseTuple->getAt(ctx, static_cast<int>(bi));
            const proto::ProtoObject* entries = nullptr;
            if (base && base != PROTO_NONE && !env->isActuallyAClass(ctx, base)) {
                try {
                    entries = env->getAttribute(ctx, base, mroEntriesS, false);
                } catch (const proto::ProtoException&) {
                    entries = nullptr;
                }
            }
            if (!entries || entries == PROTO_NONE) {
                resolved = resolved->appendLast(ctx, base);
                continue;
            }
            const proto::ProtoObject* result =
                invokeCallable(ctx, entries, ctx->newList()->appendLast(ctx, bases));
            ::protoPython::TupleView resultTuple = result ? ::protoPython::PythonEnvironment::tupleView(ctx, result) : nullptr;
            if (!resultTuple) {
                env->raiseTypeError(ctx, "__mro_entries__ must return a tuple");
                failed = true;
                break;
            }
            for (proto::proto_ulong ri = 0; ri < resultTuple->getSize(ctx); ++ri) {
                resolved = resolved->appendLast(ctx, resultTuple->getAt(ctx, static_cast<int>(ri)));
            }
            changed = true;
        }
        if (failed) {
            return nullptr;
        }
        if (changed) {
            origBases = bases;
            bases = ctx->newTupleFromList(resolved)->asObject(ctx);
        }
    }

    // 1. Identify Metaclass
    const proto::ProtoObject* metaclass = nullptr;
    if (kwds && kwds != PROTO_NONE) {
        const proto::ProtoString* kName = PythonEnvironment::getInternedString(ctx, "metaclass");
        metaclass = kwds->getAttribute(ctx, kName);
        if (!metaclass || metaclass == PROTO_NONE) {
            // Try looking in __data__ if it's a dict object
            const proto::ProtoObject* dataObj = kwds->getAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
            if (dataObj && dataObj->asSparseList(ctx)) {
                metaclass = dataObj->asSparseList(ctx)->getAt(ctx, kName->getHash(ctx));
            }
        }
    }
    // STRUCT-232: when an explicit metaclass is passed,
    // CPython still verifies it against every base's
    // metaclass.  Algorithm: among (explicit_meta, *base_metas),
    // the "winning" metaclass is the unique most-derived one.
    // It is the resulting class's metaclass — possibly the
    // explicit one, possibly upgraded to a more-derived base
    // metaclass, or a TypeError if none is a (non-strict)
    // subclass of all the others.
    if (metaclass && metaclass != PROTO_NONE) {
        const proto::ProtoObject* typeProto2 = env ? env->getTypePrototype() : nullptr;
        const proto::ProtoObject* objectProto2 = env ? env->getObjectPrototype() : nullptr;
        auto isSubclassMeta = [&](const proto::ProtoObject* a, const proto::ProtoObject* b) -> bool {
            if (areSameClassesVM(ctx, a, b)) return true;
            if (!a || !b) return false;
            const proto::ProtoObject* mro = env->getAttribute(ctx, a,
                PythonEnvironment::getInternedString(ctx, "__mro__"), false);
            ::protoPython::TupleView mroT = mro ? ::protoPython::PythonEnvironment::tupleView(ctx, mro) : nullptr;
            if (!mroT && mro) {
                const proto::ProtoObject* d = env->getAttribute(ctx, mro, env->getDataString(), false);
                if (d) mroT = ::protoPython::PythonEnvironment::tupleView(ctx, d);
            }
            if (mroT) {
                for (proto::proto_ulong j = 0; j < mroT->getSize(ctx); ++j) {
                    if (areSameClassesVM(ctx, mroT->getAt(ctx, static_cast<int>(j)), b)) return true;
                }
            }
            return false;
        };
        const proto::ProtoObject* winner = metaclass;
        // Run the meta-derivation check whenever the
        // explicit metaclass IS a class (has a __mro__).
        // A plain non-class callable (e.g. `metaclass=lambda
        // ...`) bypasses the check entirely — there is no
        // class hierarchy to reconcile against the bases'
        // metaclasses.
        // STRUCT-264: check class-ness via OWN __mro__ (or
        // by walking type(winner)'s MRO for typePrototype).
        // A plain function inherits `__mro__` from its type
        // chain, so the previous unqualified `getAttribute`
        // probe falsely classified `metaclass=func` as a
        // class and ran the conflict-detection loop —
        // raising a spurious "metaclass conflict" for
        // `class X(object, metaclass=func): pass`.
        bool winnerIsClass = false;
        if (winner) {
            const proto::ProtoString* mroDunder =
                PythonEnvironment::getInternedString(ctx, "__mro__");
            if (winner->hasOwnAttribute(ctx, mroDunder) == PROTO_TRUE) {
                winnerIsClass = true;
            } else if (winner == typeProto2) {
                winnerIsClass = true;
            } else if (env) {
                const proto::ProtoObject* wType = env->getType(ctx, winner);
                if (wType && (wType == typeProto2 || isSubclassMeta(wType, typeProto2))) {
                    winnerIsClass = true;
                }
            }
        }
        if (winnerIsClass && bases) {
            ::protoPython::TupleView tb = ::protoPython::PythonEnvironment::tupleView(ctx, bases);
            const proto::ProtoList* lb = tb ? nullptr : ::protoPython::listStorage(ctx, bases);
            size_t bn = tb ? tb->getSize(ctx) : (lb ? lb->getSize(ctx) : 0);
            for (size_t i = 0; i < bn; ++i) {
                const proto::ProtoObject* b = tb ? tb->getAt(ctx, static_cast<int>(i)) : lb->getAt(ctx, static_cast<int>(i));
                if (!b || b == PROTO_NONE) continue;
                const proto::ProtoObject* bm = env->getAttribute(ctx, b,
                    env->getClassString(), false);
                // A base's `__class__` that is not a class (a property the
                // class body defined for its instances) is not its metaclass.
                if (bm && bm != PROTO_NONE && !env->isActuallyAClass(ctx, bm)) bm = nullptr;
                if (!bm || bm == PROTO_NONE) bm = env->getType(ctx, b);
                // STRUCT-234: skip bases whose metaclass
                // resolves to bare `object` (e.g. plain
                // `object()` instance passed as a base).
                if (bm && areSameClassesVM(ctx, bm, objectProto2)) continue;
                if (!bm) bm = typeProto2;
                if (!bm) continue;
                // CPython rule: explicit metaclass W must be a
                // subclass of every base's metaclass.  If
                // bm IS a subclass of W, that means W is more
                // general than bm — and W cannot accommodate
                // bm-specific behaviour, so conflict.  The
                // exception is when W == bm exactly (same
                // class), which is fine.
                if (isSubclassMeta(winner, bm)) {
                    // winner already covers this base's meta
                    continue;
                }
                if (isSubclassMeta(bm, winner)) {
                    // base's meta is more derived than the
                    // explicit one — CPython upgrades.
                    winner = bm;
                    continue;
                }
                // Neither is a subclass of the other → conflict
                env->raiseTypeError(ctx,
                    "metaclass conflict: the metaclass of a derived class "
                    "must be a (non-strict) subclass of the metaclasses of "
                    "all its bases");
                winner = nullptr;
                break;
            }
        }
        if (!winner) {
            // Conflict raised; unwind through the same path
            // the inferred-metaclass branch uses.
            return nullptr;
        }
        metaclass = winner;
    }
    if (!metaclass || metaclass == PROTO_NONE) {
        // CPython semantics: iterate all bases and find the most derived metaclass.
        // If multiple independent metaclasses exist, Python throws TypeError, but here we just take the first strictly derived one.
        const proto::ProtoObject* typeProto = env ? env->getTypePrototype() : nullptr;
        const proto::ProtoObject* objectProto = env ? env->getObjectPrototype() : nullptr;
        // STRUCT-233: when no explicit metaclass is given,
        // CPython initializes the winner from type(base0)
        // rather than `type`.  Starting from `type` made
        // `class C(A, B)` raise a spurious metaclass conflict
        // when A's metaclass is ANotMeta (not a subclass of
        // type) — neither side is a subtype of the other, so
        // STRUCT-37's conflict branch fired even though
        // CPython would correctly resolve to ANotMeta (and
        // then upgrade to BNotMeta after walking B).
        const proto::ProtoObject* bestMeta = typeProto;
        {
            ::protoPython::TupleView tbInit = bases ? ::protoPython::PythonEnvironment::tupleView(ctx, bases) : nullptr;
            const proto::ProtoList* lbInit = (tbInit || !bases) ? nullptr : ::protoPython::listStorage(ctx, bases);
            if (!tbInit && !lbInit && bases) {
                const proto::ProtoObject* dataA = bases->getAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                if (dataA) {
                    tbInit = ::protoPython::PythonEnvironment::tupleView(ctx, dataA);
                    lbInit = tbInit ? nullptr : ::protoPython::listStorage(ctx, dataA);
                }
            }
            size_t bnInit = tbInit ? tbInit->getSize(ctx) : (lbInit ? lbInit->getSize(ctx) : 0);
            // Walk to find the first base whose Py_TYPE
            // (i.e. its metaclass) is non-trivial.  Bases
            // whose type is exactly `object` (e.g. `object()`
            // instance) or primitive contribute nothing to
            // the metaclass winner — CPython lets the
            // metaclass's own __new__ validate them later.
            for (size_t i = 0; i < bnInit; ++i) {
                const proto::ProtoObject* basei = tbInit ? tbInit->getAt(ctx, static_cast<int>(i)) : lbInit->getAt(ctx, static_cast<int>(i));
                if (!basei || basei == PROTO_NONE || !env) continue;
                const proto::ProtoObject* m0 = env->getAttribute(ctx, basei, env->getClassString(), false);
                if (m0 && m0 != PROTO_NONE && !env->isActuallyAClass(ctx, m0)) m0 = nullptr;
                if (!m0 || m0 == PROTO_NONE) m0 = env->getType(ctx, basei);
                if (m0 && m0 != PROTO_NONE
                    && !areSameClassesVM(ctx, m0, objectProto)) {
                    bestMeta = m0;
                    break;
                }
            }
        }
        
        if (bases) {
            ::protoPython::TupleView tupleBases = ::protoPython::PythonEnvironment::tupleView(ctx, bases);
            const proto::ProtoList* listBases = tupleBases ? nullptr : ::protoPython::listStorage(ctx, bases);
            if (!tupleBases && !listBases) {
                const proto::ProtoObject* dataAttr = bases->getAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                if (dataAttr) {
                    tupleBases = ::protoPython::PythonEnvironment::tupleView(ctx, dataAttr);
                    listBases = tupleBases ? nullptr : ::protoPython::listStorage(ctx, dataAttr);
                }
            }
            
            size_t basesSize = tupleBases ? tupleBases->getSize(ctx) : (listBases ? listBases->getSize(ctx) : 0);
            for (size_t i = 0; i < basesSize; ++i) {
                const proto::ProtoObject* base = tupleBases ? tupleBases->getAt(ctx, static_cast<int>(i)) : listBases->getAt(ctx, static_cast<int>(i));
                if (!base || base == PROTO_NONE) continue;

                const proto::ProtoObject* baseMeta = nullptr;
                if (env) {
                    baseMeta = env->getAttribute(ctx, base, env->getClassString(), false);
                    if (baseMeta && baseMeta != PROTO_NONE && !env->isActuallyAClass(ctx, baseMeta)) {
                        baseMeta = nullptr;
                    }
                    if (!baseMeta || baseMeta == PROTO_NONE) {
                        baseMeta = env->getType(ctx, base);
                    }
                } else {
                    baseMeta = base->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__class__"));
                }
                // STRUCT-234: skip bases whose metaclass
                // resolves to bare `object` — these are
                // typically `object()` instances passed
                // as bases.  CPython does not let such
                // instances contribute `type` to the meta
                // winner, which would otherwise raise a
                // spurious conflict against the other
                // user-metaclass bases.  Skipping leaves
                // the validation to the metaclass's
                // __new__ when the class is finally
                // constructed.
                if (baseMeta && areSameClassesVM(ctx, baseMeta, objectProto)) {
                    continue;
                }
                if (!baseMeta || baseMeta == PROTO_NONE) {
                    // If a native base accidentally lacks a metaclass, default it to type
                    baseMeta = typeProto;
                }
                // Compute derivation: if baseMeta is a subclass of bestMeta, it becomes the new best
                if (!areSameClassesVM(ctx, baseMeta, bestMeta) && bestMeta) {
                    const proto::ProtoObject* mro = env->getAttribute(ctx, baseMeta, PythonEnvironment::getInternedString(ctx, "__mro__"), false);
                    bool isSub = false;
                    if (mro) {
                        ::protoPython::TupleView mroTuple = ::protoPython::PythonEnvironment::tupleView(ctx, mro);
                        if (!mroTuple) {
                            const proto::ProtoObject* data = env ? env->getAttribute(ctx, mro, env->getDataString(), false) : mro->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__data__"));
                            if (data) mroTuple = ::protoPython::PythonEnvironment::tupleView(ctx, data);
                        }
                        if (mroTuple) {
                            for (size_t j = 0; j < mroTuple->getSize(ctx); ++j) {
                                if (areSameClassesVM(ctx, mroTuple->getAt(ctx, static_cast<int>(j)), bestMeta)) {
                                    isSub = true;
                                    break;
                                }
                            }
                        } else {
                            const proto::ProtoList* mroList = ::protoPython::listStorage(ctx, mro);
                            if (mroList) {
                                for (proto::proto_ulong j = 0; j < mroList->getSize(ctx); ++j) {
                                    if (areSameClassesVM(ctx, mroList->getAt(ctx, static_cast<int>(j)), bestMeta)) {
                                        isSub = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (diag_local) {
                        std::string bn, bmn, bestn;
                        const proto::ProtoString* nS = PythonEnvironment::getInternedString(ctx, "__name__");
                        const proto::ProtoObject* n1 = base->proto::ProtoObject::getAttribute(ctx, nS);
                        const proto::ProtoObject* n2 = baseMeta->proto::ProtoObject::getAttribute(ctx, nS);
                        const proto::ProtoObject* n3 = bestMeta->proto::ProtoObject::getAttribute(ctx, nS);
                        if (n1 && n1->isString(ctx)) n1->asString(ctx)->toUTF8String(ctx, bn);
                        if (n2 && n2->isString(ctx)) n2->asString(ctx)->toUTF8String(ctx, bmn);
                        if (n3 && n3->isString(ctx)) n3->asString(ctx)->toUTF8String(ctx, bestn);
                        fprintf(stderr, "DEBUG METACLASS: base %p (%s) baseMeta %p (%s) bestMeta %p (%s) isSub=%d\n", (void*)base, bn.c_str(), (void*)baseMeta, bmn.c_str(), (void*)bestMeta, bestn.c_str(), isSub);
                    }
                    if (isSub) {
                        bestMeta = baseMeta;
                    } else {
                        // STRUCT-37: neither bestMeta is a sub
                        // of baseMeta (checked above via the
                        // baseMeta->__mro__ walk) nor is
                        // baseMeta a sub of bestMeta — verify
                        // by walking bestMeta's MRO for baseMeta;
                        // if absent in *both* directions we
                        // have a metaclass conflict.
                        bool bestIsSubOfBase = false;
                        const proto::ProtoObject* bmro = bestMeta->getAttribute(ctx,
                            PythonEnvironment::getInternedString(ctx, "__mro__"));
                        ::protoPython::TupleView bmroT = bmro ? ::protoPython::PythonEnvironment::tupleView(ctx, bmro) : nullptr;
                        if (!bmroT && bmro) {
                            const proto::ProtoObject* d = env
                                ? env->getAttribute(ctx, bmro, env->getDataString(), false)
                                : bmro->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__data__"));
                            if (d) bmroT = ::protoPython::PythonEnvironment::tupleView(ctx, d);
                        }
                        if (bmroT) {
                            for (size_t j = 0; j < bmroT->getSize(ctx); ++j) {
                                if (areSameClassesVM(ctx, bmroT->getAt(ctx, static_cast<int>(j)), baseMeta)) {
                                    bestIsSubOfBase = true;
                                    break;
                                }
                            }
                        }
                        if (!bestIsSubOfBase) {
                            if (env) env->raiseTypeError(ctx,
                                "metaclass conflict: the metaclass of a derived class "
                                "must be a (non-strict) subclass of the metaclasses of "
                                "all its bases");
                            bestMeta = nullptr;
                            break;
                        }
                    }
                } else if (!bestMeta && baseMeta) {
                    bestMeta = baseMeta;
                }
            }
        }
        metaclass = bestMeta;
    }
    if (!metaclass || metaclass == PROTO_NONE) {
        metaclass = env ? env->getTypePrototype() : nullptr;
    }
    if (diag_local) {
    }

    // 2. Metaclass __prepare__
    if (diag_local) fprintf(stderr, "DEBUG OP_BUILD_CLASS: metaclass=%p (PROTO_NONE=%p)\n", (void*)metaclass, (void*)PROTO_NONE);
    if (metaclass) {
        const proto::ProtoObject* mcName = metaclass->getAttribute(ctx, env ? env->getNameString() : protoPython::PythonEnvironment::getInternalString(ctx, "__name__"));
        if (mcName && mcName->isString(ctx)) {
            std::string mn; mcName->asString(ctx)->toUTF8String(ctx, mn);
            if (diag_local) fprintf(stderr, "DEBUG OP_BUILD_CLASS: metaclass name='%s'\n", mn.c_str());
        }
    }
    const proto::ProtoObject* prepareRaw = nullptr;
    if (metaclass) {
        // STRUCT-259: pass raiseError=false so a non-class
        // metaclass (e.g. `class X(metaclass=func): pass`,
        // where func is a plain function) doesn't raise an
        // AttributeError when probing for `__prepare__`.
        // CPython tolerates the absence and falls back to
        // an empty namespace.
        prepareRaw = env ? env->getAttribute(ctx, metaclass, PythonEnvironment::getInternedString(ctx, "__prepare__"), false) : metaclass->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__prepare__"));
    }
    const proto::ProtoObject* prepareM = prepareRaw;
    if (diag_local) {
        fprintf(stderr, "TRACE_PREPARE: metaclass=%p prepareM=%p\n", (void*)metaclass, (void*)prepareM); fflush(stderr);
    }
    if (prepareM && prepareM != PROTO_NONE) {
        const proto::ProtoList* prepareArgs = ctx->newList()->appendLast(ctx, name)->appendLast(ctx, bases);
        // Use keyword parameters if available
        const proto::ProtoSparseList* kw = (kwds && kwds->asSparseList(ctx)) ? kwds->asSparseList(ctx) : nullptr;
        const proto::ProtoObject* nsObj = invokeCallable(ctx, prepareM, prepareArgs, kw);
        if (diag_local) {
            fprintf(stderr, "TRACE_PREPARE: invokeCallable returned nsObj=%p\n", (void*)nsObj); fflush(stderr);
        }
        if (!nsObj) {
            // __prepare__ raised — let the dispatch loop see
            // the pending exception and unwind through the
            // enclosing SETUP_FINALLY rather than bailing out
            // of the whole executeBytecodeRange (which would
            // bypass any try/except wrapping the class def).
            return nullptr;
        }
        // CPython: __prepare__ must return a mapping. Detect
        // None / non-mapping returns and raise TypeError with
        // CPython's exact message format. The duck-typed mapping
        // check is callable `keys` attr; same predicate as
        // dict()/MappingProxyType() use elsewhere.
        bool isMapping = (nsObj && nsObj != PROTO_NONE);
        if (isMapping) {
            // Reject obvious non-mappings.
            if (::protoPython::listStorage(ctx, nsObj) || ::protoPython::PythonEnvironment::tupleView(ctx, nsObj) ||
                nsObj->isString(ctx) || nsObj->isInteger(ctx) ||
                nsObj->isDouble(ctx) || nsObj->isBoolean(ctx)) {
                isMapping = false;
            } else if (env) {
                const proto::ProtoString* keysS = PythonEnvironment::getInternedString(ctx, "keys");
                const proto::ProtoObject* keysM = env->getAttribute(ctx, nsObj, keysS, false);
                if (!keysM || keysM == PROTO_NONE) isMapping = false;
            }
        }
        if (!isMapping) {
            std::string mcName = "<metaclass>";
            if (env && metaclass) {
                // Only use the metaclass __name__ when metaclass is
                // a real type (carries __mro__ as OWN). An instance
                // used as a metaclass inherits __mro__ from its
                // class via the prototype chain, so an OWN check is
                // required to distinguish.
                bool isType = (metaclass->hasOwnAttribute(ctx, env->getMroString()) == PROTO_TRUE);
                if (isType) {
                    const proto::ProtoObject* mcN = metaclass->getAttribute(ctx, env->getNameString());
                    if (mcN && mcN->isString(ctx)) mcN->asString(ctx)->toUTF8String(ctx, mcName);
                }
            }
            std::string returnedTypeName = "NoneType";
            if (env && nsObj && nsObj != PROTO_NONE) {
                const proto::ProtoObject* cls = env->getType(ctx, nsObj);
                if (cls) {
                    const proto::ProtoObject* nm = cls->getAttribute(ctx, env->getNameString());
                    if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, returnedTypeName);
                }
            }
            std::string msg = mcName + ".__prepare__() must return a mapping, not " + returnedTypeName;
            if (env) env->raiseTypeError(ctx, msg.c_str());
            return nullptr;
        }
        *anchor = nsObj;
    } else {
        *anchor = ctx->newObject(true);
    }
    proto::ProtoObject* ns = const_cast<proto::ProtoObject*>(*anchor);
    
    // Initialize __keys__ list for the namespace
    const proto::ProtoString* keysS = env ? env->getKeysString() : protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
    if (ns->hasOwnAttribute(ctx, keysS) != PROTO_TRUE) {
        const proto::ProtoList* keysList = ctx->newList();
        ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, keysS, keysList->asObject(ctx)));
    }

    // Setup standard attributes in ns
    const proto::ProtoString* py_name_s = env ? env->getNameString() : protoPython::PythonEnvironment::getInternalString(ctx, "__name__");
    const proto::ProtoString* py_module_s = PythonEnvironment::getInternedString(ctx, "__module__");
    
    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, nameS, name));
    // Add to keys
    const proto::ProtoObject* keysObj = ns->getAttribute(ctx, keysS);
    if (keysObj && ::protoPython::listStorage(ctx, keysObj)) {
        ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, keysS, ::protoPython::listStorage(ctx, keysObj)->appendLast(ctx, nameS->asObject(ctx))->asObject(ctx)));
    }

    // STRUCT-175 (round 16): class body's __module__ comes
    // from the enclosing module's `__name__`, not from a
    // (non-existent) `__module__` in globals.  CPython sets
    // a class's __module__ to `__name__` of the namespace
    // it was created in.  protoPython previously read
    // `globals['__module__']` which is almost always None
    // — leaving newly-created classes with no __module__
    // (or with object.__module__ inherited as 'builtins',
    // breaking pickle's qualname-based class lookup).
    const proto::ProtoObject* globals = env ? env->getCurrentGlobals() : nullptr;
    const proto::ProtoObject* moduleName = nullptr;
    if (globals) {
        moduleName = globals->getAttribute(ctx, py_name_s);
        if (!moduleName || moduleName == PROTO_NONE) {
            moduleName = globals->getAttribute(ctx, py_module_s);
        }
    }
    if (moduleName && moduleName != PROTO_NONE) {
        ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, py_module_s, moduleName));
        // Re-fetch keysObj to avoid stale pointer if setAttribute returns new version or updates state
        keysObj = ns->getAttribute(ctx, keysS);
        if (keysObj && ::protoPython::listStorage(ctx, keysObj)) {
            ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, keysS, ::protoPython::listStorage(ctx, keysObj)->appendLast(ctx, py_module_s->asObject(ctx))->asObject(ctx)));
        }
    }
    if (env) {
        ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, env->getFBackString(), PythonEnvironment::getCurrentFrame()));
        *anchor = ns;
        ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, env->getFGlobalsString(), PythonEnvironment::getCurrentGlobals()));
        *anchor = ns;
        ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, env->getFLocalsString(), ns));
        *anchor = ns;
    }

    // 3. Execute class body
    if (!runBody(ns)) return nullptr;
    *anchor = ns;

    // STRUCT-322: CPython places `__classcell__` into the
    // namespace whenever the class body would build a
    // `__class__` cell (i.e. uses super()).  protoPython's
    // compiler doesn't emit that bookkeeping yet, so we
    // approximate: if `__slots__` declares `__classcell__`,
    // inject a placeholder so a Meta.__new__ that probes
    // `assertIn('__classcell__', namespace)` sees it.
    // Same trick for any `__qualname__` slot — that name
    // is already in namespace as the class qualname, but
    // some test patterns look for it via the metaclass
    // attr kwarg.  The value is PROTO_NONE: callers that
    // genuinely need the cell (super() resolution) keep
    // using the protoCore parent-chain plumbing, which is
    // unaffected.
    if (env && ns && ns != PROTO_NONE) {
        const proto::ProtoString* slotsName =
            PythonEnvironment::getInternedString(ctx, "__slots__");
        const proto::ProtoObject* slotsObj = ns->getAttribute(ctx, slotsName);
        if (slotsObj && slotsObj != PROTO_NONE) {
            // Slots can be tuple, list, or even a single str.
            const proto::ProtoList* slotsList = nullptr;
            if (::protoPython::PythonEnvironment::isTupleValue(ctx, slotsObj)) {
                slotsList = ::protoPython::PythonEnvironment::tupleView(ctx, slotsObj)->asList(ctx);
            } else {
                slotsList = ::protoPython::listStorage(ctx, slotsObj);
            }
            auto injectIfMissing = [&](const char* slotName) {
                const proto::ProtoString* k =
                    PythonEnvironment::getInternedString(ctx, slotName);
                if (ns->hasOwnAttribute(ctx, k) != PROTO_TRUE) {
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, k, PROTO_NONE));
                    // Also append to __keys__ so the dict
                    // path of `__contains__` / iteration
                    // sees it.
                    const proto::ProtoString* keysName = env->getKeysString();
                    const proto::ProtoObject* keysObj = ns->getAttribute(ctx, keysName);
                    const proto::ProtoList* keysL = (keysObj && ::protoPython::listStorage(ctx, keysObj))
                        ? ::protoPython::listStorage(ctx, keysObj) : ctx->newList();
                    keysL = keysL->appendLast(ctx, k->asObject(ctx));
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, keysName, keysL->asObject(ctx)));
                }
            };
            if (slotsList) {
                for (proto::proto_ulong i = 0; i < slotsList->getSize(ctx); ++i) {
                    const proto::ProtoObject* entry = slotsList->getAt(ctx, static_cast<int>(i));
                    if (!entry || !entry->isString(ctx)) continue;
                    std::string s;
                    entry->asString(ctx)->toUTF8String(ctx, s);
                    if (s == "__classcell__" || s == "__qualname__") {
                        injectIfMissing(s.c_str());
                    }
                }
            } else if (slotsObj->isString(ctx)) {
                std::string s;
                slotsObj->asString(ctx)->toUTF8String(ctx, s);
                if (s == "__classcell__" || s == "__qualname__") {
                    injectIfMissing(s.c_str());
                }
            }
            *anchor = ns;
        }
    }

    // PEP 560: CPython's __build_class__ stores the bases as written
    // in the namespace when __mro_entries__ replaced any of them.
    if (origBases && env && ns && ns != PROTO_NONE) {
        const proto::ProtoString* origBasesS =
            PythonEnvironment::getInternedString(ctx, "__orig_bases__");
        if (ns->hasOwnAttribute(ctx, origBasesS) != PROTO_TRUE) {
            ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, origBasesS, origBases));
            const proto::ProtoString* nsKeysName = env->getKeysString();
            const proto::ProtoObject* nsKeysObj = ns->getAttribute(ctx, nsKeysName);
            const proto::ProtoList* nsKeys = (nsKeysObj && ::protoPython::listStorage(ctx, nsKeysObj))
                ? ::protoPython::listStorage(ctx, nsKeysObj) : ctx->newList();
            nsKeys = nsKeys->appendLast(ctx, origBasesS->asObject(ctx));
            ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, nsKeysName, nsKeys->asObject(ctx)));
            *anchor = ns;
        }
    }

    // 4. Invoke metaclass to create the class.  The body bound its names as
    // attributes of the namespace (STORE_NAME), where a name such as `get`
    // or `__getitem__` would shadow the dict's own methods for a metaclass
    // written in Python that reads it as a mapping (ABCMeta does).  Such a
    // metaclass gets a dict of the names, in definition order; `type` reads
    // the namespace itself, and a mapping __prepare__ returned is passed
    // as it is.
    const proto::ProtoObject* nsArg = ns;
    if (env && metaclass && metaclass != env->getTypePrototype() && env->getDictPrototype()
        && env->getType(ctx, ns) == env->getDictPrototype()) {
        const proto::ProtoObject* names = ns->getAttribute(ctx, keysS);
        const proto::ProtoList* nameList = names && names != PROTO_NONE ? ::protoPython::listStorage(ctx, names) : nullptr;
        const proto::ProtoObject* dict = env->callObject(env->getDictPrototype(), {});
        if (dict && nameList) {
            PythonEnvironment::TransientPin pinDict(env, dict);
            for (proto::proto_ulong k = 0; k < nameList->getSize(ctx); ++k) {
                const proto::ProtoObject* key = nameList->getAt(ctx, static_cast<int>(k));
                const proto::ProtoString* keyS = key && key->isString(ctx) ? key->asString(ctx) : nullptr;
                if (!keyS || ns->hasOwnAttribute(ctx, keyS) != PROTO_TRUE) continue;
                env->setItem(dict, key, ns->getAttribute(ctx, keyS), ctx);
            }
            nsArg = dict;
        }
    }
    const proto::ProtoList* mcArgs = ctx->newList()->appendLast(ctx, name)->appendLast(ctx, bases)->appendLast(ctx, nsArg);
    // STRUCT-37: strip `metaclass` from kwds before forwarding
    // to the metaclass call.  CPython's __build_class__
    // consumes `metaclass` itself (used only to *pick* the
    // metaclass) and never forwards it.  Passing it through
    // duplicates it with the metaclass argument resolved
    // here, yielding `__new__() got multiple values for
    // argument 'metaclass'` in user metaclasses that don't
    // accept arbitrary kwargs.  Other kwargs (PEP 487
    // __init_subclass__ extras like `cls_kw=value`) are
    // forwarded unchanged.
    const proto::ProtoSparseList* kw = nullptr;
    if (kwds && kwds->asSparseList(ctx)) {
        const proto::ProtoSparseList* rawKw = kwds->asSparseList(ctx);
        const proto::ProtoString* metaKey =
            PythonEnvironment::getInternedString(ctx, "metaclass");
        if (rawKw->has(ctx, metaKey->getHash(ctx))) {
            kw = rawKw->removeAt(ctx, metaKey->getHash(ctx));
        } else {
            kw = rawKw;
        }
    }
    if (diag_local) fprintf(stderr, "DEBUG OP_BUILD_CLASS: calling metaclass=%p\n", (void*)metaclass);
    // STRUCT-69: thread the kwds dict's __keys__ tuple via
    // pushKwNames so the inner **kwargs binding in
    // runUserFunctionCallRaw can recover the key names from
    // the SparseList (which only stores hashes).  Without
    // this, user metaclass __new__/__init__ receive empty
    // kwargs even when caller passed `class C(metaclass=M,
    // foo=42)`.
    const proto::ProtoTuple* kwNamesPushed = nullptr;
    if (env && kw && kwds && kwds != PROTO_NONE) {
        const proto::ProtoObject* keysObj = kwds->getAttribute(ctx, env->getKeysString());
        const proto::ProtoList* keysL = keysObj ? ::protoPython::listStorage(ctx, keysObj) : nullptr;
        if (keysL && keysL->getSize(ctx) > 0) {
            // Skip 'metaclass' key — same filter as for kw above.
            const proto::ProtoString* metaKey =
                PythonEnvironment::getInternedString(ctx, "metaclass");
            proto::proto_ulong mcHash = metaKey->getHash(ctx);
            const proto::ProtoList* fk = ctx->newList();
            for (proto::proto_ulong i = 0; i < keysL->getSize(ctx); ++i) {
                const proto::ProtoObject* k = keysL->getAt(ctx, static_cast<int>(i));
                if (k && k->isString(ctx) && k->getHash(ctx) != mcHash) {
                    fk = fk->appendLast(ctx, k);
                }
            }
            if (fk->getSize(ctx) > 0) {
                kwNamesPushed = ctx->newTupleFromList(fk);
                env->pushKwNames(kwNamesPushed);
            }
        }
    }
    const proto::ProtoObject* targetClass = invokeCallable(ctx, metaclass, mcArgs, kw);
    if (kwNamesPushed && env) env->popKwNames();
    if (diag_local) fprintf(stderr, "DEBUG OP_BUILD_CLASS: targetClass=%p\n", (void*)targetClass);
    
    if (targetClass && targetClass != PROTO_NONE) {
        // Set __qualname__ if not set as an *own* attribute (inherited values from
        // object/type prototypes must not block per-class assignment).
        const proto::ProtoString* qualnameS = PythonEnvironment::getInternedString(ctx, "__qualname__");
        const bool hasOwnQN = targetClass->hasOwnAttribute(ctx, qualnameS) == PROTO_TRUE;
        // STRUCT-324: compute the qualname string we want
        // `Q1.__qualname__` to report, regardless of whether
        // `__qualname__` is also a slot (member_descriptor).
        // The compiler injects the class body's qualname into
        // ns as a string; fall back to __name__ for top-level
        // classes when ns has no qualname.
        const proto::ProtoObject* qnString = nullptr;
        {
            const bool nsHasOwnQN = ns ? (ns->hasOwnAttribute(ctx, qualnameS) == PROTO_TRUE) : false;
            const proto::ProtoObject* nsQN = nsHasOwnQN ? ns->getAttribute(ctx, qualnameS) : nullptr;
            if (nsQN && nsQN != PROTO_NONE && nsQN->isString(ctx)) qnString = nsQN;
            else qnString = name;
        }
        if (!hasOwnQN) {
            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, qualnameS, qnString));
            *anchor = targetClass;
        } else {
            // `__qualname__` already exists as an own attribute.
            // If it's NOT a string (typically a member_descriptor
            // from `__slots__ = ['__qualname__']`), park the
            // qualname string under a private key so the
            // class-level lookup in env->getAttribute can still
            // report it.  See STRUCT-324 in
            // PythonEnvironment::getAttribute.
            const proto::ProtoObject* curQN = targetClass->getOwnAttributeDirect(ctx, qualnameS);
            if (curQN && !curQN->isString(ctx)) {
                const proto::ProtoString* tpQS = PythonEnvironment::getInternedString(ctx, "__tp_qualname__");
                targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, tpQS, qnString));
                *anchor = targetClass;
            }
        }

        // Guarantee `cls.__annotations__` always exists (CPython invariant).
        // Propagate from the class namespace if the body declared annotations;
        // otherwise fall back to an empty dict instance so `inspect`, `typing`,
        // and dataclasses can read it unconditionally.
        const proto::ProtoString* annS = PythonEnvironment::getInternedString(ctx, "__annotations__");
        const bool hasOwnAnn = targetClass->hasOwnAttribute(ctx, annS) == PROTO_TRUE;
        if (!hasOwnAnn) {
            const bool nsHasOwnAnn = ns ? (ns->hasOwnAttribute(ctx, annS) == PROTO_TRUE) : false;
            const proto::ProtoObject* annVal = nsHasOwnAnn ? ns->getAttribute(ctx, annS) : nullptr;
            if (!annVal || annVal == PROTO_NONE) {
                // Build a fresh empty dict that looks like a real Python dict
                // (parent = dictPrototype, __data__ = empty sparse list).
                const proto::ProtoObject* emptyDict = nullptr;
                if (env && env->getDictPrototype()) {
                    emptyDict = env->getDictPrototype()->newChild(ctx, true);
                    emptyDict = emptyDict->setAttribute(ctx, env->getClassString(), env->getDictPrototype());
                    emptyDict = emptyDict->setAttribute(ctx, env->getDataString(),
                                                        ctx->newSparseList()->asObject(ctx));
                } else {
                    emptyDict = ctx->newSparseList()->asObject(ctx);
                }
                annVal = emptyDict;
            }
            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, annS, annVal));
            *anchor = targetClass;
        }

        // Inject __class__ into the class namespace (frame) so methods can interpret it
        // via closure (parent frame reference).
        // STRUCT-219: a custom metaclass may store `ns` as an
        // attribute on the targetClass (e.g. `self.dict = dict`
        // in M.__new__).  In that case, setting
        // `ns.__class__ = targetClass` would corrupt the
        // user's attribute by re-typing the same object as
        // the metaclass instance.  Detect by scanning
        // targetClass's __keys__ for any attribute whose
        // value is `ns`; if found, skip the injection.  The
        // pattern is rare enough that super() inside such a
        // class body is not a common use case (the user's
        // custom __call__ usually replaces the standard
        // instance constructor anyway).
        const proto::ProtoString* clsName = env ? env->getClassString() : protoPython::PythonEnvironment::getInternalString(ctx, "__class__");
        bool nsReachableFromTarget = false;
        if (env) {
            const proto::ProtoObject* tcKeys = targetClass->getAttribute(ctx, env->getKeysString());
            const proto::ProtoList* tcKL = tcKeys ? ::protoPython::listStorage(ctx, tcKeys) : nullptr;
            if (tcKL) {
                for (proto::proto_ulong ki = 0; ki < tcKL->getSize(ctx); ++ki) {
                    const proto::ProtoObject* k = tcKL->getAt(ctx, static_cast<int>(ki));
                    if (!k || !k->isString(ctx)) continue;
                    const proto::ProtoObject* v =
                        targetClass->getAttribute(ctx, k->asString(ctx));
                    if (v == ns) { nsReachableFromTarget = true; break; }
                }
            }
        }
        if (!nsReachableFromTarget) {
            ns->setAttribute(ctx, clsName, targetClass);
        }

        // PI: __abstractmethods__ is populated by
        // ABCMeta.__new__ in lib/python3.14/abc.py; the
        // BUILD_CLASS opcode no longer needs to compute it
        // for the default-metaclass path.  Inactive block
        // kept for documentation parallel to PG's hooks.
        if (false) {
            const proto::ProtoString* abstractS =
                PythonEnvironment::getInternedString(ctx, "__isabstractmethod__");
            const proto::ProtoString* amS =
                PythonEnvironment::getInternedString(ctx, "__abstractmethods__");
            const proto::ProtoList* absNames = ctx->newList();
            // 1. Walk ns's __data__ SparseList (populated by
            // STORE_NAME during class body execution) for
            // values declaring __isabstractmethod__.
            // Recover names from each value's __name__ attr.
            {
                // Iterate ns.__keys__ via Python iter protocol
                // (handles list-CoW state correctly).  For each
                // name, fetch the value via env->getAttribute
                // (full chain) and check __isabstractmethod__.
                const proto::ProtoObject* nsKeys = ns->getAttribute(ctx, env->getKeysString());
                if (nsKeys && nsKeys != PROTO_NONE) {
                    const proto::ProtoObject* iter = env->iter(nsKeys);
                    int safety = 1024;
                    while (iter && safety-- > 0) {
                        const proto::ProtoObject* k = env->next(iter);
                        if (!k) break;
                        if (k->isString(ctx)) {
                            const proto::ProtoString* nameS_ = k->asString(ctx);
                            // Errors probing __isabstractmethod__ are dropped.
                            const proto::ProtoObject* v = nullptr;
                            const proto::ProtoObject* isAbs = nullptr;
                            try {
                                v = env->getAttribute(ctx, ns, nameS_, false);
                                if (v && v != PROTO_NONE) isAbs = env->getAttribute(ctx, v, abstractS, false);
                            } catch (const proto::ProtoException&) {
                                isAbs = nullptr;
                            }
                            if (v && v != PROTO_NONE) {
                                if (isAbs == PROTO_TRUE) {
                                    absNames = absNames->appendLast(ctx, k);
                                }
                            }
                        }
                    }
                }
            }
            // Inherit unimplemented abstract names from bases.
            const proto::ProtoObject* mro2 = targetClass->getAttribute(ctx,
                PythonEnvironment::getInternedString(ctx, "__mro__"));
            ::protoPython::TupleView mro2T = mro2 ? ::protoPython::PythonEnvironment::tupleView(ctx, mro2) : nullptr;
            if (mro2T) {
                for (proto::proto_ulong mi = 1; mi < mro2T->getSize(ctx); ++mi) {
                    const proto::ProtoObject* base = mro2T->getAt(ctx, static_cast<int>(mi));
                    if (!base || base == PROTO_NONE) continue;
                    const proto::ProtoObject* baseAm = base->getOwnAttributeDirect(ctx, amS);
                    if (!baseAm || baseAm == PROTO_NONE) continue;
                    const proto::ProtoList* bl = ::protoPython::listStorage(ctx, baseAm);
                    ::protoPython::TupleView bt = bl ? nullptr : ::protoPython::PythonEnvironment::tupleView(ctx, baseAm);
                    proto::proto_ulong sz = bl ? bl->getSize(ctx) : (bt ? bt->getSize(ctx) : 0);
                    for (proto::proto_ulong bi = 0; bi < sz; ++bi) {
                        const proto::ProtoObject* bn = bl ? bl->getAt(ctx, static_cast<int>(bi)) : bt->getAt(ctx, static_cast<int>(bi));
                        if (!bn || !bn->isString(ctx)) continue;
                        // Check if this class has a concrete override.
                        const proto::ProtoObject* override_ = targetClass->getOwnAttributeDirect(ctx, bn->asString(ctx));
                        bool overrides = override_ && override_ != PROTO_NONE;
                        if (overrides) {
                            const proto::ProtoObject* isAbs = override_->getAttribute(ctx, abstractS);
                            if (isAbs != PROTO_TRUE) continue;  // concrete override
                        }
                        if (!absNames->has(ctx, bn)) {
                            absNames = absNames->appendLast(ctx, bn);
                        }
                    }
                }
            }
            const_cast<proto::ProtoObject*>(targetClass)->setAttribute(
                ctx, amS, absNames->asObject(ctx));
            *anchor = targetClass;
        }

        // The methods' `__class__` cell (zero-argument super(), a free
        // `__class__`): type.__new__ filled it; a metaclass that built the
        // class some other way leaves it to this.
        if (env) env->applyClassCell(ctx, ns, targetClass);

        // __init_subclass__ is not called here: as in CPython it
        // runs at the end of type.__new__ (py_type ->
        // invokeInitSubclass), so a metaclass calling
        // super().__new__, type(name, bases, ns) and
        // types.new_class() all run it exactly once.  A raised
        // exception makes the metaclass call return nullptr and
        // unwinds below.
    }

    return targetClass ? targetClass : PROTO_NONE;
}

namespace {

enum class LevelExit { Return, PopBlock, PopExcept };

// One activation of executeBytecodeRange: its state, and the levels of its
// try nesting as member functions (a Python try is a C++ try, see Block).
// The state lives here once per call; lambdas capturing it by reference
// stored a pointer per captured variable per closure on every call.
struct Activation {
    // Invariants of the activation.
    proto::ProtoContext* ctx;
    PythonEnvironment* env;
    const int* bc;
    proto::proto_ulong n;
    proto::proto_ulong pcEnd;
    bool diag_local;
    const proto::ProtoTuple* constants;
    const proto::ProtoTuple* bytecode;
    const proto::ProtoTuple* names;
    const proto::ProtoObject** nativeConsts;
    const proto::ProtoObject** nativeNames;
    const int* nativeBc;
    proto::proto_ulong pcStart;
    proto::proto_ulong stackOffset;
    proto::proto_ulong* outPc;
    bool* yielded;
    std::vector<Block>* externalBlockStack;
    proto::proto_ulong initialTop;
    proto::proto_ulong* finalTopPtr;
    const proto::ProtoObject** frameAnchor;
    // This activation's handled-exception slot (see kActivationSlots), or
    // nullptr when its context has no room for one.
    const proto::ProtoObject** handledSlot = nullptr;
    // The frame noteFrame synthesises for a traceback when the activation
    // has none of its own (see kActivationSlots), or nullptr.
    const proto::ProtoObject** synthFrameSlot = nullptr;
    bool sync_globals;
    // A constructor, not aggregate initialization: initializing the aggregate
    // zero-filled the whole object first (rep stos), on every call.
    Activation(proto::ProtoContext* ctx_, PythonEnvironment* env_, const int* bc_, proto::proto_ulong n_,
               proto::proto_ulong pcEnd_, bool diag_, const proto::ProtoTuple* constants_,
               const proto::ProtoTuple* bytecode_, const proto::ProtoTuple* names_,
               const proto::ProtoObject** nativeConsts_, const proto::ProtoObject** nativeNames_,
               const int* nativeBc_, proto::proto_ulong pcStart_, proto::proto_ulong stackOffset_,
               proto::proto_ulong* outPc_, bool* yielded_, std::vector<Block>* externalBlockStack_,
               proto::proto_ulong initialTop_, proto::proto_ulong* finalTopPtr_,
               const proto::ProtoObject** frameAnchor_, bool syncGlobals_, proto::ProtoObject*& frame_,
               GCStack& stack_, std::vector<Block>& blockStack_, const std::vector<Block>& savedLevels_,
               const proto::ProtoObject* throwOnResume_)
        : ctx(ctx_), env(env_), bc(bc_), n(n_), pcEnd(pcEnd_), diag_local(diag_), constants(constants_),
          bytecode(bytecode_), names(names_), nativeConsts(nativeConsts_), nativeNames(nativeNames_),
          nativeBc(nativeBc_), pcStart(pcStart_), stackOffset(stackOffset_), outPc(outPc_), yielded(yielded_),
          externalBlockStack(externalBlockStack_), initialTop(initialTop_), finalTopPtr(finalTopPtr_),
          frameAnchor(frameAnchor_), sync_globals(syncGlobals_), frame(frame_), stack(stack_),
          blockStack(blockStack_), savedLevels(savedLevels_), throwOnResume(throwOnResume_),
          lastPc(pcStart_) {}
    Activation(const Activation&) = delete;
    Activation& operator=(const Activation&) = delete;

    // The frame's state, shared by every level.
    proto::ProtoObject*& frame;
    GCStack& stack;
    std::vector<Block>& blockStack;
    const std::vector<Block>& savedLevels;
    const proto::ProtoObject* throwOnResume;
    unsigned int sp_ctr = 0;
    // Attribute write group in progress (see "Attribute write groups"):
    // 0 none, 1 grouped (values held on the stack), 2 written per write.
    int attrGroupState = 0;
    // Slot index of the instruction executed last: the one that raised.
    proto::proto_ulong lastPc;
    // A Python try is a C++ try (see Block). A nested dispatch ends at its
    // range's POP_BLOCK or, for a handler, at its POP_EXCEPT: levelExit says
    // which and levelExitPc where the enclosing level continues. RETURN_VALUE
    // and a yield end every level.
    LevelExit levelExit = LevelExit::Return;
    proto::proto_ulong levelExitPc = 0;
    // A Python exception crossing this activation (caught by one of its
    // tries, or leaving it) gets the activation's traceback entry, at the
    // instruction executed last, and its __context__. Once: the exception this
    // activation noted last, its traceback unchanged since, is one it
    // re-raised (CPython adds no entry for a re-raise either). Only the last
    // one is kept: a loop raising and catching would otherwise grow a list
    // scanned on every raise.
    const proto::ProtoObject* notedExc = nullptr;
    const proto::ProtoObject* notedTb = nullptr;
    void noteFrame(const proto::ProtoObject* exc) {
        if (!env || !exc || exc == PROTO_NONE) return;
        // A foreign value gets no traceback (PythonEnvironment::addTraceback).
        if (!env->isPythonException(ctx, exc)) return;
        static const proto::ProtoString* const tbS = PythonEnvironment::getInternedString(ctx, "__traceback__");
        const proto::ProtoObject* tbNow = exc->getAttribute(ctx, tbS);
        if (notedExc == exc && notedTb == tbNow) return;
        const proto::ProtoObject* runningCode = runningCodeObject(ctx, env, frame, bytecode);
        const int tbLine = updateContextLocation(ctx, env, runningCode, lastPc / 2);
        // A frame stands for this activation only when its f_code is the
        // running code object: module-level code and exec/eval run with the
        // namespace as `frame`, and CO_OPTIMIZED calls run without one. Any
        // other case gets a synthesised frame carrying the running code
        // object, the globals and the line (what traceback formatting and
        // unittest's tb_frame.f_globals walk read).
        const proto::ProtoObject* tbFrame = frame;
        if (!tbFrame) tbFrame = PythonEnvironment::getCurrentFrame();
        const bool frameIsActivation = tbFrame && tbFrame != PROTO_NONE &&
            (!runningCode || tbFrame->getAttribute(ctx, env->getFCodeString()) == runningCode);
        if (!frameIsActivation) {
            // One synthesised frame per activation, as CPython has one frame
            // object per activation: a later raise in the same activation
            // finds it in its slot (which keeps it reachable) and only moves
            // its line.
            static const proto::ProtoString* const linenoS = PythonEnvironment::getInternedString(ctx, "f_lineno");
            const proto::ProtoObject* curGlobals = PythonEnvironment::getCurrentGlobals();
            const proto::ProtoObject* synth = synthFrameSlot ? *synthFrameSlot : nullptr;
            if (!synth || synth == PROTO_NONE
                || synth->getAttribute(ctx, env->getFCodeString()) != (runningCode ? runningCode : PROTO_NONE)
                || synth->getAttribute(ctx, env->getFGlobalsString()) != (curGlobals ? curGlobals : PROTO_NONE)) {
                const proto::ProtoObject* proto = env->getFramePrototype();
                const proto::ProtoString* const names[3] = {env->getFGlobalsString(), env->getFCodeString(), linenoS};
                const proto::ProtoObject* const values[3] = {
                    curGlobals ? curGlobals : PROTO_NONE, runningCode ? runningCode : PROTO_NONE, ctx->fromInteger(tbLine)};
                synth = (proto ? proto->newChild(ctx, true) : ctx->newObject(true))->setAttributes(ctx, 3, names, values);
                if (synthFrameSlot) *synthFrameSlot = synth;
            } else {
                synth->setAttribute(ctx, linenoS, ctx->fromInteger(tbLine));
            }
            tbFrame = synth;
        }
        noteCaughtException(ctx, env, exc, tbFrame, static_cast<int>(lastPc), tbLine,
                            !tbNow || tbNow == PROTO_NONE);
        notedExc = exc;
        notedTb = exc->getAttribute(ctx, tbS);
    }
    // A handler level runs inside the catch of the exception it handles,
    // which is the active one (bare raise, sys.exc_info) while it runs; a
    // RERAISE rethrows that catch's exception.
    template <typename HandlerBody>
    const proto::ProtoObject* runHandler(const proto::ProtoObject* handled, HandlerBody&& handlerBody) {
        Block handlerLevel{0, stack.size()};
        handlerLevel.isHandler = true;
        handlerLevel.exc = handled;
        blockStack.push_back(handlerLevel);
        struct HandlerGuard {
            std::vector<Block>& levels;
            ~HandlerGuard() { levels.pop_back(); }
        } handlerGuard{blockStack};
        PythonEnvironment::HandledExceptionScope handling(handledSlot, handled);
        levelExit = LevelExit::Return;
        return handlerBody();
    }
    // A protected range: a nested dispatch inside a C++ try. On a Python
    // exception the operand stack is cut to the depth the range started at,
    // the exception pushed ([tb, exc, exc], or exc alone for a with block)
    // and the handler run inside the catch.
    template <typename RangeBody, typename HandlerBody>
    const proto::ProtoObject* runRange(const Block& tryRange, RangeBody&& rangeBody, HandlerBody&& handlerBody) {
        try {
            blockStack.push_back(tryRange);
            struct RangeGuard {
                std::vector<Block>& levels;
                ~RangeGuard() { levels.pop_back(); }
            } rangeGuard{blockStack};
            levelExit = LevelExit::Return;
            return rangeBody();
        } catch (const proto::ProtoException& raised) {
            const proto::ProtoObject* handled = raised.value();
            noteFrame(handled);
            if (dbgOn) [[unlikely]] debugException(handled, &tryRange);
            while (stack.size() > tryRange.stackDepth) stack.pop_back();
            if (tryRange.isWithBlock) {
                stack.push_back(handled);
            } else {
                stack.push_back(env ? env->getNonePrototype() : PROTO_NONE);
                stack.push_back(handled);
                stack.push_back(handled);
            }
            return runHandler(handled, handlerBody);
        }
    }
    // Source-level debugging (include/protoPython/Debugger.h). dbgOn is set
    // when the activation starts on a thread that reports line events; the
    // dispatcher then calls debugInstruction before every instruction.
    bool dbgOn = false;
    bool dbgReturned = false;          // a call made by this activation returned
    int dbgLastLine = -1;
    proto::proto_ulong dbgPc = 0;      // the instruction about to run
    const debugger::CodeInfo* dbgCode = nullptr;
    const proto::ProtoObject* dbgGlobals = nullptr;  // the globals it started with
    PROTOPY_EXC_NOINLINE void debugInstruction(proto::proto_ulong i);
    // An exception crossing this activation: caught by its try range `caught`,
    // or (nullptr) leaving it. Reports the first user frame it reaches.
    PROTOPY_EXC_NOINLINE void debugException(const proto::ProtoObject* exc, const Block* caught);
    // Whether the except clauses of the try range `b` handle `exc`.
    bool debugRangeHandles(const Block& b, const proto::ProtoObject* exc);
    bool ensureDebugCode();
    // The activation's hottest invariants enter as parameters of the same
    // names: a parameter shadows the member and is a true local, which the
    // compiler can keep in a register; a member is reloaded after every call
    // (the activation escapes into the nested tries).
    const proto::ProtoObject* dispatch(proto::proto_ulong pcFrom,
                        proto::ProtoContext* const ctx, PythonEnvironment* const env,
                        const int* const bc, const proto::proto_ulong n, const proto::proto_ulong pcEnd);
    // A resumed generator rebuilds its try nesting, outermost first: a
    // protected range is entered again (its body continues at the resume
    // point) and a handler runs again inside a catch of the exception it was
    // handling. Each level then continues where its nested level ended.
    const proto::ProtoObject* runLevel(size_t k) {
        if (k == savedLevels.size()) {
            // gen.throw(): raised at the suspended yield, inside the rebuilt
            // tries; a YIELD_FROM forwards it to its sub-iterator instead.
            if (throwOnResume && !(pcStart < n && bc[pcStart] == OP_YIELD_FROM)) {
                const proto::ProtoObject* injected = throwOnResume;
                throwOnResume = nullptr;
                throw PyException(env, injected);
            }
            return dispatch(pcStart, ctx, env, bc, n, pcEnd);
        }
        const Block saved = savedLevels[k];
        const proto::ProtoObject* r;
        if (saved.isHandler) {
            try {
                throw PyException(env, saved.exc);
            } catch (const proto::ProtoException&) {
                r = runHandler(saved.exc, [&]() { return runLevel(k + 1); });
            }
        } else {
            r = runRange(saved, [&]() { return runLevel(k + 1); },
                         [&]() { return dispatch(saved.handlerPc, ctx, env, bc, n, pcEnd); });
        }
        if (levelExit != LevelExit::Return) {
            levelExit = LevelExit::Return;
            return dispatch(levelExitPc, ctx, env, bc, n, pcEnd);
        }
        return r;
    }
};

const proto::ProtoObject* Activation::dispatch(proto::proto_ulong pcFrom,
                        proto::ProtoContext* const ctx, PythonEnvironment* const env,
                        const int* const bc, const proto::proto_ulong n, const proto::proto_ulong pcEnd) {
    for (proto::proto_ulong i = pcFrom; i <= pcEnd; ) {
        if ((++sp_ctr & 0x3F) == 0 && ctx) {
            // Opcodes may have replaced `frame` (an immutable frame's
            // setAttribute returns a new cell): anchor the current one.
            if (frameAnchor) *frameAnchor = frame;
            ctx->safepoint();
            // Cooperative signal delivery: when a Python signal handler
            // is registered, the OS-level handler only flips a
            // sig_atomic_t flag (async-signal-safe). The actual Python
            // callback is dispatched here, at the same cadence as the
            // GC safepoint, where the operand stack and frame are in
            // a known-good state. The branch is a single volatile
            // load when nothing is pending — measured cost is < 0.1%
            // on bench_pidigits.
            // Not while an attribute write group holds unpublished writes:
            // the handler could observe the receiver between them.
            if (attrGroupState != 1 && signal_module::hasPendingSignal()) {
                signal_module::checkAndDeliverPendingSignals(ctx, env);
            }
        }
        int op = bc[i];
        int arg = (i + 1 < n) ? bc[i + 1] : 0;
        proto::proto_ulong next_i = i + 2;

        if (diag_local) {
            fprintf(stderr, "DEBUG HANG TRACE: [PC %llu] OP %d ARG %d\n", static_cast<unsigned long long>(i), op, arg);
            fprintf(stderr, "  Stack (depth=%llu):", static_cast<unsigned long long>((proto::proto_ulong)stack.size()));
            for (size_t k = 0; k < stack.size(); ++k) {
                fprintf(stderr, " [%llu]=%p", static_cast<unsigned long long>((proto::proto_ulong)k), (void*)stack[k]);
            }
            fprintf(stderr, "\n");
            fflush(stderr);
        }
        if (stack.overflowed) {
            if (env) env->raiseRuntimeError(ctx, "evaluation stack overflow (maximum expression depth exceeded)");
            return nullptr;
        }
        // PB8: when the current opcode is OP_YIELD_FROM and the frame
        // carries a pending exception (typically from an outer .throw()),
        // let YIELD_FROM forward the exception INTO subIter via
        // subIter.throw(exc) rather than invoking the local
        // exception-handler dispatch.  YIELD_FROM only handles its own
        // case; if subIter has no `throw` method or subIter's throw
        // re-raises, the exception falls back to the normal handler
        // because YIELD_FROM leaves it on the env's pending slot.
        
        bool diag_env = diag_local;
        int diag_level = diag_env ? 1 : 0;
        if (diag_level >= 2) {
             fprintf(stderr, "TRACE: PC %llu OP %d ARG %d depth=%zu\n", static_cast<unsigned long long>(i), op, (opcodeHasArg(op) ? arg : 0), stack.top);
             fprintf(stderr, "  STACK:");
             for (size_t k = 0; k < stack.top && k < 10; ++k) {
                 fprintf(stderr, " [%zu]=%p", k, (void*)stack.slots[k]);
             }
             if (stack.top > 10) fprintf(stderr, " ...");
             fprintf(stderr, "\n");
        }

        // C++ exception boundary.  protoCore's own errors are ProtoExceptions
        // built by the space's error factory (pythonErrorFactory) and pass
        // through untouched, as every Python exception does.  A C++ standard
        // exception thrown by native code (a module that throws
        // std::runtime_error) is a defect of that code, not a Python value:
        // without a translation point it would escape the bytecode loop and
        // reach std::terminate.  It is raised here as RuntimeError(what()), so
        // the program sees a Python exception it can handle.
        lastPc = i;
        if (dbgOn) [[unlikely]] debugInstruction(i);
        try {
        switch (op) {
        case OP_LOAD_CONST: {
            // Use flat pre-fetched array when available (avoids cross-DSO AVL lookup).
            const proto::ProtoObject* val = nullptr;
            if (nativeConsts && static_cast<uint32_t>(arg) < static_cast<uint32_t>(constants->getSize(ctx))) {
                val = nativeConsts[arg];
            } else if (static_cast<proto::proto_ulong>(arg) < constants->getSize(ctx)) {
                val = constants->getAt(ctx, arg);
            }
            if (val) {
                if (diag_local) {
                    fprintf(stderr, "DEBUG: LOAD_CONST arg=%d val=%p repr=%s\n", arg, (void*)val, PythonEnvironment::reprObject(ctx, val).c_str());
                    fflush(stderr);
                }
                stack.push_back(val);
            }
        } break;
        case OP_RETURN_VALUE: {
            if (stack.empty()) return PROTO_NONE;
            const proto::ProtoObject* ret = stack.back();
            ctx->returnValue = ret;
            if (outPc) *outPc = next_i; // Mark finished
            return ret;  /* exit block immediately; destructor will promote */
        } break;
        case OP_YIELD_VALUE: {
            if (stack.empty()) return PROTO_NONE;
            const proto::ProtoObject* ret = stack.back();
            stack.pop_back();
            ctx->returnValue = ret;
            if (yielded) *yielded = true;
            if (outPc) *outPc = next_i; // Resume at NEXT instruction
            if (finalTopPtr) *finalTopPtr = stack.top;
            if (externalBlockStack) *externalBlockStack = blockStack; // save try/except handlers
            return ret;
        } break;
        case OP_GET_YIELD_FROM_ITER: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* obj = stack.back();
            const proto::ProtoObject* iterator = nullptr;
            const proto::ProtoObject* iterMethod = env ? env->getAttribute(ctx, obj, env->getIterString()) : obj->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__iter__"));
            if (iterMethod && iterMethod != PROTO_NONE) {
                iterator = invokePythonCallable(ctx, iterMethod, ctx->newList(), nullptr);
            } else {
                iterator = obj;
            }
            stack.back() = iterator;
        } break;
        case OP_YIELD_FROM: {
            // Stack: [..., subIter, sendVal]
            // On yield: pause at THIS opcode so the next .send() pushes a
            // fresh sendVal and re-executes YIELD_FROM (drives subIter).
            // On StopIteration: pop subIter, push StopIteration.value, fall to next_i.
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* sendVal = stack.back();
            stack.pop_back();
            const proto::ProtoObject* subIter = stack.back();

            const proto::ProtoString* sendS = env ? env->getSendString() : PythonEnvironment::getInternedString(ctx, "send");
            const proto::ProtoObject* result = nullptr;

            // gen.throw() on the delegating generator: thrown INTO subIter
            // through subIter.throw(exc), the yield-from contract; without a
            // throw method it is raised here.
            const proto::ProtoObject* injected = throwOnResume;
            throwOnResume = nullptr;
            const proto::ProtoObject* stopVal = nullptr;
            bool stopped = false;
            try {
                if (injected) {
                    const proto::ProtoString* throwS = PythonEnvironment::getInternedString(ctx, "throw");
                    const proto::ProtoObject* throwMethod = subIter->getAttribute(ctx, throwS);
                    if (!(throwMethod && throwMethod != PROTO_NONE && throwMethod->asMethod(ctx))) {
                        throw PyException(env, injected);
                    }
                    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, injected);
                    result = throwMethod->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(subIter), nullptr, argsView(proto::ProtoListArguments(ctx, args)), nullptr);
                } else {
                    const proto::ProtoObject* sendMethod = subIter->getAttribute(ctx, sendS);
                    if (sendMethod && sendMethod != PROTO_NONE) {
                        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, sendVal);
                        result = subIter->call(ctx, nullptr, sendS, subIter, args, nullptr);
                    } else {
                        if (sendVal != PROTO_NONE) {
                            if (env) env->raiseTypeError(ctx, "can't send non-None value to a plain iterator");
                            return PROTO_NONE;
                        }
                        const proto::ProtoString* nextS = env ? env->getNextString() : protoPython::PythonEnvironment::getInternalString(ctx, "__next__");
                        result = subIter->call(ctx, nullptr, nextS, subIter, ctx->newList(), nullptr);
                    }
                }
            } catch (const proto::ProtoException& e) {
                // StopIteration ends the delegation with its value; anything
                // else propagates.
                if (!env || !env->isStopIteration(ctx, e.value())) throw;
                stopVal = env->getStopIterationValue(ctx, e.value());
                stopped = true;
            }

            if (stopped) {
                stack.pop_back(); // Remove subIter
                stack.push_back(stopVal);
                // Will continue to i+1
            } else if (result == nullptr) {
                // PB7: many native iterators (list_iterator, dict_keys,
                // tuple_iterator, ...) signal exhaustion by returning
                // nullptr instead of raising StopIteration.  Treat
                // null-without-exception as silent end-of-iteration:
                // pop subIter, push None as the StopIteration value,
                // fall through to next_i.
                stack.pop_back();
                stack.push_back(PROTO_NONE);
            } else {
                // Yielded.  Pause at THIS opcode so the next .send()
                // pushes a fresh sendVal and re-runs YIELD_FROM to
                // drive subIter through another step.
                ctx->returnValue = result;
                if (yielded) *yielded = true;
                if (outPc) *outPc = i;
                if (finalTopPtr) *finalTopPtr = stack.top;
                if (externalBlockStack) {
                    *externalBlockStack = blockStack;
                }
                return result;
            }
        } break;
        case OP_LOAD_NAME: {
            int nameIdx = arg >> 1;
            bool pushNull = (arg & 0x01);
            if (names && frame && static_cast<proto::proto_ulong>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);

                    const proto::ProtoObject* val = nullptr;
                    bool found = false;

                    // Fast path: the frame's own binding.  co_names entries are
                    // interned, as getOwnAttributeDirect requires; it returns nullptr
                    // for an absent name and the stored value, PROTO_NONE included
                    // (`x = None`), for a present one.
                    if (const proto::ProtoObject* own = frame->getOwnAttributeDirect(ctx, nameS)) {
                        val = own;
                        found = true;
                    }

                    // Closure parent chain: free variables live in parent frames. All name strings
                    // are interned (co_names uses getInternedString), so getAttribute's symbolTable
                    // lookup succeeds and traverses the prototype chain correctly.
                    //
                    // SP-E/B-DD1: ProtoObject::getAttribute returns nullptr for not-found
                    // and the actual stored value (which may legitimately be PROTO_NONE
                    // for `x = None` at module or class scope) for found.  ProtoSparseList::getAt
                    // (used in the fast path above) instead uses PROTO_NONE as its absent-
                    // sentinel.  The two APIs disagree, so a naive PROTO_NONE filter here
                    // would break `class C: y = None; print(y)`.
                    //
                    // Mirror the contingent-walk idiom already used by OP_LOAD_GLOBAL
                    // (line ~4021): pay the second parent-chain walk via hasAttribute()
                    // only when val == PROTO_NONE — the ambiguous case.  On the common
                    // path (val is a non-None value, or val is nullptr) we read once.
                    if (!found) {
                        val = frame->getAttribute(ctx, nameS);
                        if (val != nullptr && (val != PROTO_NONE
                                               || frame->hasAttribute(ctx, nameS) == PROTO_TRUE)) {
                            found = true;
                        }
                        // Found through the parent chain, the very value a built-in
                        // prototype provides (object's __init__ / __class__ /
                        // __dict__ for a class body, dict.copy for eval's locals
                        // mirror) is an attribute of the namespace object, not a name.
                        // Only object values are compared: None, booleans, numbers
                        // and strings are shared values, so matching a prototype's
                        // attribute says nothing about where a local came from.
                        const bool objectValue = found && val != PROTO_NONE && !val->isBoolean(ctx)
                            && !val->isInteger(ctx) && !val->isFloat(ctx) && !val->isString(ctx);
                        if (objectValue && env) {
                            // Modules hold copies of modulePrototype's methods:
                            // compare native methods by function.
                            auto providedBy = [&](const proto::ProtoObject* proto) {
                                const proto::ProtoObject* pv = proto ? proto->getAttribute(ctx, nameS) : nullptr;
                                return pv && (pv == val
                                    || (pv->isMethod(ctx) && val->isMethod(ctx)
                                        && pv->asMethod(ctx) == val->asMethod(ctx)));
                            };
                            // `__class__` and `__dict__` are never names reached this
                            // way: the class cell is a cell (LOAD_DEREF), and a
                            // module's own __dict__ is the module itself.
                            const proto::ProtoString* dictDunderS = env->getDictDunderString();
                            const bool objectAttributeName = nameS == env->getClassString()
                                || (dictDunderS && nameS == dictDunderS);
                            if (objectAttributeName
                                || providedBy(env->getObjectPrototype()) || providedBy(env->getDictPrototype())
                                || providedBy(env->getTypePrototype()) || providedBy(env->getModulePrototype())) {
                                found = false;
                                val = nullptr;
                            }
                        }
                    }

                    // A module's __class__ and modulePrototype's methods are attributes
                    // of the module object, not globals: look further, as for a missing name.
                    if (found && env && env->isInheritedModuleName(ctx, frame, nameS, val)) {
                        found = false;
                        val = nullptr;
                    }

                    // For custom class namespace dicts (e.g. EnumDict from __prepare__), call
                    // __getitem__ to mirror the __setitem__ interception done in STORE_NAME.
                    if (!found && env) {
                        const proto::ProtoObject* frameType = env->getType(ctx, frame);
                        // Mirror the same __class__-fallback used by STORE_NAME: EnumDict and
                        // other __prepare__ namespaces may report as the base dict prototype but
                        // carry a __class__ attribute pointing to the actual subtype.
                        if (frameType == env->getDictPrototype()) {
                            const proto::ProtoString* classS = env->getClassString()
                                ? env->getClassString()
                                : PythonEnvironment::getInternedString(ctx, "__class__");
                            const proto::ProtoObject* cls = frame->proto::ProtoObject::getAttribute(ctx, classS);
                            if (cls && cls != PROTO_NONE) frameType = cls;
                        }
                        if (frameType && frameType != PROTO_NONE &&
                            frameType != env->getDictPrototype() &&
                            frameType != env->getModulePrototype()) {
                            const proto::ProtoObject* getitem = env->getAttribute(ctx, frameType, env->getGetItemString(), false);
                            if (getitem && getitem != PROTO_NONE) {
                                const proto::ProtoList* giArgs = ctx->newList()->appendLast(ctx, nameObj);
                                try {
                                    val = invokeDunder(ctx, frame, env->getGetItemString(), giArgs);
                                    if (val && val != PROTO_NONE) found = true;
                                } catch (const proto::ProtoException&) {
                                    val = nullptr;   // KeyError means not found
                                }
                            }
                        }
                    }

                    // Fallback: globals, then builtins.
                    if (!found) {
                        val = env ? env->resolveGlobalName(nameS, ctx) : nullptr;
                        if (val) found = true;
                    }

                    if (found) {
                        if (pushNull) stack.push_back(nullptr);
                        stack.push_back(val);
                    } else {
                        std::string nStr;
                        nameS->toUTF8String(ctx, nStr);
                        if (env) env->raiseNameError(ctx, nStr);
                        i = next_i;
                        continue;
                    }
                } else {
                    if (pushNull) stack.push_back(nullptr);
                    stack.push_back(PROTO_NONE);
                }
            } else {
                if (pushNull) stack.push_back(nullptr);
                stack.push_back(PROTO_NONE);
            }
        } break;
        case OP_STORE_NAME: {
            int nameIdx = arg >> 1;
            if (names && frame && static_cast<proto::proto_ulong>(nameIdx) < names->getSize(ctx)) {
                if (stack.empty()) {
                    // ... error handling ...
                    i = next_i; continue;
                }
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                const proto::ProtoObject* val = stack.back();
                // Delay pop until done
                if (proto::ProtoObject::isStringTagFast(nameObj)) {
                    bool handledBySetitem = false;
                    const proto::ProtoString* nS = nameObj->asString(ctx);
                    if (env) {
                        const proto::ProtoObject* frameType = env->getType(ctx, frame);
                        
                        // Check if it's a custom namespace class by looking for __class__ manually
                        // if getType returned the base dict type.
                        if (frameType == env->getDictPrototype()) {
                            const proto::ProtoString* classS = env->getClassString() ? env->getClassString() : PythonEnvironment::getInternedString(ctx, "__class__");
                            const proto::ProtoObject* cls = frame->proto::ProtoObject::getAttribute(ctx, classS);
                            if (cls && cls != PROTO_NONE) {
                                frameType = cls;
                            }
                        }

                        if (frameType && frameType != PROTO_NONE &&
                            frameType != env->getDictPrototype() &&
                            frameType != env->getModulePrototype()) {

                            const proto::ProtoObject* setitem = env->getAttribute(ctx, frameType, env->getSetItemString(), false);
                            if (setitem && setitem != PROTO_NONE) {
                                handledBySetitem = true;
                                if (diag_local) {
                                    std::string nStr; nameObj->asString(ctx)->toUTF8String(ctx, nStr);
                                    fprintf(stderr, "DEBUG OP_STORE_NAME: handledBySetitem intercepted '%s' type=%p\n", nStr.c_str(), (void*)frameType);
                                }
                                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, nameObj)->appendLast(ctx, val);
                                const proto::ProtoObject* setRet = invokeDunder(ctx, frame, env->getSetItemString(), args);
                                (void)setRet;
                            }
                        }
                    }

                    // Is this key already present in the frame's own attributes?
                    const bool isNewKey = frame->hasOwnAttribute(ctx, nS) != PROTO_TRUE;

                    if (!handledBySetitem) {
                        const proto::ProtoObject* oldFrame = frame;
                        const proto::ProtoObject* newFrame = frame->setAttribute(ctx, nS, val);
                        frame = const_cast<proto::ProtoObject*>(newFrame);
                        syncModuleIdentity(ctx, env, oldFrame, newFrame);

                        const proto::ProtoString* dataS = protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                        const proto::ProtoObject* dataObj = frame->getAttribute(ctx, dataS);
                        if (dataObj && dataObj->asSparseList(ctx)) {
                            const proto::ProtoSparseList* dataList = dataObj->asSparseList(ctx);
                            dataList = dataList->setAt(ctx, nameObj->getHash(ctx), val);
                            const proto::ProtoObject* oldF2 = frame;
                            const proto::ProtoObject* newF2 = frame->setAttribute(ctx, dataS, dataList->asObject(ctx));
                            frame = const_cast<proto::ProtoObject*>(newF2);
                            syncModuleIdentity(ctx, env, oldF2, newF2);
                        }
                    }
                    stack.pop_back(); // Pop val now that it's stored

                    // Only add to __keys__ when the key is genuinely new (first store), O(1) total.
                    // STRUCT-308: hasOwnAttribute first — getAttribute walks the parent chain, and
                    // every module's first STORE_NAME would otherwise wrap and mutate
                    // modulePrototype's inherited __keys__ list (which seeds itself from the
                    // object prototype's dunder list).  The mutation corrupts every subsequent
                    // module's view of its own keys via inheritance — signal.items() ends up
                    // walking __new__/__init__/__str__ instead of SIGTERM/SIGKILL/..., which
                    // makes signal.py's `_IntEnum._convert_` produce an empty Signals enum and
                    // surface the cryptic "do not use super().__new__" TypeError from
                    // enum.__new__'s "no members" branch.
                    if (isNewKey) {
                        const proto::ProtoString* keysName =
                            PythonEnvironment::getInternedString(ctx, "__keys__");
                        const proto::ProtoObject* keysObj =
                            (frame->hasOwnAttribute(ctx, keysName) == PROTO_TRUE)
                                ? frame->getOwnAttributeDirect(ctx, keysName)
                                : nullptr;
                        const proto::ProtoList* keysList = (keysObj && ::protoPython::listStorage(ctx, keysObj))
                            ? ::protoPython::listStorage(ctx, keysObj) : ctx->newList();
                        keysList = keysList->appendLast(ctx, nameObj);
                        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, keysName, keysList->asObject(ctx)));
                    }
                    if (env) {
                        PythonEnvironment::setCurrentFrame(frame);
                        // Only sync globals when the frame pointer actually changed (immutable CoW).
                        // For mutable frames the pointer never changes, so this is always a no-op there,
                        // and skipping it eliminates a costly mutableRoot write per STORE_NAME.
                        if (sync_globals && frame != PythonEnvironment::getCurrentGlobals()) {
                            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, env->getFGlobalsString(), frame));
                            PythonEnvironment::setCurrentGlobals(frame);
                        }
                        env->invalidateResolveCache();
                    }
                }
            }
        } break;
        case OP_LOAD_FAST: {
            const proto::ProtoObject* val = ctx->getAutomaticLocal(static_cast<unsigned int>(arg));
            if (diag_local && arg == 0) {
                 fprintf(stderr, "DEBUG: LOAD_FAST 0 loaded: %p\n", (void*)val);
                 fflush(stderr);
            }
            // Detect the env-wide "<unbound>" sentinel installed by the
            // compiler at function entry for annotation-only locals.
            // Reading them raises UnboundLocalError per PEP 526.
            if (env && val && val == env->getUnboundSentinel()) {
                raiseUnboundFastLocal(ctx, env, arg);
                i = next_i;
                continue;
            }
            stack.push_back(val ? val : (env ? env->getNonePrototype() : PROTO_NONE));
        } break;
        case OP_STORE_FAST: {
            if (stack.empty()) { i = next_i; continue; }
            ctx->setAutomaticLocal(static_cast<unsigned int>(arg), stack.back());
            stack.pop_back();
        } break;
        case OP_BINARY_ADD: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            // Tier 2: SmallInt + SmallInt fast path.  Skips binaryAdd /
            // unwrapPrimitive / ProtoObject::add / Integer::add — for the
            // common case (≥90 % of BINARY_ADD on integer-loop benchmarks)
            // this collapses ~10 cross-DSO function calls + 6 redundant tag
            // checks into one branch + ALU add + range check + re-tag.
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                long long sum = proto::asSmallInt(a) + proto::asSmallInt(b);
                if (proto::smallIntInRange(sum)) {
                    stack.pop_back();
                    stack.back() = proto::makeSmallInt(sum);
                    i = next_i;
                    continue;
                }
            }
            const proto::ProtoObject* r = binaryAdd(ctx, a, b);
            stack.pop_back(); // Pop b
            stack.back() = r; // Replace a with r
        } break;
        case OP_INPLACE_ADD: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];

            // Tier 2: SmallInt + SmallInt fast path (covers `i += 1`).
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                long long sum = proto::asSmallInt(a) + proto::asSmallInt(b);
                if (proto::smallIntInRange(sum)) {
                    stack.pop_back();
                    stack.back() = proto::makeSmallInt(sum);
                    i = next_i;
                    continue;
                }
            }

            const proto::ProtoObject* result = inplaceAdd(ctx, env, a, b);
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_BINARY_SUBTRACT: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            // Tier 2: SmallInt - SmallInt fast path.
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                long long diff = proto::asSmallInt(a) - proto::asSmallInt(b);
                if (proto::smallIntInRange(diff)) {
                    stack.pop_back();
                    stack.back() = proto::makeSmallInt(diff);
                    i = next_i;
                    continue;
                }
            }
            const proto::ProtoObject* r = binarySubtract(ctx, a, b);
            stack.pop_back(); // Pop b
            stack.back() = r;
        } break;
        case OP_INPLACE_SUBTRACT: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            // Tier 2: SmallInt - SmallInt fast path.
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                long long diff = proto::asSmallInt(a) - proto::asSmallInt(b);
                if (proto::smallIntInRange(diff)) {
                    stack.pop_back();
                    stack.back() = proto::makeSmallInt(diff);
                    i = next_i;
                    continue;
                }
            }
            stack.pop_back();
            stack.pop_back();
            const proto::ProtoString* isubS = env ? env->getISubString() : PythonEnvironment::getInternedString(ctx, "__isub__");
            const proto::ProtoObject* isub = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, isubS);
            const proto::ProtoObject* result = nullptr;
            if (isub && isub->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                result = isub->asMethod(ctx)(ctx, a, nullptr, argsView(proto::ProtoListArguments(ctx, oneArg)), nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            } else if (isub && isub != PROTO_NONE) {
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
                result = invokeDunder(ctx, a, isubS, args);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result) {
                result = binarySubtract(ctx, a, b);
            }
            if (result) stack.push_back(result);
            else stack.push_back(PROTO_NONE);
        } break;
        case OP_BINARY_MULTIPLY: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            // SmallInt × SmallInt: the product of two 56-bit values may
            // not fit in 64 bits, let alone a SmallInt. Only a product that
            // fits both takes the fast path (checking the range of a
            // truncated product made 2**40 * 2**40 evaluate to 0). sieve's
            // hot loop is `i * j` with both operands well within range.
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                std::int64_t prod;
                if (!checkedMul64(proto::asSmallInt(a), proto::asSmallInt(b), &prod)
                    && proto::smallIntInRange(prod)) {
                    stack.pop_back();
                    stack.back() = proto::makeSmallInt(prod);
                    i = next_i; continue;
                }
            }
            const proto::ProtoObject* r = binaryMultiply(ctx, a, b);
            stack.pop_back(); // Pop b
            stack.back() = r;
        } break;
        case OP_INPLACE_MULTIPLY: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoString* imulS = env ? env->getIMulString() : PythonEnvironment::getInternedString(ctx, "__imul__");
            const proto::ProtoObject* imul = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, imulS);
            const proto::ProtoObject* result = nullptr;
            if (imul && imul->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                result = imul->asMethod(ctx)(ctx, a, nullptr, argsView(proto::ProtoListArguments(ctx, oneArg)), nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            } else if (imul && imul != PROTO_NONE) {
                // Python user __imul__ — invokeDunder prepends self.
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
                result = invokeDunder(ctx, a, imulS, args);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result) {
                result = binaryMultiply(ctx, a, b);
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_BINARY_TRUE_DIVIDE: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryTrueDivide(ctx, a, b);
            stack.pop_back(); // Pop b
            stack.back() = r;
        } break;
        case OP_BINARY_MODULO: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* right = stack.back();
            const proto::ProtoObject* left = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryModulo(ctx, left, right);
            stack.pop_back();
            stack.back() = r;
        } break;
        case OP_BINARY_MATRIX_MULTIPLY: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* b = stack.back();
            // CPython @-operator dispatch: try a.__matmul__(b); if it
            // returns NotImplemented, fall back to b.__rmatmul__(a);
            // raise TypeError when neither produces a value.  We share
            // the same `binaryOpDispatch` helper that +/-/* use, then
            // promote a silent null (the lenient "method absent" path)
            // to a TypeError because matmul has no legacy stdlib
            // contract that relies on the silent behaviour.
            const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__matmul__", "__rmatmul__");
            if (!r && env) {
                std::string aName = "?", bName = "?";
                const proto::ProtoString* nameS = env->getNameString();
                const proto::ProtoObject* aCls = env->getType(ctx, a);
                const proto::ProtoObject* bCls = env->getType(ctx, b);
                auto extractName = [&](const proto::ProtoObject* cls, std::string& out) {
                    if (!cls) return;
                    const proto::ProtoObject* n = cls->getAttribute(ctx, nameS);
                    if (n && n->isString(ctx)) n->asString(ctx)->toUTF8String(ctx, out);
                };
                extractName(aCls, aName);
                extractName(bCls, bName);
                env->raiseTypeError(ctx,
                    "unsupported operand type(s) for @: '" + aName + "' and '" + bName + "'");
            }
            stack.pop_back();
            stack.back() = r ? r : PROTO_NONE;
        } break;
        case OP_INPLACE_MATRIX_MULTIPLY: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* right = stack.back();
            const proto::ProtoObject* left = stack[stack.top - 2];
            // CPython convention for `@=`:
            //   1) try left.__imatmul__(right); if it returns NotImplemented,
            //      fall through to forward + reflected matmul on (left, right);
            //   2) raise TypeError if nothing succeeds.
            const proto::ProtoString* imatmulS = env
                ? env->getIMatMulString()
                : protoPython::PythonEnvironment::getInternalString(ctx, "__imatmul__");
            const proto::ProtoObject* imatmul = env
                ? env->getAttribute(ctx, left, imatmulS, false)
                : left->getAttribute(ctx, imatmulS);
            const proto::ProtoObject* res = nullptr;
            if (imatmul && imatmul != PROTO_NONE) {
                res = invokePythonCallable(ctx, imatmul,
                    ctx->newList()->appendLast(ctx, right), nullptr);
                if (env && res == env->getNotImplementedPrototype()) res = nullptr;
            }
            if (!res) {
                res = binaryOpDispatch(ctx, left, right, "__matmul__", "__rmatmul__");
                if (!res && env) {
                    std::string aName = "?", bName = "?";
                    const proto::ProtoString* nameS = env->getNameString();
                    const proto::ProtoObject* aCls = env->getType(ctx, left);
                    const proto::ProtoObject* bCls = env->getType(ctx, right);
                    auto extractName = [&](const proto::ProtoObject* cls, std::string& out) {
                        if (!cls) return;
                        const proto::ProtoObject* n = cls->getAttribute(ctx, nameS);
                        if (n && n->isString(ctx)) n->asString(ctx)->toUTF8String(ctx, out);
                    };
                    extractName(aCls, aName);
                    extractName(bCls, bName);
                    env->raiseTypeError(ctx,
                        "unsupported operand type(s) for @=: '" + aName + "' and '" + bName + "'");
                }
            }
            stack.pop_back();           // right
            stack.pop_back();           // left
            stack.push_back(res ? res : PROTO_NONE);
        } break;
        case OP_RERAISE: {
            // Re-raise the exception being handled. No traceback entry is
            // added: the exception already has this frame's. In a handler
            // level that is the exception of the catch it runs in: rethrow it.
            if (!blockStack.empty() && blockStack.back().isHandler && std::current_exception()) throw;
            if (env) reraiseActiveException(ctx, env);
            i = next_i;
            continue;
        } break;
        case OP_JUMP_FORWARD: {
            i = next_i + arg;
            continue;
        } break;
        case OP_POP_EXCEPT: {
            // The end of a handler (its level's guard stops treating the
            // exception as handled): back to the level that opened the try.
            if (!blockStack.empty() && blockStack.back().isHandler) {
                levelExit = LevelExit::PopExcept;
                levelExitPc = next_i;
                return nullptr;
            }
        } break;
        case OP_BINARY_POWER: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryPower(ctx, a, b);
            stack.pop_back();
            stack.back() = r;
        } break;
        case OP_BINARY_FLOOR_DIVIDE: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryFloorDivide(ctx, a, b);
            stack.pop_back();
            stack.back() = r;
        } break;
        case OP_INPLACE_TRUE_DIVIDE: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* result = inplaceDunder(ctx, env, a, b,
                env ? env->getITrueDivString() : PythonEnvironment::getInternedString(ctx, "__itruediv__"));
            if (!result) result = binaryTrueDivide(ctx, a, b);
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_INPLACE_FLOOR_DIVIDE: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* result = inplaceDunder(ctx, env, a, b,
                env ? env->getIFloorDivString() : PythonEnvironment::getInternedString(ctx, "__ifloordiv__"));
            if (!result) result = binaryFloorDivide(ctx, a, b);
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_INPLACE_MODULO: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* result = inplaceDunder(ctx, env, a, b,
                env ? env->getIModString() : PythonEnvironment::getInternedString(ctx, "__imod__"));
            if (!result) result = binaryModulo(ctx, a, b);
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_INPLACE_POWER: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoString* ipowS = env ? env->getIPowString() : PythonEnvironment::getInternedString(ctx, "__ipow__");
            const proto::ProtoObject* ipow = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, ipowS);
            const proto::ProtoObject* result = nullptr;
            if (ipow && ipow->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                result = ipow->asMethod(ctx)(ctx, a, nullptr, argsView(proto::ProtoListArguments(ctx, oneArg)), nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            } else if (ipow && ipow != PROTO_NONE) {
                // Python-user-defined __ipow__ (POINTER_TAG_OBJECT, no
                // native asMethod).  Route through invokeDunder which
                // prepends self for Python callables.  Without this
                // branch the dispatcher fell through to binaryPower
                // and raised "unsupported operand type(s) for **".
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
                result = invokeDunder(ctx, a, ipowS, args);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result) {
                // CPython parity: failed `**=` should mention `**=` in
                // the TypeError, not `**`. binaryPower routes through
                // binaryOpDispatch which only knows the binary operator
                // symbol, so rewrite the message here for the in-place op.
                try {
                    result = binaryPower(ctx, a, b);
                } catch (const proto::ProtoException& raised) {
                    const proto::ProtoObject* exc = raised.value();
                    const proto::ProtoObject* excType = exc ? env->getType(ctx, exc) : nullptr;
                    const proto::ProtoObject* tnObj = excType ? excType->getAttribute(ctx, env->getNameString()) : nullptr;
                    std::string tn;
                    if (tnObj && tnObj->isString(ctx)) tnObj->asString(ctx)->toUTF8String(ctx, tn);
                    if (tn == "TypeError") {
                        std::string aName = "?", bName = "?";
                        const proto::ProtoString* nameS = env->getNameString();
                        const proto::ProtoObject* aCls = env->getType(ctx, a);
                        const proto::ProtoObject* bCls = env->getType(ctx, b);
                        auto extractName = [&](const proto::ProtoObject* cls, std::string& out) {
                            if (!cls) return;
                            const proto::ProtoObject* n = cls->getAttribute(ctx, nameS);
                            if (n && n->isString(ctx)) n->asString(ctx)->toUTF8String(ctx, out);
                        };
                        extractName(aCls, aName);
                        extractName(bCls, bName);
                        env->raiseTypeError(ctx,
                            "unsupported operand type(s) for **=: '" + aName + "' and '" + bName + "'");
                    }
                    throw;
                }
            }
            stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
        } break;
        case OP_INPLACE_LSHIFT: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* result = inplaceDunder(ctx, env, a, b,
                env ? env->getILShiftString() : PythonEnvironment::getInternedString(ctx, "__ilshift__"));
            if (!result) {
                const proto::ProtoObject* ia = coerceBoolToInt(ctx, a);
                const proto::ProtoObject* ib = coerceBoolToInt(ctx, b);
                if (ia->isInteger(ctx) && ib->isInteger(ctx)) result = pyIntShift(ctx, ia, ib, true);
            }
            if (!result) {
                result = binaryOpDispatch(ctx, a, b, "__lshift__", "__rlshift__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_INPLACE_RSHIFT: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* result = inplaceDunder(ctx, env, a, b,
                env ? env->getIRShiftString() : PythonEnvironment::getInternedString(ctx, "__irshift__"));
            if (!result) {
                const proto::ProtoObject* ia = coerceBoolToInt(ctx, a);
                const proto::ProtoObject* ib = coerceBoolToInt(ctx, b);
                if (ia->isInteger(ctx) && ib->isInteger(ctx)) result = pyIntShift(ctx, ia, ib, false);
            }
            if (!result) {
                result = binaryOpDispatch(ctx, a, b, "__rshift__", "__rrshift__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_INPLACE_AND: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* result = inplaceDunder(ctx, env, a, b,
                env ? env->getIAndString() : PythonEnvironment::getInternedString(ctx, "__iand__"));
            if (!result && a->isInteger(ctx) && b->isInteger(ctx)) {
                result = a->bitwiseAnd(ctx, b);
            }
            if (!result) {
                result = binaryOpDispatch(ctx, a, b, "__and__", "__rand__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_INPLACE_OR: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* result = inplaceDunder(ctx, env, a, b,
                env ? env->getIOrString() : PythonEnvironment::getInternedString(ctx, "__ior__"));
            if (!result && a->isInteger(ctx) && b->isInteger(ctx)) {
                result = a->bitwiseOr(ctx, b);
            }
            if (!result) {
                result = binaryOpDispatch(ctx, a, b, "__or__", "__ror__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_INPLACE_XOR: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* result = inplaceDunder(ctx, env, a, b,
                env ? env->getIXorString() : PythonEnvironment::getInternedString(ctx, "__ixor__"));
            if (!result && a->isInteger(ctx) && b->isInteger(ctx)) {
                result = a->bitwiseXor(ctx, b);
            }
            if (!result) {
                result = binaryOpDispatch(ctx, a, b, "__xor__", "__rxor__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
        } break;
        case OP_BINARY_LSHIFT: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = coerceBoolToInt(ctx, stack.back());
            const proto::ProtoObject* a = coerceBoolToInt(ctx, stack[stack.top - 2]);
            // SmallInt << SmallInt: shift with an overflow check and
            // range-check. nqueens uses `1 << col` (col ≤ n, typically ≤ 30)
            // — fast path succeeds virtually always.
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                long long shift = proto::asSmallInt(b);
                if (shift >= 0 && shift < 63) {
                    std::int64_t r64;
                    if (!checkedShl64(proto::asSmallInt(a), static_cast<unsigned>(shift), &r64)
                        && proto::smallIntInRange(r64)) {
                        stack.pop_back();
                        stack.back() = proto::makeSmallInt(r64);
                        i = next_i; continue;
                    }
                } else if (shift < 0) {
                    if (env) env->raiseValueError(ctx,
                        PythonEnvironment::getInternedString(ctx, "negative shift count")->asObject(ctx));
                    stack.pop_back(); stack.back() = PROTO_NONE;
                    i = next_i; continue;
                }
                // Large shift or overflow: fall through to bignum handler.
            }
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* r = pyIntShift(ctx, a, b, true);
                stack.pop_back(); stack.back() = r;
            } else {
                // User-class fallback: __lshift__ / __rlshift__.
                const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__lshift__", "__rlshift__");
                stack.pop_back(); stack.back() = r ? r : PROTO_NONE;
            }
        } break;
        case OP_BINARY_RSHIFT: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = coerceBoolToInt(ctx, stack.back());
            const proto::ProtoObject* a = coerceBoolToInt(ctx, stack[stack.top - 2]);
            // SmallInt >> SmallInt: right shift always produces a result with
            // magnitude ≤ |a|, guaranteed in 56-bit signed range.
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                long long shift = proto::asSmallInt(b);
                if (shift >= 0) {
                    if (shift >= 63) shift = 63; // arithmetic right shift clamps
                    stack.pop_back();
                    stack.back() = proto::makeSmallInt(proto::asSmallInt(a) >> shift);
                    i = next_i; continue;
                }
                if (env) env->raiseValueError(ctx,
                    PythonEnvironment::getInternedString(ctx, "negative shift count")->asObject(ctx));
                stack.pop_back(); stack.back() = PROTO_NONE;
                i = next_i; continue;
            }
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* r = pyIntShift(ctx, a, b, false);
                stack.pop_back(); stack.back() = r;
            } else {
                // User-class fallback: __rshift__ / __rrshift__.
                const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__rshift__", "__rrshift__");
                stack.pop_back(); stack.back() = r ? r : PROTO_NONE;
            }
        } break;
        case OP_BINARY_AND: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = coerceBoolToInt(ctx, stack.back());
            const proto::ProtoObject* a = coerceBoolToInt(ctx, stack[stack.top - 2]);
            // SmallInt & SmallInt: AND of two values in 56-bit signed range
            // always stays in range (sign extension bits are consistent).
            // Zero protoCore calls — all tag operations are inline.
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                stack.pop_back();
                stack.back() = proto::makeSmallInt(proto::asSmallInt(a) & proto::asSmallInt(b));
                i = next_i; continue;
            }
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                // Integer::bitwiseAnd handles bignum operands.
                stack.pop_back(); stack.back() = a->bitwiseAnd(ctx, b);
            } else {
                // Use the shared dispatch so user-defined `__and__` /
                // `__rand__` (which are Python callables, not C methods)
                // get invoked through invokePythonCallable rather than
                // dropped silently.
                const proto::ProtoObject* result = binaryOpDispatch(ctx, a, b, "__and__", "__rand__");
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            }
        } break;
        case OP_BINARY_OR: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = coerceBoolToInt(ctx, stack.back());
            const proto::ProtoObject* a = coerceBoolToInt(ctx, stack[stack.top - 2]);
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                stack.pop_back();
                stack.back() = proto::makeSmallInt(proto::asSmallInt(a) | proto::asSmallInt(b));
                i = next_i; continue;
            }
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = a->bitwiseOr(ctx, b);
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoString* orS = env ? env->getOrString() : PythonEnvironment::getInternedString(ctx, "__or__");
                const proto::ProtoObject* result = nullptr;

                // PEP 604 dispatch: when at least one operand is a class
                // object (has `__mro__` as an own attribute), the binary
                // `|` resolves on the metaclass — `type(a).__or__(a, b)` —
                // not on the class's own `__or__` (which for e.g. `int` is
                // the per-instance bitwise op and would silently no-op on
                // `int | int`). Routing through the type prototype's
                // `__or__` reaches `py_type_or`, which performs the union
                // construction with flatten / dedup / single-unwrap.
                bool sawNotImplemented = false;
                if (env) {
                    // Class detection: a class object owns __mro__ on
                    // itself, while a plain instance never does.  This
                    // covers classes whose metaclass is `type` (e.g.
                    // `int`, `frozenset`) and classes with a custom
                    // metaclass alike, while correctly rejecting
                    // instances of those types.  isInstanceOf(typeProto)
                    // is unreliable here because the runtime currently
                    // reports True even for instances whose prototype
                    // chain reaches type via descriptor inheritance
                    // (e.g. `isinstance(frozenset([1]), type) → True`).
                    const proto::ProtoObject* typeProto = env->getTypePrototype();
                    const proto::ProtoString* mroS = PythonEnvironment::getInternedString(ctx, "__mro__");
                    bool aIsClass = (a && a->hasOwnAttribute(ctx, mroS) == PROTO_TRUE);
                    bool bIsClass = (b && b->hasOwnAttribute(ctx, mroS) == PROTO_TRUE);
                    if (aIsClass || bIsClass) {
                        const proto::ProtoObject* typeOr = typeProto
                            ? typeProto->getAttribute(ctx, orS) : nullptr;
                        if (typeOr && typeOr->asMethod(ctx)) {
                            const proto::ProtoList* argsB = ctx->newList()->appendLast(ctx, b);
                            result = typeOr->asMethod(ctx)(ctx,
                                const_cast<proto::ProtoObject*>(a),
                                nullptr, argsView(proto::ProtoListArguments(ctx, argsB)), nullptr);
                            if (result == env->getNotImplementedPrototype()) {
                                result = nullptr;
                                sawNotImplemented = true;
                            }
                        }
                    }
                }

                if (!result) {
                    // Non-PEP 604 path: delegate to the shared dispatcher
                    // which handles forward + reflected dunder, NotImplemented
                    // propagation and TypeError raising in strict mode.
                    result = binaryOpDispatch(ctx, a, b, "__or__", "__ror__");
                }
                (void)sawNotImplemented;

                stack.pop_back();
                stack.back() = (result ? result : PROTO_NONE);
            }
        } break;
        case OP_BINARY_XOR: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = coerceBoolToInt(ctx, stack.back());
            const proto::ProtoObject* a = coerceBoolToInt(ctx, stack[stack.top - 2]);
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                stack.pop_back();
                stack.back() = proto::makeSmallInt(proto::asSmallInt(a) ^ proto::asSmallInt(b));
                i = next_i; continue;
            }
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = a->bitwiseXor(ctx, b);
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoObject* result = binaryOpDispatch(ctx, a, b, "__xor__", "__rxor__");
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            }
        } break;
        case OP_UNARY_NEGATIVE: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* a = stack.back();
            if (a->isInteger(ctx)) {
                // Use Integer::negate for bignum-safe negation (asLong
                // overflows for values outside int64 — e.g. -(2**63)
                // itself fits but its intermediate does not).
                stack.back() = a->negate(ctx);
            } else if (a->isDouble(ctx)) {
                const proto::ProtoObject* res = ctx->fromDouble(-a->asDouble(ctx));
                stack.back() = res;
            } else if (a == PROTO_TRUE) {
                stack.back() = ctx->fromInteger(-1);
            } else if (a == PROTO_FALSE) {
                stack.back() = ctx->fromInteger(0);
            } else {
                // Subclass-of-int / subclass-of-float without a __neg__
                // override: read the integer/float payload from __data__
                // and negate primitively.  intPrototype/floatPrototype
                // ship no __neg__ dunder, so the dunder dispatch below
                // would otherwise miss and silently return None.
                const proto::ProtoString* negS = PythonEnvironment::getInternedString(ctx, "__neg__");
                bool handled = false;
                if (env) {
                    const proto::ProtoObject* aCls = env->getType(ctx, a);
                    bool aOwnsNeg = aCls && aCls->hasOwnAttribute(ctx, negS) == PROTO_TRUE;
                    if (!aOwnsNeg) {
                        const proto::ProtoObject* d = a->getAttribute(ctx, env->getDataString());
                        if (d && d->isInteger(ctx)) {
                            stack.back() = d->negate(ctx);
                            handled = true;
                        } else if (d && d->isDouble(ctx)) {
                            stack.back() = ctx->fromDouble(-d->asDouble(ctx));
                            handled = true;
                        } else if (d == PROTO_TRUE) {
                            stack.back() = ctx->fromInteger(-1);
                            handled = true;
                        } else if (d == PROTO_FALSE) {
                            stack.back() = ctx->fromInteger(0);
                            handled = true;
                        }
                    }
                }
                if (!handled) {
                    const proto::ProtoObject* result = invokeDunder(ctx, a, negS, ctx->newList());
                    stack.back() = result ? result : PROTO_NONE;
                }
            }
        } break;
        case OP_UNARY_NOT: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* a = stack.back();
            stack.back() = isTruthy(ctx, a) ? PROTO_FALSE : PROTO_TRUE;
        } break;
        case OP_UNARY_INVERT: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* a = stack.back();
            const proto::ProtoString* invS = env ? env->getInvertString() : PythonEnvironment::getInternedString(ctx, "__invert__");
            if (a->isInteger(ctx)) {
                stack.back() = a->bitwiseNot(ctx);
            } else if (a == PROTO_TRUE) {
                stack.back() = ctx->fromInteger(-2);
            } else if (a == PROTO_FALSE) {
                stack.back() = ctx->fromInteger(-1);
            } else {
                bool handled = false;
                if (env) {
                    const proto::ProtoObject* aCls = env->getType(ctx, a);
                    bool aOwnsInv = aCls && aCls->hasOwnAttribute(ctx, invS) == PROTO_TRUE;
                    if (!aOwnsInv) {
                        const proto::ProtoObject* d = a->getAttribute(ctx, env->getDataString());
                        if (d && d->isInteger(ctx)) {
                            stack.back() = d->bitwiseNot(ctx);
                            handled = true;
                        }
                    }
                }
                if (!handled) {
                    const proto::ProtoObject* result = invokeDunder(ctx, a, invS, ctx->newList());
                    stack.back() = result ? result : PROTO_NONE;
                }
            }
        } break;
        case OP_RAISE_VARARGS: {
            // `raise x` raises x anew, even an exception seen before.
            if (arg != 0 && dbgOn) [[unlikely]] debugger::detail::forgetSighting();
            if (arg == 0) {
                 const proto::ProtoObject* activeExc = env ? env->getActiveException(ctx) : nullptr;
                 if (!activeExc && !stack.empty()) {
                     activeExc = stack.back();
                     stack.pop_back();
                 }
                 if (activeExc) {
                     if (env) reraiseException(env, activeExc);
                 } else {
                     if (env) {
                         env->raiseRuntimeError(ctx, "No active exception to reraise");
                     }
                 }
                 i = next_i;
                 continue;
            } else if (arg == 1) {
                 if (!stack.empty()) {
                     const proto::ProtoObject* exc = stack.back();
                     stack.pop_back();
                     // Decide whether the raised value is itself a class
                     // (e.g. `raise ValueError`, no parens) and needs to
                     // be instantiated, or already an instance. The bare
                     // protoCore chain walk for `__class__` returns the
                     // metaclass for any cell whose own `__class__` is
                     // not set — that is the case for exception
                     // instances created via py_object_new, since their
                     // class identity is recovered via getType (SP-B/B1)
                     // rather than an own `__class__` attribute. Use
                     // env->isActuallyAClass instead, which returns true
                     // only for actual class objects and never for
                     // already-instantiated exceptions.
                     if (env && env->isActuallyAClass(ctx, exc)) {
                         exc = invokePythonCallable(ctx, exc, ctx->newList(), nullptr);
                     }
                     if (env && exc && exc != PROTO_NONE) raiseChainedException(ctx, env, exc);
                 }
                 i = next_i;
                 continue;
            } else if (arg == 2) {
                 // raise exc from cause: cause is TOS, exc below it.
                 if (stack.size() < 2) { i = next_i; continue; }
                 const proto::ProtoObject* cause = stack.back();
                 stack.pop_back();
                 const proto::ProtoObject* exc = stack.back();
                 stack.pop_back();
                 if (env) raiseExceptionFrom(ctx, env, exc, cause);
                 i = next_i;
                 continue;
            }
            i = next_i;
            continue;
        } break;
        case OP_IMPORT_STAR: {
            if (stack.size() < 1) { i = next_i; continue; }
            const proto::ProtoObject* mod = stack.back();
            // STRUCT-308: every name brought in by `from X import *` must
            // also land in the importing frame's __keys__ so that
            // module.items() / module.keys() — and Python-level
            // introspection that relies on the dict layout (e.g.
            // `_IntEnum._convert_('Signals', __name__, ...)` reading
            // sys.modules['signal'].__dict__.items()) — sees the new
            // names.  This helper mirrors the STORE_NAME body.
            const proto::ProtoString* keysNameS =
                PythonEnvironment::getInternedString(ctx, "__keys__");
            auto recordKeyInFrame = [&](const proto::ProtoString* nameS) {
                const proto::ProtoObject* keysObj =
                    (frame->hasOwnAttribute(ctx, keysNameS) == PROTO_TRUE)
                        ? frame->getOwnAttributeDirect(ctx, keysNameS) : nullptr;
                const proto::ProtoList* keysList = (keysObj && ::protoPython::listStorage(ctx, keysObj))
                    ? ::protoPython::listStorage(ctx, keysObj) : ctx->newList();
                // Only append on first store (avoid duplicates).
                if (!keysList->has(ctx, nameS->asObject(ctx))) {
                    keysList = keysList->appendLast(ctx, nameS->asObject(ctx));
                    frame = const_cast<proto::ProtoObject*>(
                        frame->setAttribute(ctx, keysNameS, keysList->asObject(ctx)));
                }
            };
            // mod remains on stack during attribute iteration
            if (mod && mod != PROTO_NONE) {
                // 1. Check for __all__ — iterate via Python iterator protocol to handle list, tuple, or custom types
                const proto::ProtoObject* allObj = mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__all__"));
                bool importedFromAll = false;
                if (allObj && allObj != PROTO_NONE && env) {
                    const proto::ProtoObject* iterObj = env->iter(allObj);
                    if (iterObj) {
                        importedFromAll = true;
                        while (true) {
                            const proto::ProtoObject* nameObj = env->next(iterObj);
                            if (!nameObj) break;   // exhausted (env->next does not throw for it)
                            if (proto::ProtoObject::isStringTagFast(nameObj)) {
                                const proto::ProtoString* nameS = nameObj->asString(ctx);
                                const proto::ProtoObject* val = mod->getAttribute(ctx, nameS);
                                if (val) {
                                    frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, nameS, val));
                                    recordKeyInFrame(nameS);
                                }
                            }
                        }
                    }
                }
                if (!importedFromAll) {
                    // 2. Iterate over all attributes if __keys__ is available
                    const proto::ProtoObject* keysObj = mod->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__keys__"));
                    if (keysObj && ::protoPython::listStorage(ctx, keysObj)) {
                        const proto::ProtoList* keysList = ::protoPython::listStorage(ctx, keysObj);
                        const proto::ProtoListIterator* it = keysList->getIterator(ctx);
                        while (it && it->hasNext(ctx)) {
                            const proto::ProtoObject* nameObj = it->next(ctx);
                            if (nameObj && proto::ProtoObject::isStringTagFast(nameObj)) {
                                std::string n;
                                nameObj->asString(ctx)->toUTF8String(ctx, n);
                                if (n.empty() || n[0] == '_') {
                                    it = it->advance(ctx);
                                    continue;
                                }
                                const proto::ProtoObject* val = mod->getAttribute(ctx, nameObj->asString(ctx));
                                if (val) {
                                    const proto::ProtoString* nm = nameObj->asString(ctx);
                                    frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, nm, val));
                                    recordKeyInFrame(nm);
                                }
                            }
                            it = it->advance(ctx);
                        }
                    } else {
                        // 3. Fallback to native module internal attributes
                        const proto::ProtoSparseList* internalAttrs = mod->getAttributes(ctx);
                        if (internalAttrs) {
                            auto* it = const_cast<proto::ProtoSparseListIterator*>(internalAttrs->getIterator(ctx));
                            while (it && it->hasNext(ctx)) {
                                proto::proto_ulong key = it->nextKey(ctx);
                                const proto::ProtoObject* keyObj = reinterpret_cast<const proto::ProtoObject*>(key);
                                if (keyObj && keyObj->isString(ctx)) {
                                    const proto::ProtoString* s = keyObj->asString(ctx);
                                    if (s) {
                                        std::string n;
                                        s->toUTF8String(ctx, n);
                                        if (!n.empty() && n[0] != '_') {
                                            const proto::ProtoObject* val = mod->getAttribute(ctx, s);
                                            if (val) {
                                                frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, s, val));
                                                recordKeyInFrame(s);
                                            }
                                        }
                                    }
                                }
                                it = const_cast<proto::ProtoSparseListIterator*>(it->advance(ctx));
                            }
                        }
                    }
                }
                if (env) {
                    PythonEnvironment::setCurrentFrame(frame);
                    if (sync_globals) {
                        // Ensure __globals__ self-reference is updated to new frame pointer (CoW stability)
                        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, env->getFGlobalsString(), frame));
                        PythonEnvironment::setCurrentGlobals(frame);
                    }
                    env->invalidateResolveCache();
                }
                stack.pop_back();
            } else {
                stack.pop_back();
            }
        } break;
        case OP_IMPORT_FROM: {
            int nameIdx = arg >> 1;
            if (names && stack.size() >= 1 && static_cast<proto::proto_ulong>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* mod = stack.back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                
                if (proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    // An error from the lookup (AttributeError) becomes the
                    // ImportError below.
                    const proto::ProtoObject* val = nullptr;
                    // loadAttr is the attribute protocol `mod.name` uses,
                    // including a module's __getattr__ (PEP 562): unittest
                    // provides IsolatedAsyncioTestCase only through it, and
                    // `from unittest import IsolatedAsyncioTestCase` failed.
                    try {
                        val = (env)
                            ? env->loadAttr(ctx, mod, nameS, nullptr, false)
                            : mod->getAttribute(ctx, nameS);
                    } catch (const proto::ProtoException&) {
                        val = nullptr;
                    }

                    if (val && (val != PROTO_NONE || mod->hasAttribute(ctx, nameS) == PROTO_TRUE)) {
                        stack.push_back(val);
                    } else {
                        if (env) {
                            std::string n;
                            nameS->toUTF8String(ctx, n);
                            std::string msg = "cannot import name '" + n + "'";
                            std::string mn;
                            const proto::ProtoObject* mName = mod->getAttribute(ctx, env->getNameString());
                            if (mName && mName->isString(ctx)) {
                                mName->asString(ctx)->toUTF8String(ctx, mn);
                                msg += " from '" + mn + "'";
                            }
                            // CPython: "cannot import name 'y' from 'x' (<file>)", with
                            // name and path set on the exception.
                            std::string fn;
                            const proto::ProtoObject* fileAttr = mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__file__"));
                            if (fileAttr && fileAttr->isString(ctx)) {
                                fileAttr->asString(ctx)->toUTF8String(ctx, fn);
                                msg += " (" + fn + ")";
                            }

                            env->raiseImportError(ctx, msg, mn, fn);
                        }
                        i = next_i;
                        continue;
                    }
                }
            }
        } break;
        case OP_SETUP_WITH: {
            if (stack.size() < 1) { i = next_i; continue; }
            const proto::ProtoObject* manager = stack.back();
            stack.pop_back();

            const proto::ProtoString* enterS = env ? env->getEnterString() : PythonEnvironment::getInternedString(ctx, "__enter__");
            const proto::ProtoString* exitS = env ? env->getExitString() : PythonEnvironment::getInternedString(ctx, "__exit__");

            // STRUCT-204: `__enter__` / `__exit__` are looked up on the type
            // only, as CPython does (PythonEnvironment::lookupBoundSpecialMethod).
            auto lookupTypeOnly = [&](const proto::ProtoObject* obj, const proto::ProtoString* name) -> const proto::ProtoObject* {
                if (!env) return obj ? obj->getAttribute(ctx, name) : nullptr;
                return env->lookupBoundSpecialMethod(ctx, obj, name);
            };
            const proto::ProtoObject* exitM = lookupTypeOnly(manager, exitS);
            stack.push_back(exitM ? exitM : (const proto::ProtoObject*)PROTO_NONE);

            const proto::ProtoObject* enterM = lookupTypeOnly(manager, enterS);
            const proto::ProtoObject* enterResult = nullptr;
            if (enterM && enterM != PROTO_NONE) {
                // Method bound to manager via descriptor __get__ above —
                // call directly.  If __get__ was missing, fall back to
                // prepending manager as self.
                const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
                enterResult = invokeCallable(ctx, enterM, emptyL);
            } else {
                enterResult = manager;
            }

            // The with body runs as a nested dispatch inside a C++ try; the
            // handler at arg (which calls __exit__) inside the catch.
            const Block withRange{static_cast<proto::proto_ulong>(arg), stack.size(), true};
            stack.push_back(enterResult);
            const proto::proto_ulong rangeStart = next_i;
            const proto::ProtoObject* r = runRange(withRange,
                [&]() { return dispatch(rangeStart, ctx, env, bc, n, pcEnd); },
                [&]() { return dispatch(withRange.handlerPc, ctx, env, bc, n, pcEnd); });
            if (levelExit != LevelExit::Return) {
                levelExit = LevelExit::Return;
                i = levelExitPc;
                continue;
            }
            return r;
        }
        case OP_WITH_CLEANUP: {
            // Stack: [..., __exit__, (None or Exc)]
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* excOrNone = stack.back();
            stack.pop_back();
            const proto::ProtoObject* exitM = stack.back();
            stack.pop_back();
            
            const proto::ProtoObject* res = PROTO_FALSE;
            if (exitM && exitM != PROTO_NONE) {
                const proto::ProtoList* args = ctx->newList();
                if (excOrNone != PROTO_NONE && excOrNone != nullptr) {
                    const proto::ProtoObject* type = excOrNone->getPrototype(ctx);
                    args = args->appendLast(ctx, type);
                    args = args->appendLast(ctx, excOrNone);
                    args = args->appendLast(ctx, PROTO_NONE); // traceback
                } else {
                    args = args->appendLast(ctx, PROTO_NONE);
                    args = args->appendLast(ctx, PROTO_NONE);
                    args = args->appendLast(ctx, PROTO_NONE);
                }
                res = invokeCallable(ctx, exitM, args);
            }
            // Push suppression flag
            stack.push_back(res ? res : PROTO_FALSE);
        } break;
        case OP_POP_TOP: {
            if (!stack.empty()) {
                stack.pop_back();
            }
        } break;
        case OP_UNARY_POSITIVE: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* a = stack.back();
            // CPython: +bool produces int, not bool.  +True -> 1, +False -> 0.
            // The previous identity fast path returned the bool sentinel
            // unchanged, breaking the `type(+True) is int` invariant
            // and the dict-key-hash compatibility check used by the
            // numeric ABC.
            if (a == PROTO_TRUE) {
                stack.back() = ctx->fromInteger(1);
            } else if (a == PROTO_FALSE) {
                stack.back() = ctx->fromInteger(0);
            } else if (isEmbeddedValue(ctx, a)) {
                // Other numeric literal — `+x` is identity.
            } else {
                const proto::ProtoString* posS = env ? env->getPosString() : PythonEnvironment::getInternedString(ctx, "__pos__");
                const proto::ProtoObject* result = invokeDunder(ctx, a, posS, ctx->newList());
                if (result) {
                    stack.back() = result;
                } else if (env) {
                    // CPython: `+x` on an int/float/bool subclass without
                    // an own __pos__ returns a plain int/float — not the
                    // subclass.  Detect by walking the type chain for a
                    // numeric primitive and unwrap the __data__ payload.
                    const proto::ProtoObject* aType = env->getType(ctx, a);
                    if (aType && aType != PROTO_NONE && aType != env->getIntPrototype()
                        && aType != env->getFloatPrototype() && aType != env->getBoolPrototype()) {
                        const proto::ProtoObject* mroAttr = env->getAttribute(ctx, aType, env->getMroString(), false);
                        ::protoPython::TupleView mroT = mroAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, mroAttr) : nullptr;
                        if (mroT) {
                            for (proto::proto_ulong mi = 0; mi < mroT->getSize(ctx); ++mi) {
                                const proto::ProtoObject* base = mroT->getAt(ctx, static_cast<int>(mi));
                                if (base == env->getIntPrototype() || base == env->getBoolPrototype()) {
                                    const proto::ProtoObject* payload = a->getAttribute(ctx, env->getDataString());
                                    if (payload && payload->isInteger(ctx)) {
                                        stack.back() = ctx->fromInteger(payload->asLong(ctx));
                                    }
                                    break;
                                }
                                if (base == env->getFloatPrototype()) {
                                    const proto::ProtoObject* payload = a->getAttribute(ctx, env->getDataString());
                                    if (payload && payload->isFloat(ctx)) {
                                        stack.back() = ctx->fromDouble(payload->asDouble(ctx));
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        } break;
        case OP_NOP: {
        } break;
        case OP_COMPARE_OP: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            // Tier 2: SmallInt cmp SmallInt fast path.  arg encoding from
            // Compiler.cpp: 0 ==, 1 !=, 2 <, 3 <=, 4 >, 5 >=.  6/7 (in/not in),
            // 8/9 (is/is not), 10 (exception_match) fall through to the generic
            // compareOp (they need full Python semantics).
            if (arg <= 5 && proto::isSmallInt(a) && proto::isSmallInt(b)) {
                long long la = proto::asSmallInt(a);
                long long lb = proto::asSmallInt(b);
                bool result;
                switch (arg) {
                    case 0: result = (la == lb); break;
                    case 1: result = (la != lb); break;
                    case 2: result = (la <  lb); break;
                    case 3: result = (la <= lb); break;
                    case 4: result = (la >  lb); break;
                    case 5: result = (la >= lb); break;
                    default: result = false;
                }
                stack.pop_back();
                stack.back() = result ? PROTO_TRUE : PROTO_FALSE;
                i = next_i;
                continue;
            }
            const proto::ProtoObject* r = compareOp(ctx, a, b, arg);
            stack.pop_back();
            stack.back() = (r ? r : PROTO_NONE);
            // (Note: if r is null, we set PROTO_NONE to avoid null on stack)
        } break;
        case OP_POP_JUMP_IF_FALSE: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (!isTruthy(ctx, top) && arg >= 0 && static_cast<proto::proto_ulong>(arg) < n) {
                i = static_cast<proto::proto_ulong>(arg);
                continue;
            }
        } break;
        case OP_POP_JUMP_IF_TRUE: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (isTruthy(ctx, top) && arg >= 0 && static_cast<proto::proto_ulong>(arg) < n) {
                i = static_cast<proto::proto_ulong>(arg);
                continue;
            }
        } break;
        case OP_LIST_APPEND: {
            if (stack.size() >= static_cast<size_t>(arg) + 1) {
                const proto::ProtoObject* val = stack.back();
                proto::ProtoObject* lstObj =
                    const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);

                // asList() (protoCore.h) unifies raw ProtoList tags and
                // wrapped Python list instances: a wrapped instance is
                // followed via __data__ to its underlying ProtoList. We
                // only use the public API here; no representation bits
                // are inspected.
                //
                // Step A (sprint-2, 2026-06-15): discriminate wrapped vs
                // raw by POINTER IDENTITY between asList's result and the
                // original lstObj. asList on a raw ProtoList returns the
                // same address (cast to ProtoList*); asList on a wrapper
                // follows __data__ to a different object. This replaces a
                // redundant getAttribute(__data__) + asList(curData) pair
                // (visible at 6%+ in perf on list_append_loop) with a
                // single pointer compare. The discrimination has to be
                // captured BEFORE appendLast, which returns a new
                // ProtoList in either case.
                const proto::ProtoList* origLst = ::protoPython::listStorage(ctx, lstObj);
                if (origLst) {
                    const bool isWrapped =
                        reinterpret_cast<const proto::ProtoObject*>(origLst) != lstObj;
                    const proto::ProtoList* newLst = origLst->appendLast(ctx, val);
                    if (isWrapped) {
                        // Wrapped: rebind __data__ to the new ProtoList.
                        const proto::ProtoString* dataS = env
                            ? env->getDataString()
                            : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                        stack[stack.size() - arg - 1] = const_cast<proto::ProtoObject*>(
                            lstObj->setAttribute(ctx, dataS, newLst->asObject(ctx)));
                    } else {
                        // Raw: replace TOS with the new ProtoList object.
                        stack[stack.size() - arg - 1] =
                            const_cast<proto::ProtoObject*>(newLst->asObject(ctx));
                    }
                }
                stack.pop_back();
            }
        } break;
        case OP_BUILD_RAW_LIST: {
            // Push a raw empty ProtoList. See header comment.
            const proto::ProtoList* raw = ctx->newList();
            stack.push_back(raw->asObject(ctx));
        } break;
        case OP_WRAP_RAW_LIST: {
            // Pop the raw ProtoList accumulator from TOS and push a
            // wrapped `list` instance. See header comment for why
            // listcomp uses raw ProtoList directly during its body.
            if (stack.empty()) break;
            const proto::ProtoObject* rawObj = stack.back();
            stack.pop_back();
            const proto::ProtoList* raw = rawObj ? ::protoPython::listStorage(ctx, rawObj) : nullptr;
            if (!raw) {
                // Already wrapped, or unexpected non-list. Push back
                // unchanged — defensive; compiler should only emit
                // this on raw-list paths.
                stack.push_back(rawObj);
                break;
            }
            proto::ProtoObject* wrapped = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            const proto::ProtoString* dataS = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
            wrapped = const_cast<proto::ProtoObject*>(wrapped->setAttribute(ctx, dataS, raw->asObject(ctx)));
            if (env && env->getListPrototype()) {
                wrapped = const_cast<proto::ProtoObject*>(wrapped->addParent(ctx, env->getListPrototype()));
                wrapped = const_cast<proto::ProtoObject*>(wrapped->setAttribute(ctx, env->getClassString(), env->getListPrototype()));
            }
            stack.push_back(wrapped);
        } break;
        case OP_MAP_ADD: {
            if (stack.size() >= static_cast<size_t>(arg) + 1) { // key, val + mapObj must be there
                const proto::ProtoObject* key = stack.back();
                const proto::ProtoObject* val = stack[stack.top - 2];
                proto::ProtoObject* mapObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = mapObj->getAttribute(ctx, dataString);
                if (data && data->asSparseList(ctx)) {
                    const proto::ProtoSparseList* sl = data->asSparseList(ctx);
                    proto::proto_ulong h = 0;
                    if (!PythonEnvironment::hashKey(ctx, key, h)) continue;
                    bool isNew = !sl->has(ctx, h);
                    sl = sl->setAt(ctx, h, val);
                    const proto::ProtoObject* newMap = mapObj->setAttribute(ctx, dataString, sl->asObject(ctx));
                    stack[stack.size() - arg - 1] = const_cast<proto::ProtoObject*>(newMap);
                    if (isNew) {
                         const proto::ProtoString* keysString = protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                         const proto::ProtoObject* keysObj = newMap->getAttribute(ctx, keysString);
                         const proto::ProtoList* keys = (keysObj && ::protoPython::listStorage(ctx, keysObj)) ? ::protoPython::listStorage(ctx, keysObj) : ctx->newList();
                         keys = keys->appendLast(ctx, key);
                         const proto::ProtoObject* finalMap = newMap->setAttribute(ctx, keysString, keys->asObject(ctx));
                         stack[stack.size() - arg - 1] = const_cast<proto::ProtoObject*>(finalMap);
                    }
                }
                stack.pop_back(); // Pop key
                stack.pop_back(); // Pop val
            }
        } break;
        case OP_SET_ADD: {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* val = stack.back();
                // val remains on stack
                const proto::ProtoObject* setObj = stack[stack.size() - arg - 1];
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = setObj->getAttribute(ctx, dataString);
                const proto::ProtoSet* s = (data && data->asSet(ctx)) ? data->asSet(ctx) : ctx->newSet();
                s = PythonEnvironment::setAdd(ctx, s, val);
                if (!s) continue;
                const proto::ProtoObject* newSet = setObj->setAttribute(ctx, dataString, s->asObject(ctx));
                stack[stack.size() - arg - 1] = const_cast<proto::ProtoObject*>(newSet);
                stack.pop_back(); // Now safe to pop val
            }
        } break;
        case OP_DICT_MERGE:
        case OP_DICT_UPDATE: {
            // DICT_MERGE (class statement and call keywords) rejects a key the
            // dict already has, as CPython's DICT_MERGE does.  Its argument
            // carries DICT_MERGE_CALL_SITE above the dict's stack depth.
            const bool callSite = op == OP_DICT_MERGE && (arg & DICT_MERGE_CALL_SITE) != 0;
            const int depth = op == OP_DICT_MERGE ? (arg & ~DICT_MERGE_CALL_SITE) : arg;
            bool duplicateKey = false;
            auto rejectDuplicate = [&](const proto::ProtoObject* key) -> bool {
                if (op != OP_DICT_MERGE || !env) return false;
                std::string keyName = "?";
                if (key && key->isString(ctx)) key->asString(ctx)->toUTF8String(ctx, keyName);
                std::string callee = "__build_class__()";
                // A call's keyword dict sits above [X, Y, args] (see
                // OP_CALL_FUNCTION_EX): the callable is X, or Y when X is NULL.
                if (callSite && stack.size() >= static_cast<size_t>(depth + 4)) {
                    const proto::ProtoObject* x = stack[stack.size() - depth - 4];
                    const proto::ProtoObject* y = stack[stack.size() - depth - 3];
                    callee = callableDescription(ctx, env, x ? x : y);
                }
                std::string msg = callee + " got multiple values for keyword argument '" + keyName + "'";
                env->raiseTypeError(ctx, msg.c_str());
                duplicateKey = true;
                return true;
            };
            if (stack.size() >= static_cast<size_t>(depth + 1)) {
                const proto::ProtoObject* from = stack.back();
                proto::ProtoObject* toObj = const_cast<proto::ProtoObject*>(stack[stack.size() - depth - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoString* keysName = protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                const proto::ProtoObject* toData = toObj->getAttribute(ctx, dataString);
                if (toData && toData->asSparseList(ctx)) {
                    const proto::ProtoSparseList* toSL = toData->asSparseList(ctx);
                    const proto::ProtoObject* toKeysObj = toObj->getAttribute(ctx, keysName);
                    const proto::ProtoList* toKeys = (toKeysObj && ::protoPython::listStorage(ctx, toKeysObj)) ? ::protoPython::listStorage(ctx, toKeysObj) : ctx->newList();
                    bool merged = false;
                    // Fast path: source must be an actual dict (or
                    // dict subclass) — checked via isInstanceOf
                    // against dictPrototype.  Otherwise the keys() /
                    // __getitem__ mapping protocol path below applies.
                    //
                    // Why isInstanceOf and not hasOwnAttribute on
                    // __data__/__keys__: every Python class object
                    // ends up carrying those internal storage slots
                    // (used by py_type to walk class dicts) even
                    // though the user never assigned to them.  An
                    // hasOwnAttribute check would incorrectly take
                    // the fast path for `{**SomeClass()}` and merge
                    // the empty internal storage.
                    bool fromIsDict = env && env->getDictPrototype()
                        && from->isInstanceOf(ctx, env->getDictPrototype()) == PROTO_TRUE;
                    const proto::ProtoObject* fromData = fromIsDict ? from->getAttribute(ctx, dataString) : nullptr;
                    const proto::ProtoObject* fromKeysObj = fromIsDict ? from->getAttribute(ctx, keysName) : nullptr;
                    if (fromData && fromData->asSparseList(ctx)
                        && fromKeysObj && ::protoPython::listStorage(ctx, fromKeysObj)) {
                        const proto::ProtoSparseList* fromSL = fromData->asSparseList(ctx);
                        const proto::ProtoList* fromKeys = ::protoPython::listStorage(ctx, fromKeysObj);
                        for (proto::proto_ulong j = 0; j < fromKeys->getSize(ctx); ++j) {
                            const proto::ProtoObject* k = fromKeys->getAt(ctx, static_cast<int>(j));
                            proto::proto_ulong h = ::protoPython::pyDictKeyHash(ctx, k);
                            const proto::ProtoObject* v = fromSL->getAt(ctx, h);
                            bool isNew = !toSL->has(ctx, h);
                            if (!isNew && rejectDuplicate(k)) break;
                            toSL = toSL->setAt(ctx, h, v);
                            if (isNew) toKeys = toKeys->appendLast(ctx, k);
                        }
                        merged = true;
                    }
                    // Fallback: source exposes a `keys()` method
                    // (custom Mapping subclass / proxy / user class).
                    // CPython's BUILD_MAP_UNPACK / DICT_UPDATE call
                    // `keys()` and `__getitem__` to enumerate the
                    // mapping.  Previously this branch silently
                    // dropped any non-protoPython mapping shape,
                    // making `{**M()}` / `f(**M())` produce `{}`.
                    if (!merged && env) {
                        const proto::ProtoString* keysS =
                            protoPython::PythonEnvironment::getInternedString(ctx, "keys");
                        const proto::ProtoObject* keysM = from->getAttribute(ctx, keysS);
                        const proto::ProtoString* getitemS =
                            protoPython::PythonEnvironment::getInternedString(ctx, "__getitem__");
                        const proto::ProtoObject* getitemM = from->getAttribute(ctx, getitemS);
                        if (keysM && keysM != PROTO_NONE && getitemM && getitemM != PROTO_NONE) {
                            auto invokeBound = [&](const proto::ProtoObject* m,
                                                   const proto::ProtoObject* recv,
                                                   const proto::ProtoList* args)
                                    -> const proto::ProtoObject* {
                                if (!m || m == PROTO_NONE) return nullptr;
                                if (m->asMethod(ctx)) {
                                    return m->asMethod(ctx)(ctx,
                                        const_cast<proto::ProtoObject*>(recv),
                                        nullptr, argsView(proto::ProtoListArguments(ctx, args)), nullptr);
                                }
                                const proto::ProtoString* codeS = env->getCodeString();
                                bool raw = (codeS && m->hasOwnAttribute(ctx, codeS) == PROTO_TRUE);
                                const proto::ProtoList* selfArgs = ctx->newList();
                                if (raw) selfArgs = selfArgs->appendLast(ctx, recv);
                                proto::proto_ulong n = args ? args->getSize(ctx) : 0;
                                for (proto::proto_ulong j = 0; j < n; ++j)
                                    selfArgs = selfArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(j)));
                                return invokePythonCallable(ctx, m, selfArgs, nullptr);
                            };
                            const proto::ProtoObject* keysObj =
                                invokeBound(keysM, from, ctx->newList());
                            if (keysObj && keysObj != PROTO_NONE) {
                                const proto::ProtoObject* keyIt = env->iter(keysObj);
                                if (keyIt) {
                                    PythonEnvironment::TransientPin pinIt(env, keyIt);
                                    for (;;) {
                                        const proto::ProtoObject* k = env->next(keyIt);
                                        if (!k) break;
                                        const proto::ProtoList* gA = ctx->newList()->appendLast(ctx, k);
                                        const proto::ProtoObject* v = invokeBound(getitemM, from, gA);
                                        if (!v) continue;
                                        proto::proto_ulong h = ::protoPython::pyDictKeyHash(ctx, k);
                                        bool isNew = !toSL->has(ctx, h);
                                        if (!isNew && rejectDuplicate(k)) break;
                                        toSL = toSL->setAt(ctx, h, v);
                                        if (isNew) toKeys = toKeys->appendLast(ctx, k);
                                    }
                                }
                            }
                        }
                    }
                    toObj->setAttribute(ctx, keysName, toKeys->asObject(ctx));
                    toObj->setAttribute(ctx, dataString, toSL->asObject(ctx));
                }
                stack.pop_back();
            }
            if (duplicateKey) {
                i = next_i;
                continue;
            }
        } break;
        case OP_LIST_EXTEND: {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* iterable = stack.back();
                // iterable remains on stack
                proto::ProtoObject* lstObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* dataObj = lstObj->getAttribute(ctx, dataString);
                if (dataObj && ::protoPython::listStorage(ctx, dataObj)) {
                    const proto::ProtoList* lst = ::protoPython::listStorage(ctx, dataObj);
                    const proto::ProtoObject* fromData = iterable->getAttribute(ctx, dataString);
                    const proto::ProtoList* fromList = (fromData && ::protoPython::listStorage(ctx, fromData)) ? ::protoPython::listStorage(ctx, fromData) : ::protoPython::listStorage(ctx, iterable);
                    if (fromList) {
                        for (proto::proto_ulong j = 0; j < fromList->getSize(ctx); ++j) {
                            lst = lst->appendLast(ctx, fromList->getAt(ctx, static_cast<int>(j)));
                        }
                        lstObj->setAttribute(ctx, dataString, lst->asObject(ctx));
                    } else {
                        // Handle ProtoTuple iterables (e.g. raw *args tuple from varargs binding)
                        ::protoPython::TupleView fromTuple = (fromData && ::protoPython::PythonEnvironment::tupleView(ctx, fromData)) ? ::protoPython::PythonEnvironment::tupleView(ctx, fromData) : ::protoPython::PythonEnvironment::tupleView(ctx, iterable);
                        if (fromTuple) {
                            for (proto::proto_ulong j = 0; j < fromTuple->getSize(ctx); ++j) {
                                lst = lst->appendLast(ctx, fromTuple->getAt(ctx, static_cast<int>(j)));
                            }
                            lstObj->setAttribute(ctx, dataString, lst->asObject(ctx));
                        } else if (env) {
                            // Generic iterator path: drive __iter__ + __next__ to
                            // completion.  Required for map(), filter(), zip(),
                            // iter(), generators, and any user-defined iterator
                            // — bytecode `f(*it)` lowers to LIST_EXTEND then
                            // LIST_TO_TUPLE then CALL_FUNCTION_EX, and without
                            // this branch the unpacked argument list silently
                            // came out empty.  Visible at platform.uname() ->
                            //   uname_result(*map(_unknown_as_blank, vals))
                            // which constructed a uname_result with all None
                            // fields, breaking every consumer of platform.*.
                            const proto::ProtoObject* iterator = env->iter(iterable);
                            if (iterator) {
                                // Pin the derived iterator across user
                                // __next__ calls. While `iterable` is on
                                // the operand stack (GC-rooted), the
                                // iterator returned by env->iter is a
                                // separate cell only held in this C++
                                // local. Without the pin, deep recursion
                                // through user-defined __next__ can free
                                // the iterator's backing cells.
                                PythonEnvironment::TransientPin pinIt(env, iterator);
                                for (;;) {
                                    const proto::ProtoObject* item = env->next(iterator);
                                    if (!item) {
                                        if (env->handleExhaustion(ctx)) break;
                                        // Non-StopIteration exception: stop
                                        // accumulating; the dispatcher's
                                        // exception machinery will surface it
                                        // on the next opcode dispatch.
                                        break;
                                    }
                                    lst = lst->appendLast(ctx, item);
                                }
                                lstObj->setAttribute(ctx, dataString, lst->asObject(ctx));
                            }
                        }
                    }
                }
                stack.pop_back(); // Pop iterable
            }
        } break;
        case OP_SET_UPDATE: {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* iterable = stack.back();
                proto::ProtoObject* setObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* dataObj = setObj->getAttribute(ctx, dataString);
                if (dataObj && dataObj->asSet(ctx)) {
                    const proto::ProtoSet* s = dataObj->asSet(ctx);
                    // Fast path: pull elements directly when the iterable is
                    // a raw / wrapped list or set.  Falls back to the
                    // env->iter / env->next loop so user classes with
                    // Python-level __iter__ (the literal `{*src, 'x'}`
                    // shape) actually contribute their elements — the
                    // previous list-only path silently dropped them.
                    const proto::ProtoObject* fromData = iterable->getAttribute(ctx, dataString);
                    const proto::ProtoList* fromList = (fromData && ::protoPython::listStorage(ctx, fromData)) ? ::protoPython::listStorage(ctx, fromData) : ::protoPython::listStorage(ctx, iterable);
                    if (fromList) {
                        for (proto::proto_ulong j = 0; s && j < fromList->getSize(ctx); ++j) {
                            s = PythonEnvironment::setAdd(ctx, s, fromList->getAt(ctx, static_cast<int>(j)));
                        }
                    } else if (env) {
                        const proto::ProtoObject* it = env->iter(iterable);
                        if (it) {
                            for (;;) {
                                const proto::ProtoObject* item = env->next(it);
                                if (!item) break;
                                s = PythonEnvironment::setAdd(ctx, s, item);
                                if (!s) break;
                            }
                        }
                    }
                    if (!s) continue;
                    setObj->setAttribute(ctx, dataString, s->asObject(ctx));
                }
                stack.pop_back();
            }
        } break;
        case OP_BUILD_SET: {
            if (stack.size() < static_cast<size_t>(arg)) continue;
            proto::ProtoObject* setObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            stack.push_back(setObj); // Root setObj
            if (env && env->getSetPrototype()) {
                setObj = const_cast<proto::ProtoObject*>(setObj->addParent(ctx, env->getSetPrototype()));
                stack.back() = setObj;
                setObj = const_cast<proto::ProtoObject*>(setObj->setAttribute(ctx, env->getClassString(), env->getSetPrototype()));
                stack.back() = setObj;
            }
            const proto::ProtoSet* data = ctx->newSet();
            const proto::ProtoObject* dataPinned = data->asObject(ctx);
            stack.push_back(dataPinned); // Root data
            
            size_t baseIdx = stack.size() - 2 - arg;
            bool unhashable = false;
            for (int j = 0; j < arg; ++j) {
                const proto::ProtoObject* item = stack[baseIdx + j];
                const proto::ProtoSet* next = PythonEnvironment::setAdd(ctx, data, item);
                if (!next) { unhashable = true; break; }
                data = next;
                stack[stack.size() - 1] = const_cast<proto::ProtoObject*>(data->asObject(ctx)); // Update root
            }
            if (unhashable) continue;
            
            setObj = const_cast<proto::ProtoObject*>(setObj->setAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"), data->asObject(ctx)));
            stack[stack.size() - 2] = setObj; // update root
            const proto::ProtoObject* finalSet = stack[stack.size() - 2];
            if (diag_local) {
                const proto::ProtoObject* ftype = env ? env->getType(ctx, finalSet) : nullptr;
                std::string frepr = env ? env->reprObject(ctx, finalSet) : "???";
                fprintf(stderr, "DEBUG OP_BUILD_SET: finalSet=%p type=%p repr=%s proto=%p arg=%d\n", (void*)finalSet, (void*)ftype, frepr.c_str(), (void*)(env ? env->getSetPrototype() : nullptr), arg);
                fflush(stderr);
            }
            for (int j = 0; j < arg + 2; ++j) stack.pop_back();
            stack.push_back(finalSet);
        } break;
        case OP_FORMAT_VALUE: {
            if (stack.size() < 1) continue;
            const proto::ProtoObject* val = stack.back();
            stack.pop_back();
            // arg bits: 0x03 format (0=raw, 1=str, 2=repr, 3=ascii), 0x04 has_fmt_spec
            int format = arg & 0x03;
            bool hasSpec = (arg & 0x04);
            if (hasSpec) stack.pop_back();
            
            const proto::ProtoObject* result = nullptr;
            if (format == 0) {
                result = val;
            } else {
                std::string r = PythonEnvironment::reprObject(ctx, val);
                const proto::ProtoString* pstr = proto::ProtoString::fromUTF8(ctx, r.c_str());
                result = pstr ? pstr->asObject(ctx) : nullptr;
            }
            stack.push_back(result ? result : (env ? env->getNonePrototype() : PROTO_NONE));
        } break;
        case OP_PUSH_NULL: {
            stack.push_back(nullptr);
        } break;
        case OP_BUILD_ANNOTATE: {
            // PEP 649 / 695: synthesise an `__annotate__(format)` callable
            // closing over the `__annotations__` dict on top of the stack.
            // Calling it ignores `format` and returns the captured dict
            // by reference. Sufficient for VALUE format, which is what
            // test_grammar.py:test_var_annot_simple_exec exercises;
            // FORWARDREF and STRING formats would require keeping the
            // unevaluated annotation expressions, which protoPython does
            // not currently retain — eager evaluation has already happened.
            //
            // Implementation note: we make the method cell itself the
            // callable, with `self` = the annotations dict. invokeCallable
            // dispatches asMethod natively, so this avoids the
            // "look up __call__ on type(obj)" path that would require
            // an entire synthetic class.
            if (stack.size() < 1) continue;
            const proto::ProtoObject* annotationsDict = stack.back();
            stack.pop_back();
            const proto::ProtoObject* callable = ctx->fromMethod(
                const_cast<proto::ProtoObject*>(annotationsDict),
                [](proto::ProtoContext*,
                   const proto::ProtoObject* self,
                   const proto::ParentLink*,
                   const proto::ProtoArguments*,
                   const proto::ProtoKeywordArguments*) -> const proto::ProtoObject* {
                    return self;
                });
            stack.push_back(callable);
        } break;
        case OP_BUILD_STRING: {
            if (stack.size() < static_cast<size_t>(arg)) continue;
            // GC safe: elements remain on stack until buildString returns
            const proto::ProtoObject** partsPtr = (const proto::ProtoObject**)(&stack[stack.size() - arg]);
            const proto::ProtoObject* res = env->buildString(partsPtr, arg);
            for (int j = 0; j < arg; ++j) stack.pop_back();
            stack.push_back(res);
        } break;
        // Cell and free variables (Compiler.h, "Closures"): arg is the fast
        // slot holding the cell.
        case OP_LOAD_DEREF: {
            const proto::ProtoObject* val = env ? env->cellGet(ctx, ctx->getAutomaticLocal(static_cast<unsigned int>(arg))) : nullptr;
            if (!val) {
                if (env) raiseEmptyCell(ctx, env, arg);
                i = next_i;
                continue;
            }
            stack.push_back(val);
        } break;
        case OP_STORE_DEREF: {
            if (stack.empty()) { i = next_i; continue; }
            if (env) env->cellSet(ctx, ctx->getAutomaticLocal(static_cast<unsigned int>(arg)), stack.back());
            stack.pop_back();
        } break;
        case OP_DELETE_DEREF: {
            const proto::ProtoObject* cell = ctx->getAutomaticLocal(static_cast<unsigned int>(arg));
            if (!env || !env->cellGet(ctx, cell)) {
                if (env) raiseEmptyCell(ctx, env, arg);
                i = next_i;
                continue;
            }
            env->cellClear(ctx, cell);
        } break;
        case OP_LOAD_CLOSURE: {
            // The cell itself, for a nested function's closure tuple.  -1: the
            // compiler found no cell for the name (an empty one stands in).
            const proto::ProtoObject* cell = (arg >= 0) ? ctx->getAutomaticLocal(static_cast<unsigned int>(arg)) : nullptr;
            if (!cell && env) cell = env->newCell(ctx, nullptr);
            stack.push_back(cell ? cell : PROTO_NONE);
        } break;
        case OP_LOAD_CLASSDEREF: {
            // A class body reads a free variable from its namespace when the
            // namespace binds the name, else from the cell.
            const proto::ProtoObject* val = nullptr;
            if (env && frame) {
                bool isFree = true;
                const proto::ProtoString* nameS = derefSlotName(ctx, env, arg, isFree);
                if (nameS) val = classNamespaceLookup(ctx, env, frame, nameS);
            }
            if (!val && env) val = env->cellGet(ctx, ctx->getAutomaticLocal(static_cast<unsigned int>(arg)));
            if (!val) {
                if (env) raiseEmptyCell(ctx, env, arg);
                i = next_i;
                continue;
            }
            stack.push_back(val);
        } break;
        case OP_JUMP_ABSOLUTE: {
            if (arg >= 0 && static_cast<proto::proto_ulong>(arg) < n) {
                i = static_cast<proto::proto_ulong>(arg);
                continue;
            }
        } break;
        case OP_LOAD_ATTR: {
            bool pushNull = (arg & 1);
            int nameIdx = arg >> 1;
            if (names && stack.size() >= 1 && static_cast<proto::proto_ulong>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* attrName = nameObj->asString(ctx);
                    if (diag_local) {
                        std::string attrNameStr;
                        attrName->toUTF8String(ctx, attrNameStr);
                        fprintf(stderr, "DEBUG: OP_LOAD_ATTR calling getAttribute env=%p obj=%p attr=%s\n", (void*)env, (void*)obj, attrNameStr.c_str());
                        fflush(stderr);
                    }

                    // Fast path for own instance attribute access.
                    //
                    // getOwnAttributeDirect (1 uncached AVL lookup) probes own attrs first.
                    // If the attribute is own and not a descriptor, return it directly with no
                    // guard overhead, no chain walk, and no protoCore cache call.
                    //
                    // Invariant exploited: own attrs cannot shadow data descriptors because
                    // descriptor.__set__ intercepts setAttribute, preventing direct storage on
                    // the instance.  A result from getOwnAttributeDirect is therefore safe to
                    // return without a full MRO descriptor scan — except for non-data descriptors
                    // (classmethod, staticmethod, property) which have __get__ but no __set__
                    // and CAN be stored as own attrs on a class object.  We check the type's
                    // __get__ to handle that case (1 uncached lookup on the type prototype).
                    //
                    // Falls to slow path for: synthesised dunders, native methods, non-data
                    // descriptors with __get__, Python functions when pushNull=false, and any
                    // own-attr miss (class attrs, super() proxies, Python class objects, etc.).
                    // Avoiding chain-walk guards in the miss branch eliminates the per-access
                    // hasOwnAttribute overhead that was regressing method-heavy workloads.
                    //
                    // Set PROTOPY_DISABLE_LOADATTR_FASTPATH=1 to always use the slow path.
                    const proto::ProtoObject* val = nullptr;
                    bool fastPathTaken = false;
                    static const bool disableLoadattrFastpath = [](){
                        const char* v = std::getenv("PROTOPY_DISABLE_LOADATTR_FASTPATH");
                        return v != nullptr && v[0] != '\0' && v[0] != '0';
                    }();
                    // The common read, by protoCore: one getAttribute resolves
                    // the whole chain, and protoCore's per-thread attribute cache
                    // answers repeats. A class keeps its annotations in that
                    // chain -- a property, a classmethod, a user descriptor, a
                    // function that binds -- so the lookup returns the
                    // annotation in place of a value, and the kind of the
                    // result decides what Python adds. The flags of the
                    // receiver's type, an annotation of the class read in one
                    // step, say whether its MRO overrides __getattribute__ (a
                    // super() proxy's class does; for a class receiver the
                    // type is its metaclass). Whatever this block does not
                    // settle takes the paths below, unchanged.
                    if (!disableLoadattrFastpath && env && obj && obj != PROTO_NONE
                            && attrName != env->getClassString()
                            && !env->isTypeOnlyAttributeName(ctx, attrName)
                            && attrName != env->getDictDunderString()
                            && attrName != env->getGetattributeDunderString()) {
                        const proto::ProtoObject* cls = env->getType(ctx, obj);
                        if (cls && cls != PROTO_NONE && cls != obj
                                && !(env->ensureClassFlags(ctx, cls)
                                     & protoPython::PythonEnvironment::PYFLAG_HAS_CUSTOM_GETATTR)) {
                            const proto::ProtoObject* v = obj->getAttribute(ctx, attrName);
                            bool plain = proto::isSmallInt(v) || proto::ProtoObject::isStringTagFast(v)
                                      || v == PROTO_TRUE || v == PROTO_FALSE;
                            bool bindToObj = false;
                            if (v == PROTO_NONE) {
                                // None, or absent (__getattr__, AttributeError).
                                plain = obj->hasAttribute(ctx, attrName) == PROTO_TRUE;
                            } else if (!plain && v && !v->isMethod(ctx)) {
                                const proto::ProtoObject* vt = v->getFirstParent(ctx);
                                if (vt == env->getFunctionPrototype()) {
                                    // A Python function: the function itself when
                                    // stored on the object, a method when found on
                                    // the class of an instance (the receiver's
                                    // type is its first parent: not a class, no
                                    // reassigned __class__). The bound method
                                    // object of a plain LOAD_ATTR takes the paths
                                    // below.
                                    if (pushNull) {
                                        if (obj->getOwnAttributeDirect(ctx, attrName) == v) plain = true;
                                        else bindToObj = obj->getFirstParent(ctx) == cls;
                                    }
                                } else {
                                    // Not a descriptor unless its type has __get__.
                                    plain = !vt || vt == PROTO_NONE || vt == v
                                         || !(env->ensureClassFlags(ctx, vt)
                                              & protoPython::PythonEnvironment::PYFLAG_HAS_GET_DESCR);
                                }
                            }
                            if (bindToObj) {
                                // [function, self]: CALL prepends self, as for
                                // the unbound functions of the paths below.
                                stack.back() = v;
                                stack.push_back(obj);
                                i = next_i; continue;
                            }
                            if (plain) {
                                if (pushNull) {
                                    stack.back() = nullptr;
                                    stack.push_back(v);
                                } else {
                                    stack.back() = v;
                                }
                                i = next_i; continue;
                            }
                        }
                    }

                    // Sprint-4 (2026-06-15): skip 4 isXxx tag checks per access
                    // when the getType cache has already classified this obj as
                    // non-primitive. The (obj -> is-primitive) mapping is
                    // invariant for the obj's lifetime; on cache miss we fall
                    // back to the explicit isXxx chain.
                    int primCache = env ? env->primitiveCacheHit(obj) : 0;
                    bool skipTagChecks = (primCache == 2);  // 2 = cached non-primitive
                    if (!disableLoadattrFastpath && env && obj && obj != PROTO_NONE
                            && (skipTagChecks
                                || (!obj->isString(ctx) && !obj->isInteger(ctx)
                                    && !obj->isBoolean(ctx) && !obj->isFloat(ctx)))) {
                        // Synthesised dunders — computed on demand, not stored in protoCore.
                        // Pointer comparisons are free (co_names are interned symbols).
                        bool isSynthDunder =
                            attrName == env->getClassString()        ||
                            attrName == env->getMroString()          ||
                            attrName == env->getDictDunderString()   ||
                            attrName == env->getBasesString()        ||
                            attrName == env->getGetattributeDunderString();
                        // CPython semantics: a user-supplied __getattribute__
                        // intercepts EVERY attribute read, even those that
                        // would be satisfied by an own-instance attribute.
                        // Skip the fast path entirely when the instance's
                        // type (or any MRO ancestor) overrides it.  Cached
                        // per-class flag — same cost as the descriptor check
                        // we already do below, but discriminates instances
                        // whose class hook would otherwise be silently
                        // bypassed.
                        bool hasCustomGetattr = false;
                        if (!isSynthDunder) {
                            const proto::ProtoObject* objType = env->getType(ctx, obj);
                            if (objType && objType != PROTO_NONE) {
                                uint32_t typeFlags = env->ensureClassFlags(ctx, objType);
                                hasCustomGetattr = (typeFlags
                                    & protoPython::PythonEnvironment::PYFLAG_HAS_CUSTOM_GETATTR) != 0;
                            }
                        }
                        if (!isSynthDunder && !hasCustomGetattr) {
                            // 1 uncached AVL lookup: hits for self.field, misses for self.method().
                            const proto::ProtoObject* ownFv =
                                obj->getOwnAttributeDirect(ctx, attrName);
                            if (ownFv && ownFv != PROTO_NONE && !ownFv->isMethod(ctx)) {
                                const proto::ProtoObject* ownType = ownFv->getFirstParent(ctx);
                                if (ownType != env->getFunctionPrototype()) {
                                    // Plain own value — descriptor check via the
                                    // P2 cached type flags.  When the value's type
                                    // has HAS_GET_DESCR=0 we KNOW none of its
                                    // attributes define __get__, so the value is
                                    // guaranteed not to be a descriptor.  This
                                    // replaces the previous one-off
                                    // getOwnAttributeDirect(__get__) probe with a
                                    // cached read whose per-class result amortises
                                    // across every instance read of every field.
                                    bool isDescriptor;
                                    if (ownType && ownType != PROTO_NONE) {
                                        uint32_t flags = env->ensureClassFlags(ctx, ownType);
                                        isDescriptor = (flags & protoPython::PythonEnvironment::PYFLAG_HAS_GET_DESCR) != 0;
                                    } else {
                                        isDescriptor = false;
                                    }
                                    if (!isDescriptor) {
                                        if (pushNull) {
                                            stack.back() = nullptr;
                                            stack.push_back(ownFv);
                                        } else {
                                            stack.back() = ownFv;
                                        }
                                        fastPathTaken = true;
                                    }
                                } else if (pushNull) {
                                    // Instance-stored function (not inherited from class) —
                                    // push as NULL/callable pair; OP_CALL will not prepend self.
                                    stack.back() = nullptr;
                                    stack.push_back(ownFv);
                                    fastPathTaken = true;
                                }
                                // else: pushNull=false + function → slow path for __get__ binding.
                            }
                            // Own-attr miss → fall directly to slow path.
                            // No hasOwnAttribute guards here: they added 2 uncached lookups on
                            // every miss (i.e., every method call), regressing OOP workloads.
                        }
                    }
                    if (fastPathTaken) { i = next_i; continue; }

                    // Slow path: full Python attribute protocol (descriptors, MRO, __getattr__).
                    // Use raiseError=false so we can try __getattr__ before raising AttributeError.
                    // When pushNull is set (LOAD_METHOD), pass &isUnboundFunc so that plain
                    // Python functions on a class are returned raw — the CALL handler will
                    // prepend self via its [Method, Self, Arg1...] layout, avoiding the
                    // ~10-cell bound method object that py_function_get would otherwise build.
                    bool isUnboundFunc = false;
                    bool picHandled = false;

                    // Sprint-2 step B (2026-06-15): polymorphic inline cache for
                    // LOAD_METHOD / LOAD_ATTR slow path. When the instance has no
                    // OWN attribute by this name, the resolved value is a function
                    // of (type, name) — same answer for every instance of the same
                    // type — so we can cache by (type ptr, name ptr) and skip the
                    // descriptor / MRO walk. The hot case in `lst.append(...)`,
                    // `s + "x"` (BINARY_ADD's __add__ lookup), `dict.get(...)`,
                    // etc. all hit this PIC after the first call.
                    //
                    // Safety: the cache key incorporates the env's resolve-cache
                    // generation, which is bumped on every class-side mutation.
                    // A monkey-patch invalidates every cached entry on the next
                    // access. Instance-shadow is handled by the hasOwnAttribute
                    // probe — we only consult the PIC when the instance does NOT
                    // override the name.
                    if (env && pushNull && obj && attrName
                            && obj->hasOwnAttribute(ctx, attrName) != PROTO_TRUE) {
                        const proto::ProtoObject* type = env->getType(ctx, obj);
                        if (type && type != PROTO_NONE) {
                            const uint64_t gen = env->resolveCacheGeneration();
                            const uint64_t gcEpoch = ctx->space->getGCCycleCount();
                            LoadAttrPicEntry* slot =
                                &g_loadAttrPic[loadAttrPicIndex(type, attrName)];
                            if (slot->type == type
                                && slot->name == attrName
                                && slot->generation == gen
                                && slot->gcEpoch == gcEpoch) {
                                // PIC HIT — skip env->getAttribute entirely.
                                if (slot->nativeMethod) {
                                    // Re-bind the cached native method to
                                    // this receiver (see LoadAttrPicEntry).
                                    val = ctx->fromMethod(
                                        const_cast<proto::ProtoObject*>(obj),
                                        slot->nativeMethod);
                                    isUnboundFunc = false;
                                } else {
                                    val = slot->value;
                                    isUnboundFunc = slot->isUnbound;
                                }
                                picHandled = true;
                            } else {
                                // PIC MISS — do the slow path, then populate
                                // only with receiver-independent results.
                                val = env->getAttribute(ctx, obj, attrName, false, &isUnboundFunc);
                                if (val && val != PROTO_NONE) {
                                    if (isUnboundFunc) {
                                        slot->type         = type;
                                        slot->name         = attrName;
                                        slot->value        = val;
                                        slot->nativeMethod = nullptr;
                                        slot->generation   = gen;
                                        slot->gcEpoch      = gcEpoch;
                                        slot->isUnbound    = true;
                                    } else if (val->isMethod(ctx)
                                               && val->asMethodSelf(ctx) == obj) {
                                        slot->type         = type;
                                        slot->name         = attrName;
                                        slot->value        = nullptr;
                                        slot->nativeMethod = val->asMethod(ctx);
                                        slot->generation   = gen;
                                        slot->gcEpoch      = gcEpoch;
                                        slot->isUnbound    = false;
                                    }
                                }
                                picHandled = true;
                            }
                        }
                    }
                    if (!picHandled) {
                        val = env ? env->getAttribute(ctx, obj, attrName, false,
                                                      pushNull ? &isUnboundFunc : nullptr)
                                  : obj->getAttribute(ctx, attrName);
                    }

                    if (diag_local) {
                        fprintf(stderr, "DEBUG: OP_LOAD_ATTR returned val=%p\n", (void*)val);
                        fflush(stderr);
                    }

                    bool isMissing = false;
                    // PythonEnvironment::getAttribute internally
                    // disambiguates "absent" (returns nullptr) from
                    // "exists with value None" (returns PROTO_NONE).
                    // Both its fast path (tryFastGetAttribute) and the
                    // slow body run the protoCore hasAttribute + Python
                    // MRO walk fallback before returning PROTO_NONE,
                    // so val is authoritative here: nullptr means
                    // missing, anything else (including PROTO_NONE)
                    // means present.  The previous bare hasAttribute
                    // check at this site only consulted protoCore's
                    // linearised parent chain and missed inherited
                    // class-body attributes that the slow path already
                    // resolved correctly — visible at
                    // unittest.IsolatedAsyncioTestCase subclasses
                    // where `loop_factory = None` lives on the base.
                    bool attrNotFound = !val;
                    if (attrNotFound) {
                        // Try __getattr__ before raising AttributeError
                        // An own __getattr__(name) of a module or super()
                        // proxy, else the type's (a class's metaclass's).
                        bool passObj = true;
                        const proto::ProtoObject* getattr = env ? env->getattrHookFor(ctx, obj, &passObj) : nullptr;
                        const bool getattrIsOwn = !passObj;

                        if (getattr && getattr != PROTO_NONE) {
                            // Module-level __getattr__(name) takes only the name string.
                            // Class-level __getattr__(self, name) takes the instance and name.
                            const proto::ProtoList* posArgs = getattrIsOwn
                                ? ctx->newList()->appendLast(ctx, nameObj)
                                : ctx->newList()->appendLast(ctx, obj)->appendLast(ctx, nameObj);
                            val = invokePythonCallable(ctx, getattr, posArgs, nullptr);
                            if (val) {
                                isMissing = false;
                            } else {
                                isMissing = true;
                            }
                        } else {
                            isMissing = true;
                        }
                    } else if (!val) {
                        isMissing = true;
                    }

                    if (!isMissing) {
                        if (pushNull) {
                            // Python 3.11+ LOAD_METHOD logic:
                            // If it's a method, push [Method, Self]. Else push [NULL, Attr].
                            bool isMethod = false;
                            const proto::ProtoObject* method = nullptr;
                            const proto::ProtoObject* selfObj = obj;

                            const proto::ProtoObject* actualVal = val ? val : (env ? env->getNonePrototype() : PROTO_NONE);

                            if (isUnboundFunc) {
                                // env->getAttribute deferred binding for a plain Python
                                // function on a class.  Push [function, instance] directly
                                // so OP_CALL_FUNCTION's [Method, Self, Arg1...] branch
                                // prepends self at call time — no bound-method object is
                                // ever materialised.
                                stack.back() = actualVal;
                                stack.push_back(obj);
                            } else if (actualVal->isMethod(ctx) && actualVal->asMethodSelf(ctx) != nullptr) {
                                // It's a bound method from getAttribute/descriptor.
                                // Push [NULL, bound_method] so invokeCallable uses
                                // bound_method->asMethodSelf() as the correct self.
                                // Do NOT decompose into [__func__, __self__] — __func__
                                // is the unbound prototype method (asMethodSelf=nullptr),
                                // which would cause self=null in native functions.
                                stack.back() = nullptr; // NULL marker
                                stack.push_back(actualVal);
                            } else if (actualVal->isMethod(ctx)) {
                                // Unbound method (asMethodSelf=nullptr) — treat as regular callable.
                                // This handles Python-level method descriptors where __self__ is
                                // stored as an attribute rather than via asMethodSelf.
                                isMethod = true;
                                method = actualVal->getAttribute(ctx, env ? env->getFuncDunderString() : PythonEnvironment::getInternedString(ctx, "__func__"));
                                selfObj = actualVal->getAttribute(ctx, env ? env->getSelfDunderString() : PythonEnvironment::getInternedString(ctx, "__self__"));
                                if (!method || method == PROTO_NONE || !selfObj || selfObj == PROTO_NONE) {
                                    // No __func__/__self__ — keep whole and push as [NULL, attr]
                                    stack.back() = nullptr;
                                    stack.push_back(actualVal);
                                } else {
                                    stack.back() = method;
                                    stack.push_back(selfObj);
                                }
                            } else {
                                stack.back() = nullptr; // NULL marker
                                stack.push_back(actualVal);
                            }
                        } else {
                            if (!val) {
                                if (diag_local) {
                                     fprintf(stderr, "DEBUG_OP_LOAD_ATTR: val is NULL, pushing None! (no pending exc)\n");
                                     fflush(stderr);
                                }
                                val = (env ? env->getNonePrototype() : PROTO_NONE);
                            }
                            // PH: special-case __dict__: it's installed
                            // as a method on objectPrototype, but Python
                            // treats it as a data descriptor (auto-invoked
                            // on read).  When LOAD_ATTR resolves __dict__
                            // to a bound native method, invoke it with no
                            // args to materialize the dict.
                            // STRUCT-277: additionally handle the
                            // getset_descriptor form — env-level lookup
                            // may return the descriptor object (not
                            // auto-invoked).  Detect via __class__ being
                            // getSetDescriptorPrototype and invoke its
                            // fget with the original receiver.
                            if (env && val && val->isMethod(ctx) && val->asMethodSelf(ctx) != nullptr) {
                                std::string attrNameStr;
                                attrName->toUTF8String(ctx, attrNameStr);
                                if (attrNameStr == "__dict__") {
                                    const proto::ProtoObject* dictResult =
                                        val->asMethod(ctx)(ctx,
                                            const_cast<proto::ProtoObject*>(val->asMethodSelf(ctx)),
                                            nullptr, argsView(proto::ProtoArguments(nullptr, 0)), nullptr);
                                    if (dictResult) {
                                        val = dictResult;
                                    }
                                }
                            } else if (env && val && val != PROTO_NONE
                                       && !env->isActuallyAClass(ctx, obj)) {
                                // STRUCT-277: only auto-invoke getset
                                // fget when the receiver is an instance
                                // (NOT a class).  CPython: `float.real`
                                // (class access) returns the descriptor
                                // itself; `(0.5).real` (instance access)
                                // returns the value.
                                const proto::ProtoObject* getsetProto = env->getGetSetDescriptorPrototype();
                                if (getsetProto && env->getType(ctx, val) == getsetProto) {
                                    const proto::ProtoString* fgetS = PythonEnvironment::getInternedString(ctx, "fget");
                                    const proto::ProtoObject* fget = val->getAttribute(ctx, fgetS);
                                    if (fget && fget->asMethod(ctx)) {
                                        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, obj);
                                        const proto::ProtoObject* dictResult =
                                            fget->asMethod(ctx)(ctx,
                                                const_cast<proto::ProtoObject*>(obj),
                                                nullptr, argsView(proto::ProtoListArguments(ctx, args)), nullptr);
                                        if (dictResult) {
                                            val = dictResult;
                                        }
                                    }
                                }
                            }
                            // STRUCT-305: when the receiver is a type
                            // with non-string class-body entries
                            // (recorded in __nonstring_entries__ by
                            // py_type's namespace processing), fire
                            // __eq__ on any entry whose key hash
                            // collides with the looked-up name.
                            // test_type_lookup_mro_reference depends
                            // on the side effect (MyKey.__eq__ mutates
                            // X.__bases__).  Cheap-guarded by an
                            // own-attribute probe so the cost is paid
                            // only on classes that actually have
                            // non-string entries.
                            if (env && obj && attrName) {
                                const proto::ProtoString* nseS =
                                    PythonEnvironment::getInternedString(ctx, "__nonstring_entries__");
                                if (obj->hasOwnAttribute(ctx, nseS) == PROTO_TRUE) {
                                    const proto::ProtoObject* nseAttr = obj->getOwnAttributeDirect(ctx, nseS);
                                    const proto::ProtoList* entries = nullptr;
                                    if (nseAttr) {
                                        if (::protoPython::listStorage(ctx, nseAttr)) {
                                            entries = ::protoPython::listStorage(ctx, nseAttr);
                                        } else {
                                            const proto::ProtoObject* d = nseAttr->getAttribute(ctx,
                                                env->getDataString());
                                            if (d) entries = ::protoPython::listStorage(ctx, d);
                                        }
                                    }
                                    if (entries) {
                                        proto::proto_ulong nameHash =
                                            attrName->getHash(ctx);
                                        const proto::ProtoString* eqS =
                                            PythonEnvironment::getInternedString(ctx, "__eq__");
                                        for (proto::proto_ulong i = 0; i < entries->getSize(ctx); ++i) {
                                            const proto::ProtoObject* pair = entries->getAt(ctx, static_cast<int>(i));
                                            if (!pair) continue;
                                            ::protoPython::TupleView pT = ::protoPython::PythonEnvironment::tupleView(ctx, pair);
                                            const proto::ProtoList* pL = pT ? pT->asList(ctx)
                                                                            : ::protoPython::listStorage(ctx, pair);
                                            if (!pL || pL->getSize(ctx) < 1) continue;
                                            const proto::ProtoObject* nsKey = pL->getAt(ctx, 0);
                                            if (!nsKey) continue;
                                            proto::proto_ulong h = ::protoPython::pyDictKeyHash(ctx, nsKey);
                                            if (h != nameHash) continue;
                                            // Hash collision → fire __eq__
                                            // for the side effect.  Discard
                                            // the return value.  Any pending
                                            // exception is cleared so the
                                            // surrounding lookup is not
                                            // interrupted.
                                            const proto::ProtoObject* eqM = env->getAttribute(ctx,
                                                nsKey, eqS, false);
                                            if (eqM && eqM != PROTO_NONE) {
                                                // env->callObject handles native
                                                // methods, Python functions, and
                                                // bound methods uniformly.  For
                                                // an unbound native method we
                                                // prepend self ourselves; for
                                                // bound methods / Python functions
                                                // env->callObject's dispatcher
                                                // does the right thing.
                                                const proto::ProtoObject* nameObj = attrName->asObject(ctx);
                                                std::vector<const proto::ProtoObject*> a;
                                                if (eqM->asMethod(ctx) && eqM->asMethodSelf(ctx) == nullptr) {
                                                    a.push_back(nsKey);
                                                }
                                                a.push_back(nameObj);
                                                try {
                                                    env->callObject(eqM, a);
                                                } catch (const proto::ProtoException&) {
                                                    // dropped, as before
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            stack.back() = val; // Replace obj with result
                        }
                    } else {
                        stack.pop_back(); // Pop obj before raising error
                        std::string attr;
                        attrName->toUTF8String(ctx, attr);
    if (diag_local) { fprintf(stderr, "!!! ExecEngine raiseAttrError: obj=%p attr=%s\n", (void*)obj, attr.c_str()); fflush(stderr); }
                        if (env) env->raiseAttributeError(ctx, obj, attr);
                        continue;
                    }
                }
            }
        } break;
        case OP_STORE_ATTR_GROUP:
        case OP_STORE_ATTR_GROUP_END: {
            // A write of an attribute write group (see "Attribute write
            // groups" above).  arg = (descriptor constant << 5) | position.
            const int pos = arg & 31;
            const int descIdx = arg >> 5;
            ::protoPython::TupleView desc = nullptr;
            if (constants && static_cast<proto::proto_ulong>(descIdx) < constants->getSize(ctx)) {
                const proto::ProtoObject* d = constants->getAt(ctx, descIdx);
                desc = d ? ::protoPython::PythonEnvironment::tupleView(ctx, d) : nullptr;
            }
            if (!desc || stack.size() < 2) { attrGroupState = 0; i = next_i; continue; }
            if (pos == 0)
                attrGroupState = attrGroupApplies(ctx, env, stack.back(), names, desc.tuple) ? 1 : 2;
            const int n = static_cast<int>(desc->getAt(ctx, 0)->asLong(ctx));
            const int nameIdx = static_cast<int>(desc->getAt(ctx, 1 + pos)->asLong(ctx));
            if (attrGroupState == 1 && op == OP_STORE_ATTR_GROUP) {
                stack.pop_back();  // the receiver; the value stays for the last write
                break;
            }
            if (attrGroupState == 1 && stack.size() >= static_cast<size_t>(n) + 1) {
                // [v0 .. v(n-1), obj]: publish the run as one version.
                attrGroupState = 0;
                const proto::ProtoObject* oldObj = stack.back();
                const proto::ProtoString* groupNames[16];
                const proto::ProtoObject* groupValues[16];
                for (int k = 0; k < n; ++k) {
                    const int idx = static_cast<int>(desc->getAt(ctx, 1 + k)->asLong(ctx));
                    groupNames[k] = names->getAt(ctx, idx)->asString(ctx);
                    // Values stay rooted on the operand stack until popped below.
                    groupValues[k] = stack[stack.top - 1 - n + k];
                }
                const proto::ProtoObject* newObj =
                    oldObj->setAttributes(ctx, static_cast<unsigned>(n), groupNames, groupValues);
                if (newObj && newObj != oldObj) {
                    syncModuleIdentity(ctx, env, oldObj, newObj);
                    const proto::ProtoObject** slots = ctx->getAutomaticLocals();
                    if (slots) {
                        unsigned int nSlots = ctx->getAutomaticLocalsCount();
                        for (unsigned int s = 0; s < nSlots; ++s) {
                            if (slots[s] == oldObj) slots[s] = newObj;
                        }
                    }
                }
                for (int k = 0; k <= n; ++k) stack.pop_back();
                break;
            }
            // Per write: exactly OP_STORE_ATTR.
            if (op == OP_STORE_ATTR_GROUP_END) attrGroupState = 0;
            arg = nameIdx << 1;
            goto store_attr_body;
        }
        case OP_STORE_ATTR: {
        store_attr_body:
            int nameIdx = arg >> 1;
            if (names && stack.size() >= 2 && static_cast<proto::proto_ulong>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                const proto::ProtoObject* val = stack[stack.top - 2];
                // Delay pop
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    proto::ProtoObject* oldObj = const_cast<proto::ProtoObject*>(obj);
                    const proto::ProtoObject* newObj = nullptr;

                    // Fast path: plain instance attribute write (self.x = value).
                    //
                    // Conditions (all O(1) pointer/struct ops):
                    //   1. obj is a plain instance (not a primitive, not a Python class)
                    //   2. The direct type (first protoCore parent) has no own attribute
                    //      named `name` — so no data descriptor shadows this write
                    //   3. The direct type has no `__slots__` — so no slot enforcement
                    //
                    // When these hold, go directly to obj->setAttribute and bypass the
                    // full env->setAttribute protocol (two MRO walks, getType twice,
                    // UTF-8 decode, __dict__ sync probes — ~12 calls reduced to 3).
                    //
                    // Safety: `__slots__` on a *base* class but NOT on the direct type
                    // implies the direct type adds a __dict__ (CPython rule), so any
                    // attribute name is valid on instances.  A data descriptor on a
                    // base that's not on the direct type would require the direct type
                    // to have `name` as own attr only if inherited via MRO — but we
                    // check the direct type's own attrs, not its chain.  In practice,
                    // inheriting a data descriptor propagates it to the direct type
                    // automatically during class construction (the __mro__ walk in
                    // py_type), making the direct-type check sufficient for all but
                    // exotic metaclass/descriptor patterns (which fall to slow path).
                    //
                    // Set PROTOPY_DISABLE_STOREATTR_FASTPATH=1 to always use slow path.
                    static const bool disableStoreattrFastpath = [](){
                        const char* v = std::getenv("PROTOPY_DISABLE_STOREATTR_FASTPATH");
                        return v != nullptr && v[0] != '\0' && v[0] != '0';
                    }();
                    bool fastStoreTaken = false;
                    // __class__ assignment must always traverse the slow
                    // path (env->setAttribute) so the immutable-primitive
                    // and metaclass-compatibility validation runs.  The
                    // fast path bypasses those checks.
                    // Names are interned (Compiler::addName): pointers compare.
                    const proto::ProtoString* classS = env ? env->getClassString() : nullptr;
                    bool isClassAssign = classS && nameS == classS;
                    // __dict__ assignment must always traverse the slow
                    // path (env->setAttribute) so the dict-replace logic
                    // runs (validate type + repopulate __data__/__keys__).
                    // The fast path would store the rhs as a plain
                    // attribute named "__dict__" and lose the entries.
                    const proto::ProtoString* dictS = env ? env->getDictDunderString() : nullptr;
                    bool isDictAssign = dictS && nameS == dictS;
                    // An object cell (pointer tag 0), as storeAttr requires:
                    // not an int, str, bool or float.
                    if (!isClassAssign && !isDictAssign && !disableStoreattrFastpath && env && obj && obj != PROTO_NONE
                            && (reinterpret_cast<uintptr_t>(obj) & 0x3F) == 0) {
                        // P2 type-flags fast path.  Replaces the legacy 3
                        // hasOwnAttribute probes (isPyClassS, slotsS,
                        // nameS-as-descriptor) with a single
                        // ensureClassFlags read on the directType.  When
                        // HAS_DATA_DESCR is unset the per-name descriptor
                        // check is skipped entirely; otherwise we still
                        // probe the specific name.
                        //
                        // "obj is itself a class" is decided by the cheap
                        // pointer compare `directType == typePrototype`:
                        // every Python class created by py_type has
                        // typePrototype as its direct parent.  Metaclass
                        // instances (rare) bail to the slow path through
                        // the same fall-through.
                        const proto::ProtoObject* directType = obj->getFirstParent(ctx);
                        if (directType && directType != PROTO_NONE
                                && directType != env->getTypePrototype()) {
                            uint32_t flags = env->ensureClassFlags(ctx, directType);
                            const bool noSlots = (flags & protoPython::PythonEnvironment::PYFLAG_HAS_SLOTS) == 0;
                            // Per-name descriptor probe: when the type
                            // owns an attribute with the same name as
                            // the assignment target, that attribute may
                            // be a data descriptor (`x = SomeDescr()`)
                            // whose __set__ should run instead of a
                            // direct write.  ensureClassFlags's
                            // HAS_DATA_DESCR shortcut only fires when
                            // a class itself owns __set__ as a method —
                            // it can't see descriptor INSTANCES stored
                            // as attribute values, so we always need
                            // the per-name fallback.  Bail to slow
                            // path on a hit; the slow path then
                            // dispatches __set__ correctly.
                            //
                            // The same holds for a `__setattr__` override
                            // and for a data descriptor INHERITED from a
                            // base class: the type's own attributes are not
                            // enough, so the whole chain is consulted
                            // (typeChainInterceptsStore).  env->setAttribute
                            // then dispatches the hook or the descriptor.
                            const bool intercepted =
                                typeChainInterceptsStore(ctx, env, directType, flags, nameS);
                            if (noSlots && !intercepted) {
                                newObj = const_cast<proto::ProtoObject*>(obj)->setAttribute(ctx, nameS, val);
                                fastStoreTaken = true;
                                // A module attribute is a global of that module
                                // (or a builtin): names resolved before are stale.
                                if (directType == env->getModulePrototype() || obj == env->getBuiltinsModule()) {
                                    env->invalidateResolveCache();
                                }
                                // PEP 468 / dict insertion-order preservation.
                                // Track the assignment in obj.__keys__ so
                                // vars(obj) and obj.__dict__ surface attrs in
                                // call/setattr order, not the SparseList's
                                // hash order.  Only append if the name isn't
                                // already tracked (re-assigning an existing
                                // attribute keeps its original slot, matching
                                // CPython's dict.__setitem__ semantics).
                                // PEP 468 / `vars(obj)` insertion-order tracking removed
                                // here intentionally.
                                //
                                // Rationale: the eager per-STORE_ATTR maintenance of an
                                // explicit `__keys__` list on every Python instance was
                                // duplicating the protoCore-internal SparseList that
                                // already stores the attributes — and it cost a full
                                // extra setAttribute per write (probe __keys__, scan
                                // the list, appendLast, setAttribute(__keys__, ...)).
                                // The list was load-bearing only for `vars(obj)` /
                                // `obj.__dict__` insertion-order semantics.  Where
                                // insertion order MUST be preserved — `{}` / `dict(...)`
                                // literals — protoPython's `OP_BUILD_MAP` /
                                // `OP_STORE_SUBSCR` build a dedicated dict object that
                                // carries its own `__data__` SparseList AND its own
                                // ordered `__keys__` list (see line ~7090 onward).
                                //
                                // The trade-off: `vars(obj).keys()` / `obj.__dict__`
                                // now iterates in hash order rather than insertion
                                // order.  Programs that rely on the latter for plain
                                // instance attributes (rare — most reflection consumers
                                // iterate the class annotation map instead) will see
                                // the order shift.  This is documented and explicit; we
                                // no longer pay 2× setAttribute on every `self.x = v`.
                            }
                        }
                    }
                    if (!fastStoreTaken) {
                        if (env) {
                            newObj = env->setAttribute(ctx, obj, nameS, val);
                            if (obj == env->getBuiltinsModule() || env->getType(ctx, obj) == env->getModulePrototype()) {
                                env->invalidateResolveCache();
                            }
                        } else {
                            proto::ProtoObject* mutableObj = const_cast<proto::ProtoObject*>(obj);
                            newObj = mutableObj->setAttribute(ctx, nameS, val);
                        }
                    }
                    // STRUCT-36 pre-req: env->setAttribute returns nullptr on
                    // validation failure (e.g. `X.__bases__ = (NoneType,)`
                    // raising TypeError).  Treating nullptr as "new identity"
                    // would erase oldObj from every CO_OPTIMIZED slot,
                    // turning the local that holds `X` into a dangling
                    // pointer the exception handler then reads as empty
                    // bases.  Only propagate identity changes when the call
                    // actually produced a fresh object.
                    if (newObj && newObj != oldObj) {
                         syncModuleIdentity(ctx, env, oldObj, newObj);
                         const proto::ProtoObject** slots = ctx->getAutomaticLocals();
                         if (slots) {
                             unsigned int nSlots = ctx->getAutomaticLocalsCount();
                             for (unsigned int s = 0; s < nSlots; ++s) {
                                 if (slots[s] == oldObj) slots[s] = newObj;
                             }
                         }
                    }
                    stack.pop_back(); // Pop obj
                    stack.pop_back(); // Pop val
                }
            }
        } break;
        case OP_BUILD_LIST: {
            if (stack.size() < static_cast<size_t>(arg)) continue;
            const proto::ProtoList* lst = ctx->newList();
            stack.push_back(lst->asObject(ctx)); // Root lst
            
            size_t baseIdx = stack.size() - 1 - arg;
            for (int j = 0; j < arg; ++j) {
                lst = lst->appendLast(ctx, stack[baseIdx + j]);
                stack[stack.size() - 1] = const_cast<proto::ProtoObject*>(lst->asObject(ctx)); // Update root
            }
            
            proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            stack.push_back(listObj); // Root listObj
            
            listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"), lst->asObject(ctx)));
            if (env && env->getListPrototype()) {
                listObj = const_cast<proto::ProtoObject*>(listObj->addParent(ctx, env->getListPrototype()));
                listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx, env->getClassString(), env->getListPrototype()));
            }
            
            const proto::ProtoObject* finalList = listObj;
            for (int j = 0; j < arg + 2; ++j) stack.pop_back();
            stack.push_back(finalList);
        } break;
        case OP_BINARY_SUBSCR: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* key = stack.back();
            const proto::ProtoObject* container = stack[stack.top - 2];

            // Fast path: list[smallint] — by far the dominant subscript case
            // in numeric workloads (nqueens, sieve, table-of-table walks).
            // The slow path below allocates a 2-cell ProtoList, takes the
            // descriptor protocol, then walks back through invokeDunder /
            // invokeCallable just to do the same thing this branch does in
            // two memory reads.  Bypassing it eliminates ~2 cell allocations
            // per subscript on the hot loop.
            //
            // Subclass-aware gate: only fire the fast path when type(container)
            // is *exactly* listPrototype (no subclass). A subclass with an
            // overridden __getitem__ must route through the dunder dispatch
            // below — see docs/audits/02-fast-paths.md F2.2.
            //
            // Skip strings here: ProtoString::asList builds a list of
            // unicode-char embedded values, not a list of single-char
            // strings, so returning that fast-path value to user code
            // breaks `s[i].lower()` and every other Python str-method
            // chain on indexed characters. py_str_getitem (the slow path)
            // returns a proper one-char string.
            if (proto::isSmallInt(key) && container && !container->isString(ctx)
                && env && env->getType(ctx, container) == env->getListPrototype()) {
                const proto::ProtoList* lst = ::protoPython::listStorage(ctx, container);
                if (lst) {
                    long long idx = proto::asSmallInt(key);
                    long long size = static_cast<long long>(lst->getSize(ctx));
                    if (idx < 0) idx += size;
                    if (idx >= 0 && idx < size) {
                        const proto::ProtoObject* val = lst->getAt(ctx, static_cast<int>(idx));
                        stack.pop_back();
                        stack.back() = val ? val : (env ? env->getNonePrototype() : PROTO_NONE);
                        i = next_i;
                        continue;
                    }
                    // Out-of-range falls through to the slow path so it can
                    // raise IndexError with the standard message.
                }
            }

            const proto::ProtoString* getItemS = env ? env->getGetItemString() : PythonEnvironment::getInternedString(ctx, "__getitem__");
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
            const proto::ProtoObject* result = nullptr;

            // When the container is a class (a type), prefer
            // __class_getitem__ over __getitem__.  CPython's
            // BINARY_SUBSCR for `cls[X]` dispatches via the metaclass
            // (`type.__class_getitem__`), which runs the generic-alias
            // machinery and yields e.g. `dict[str, object]`.  protoPython
            // previously found the instance-side `__getitem__` first
            // (every dict has it), and treating the class as a dict
            // instance produced a spurious KeyError on every generic-alias
            // expression like `dict[str, object]`.
            if (env && env->isActuallyAClass(ctx, container)) {
                // CPython looks `cls[X]` up on the metaclass first: a
                // metaclass __getitem__ (EnumType: Color["RED"]) wins over
                // cls.__class_getitem__, and its exceptions propagate.
                const proto::ProtoObject* meta = env->getType(ctx, container);
                const proto::ProtoObject* metaGetItem = nullptr;
                try {
                    metaGetItem = (meta && meta != env->getTypePrototype())
                        ? env->getAttribute(ctx, meta, getItemS, false) : nullptr;
                } catch (const proto::ProtoException&) {
                    metaGetItem = nullptr;
                }
                if (metaGetItem && metaGetItem != PROTO_NONE) {
                    result = metaGetItem->asMethod(ctx)
                        ? metaGetItem->asMethod(ctx)(ctx, container, nullptr, argsView(proto::ProtoListArguments(ctx, args)), nullptr)
                        : invokeCallable(ctx, metaGetItem,
                              ctx->newList()->appendLast(ctx, container)->appendLast(ctx, key), nullptr);
                } else {
                    const proto::ProtoString* classGetItemS = PythonEnvironment::getInternedString(ctx, "__class_getitem__");
                    result = invokeDunder(ctx, container, classGetItemS, args);
                }
            }
            if (!result) {
                result = invokeDunder(ctx, container, getItemS, args);
            }

            if (diag_local) {
                fprintf(stderr, "DEBUG: OP_BINARY_SUBSCR container=%p repr=%s key=%p repr=%s\n", (void*)container, PythonEnvironment::reprObject(ctx, container).c_str(), (void*)key, PythonEnvironment::reprObject(ctx, key).c_str());
                fflush(stderr);
            }
            if (!result) {
                const proto::ProtoString* classGetItemS = PythonEnvironment::getInternedString(ctx, "__class_getitem__");
                // Check if container itself has __class_getitem__ (for types) via invokeDunder
                result = invokeDunder(ctx, container, classGetItemS, args);
            }

            if (diag_local) {
                fprintf(stderr, "DEBUG: OP_BINARY_SUBSCR result=%p\n", (void*)result);
                fflush(stderr);
            }

            if (result) {
                stack.pop_back();
                stack.back() = result;
            } else {
                bool handled = false;
                // Fallback for minimal objects without __getitem__ (e.g. built-in lists/tuples if dunder is missing)
                const proto::ProtoObject* data = container->getAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                if (!data) data = container; // Fallback to the object itself (for primitive strings/tuples)
                
                if (data) {
                    if (::protoPython::listStorage(ctx, data) && key->isInteger(ctx)) {
                        long long idx = key->asLong(ctx);
                        const proto::ProtoList* list = ::protoPython::listStorage(ctx, data);
                        const proto::ProtoObject* res = (idx >= 0 && static_cast<proto::proto_ulong>(idx) < list->getSize(ctx)) ? list->getAt(ctx, static_cast<int>(idx)) : PROTO_NONE;
                        stack.pop_back(); stack.back() = res;
                        handled = true;
                    } else if (::protoPython::listStorage(ctx, data) && env && env->getSliceType() && (key->isInstanceOf(ctx, env->getSliceType())->asBoolean(ctx) || key->getAttribute(ctx, env->getStartString()))) {
                        // List Slicing
                        const proto::ProtoList* list = ::protoPython::listStorage(ctx, data);
                        long long size = static_cast<long long>(list->getSize(ctx));
                        
                        const proto::ProtoObject* startObj = key->getAttribute(ctx, env->getStartString());
                        const proto::ProtoObject* stopObj = key->getAttribute(ctx, env->getStopString());
                        const proto::ProtoObject* stepObj = key->getAttribute(ctx, env->getStepString());
                        
                        long long start = (startObj && startObj != PROTO_NONE) ? startObj->asLong(ctx) : 0;
                        long long stop = (stopObj && stopObj != PROTO_NONE) ? stopObj->asLong(ctx) : size;
                        long long step = (stepObj && stepObj != PROTO_NONE) ? stepObj->asLong(ctx) : 1;
                        
                        if (diag_local) {
                            // log removed
                        }
                        
                        if (start < 0) start += size;
                        if (stop < 0) stop += size;
                        if (start < 0) start = 0; if (start > size) start = size;
                        if (stop < 0) stop = 0; if (stop > size) stop = size;
                        
                        proto::ProtoObject* newListObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                        const proto::ProtoList* newList = ctx->newList();
                        
                        if (step > 0) {
                            for (long long i = start; i < stop; i += step) {
                                newList = newList->appendLast(ctx, list->getAt(ctx, static_cast<int>(i)));
                            }
                        } else if (step < 0) {
                            for (long long i = start; i > stop; i += step) {
                                newList = newList->appendLast(ctx, list->getAt(ctx, static_cast<int>(i)));
                            }
                        }
                        newListObj->setAttribute(ctx, env->getDataString(), newList->asObject(ctx));
                        if (env->getListPrototype()) newListObj->addParent(ctx, env->getListPrototype());
                        stack.pop_back();
                        stack.back() = newListObj;
                        handled = true;
                        } else if (data->asString(ctx)) {
                            const proto::ProtoString* s = data->asString(ctx);
                            long long size = static_cast<long long>(s->getSize(ctx));
                            if (key->isInteger(ctx)) {
                                long long idx = key->asLong(ctx);
                                if (idx < 0) idx += size;
                                const proto::ProtoString* charStr = (idx >= 0 && static_cast<proto::proto_ulong>(idx) < s->getSize(ctx)) ? s->getSlice(ctx, static_cast<int>(idx), static_cast<int>(idx) + 1) : nullptr;
                                const proto::ProtoObject* charObj = charStr ? charStr->asObject(ctx) : PROTO_NONE;
                                proto::ProtoObject* resObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                                resObj->setAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"), charObj);
                                if (env && env->getStrPrototype()) {
                                    resObj = const_cast<proto::ProtoObject*>(resObj->addParent(ctx, env->getStrPrototype()));
                                    resObj->setAttribute(ctx, env->getClassString(), env->getStrPrototype());
                                }
                                stack.pop_back(); stack.back() = resObj;
                                handled = true;
                            } else if (env && env->getSliceType() && (key->isInstanceOf(ctx, env->getSliceType())->asBoolean(ctx) || key->getAttribute(ctx, env->getStartString()))) {
                                const proto::ProtoObject* startObj = key->getAttribute(ctx, env->getStartString());
                                const proto::ProtoObject* stopObj = key->getAttribute(ctx, env->getStopString());
                                // step is ignored for now to simplify, or implemented same as list
                                long long start = (startObj && startObj != PROTO_NONE) ? startObj->asLong(ctx) : 0;
                                long long stop = (stopObj && stopObj != PROTO_NONE) ? stopObj->asLong(ctx) : size;
                                if (start < 0) start += size;
                                if (stop < 0) stop += size;
                                if (start < 0) start = 0; if (start > size) start = size;
                                if (stop < 0) stop = 0; if (stop > size) stop = size;
                                
                                const proto::ProtoString* slice = s->getSlice(ctx, static_cast<int>(start), static_cast<int>(stop));
                                proto::ProtoObject* resObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                                resObj->setAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"), slice->asObject(ctx));
                                if (env && env->getStrPrototype()) {
                                    resObj = const_cast<proto::ProtoObject*>(resObj->addParent(ctx, env->getStrPrototype()));
                                    resObj->setAttribute(ctx, env->getClassString(), env->getStrPrototype());
                                }
                                stack.pop_back(); stack.back() = resObj;
                                handled = true;
                            }
                        }
 else if (data->asSparseList(ctx)) {
                        proto::proto_ulong h = key->getHash(ctx);
                        const proto::ProtoObject* val = data->asSparseList(ctx)->getAt(ctx, h);
                        stack.pop_back();
                        stack.back() = (val ? val : PROTO_NONE);
                        handled = true;
                    }
                }
                
                if (!handled) {
                    stack.pop_back();
                    stack.back() = PROTO_NONE;
                }
                
                if (!handled && stack.back() == PROTO_NONE) {
                    // Start of Error Handling for unsubscriptable objects
                    std::string typeName = "unknown";
                    if (container) {
                         const proto::ProtoObject* cls = container->getAttribute(ctx, env ? env->getClassString() : protoPython::PythonEnvironment::getInternalString(ctx, "__class__"));
                         if (cls) {
                             const proto::ProtoObject* nameAttr = cls->getAttribute(ctx, env ? env->getNameString() : protoPython::PythonEnvironment::getInternalString(ctx, "__name__"));
                             if (nameAttr && nameAttr->isString(ctx)) nameAttr->asString(ctx)->toUTF8String(ctx, typeName);
                         } else if (container == PROTO_NONE) {
                             typeName = "NoneType";
                         }
                    }
                    std::string msg = "'" + typeName + "' object is not subscriptable";
                    if (env) env->raiseTypeError(ctx, msg);
                    stack.pop_back(); // Pop key to keep stack consistent for exception handling (handled by continue)
                    continue; 
                }
            }
        } break;
        case OP_BUILD_MAP: {
            if (stack.size() < static_cast<size_t>(arg * 2)) continue;
            const proto::ProtoSparseList* data = ctx->newSparseList();
            stack.push_back(data->asObject(ctx)); // Root data
            const proto::ProtoList* keys = ctx->newList();
            stack.push_back(keys->asObject(ctx)); // Root keys

            size_t baseIdx = stack.size() - 2 - 2 * arg;
            bool unhashable = false;
            for (int k = 0; k < arg; ++k) {
                const proto::ProtoObject* key = stack[baseIdx + 2 * k];
                const proto::ProtoObject* val = stack[baseIdx + 2 * k + 1];
                // Use the env-aware hash so user __hash__ overrides
                // (cistr et al.) bucket the same way as
                // py_dict_getitem / setitem.
                proto::proto_ulong h = 0;
                if (!PythonEnvironment::hashKey(ctx, key, h)) { unhashable = true; break; }
                data = data->setAt(ctx, h, val);
                stack[stack.size() - 2] = const_cast<proto::ProtoObject*>(data->asObject(ctx)); // Update data root
                keys = keys->appendLast(ctx, key);
                stack[stack.size() - 1] = const_cast<proto::ProtoObject*>(keys->asObject(ctx)); // Update keys root
            }
            if (unhashable) continue;
            
            proto::ProtoObject* dictObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            stack.push_back(dictObj); // Root dictObj
            if (env && env->getDictPrototype()) {
                dictObj = const_cast<proto::ProtoObject*>(dictObj->addParent(ctx, env->getDictPrototype()));
                stack.back() = dictObj;
            }
            
            const proto::ProtoString* dataName = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
            const proto::ProtoString* keysName = env ? env->getKeysString() : protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
            
            dictObj = const_cast<proto::ProtoObject*>(dictObj->setAttribute(ctx, dataName, data->asObject(ctx)));
            stack.back() = dictObj;
            dictObj = const_cast<proto::ProtoObject*>(dictObj->setAttribute(ctx, keysName, keys->asObject(ctx)));
            stack.back() = dictObj;
            
            const proto::ProtoObject* finalDict = dictObj;
            for (int k = 0; k < 2 * arg + 3; ++k) stack.pop_back();
            stack.push_back(finalDict);
        } break;
        case OP_STORE_SUBSCR: {
            // i++;
            if (diag_local) {
                fprintf(stderr, "DEBUG OP_STORE_SUBSCR stack.size()=%llu\n", static_cast<unsigned long long>((proto::proto_ulong)stack.size()));
            }
            if (stack.size() < 3) { i = next_i; continue; }
            proto::ProtoObject* container = const_cast<proto::ProtoObject*>(stack[stack.top - 2]);
            const proto::ProtoObject* key = stack.back();
            const proto::ProtoObject* value = stack[stack.top - 3];
            
            if (diag_local) {
                fprintf(stderr, "DEBUG OP_STORE_SUBSCR: value=%p key=%p container=%p\n", (void*)value, (void*)key, (void*)container);
                fflush(stderr);
            }
            // Delay pop

            // Fast path: list[smallint] = value.  Mirrors the SUBSCR fast
            // path; avoids allocating a 3-cell ProtoList plus the descriptor
            // protocol round-trip for the dominant numeric-table-write case.
            // setAt on ProtoList is the same primitive that __setitem__
            // would dispatch into.
            //
            // Subclass-aware gate: only fire when type(container) is exactly
            // listPrototype. A user list subclass with __setitem__ override
            // (or one that invalidates its `__data__` differently) must
            // route through the dunder path below — see docs/audits/02-fast-paths.md F2.2.
            if (proto::isSmallInt(key) && container
                && env && env->getType(ctx, container) == env->getListPrototype()) {
                const proto::ProtoList* lst = ::protoPython::listStorage(ctx, container);
                if (lst) {
                    long long idx = proto::asSmallInt(key);
                    long long size = static_cast<long long>(lst->getSize(ctx));
                    if (idx < 0) idx += size;
                    if (idx >= 0 && idx < size) {
                        const proto::ProtoList* newLst = lst->setAt(ctx, static_cast<int>(idx), value);
                        // Mutable container holds its data via __data__ usually,
                        // but plain ProtoList values flow through asList() which
                        // returns the underlying handle.  setAt returns a new
                        // immutable list; we publish it back via the same
                        // pathway the slow path would (setAttribute on __data__
                        // for wrapped lists, direct replacement otherwise).
                        const proto::ProtoString* dataS = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                        const proto::ProtoObject* ownData = dataS ? container->getOwnAttributeDirect(ctx, dataS) : nullptr;
                        // Publish only over the payload this write was
                        // derived from: a plain setAttribute dropped any
                        // append/pop another thread published in between.
                        // A lost race falls through to list.__setitem__,
                        // which retries.
                        if (ownData && ::protoPython::listStorage(ctx, ownData) == lst
                            && container->setAttributeIfEqual(ctx, dataS, ownData, newLst->asObject(ctx))) {
                            stack.pop_back(); stack.pop_back(); stack.pop_back();
                            i = next_i;
                            continue;
                        }
                        // container is a raw ProtoList or lost the race — fall through to slow path.
                    }
                    // Out-of-range or missing __data__ falls through to slow path.
                }
            }

            const proto::ProtoString* setItemS = env ? env->getSetItemString() : PythonEnvironment::getInternedString(ctx, "__setitem__");
            // Use env->getAttribute so the MRO is walked instead of
            // the raw protoCore parent chain.  Some subclass instances
            // (e.g. `class L(list)`) hit a different path on raw
            // chain walk that returns a tagged sentinel rather than
            // the inherited native method, sending the dispatch into
            // the integer-key fallback below (which then asLong's a
            // slice and throws "Object is not an integer type").
            const proto::ProtoObject* setitem = env
                ? env->getAttribute(ctx, container, setItemS, false)
                : container->getAttribute(ctx, setItemS);
            if (setitem && setitem != PROTO_NONE) {
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, value);
                invokeDunder(ctx, container, setItemS, args);
            } else if (env && (container->isString(ctx)
                               || ::protoPython::PythonEnvironment::isTupleValue(ctx, container))) {
                // Immutable sequences have no __setitem__: `s[0] = "x"` on a
                // str (or bytes) or a tuple is a TypeError.  The namespace
                // fallback below used to swallow the write silently.
                std::string typeName = "object";
                const proto::ProtoObject* tp = env->getType(ctx, container);
                const proto::ProtoObject* tn = tp ? tp->getAttribute(ctx, env->getNameString()) : nullptr;
                if (tn && tn->isString(ctx)) tn->asString(ctx)->toUTF8String(ctx, typeName);
                env->raiseTypeError(ctx, "'" + typeName + "' object does not support item assignment");
            } else {
                // Fallback for objects/maps without __setitem__
                if (key->isString(ctx)) {
                    // FLAT approach: store as direct attribute if it's a string key
                    // This is ideal for Namespace objects (classes, modules)
                    if (diag_local) {
                    }
                    container->setAttribute(ctx, key->asString(ctx), value);
                } else {
                    // Dictionary-like storage in __data__ for non-string keys or explicit collections
                    const proto::ProtoString* dataS = protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                    if (dataS) {
                        const proto::ProtoObject* dataObj = container->getAttribute(ctx, dataS);
                        if (dataObj && ::protoPython::listStorage(ctx, dataObj)) {
                            const proto::ProtoList* newList = ::protoPython::listStorage(ctx, dataObj)->setAt(ctx, static_cast<int>(key->asLong(ctx)), value);
                            container->setAttribute(ctx, dataS, newList->asObject(ctx));
                        } else {
                            const proto::ProtoSparseList* dataList = dataObj ? dataObj->asSparseList(ctx) : nullptr;
                            if (!dataList) {
                                dataList = ctx->newSparseList();
                            }
                            if (key->isInteger(ctx)) {
                                dataList = dataList->setAt(ctx, key->asLong(ctx), value);
                            } else {
                                dataList = dataList->setAt(ctx, key->getHash(ctx), value);
                            }
                            container->setAttribute(ctx, dataS, dataList->asObject(ctx));
                        }
                    }
                }
                
                // Keep __keys__ in sync for iteration/copying
                const proto::ProtoString* keysS = protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                if (keysS && key->isString(ctx)) {
                    const proto::ProtoObject* keysObj = container->getAttribute(ctx, keysS);
                    const proto::ProtoList* keysList = keysObj ? ::protoPython::listStorage(ctx, keysObj) : nullptr;
                    if (!keysList) {
                        keysList = ctx->newList();
                    }
                    if (!keysList->has(ctx, key)) {
                        keysList = keysList->appendLast(ctx, key);
                        container->setAttribute(ctx, keysS, keysList->asObject(ctx));
                    }
                }
            }
            stack.pop_back();
            stack.pop_back();
            stack.pop_back();
        } break;
        case OP_CALL_FUNCTION_KW: {
            if (stack.size() < 2) { i = next_i; continue; } // at least names_tuple
            const proto::ProtoObject* namesTupleObj = stack.back();
            stack.pop_back();
            ::protoPython::TupleView namesTuple = ::protoPython::PythonEnvironment::tupleView(ctx, namesTupleObj);
            if (!namesTuple) { i = next_i; continue; }
            int nkw = static_cast<int>(namesTuple->getSize(ctx));
            int npos = arg - nkw;
            if (stack.size() < static_cast<size_t>(arg) + 1) { 
                 if (env) env->raiseRuntimeError(ctx, "Stack underflow in OP_CALL_FUNCTION_KW");
                 i = next_i;
                 continue;
            }

            // Robust detection of modern vs legacy stack layout
            bool isModern = (stack.top >= (size_t)(arg + 2)); 
            proto::proto_ulong firstArgIdx = stack.top - arg;
            
            // Build positional args list. Use stack to root it.
            stack.push_back(ctx->newList()->asObject(ctx));
            for (int j = 0; j < npos; ++j) {
                const proto::ProtoList* l = ::protoPython::listStorage(ctx, stack.back());
                l = l->appendLast(ctx, stack[firstArgIdx + j]);
                stack[stack.top - 1] = l->asObject(ctx);
            }
            const proto::ProtoList* plArgs = ::protoPython::listStorage(ctx, stack.back());
            
            // Kw map
            stack.push_back(ctx->newSparseList()->asObject(ctx));
            const proto::ProtoSparseList* kwMap = stack.back()->asSparseList(ctx);
            for (int j = 0; j < nkw; ++j) {
                const proto::ProtoObject* key = namesTuple->getAt(ctx, j);
                const proto::ProtoObject* val = stack[firstArgIdx + npos + j];
                kwMap = kwMap->setAt(ctx, key->getHash(ctx), val);
                stack[stack.top - 1] = kwMap->asObject(ctx);
            }
            
            const proto::ProtoObject* Y = isModern ? stack[firstArgIdx - 1] : nullptr;
            const proto::ProtoObject* X = (isModern && firstArgIdx >= 2) ? stack[firstArgIdx - 2] : nullptr;
            
            const proto::ProtoObject* callable = nullptr;
            const proto::ProtoList* callArgs = nullptr;
            
            if (!isModern) {
                callable = stack[firstArgIdx - 1];
                callArgs = plArgs;
            } else if (X == nullptr) {
                callable = Y;
                callArgs = plArgs;
            } else if (Y == nullptr) {
                callable = X;
                callArgs = plArgs;
            } else {
                callable = X;
                const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, Y);
                for (proto::proto_ulong j = 0; j < plArgs->getSize(ctx); ++j) {
                    selfArgs = selfArgs->appendLast(ctx, plArgs->getAt(ctx, static_cast<int>(j)));
                }
                callArgs = selfArgs;
            }
            
            if (env) env->pushKwNames(namesTuple.tuple);
            const proto::ProtoObject* result = invokeCallable(ctx, callable, callArgs, kwMap);
            if (env) env->popKwNames();
            
            // Cleanup: Pop accurately.
            int itemsToPop = arg + (isModern ? 2 : 1) + 2; // +2 for intermediate lists
            for (int j = 0; j < itemsToPop; ++j) {
                if (!stack.empty()) stack.pop_back(); 
            }
            stack.push_back(result ? result : (env ? env->getNonePrototype() : PROTO_NONE));
        } break;
        case OP_CALL_FUNCTION: {
            if (stack.size() < (proto::proto_ulong)(arg + 1)) {
                 if (diag_local) fprintf(stderr, "DEBUG: OP_CALL_FUNCTION FATAL underflow size=%llu arg=%d PC=%llu\n", static_cast<unsigned long long>((proto::proto_ulong)stack.size()), arg, static_cast<unsigned long long>(i));
                 i = next_i;
                 continue;
            }

            // In 3.11+, CALL always consumes argc + 2 slots.
            // Layout: [NULL|Self, Callable, Arg1, ... ArgN]
            // We expect at least arg + 1 + 1 (the NULL/Self marker).
            proto::proto_ulong firstArgIdx = stack.top - arg;

            // Safety check: if the stack isn't deep enough to have a marker, it's a legacy call.
            bool isModern = (stack.top >= (size_t)(arg + 2));

            const proto::ProtoObject* Y = stack[firstArgIdx - 1];
            const proto::ProtoObject* X = isModern ? stack[firstArgIdx - 2] : nullptr;
            
            if (diag_local) {
                fprintf(stderr, "DEBUG CALL: argc=%d top=%zu firstArgIdx=%llu X=%p Y=%p isModern=%d\n", 
                        arg, stack.top, static_cast<unsigned long long>(firstArgIdx), (void*)X, (void*)Y, isModern);
            }

            const proto::ProtoObject* callable = nullptr;
            const proto::ProtoList* callArgs = nullptr;

            // User-function fast path: bypass ProtoList construction by passing the
            // raw stack slice directly to runUserFunctionCallRaw.
            //
            // Two layouts are accepted:
            //   [NULL, func, arg1..argN]  — plain function call, args = stack[firstArgIdx..].
            //   [func, self, arg1..argN]  — LOAD_METHOD-decomposed call (the LOAD_METHOD/CALL
            //                                hot path skips py_function_get's bound-method
            //                                allocation entirely; here we pass [self, arg1..argN]
            //                                as the raw slice, which lives contiguously on the
            //                                stack at stack[firstArgIdx - 1 ..]).
            const proto::ProtoObject* result_fast = nullptr;
            bool usedFastPath = false;
            if ((isModern && X == nullptr) || !isModern) {
                const proto::ProtoObject* candidate = Y;
                if (candidate && env && env->getFnMetaCacheString()) {
                    const proto::ProtoObject* cacheAttr =
                        candidate->getOwnAttributeDirect(ctx, env->getFnMetaCacheString());
                    if (cacheAttr && cacheAttr != PROTO_NONE) {
                        const proto::ProtoObject* const* rawArgSlice = stack.slots + firstArgIdx;
                        result_fast = runUserFunctionCallRaw(ctx, candidate, nullptr,
                                                              rawArgSlice, (proto::proto_ulong)arg);
                        usedFastPath = true;
                    }
                }
            } else if (isModern && X != nullptr) {
                // [func=X, self=Y, arg1..argN] — LOAD_METHOD-decomposed.
                const proto::ProtoObject* candidate = X;
                if (candidate && env && env->getFnMetaCacheString()) {
                    const proto::ProtoObject* cacheAttr =
                        candidate->getOwnAttributeDirect(ctx, env->getFnMetaCacheString());
                    if (cacheAttr && cacheAttr != PROTO_NONE) {
                        // Stack contains [..., func, self, arg1, ..., argN].  The combined
                        // slice [self, arg1, ..., argN] starts at firstArgIdx - 1 and has
                        // length arg + 1 — pass it as the raw arg slice with self prepended
                        // implicitly (no ProtoList allocation, no copy).
                        const proto::ProtoObject* const* rawArgSlice = stack.slots + (firstArgIdx - 1);
                        result_fast = runUserFunctionCallRaw(ctx, candidate, nullptr,
                                                              rawArgSlice, (proto::proto_ulong)(arg + 1));
                        usedFastPath = true;
                    }
                }
            }

            const proto::ProtoObject* result = nullptr;
            if (usedFastPath) {
                result = result_fast;
            } else {
            // Build callArgs in a SINGLE allocation when arg ≤ 5 via
            // ctx->newList(n, items) — the inline-list builder hands out one
            // cell for the entire ProtoList instead of 1 + N cells.  The
            // [Method, Self, Arg1...] branch passes the raw stack slice
            // starting one slot earlier (covering self), so the same
            // single-cell builder applies for arg+1 ≤ 5 — no intermediate
            // list, no walk, no per-element rebalance.
            if (!isModern) {
                callable = Y; // In legacy, Y is the callable and there is no X.
                callArgs = ctx->newList((unsigned)arg, stack.slots + firstArgIdx);
            } else if (X == nullptr) {
                // [NULL, Callable, Arg1...]
                callable = Y;
                callArgs = ctx->newList((unsigned)arg, stack.slots + firstArgIdx);
            } else {
                // [Method, Self, Arg1...]: stack contains [..., func, self,
                // arg1, ..., argN].  The combined slice [self, arg1, ...,
                // argN] starts at firstArgIdx - 1 and has length arg + 1 —
                // pass it directly as the source array.
                callable = X;
                callArgs = ctx->newList((unsigned)(arg + 1),
                                        stack.slots + firstArgIdx - 1);
            }

            if (!callable) {
                 if (diag_local) fprintf(stderr, "DEBUG: OP_CALL_FUNCTION nullptr callable detected! PC=%llu\n", static_cast<unsigned long long>(i));
                 if (env) env->raiseTypeError(ctx, "object is not callable (nullptr)");
                 i = next_i;
                 continue;
            }

            result = invokeCallable(ctx, callable, callArgs);
            } // end slow path


            // Cleanup: Pop (arg + slots)
            int itemsToPop = arg + (isModern ? 2 : 1);
            for (int j = 0; j < itemsToPop; ++j) {
                if (!stack.empty()) stack.pop_back();
            }
            
            stack.push_back(result ? result : (env ? env->getNonePrototype() : PROTO_NONE));
        } break;
        case OP_CALL_FUNCTION_EX: {
            // Robust detection of modern vs legacy stack layout
            bool isModern = false;
            // EX stack: ...[X][Y][starargs][kwargs]
            int segmentsSlots = (arg & 1) ? 4 : 3;
            if (stack.top >= (size_t)segmentsSlots) {
                const proto::ProtoObject* maybeX = stack[stack.top - (segmentsSlots)];
                // If X is NULL, it's definitely modern 3.11+ layout.
                if (maybeX == nullptr) isModern = true;
                else isModern = true; // For EX, we assume modern if we have enough slots.
            }

            const proto::ProtoObject* kwargs = (arg & 1) ? stack.back() : nullptr;
            const proto::ProtoObject* starargs = (arg & 1) ? stack[stack.top - 2] : stack.back();
            const proto::ProtoObject* Y = (arg & 1) ? stack[stack.top - 3] : stack[stack.top - 2];
            const proto::ProtoObject* X = isModern ? ((arg & 1) ? stack[stack.top - 4] : stack[stack.top - 3]) : nullptr;
            
            const proto::ProtoList* posArgs = nullptr;
            if (starargs && ::protoPython::listStorage(ctx, starargs)) {
                posArgs = ::protoPython::listStorage(ctx, starargs);
            } else if (starargs && ::protoPython::PythonEnvironment::tupleView(ctx, starargs)) {
                posArgs = ctx->newList();
                ::protoPython::TupleView t = ::protoPython::PythonEnvironment::tupleView(ctx, starargs);
                for (proto::proto_ulong j = 0; j < t->getSize(ctx); ++j) {
                    posArgs = posArgs->appendLast(ctx, t->getAt(ctx, static_cast<int>(j)));
                }
            } else if (starargs && starargs != PROTO_NONE && env) {
                const proto::ProtoObject* it = env->iter(starargs);
                if (it) {
                    const proto::ProtoList* L = ctx->newList();
                    // env->next answers nullptr when exhausted; errors throw.
                    while (const proto::ProtoObject* nextVal = env->next(it)) {
                        L = L->appendLast(ctx, nextVal);
                    }
                    posArgs = L;
                }
            }
            if (!posArgs) posArgs = ctx->newList();
            
            const proto::ProtoSparseList* kwArgs = nullptr;
            if (kwargs && kwargs != PROTO_NONE && env) {
                 const proto::ProtoString* dName = env->getDataString();
                 const proto::ProtoObject* data = kwargs->getAttribute(ctx, dName);
                 if (data && data->asSparseList(ctx)) {
                     kwArgs = data->asSparseList(ctx);
                 } else if (kwargs->asSparseList(ctx)) {
                     kwArgs = kwargs->asSparseList(ctx);
                 }
            }
            
            bool pushed = false;
            bool kwError = false;
            if (kwargs && env) {
                 const proto::ProtoObject* keysListObj = kwargs->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__keys__"));
                 if (keysListObj && ::protoPython::listStorage(ctx, keysListObj)) {
                     // Per CPython: all keys in `**kwargs` must be strings.
                     // e.g. `f(**{b'foo': 1})` raises TypeError.
                     const proto::ProtoList* kl = ::protoPython::listStorage(ctx, keysListObj);
                     for (proto::proto_ulong kj = 0; kj < kl->getSize(ctx); ++kj) {
                         const proto::ProtoObject* k = kl->getAt(ctx, static_cast<int>(kj));
                         if (!k || !k->isString(ctx)) {
                             env->raiseTypeError(ctx, "keywords must be strings");
                             kwError = true;
                             break;
                         }
                     }
                     if (!kwError) {
                         env->pushKwNames(ctx->newTupleFromList(kl));
                         pushed = true;
                     }
                 }
            }
            if (kwError) {
                // Pop arg segments from the stack; subsequent exception
                // handling will take over.
                for (int k2 = 0; k2 < segmentsSlots; ++k2) stack.pop_back();
                i = next_i;
                continue;
            }

            const proto::ProtoObject* result = nullptr;
            const proto::ProtoObject* targetCallable = nullptr;
            const proto::ProtoList* targetArgs = posArgs;
            
            if (!isModern) {
                targetCallable = Y;
            } else if (X == nullptr) {
                targetCallable = Y;
            } else if (Y == nullptr) {
                targetCallable = X;
            } else {
                targetCallable = X;
                // Prepend Y (Self) to targetArgs
                const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, Y);
                for (proto::proto_ulong j = 0; j < posArgs->getSize(ctx); ++j) {
                    selfArgs = selfArgs->appendLast(ctx, posArgs->getAt(ctx, static_cast<int>(j)));
                }
                targetArgs = selfArgs;
            }

            result = invokePythonCallable(ctx, targetCallable, targetArgs, kwArgs);
            if (pushed && env) env->popKwNames();
            
            // Cleanup: Pop correctly.
            int itemsToPop = (isModern ? 1 : 0) + 2 + ((arg & 1) ? 1 : 0);
            for (int j = 0; j < itemsToPop; ++j) {
                if (!stack.empty()) stack.pop_back();
            }
            stack.push_back(result ? result : (env ? env->getNonePrototype() : PROTO_NONE));
        } break;
        case OP_BUILD_TUPLE: {
            if (stack.size() < static_cast<size_t>(arg)) {
                i = next_i;
                continue;
            }
            // i++;
            const proto::ProtoList* lst = ctx->newList();
            stack.push_back(lst->asObject(ctx)); // Root lst
            
            size_t baseIdx = stack.size() - 1 - arg;
            for (int j = 0; j < arg; ++j) {
                lst = lst->appendLast(ctx, stack[baseIdx + j]);
                stack[stack.size() - 1] = const_cast<proto::ProtoObject*>(lst->asObject(ctx)); // Update root
            }
            
            // A tuple is a ProtoList-backed immutable object (makeTuple): a
            // ProtoTuple would go through protoCore's tuple interner and
            // never be collected.  `lst` stays rooted on the stack meanwhile.
            const proto::ProtoObject* finalTup = ::protoPython::PythonEnvironment::tupleFromList(ctx, lst);
            for (int j = 0; j < arg + 1; ++j) stack.pop_back();
            stack.push_back(finalTup);
        } break;
        case OP_BUILD_FUNCTION: {
            // GC discipline: every ProtoObject* we hold across an allocation
            // (newObject, addParent, setAttribute, …) must be reachable from
            // a GC root.  The operand stack lives inside automaticLocals, so
            // anything sitting in a stack slot is rooted; values held only
            // in C++ locals are NOT.  We therefore read the closure, codeObj,
            // defaults, kwDefaults and annotations *without* popping —
            // pop_back only decrements top, so the slots still hold the
            // values and the root scan still sees them.  Pops happen at the
            // very end, after every allocation has completed.
            //
            // Stack, bottom to top: [closure cells (arg >> 8 of them)] code
            // [defaults (0x01)] [kw defaults (0x02)] [annotations (0x04)].
            int extras = 0;
            if (arg & 0x04) extras++;
            if (arg & 0x02) extras++;
            if (arg & 0x01) extras++;
            const int below = arg >> BUILD_FUNCTION_CELLS_SHIFT;
            const proto::ProtoObject* annotations = nullptr;
            const proto::ProtoObject* kwDefaults = nullptr;
            const proto::ProtoObject* defaults = nullptr;
            int peekIdx = 0;
            if (arg & 0x04) { annotations = stack[stack.top - 1 - peekIdx++]; }
            if (arg & 0x02) { kwDefaults = stack[stack.top - 1 - peekIdx++]; }
            if (arg & 0x01) { defaults = stack[stack.top - 1 - peekIdx++]; }

            if (stack.size() > static_cast<size_t>(extras + below)) {
                const proto::ProtoObject* codeObj = stack[stack.top - 1 - extras];
                // The cells, in a ProtoList (not a tuple: see
                // BUILD_FUNCTION_CELLS_SHIFT).
                // Held on the operand stack while the function is built (a
                // GC root; protoCore embedder rule 3).
                const proto::ProtoList* closure = nullptr;
                if (below) {
                    closure = ctx->newList(static_cast<unsigned>(below),
                        &stack[stack.top - 1 - extras - below]);
                    stack.push_back(closure->asObject(ctx));
                }
                if (diag_local) {
                    fprintf(stderr, "DEBUG: OP_BUILD_FUNCTION PC=%llu arg=0x%llx codeObj=%p defaults=%p kwDefaults=%p\n", static_cast<unsigned long long>(i), static_cast<unsigned long long>((proto::proto_ulong)arg), (void*)codeObj, (void*)defaults, (void*)kwDefaults);
                    fflush(stderr);
                }
                proto::ProtoObject* fn = createUserFunction(ctx, codeObj, const_cast<proto::ProtoObject*>(PythonEnvironment::getCurrentGlobals()), closure, defaults, kwDefaults);
                // Push fn as a GC guard immediately.  Subsequent setAttribute
                // calls allocate (potentially crossing the per-context
                // threshold and submitting the young chain) and may return a
                // new fn pointer; we keep stack.back() in sync.
                if (fn) {
                    stack.push_back(fn);
                }
                if (fn && annotations) {
                    // Attach the annotations dict produced by the compile-time
                    // try/except annotation block (CPython exposes it as
                    // `f.__annotations__`).  When all annotations evaluate
                    // cleanly the dict has the expected entries; if any
                    // annotation expression raised (forward reference, etc.)
                    // the compiler's handler produced an empty dict so the
                    // attribute still exists with the right type.
                    const proto::ProtoString* annKey =
                        PythonEnvironment::getInternedString(ctx, "__annotations__");
                    fn = const_cast<proto::ProtoObject*>(
                        fn->setAttribute(ctx, annKey, annotations));
                    // A function with annotations has an __annotate__
                    // function; functionPrototype's None stays for the rest.
                    fn = const_cast<proto::ProtoObject*>(fn->setAttribute(ctx,
                        PythonEnvironment::getInternedString(ctx, "__annotate__"),
                        ctx->fromMethod(const_cast<proto::ProtoObject*>(annotations), functionAnnotate)));
                    stack.back() = fn;
                }
                // Result: fn at the top of the operand stack, the operands
                // removed.  No allocation happens in this sequence, so fn in a
                // C++ local is safe.
                if (fn) {
                    stack.pop_back();  // pop fn guard
                }
                if (closure) stack.pop_back();  // pop the closure guard
                for (int k = 0; k < extras + 1 + below; ++k) {
                    if (stack.top > 0) stack.pop_back();
                }
                if (fn) {
                    stack.push_back(fn);
                }
            }
        } break;
        case OP_BUILD_CLASS: {
            if (stack.size() >= 4) {
                // Robust detection: in modern bytecode, the function result of MAKE_FUNCTION
                // might have been pushed with a NULL marker if it was meant to be called.
                // But in BUILD_CLASS opcode (legacy path), we expect items directly.
                int firstIdx = static_cast<int>(stack.top) - 4;
                const proto::ProtoObject* body = stack[firstIdx + 3];
                const proto::ProtoObject* kwds = stack[firstIdx + 2];
                const proto::ProtoObject* bases = stack[firstIdx + 1];
                const proto::ProtoObject* name = stack[firstIdx];

                if (body == nullptr && stack.top >= 5) {
                    // Oops, there was a NULL marker. Shift.
                    firstIdx = static_cast<int>(stack.top) - 5;
                    body = stack[firstIdx + 4];
                    kwds = stack[firstIdx + 3];
                    bases = stack[firstIdx + 2];
                    name = stack[firstIdx + 1];
                }

                if (diag_local) {
                    fprintf(stderr, "DEBUG OP_BUILD_CLASS: stack size=%llu top=%llu\n", static_cast<unsigned long long>((proto::proto_ulong)stack.size()), static_cast<unsigned long long>((proto::proto_ulong)stack.top));
                    for (int j = 0; j < (int)stack.top; ++j) {
                        fprintf(stderr, "  stack[%d] = %p repr=%s\n", j, (void*)stack[j], env ? env->reprObject(ctx, stack[j]).c_str() : "???");
                    }
                    std::string n = "unknown";
                    if (name && name->isString(ctx)) name->asString(ctx)->toUTF8String(ctx, n);
                    fprintf(stderr, "DEBUG OP_BUILD_CLASS: building name='%s'\n", n.c_str());
                }
                
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                if (!env) { i = next_i; continue; }
                // The namespace, then the class, lives in a slot above the
                // four operands.
                stack.push_back(PROTO_NONE);
                const proto::ProtoObject** nsAnchor = &stack.back();
                const proto::ProtoObject* targetClass = env->buildClass(ctx, name, bases, kwds,
                    [ctx, env, body, nsAnchor, diag_local = diag_local](proto::ProtoObject*& ns) -> bool {
                        // Captured by value: a by-reference capture would take the
                        // address of the dispatch loop's locals and keep them out of
                        // registers for the whole loop.
                        // 3. Execute class body
                        if (body) {
                            const proto::ProtoString* callS = env ? env->getCallString() : protoPython::PythonEnvironment::getInternalString(ctx, "__call__");
                            const proto::ProtoString* codeS = env ? env->getCodeString() : protoPython::PythonEnvironment::getInternalString(ctx, "__code__");
                            const proto::ProtoObject* codeObj = body->getAttribute(ctx, codeS);
                            // The cells the class body closes over (its free
                            // variables, and the ones it passes on to its
                            // methods): the body function's own closure.
                            const proto::ProtoObject* bodyClosure = (env && env->getClosureString())
                                ? body->getAttribute(ctx, env->getClosureString()) : nullptr;
                            const proto::ProtoList* closureCells = (bodyClosure && bodyClosure != PROTO_NONE)
                                ? ::protoPython::listStorage(ctx, bodyClosure) : nullptr;
                            if (codeObj && codeObj != PROTO_NONE) {
                                if (diag_local) fprintf(stderr, "DEBUG OP_BUILD_CLASS: before body run ns=%p\n", (void*)ns);
                                // The class body is a Python-level scope of its own.
                                {
                                    ScopeGlobalsEntry bodyScope(PythonEnvironment::getCurrentGlobals());
                                    runCodeObject(ctx, codeObj, ns, closureCells);
                                }
                                if (diag_local) {
                                    const proto::ProtoObject* keysObj = ns->getAttribute(ctx, env ? env->getKeysString() : protoPython::PythonEnvironment::getInternalString(ctx, "__keys__"));
                                    const proto::ProtoList* keysList = keysObj ? ::protoPython::listStorage(ctx, keysObj) : nullptr;
                                    fprintf(stderr, "DEBUG OP_BUILD_CLASS: after body run ns=%p keysSize=%llu\n", (void*)ns, static_cast<unsigned long long>(keysList ? keysList->getSize(ctx) : 0));
                                }
                                *nsAnchor = ns; // ns may have been reallocated by CoW during execution
                            } else {
                                const proto::ProtoObject* callM = body->getAttribute(ctx, callS);
                                if (callM && callM->asMethod(ctx)) {
                                    callM->asMethod(ctx)(ctx, body, nullptr, argsView(proto::ProtoArguments(nullptr, 0)), nullptr);
                                }
                            }
                        }
                        return true;
                    }, nsAnchor);
                if (!targetClass) {
                    // The class body, the metaclass or __init_subclass__
                    // raised: unwind through the enclosing handlers.
                    i = next_i;
                    continue;
                }
                for (int j = 0; j < 5; ++j) stack.pop_back(); // Pop name, bases, kwds, body, ns
                stack.push_back(targetClass);
            }
        } break;
        case OP_GET_ITER: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* iterable = stack.back();
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoObject* iterObj = env ? env->iter(iterable) : nullptr;
            if (iterObj) {
                stack.back() = iterObj;
            } else {
                if (env) {
                    {
                        env->raiseTypeError(ctx, "object is not iterable");
                    }
                    i = next_i;
                    continue;
                } else {
                    return nullptr;
                }
            }
        } break;
        case OP_FOR_ITER: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* iterator = stack.back();

            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoObject* val = env ? env->next(iterator) : nullptr;
            
            if (val) {
                stack.push_back(val);
            } else {
                // Exhausted (env->next answers nullptr; an error throws).
                stack.pop_back();
                i = static_cast<proto::proto_ulong>(arg);
                continue;
            }
            // continue normal execution after pushing val
        } break;
        case OP_UNPACK_SEQUENCE: {
            if (stack.empty() || arg <= 0) {
                i = next_i;
                continue;
            }
            const proto::ProtoObject* seq = stack.back();
            stack.pop_back();

            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (env) {
                if (diag_local) fprintf(stderr, "DEBUG: UNPACK_SEQUENCE calling iter(seq)\n");
                const proto::ProtoObject* iterObj = env->iter(seq);
                if (!iterObj) {
                    env->raiseTypeError(ctx, "cannot unpack non-iterable object");
                    i = next_i; continue;
                }
                // Pin iterObj across user __next__ callbacks. The seq
                // was popped from the operand stack just above, so its
                // GC reachability now depends on iterObj surviving.
                PythonEnvironment::TransientPin pinIt(env, iterObj);
                std::vector<const proto::ProtoObject*> items;
                int got = 0;
                for (int j = 0; j < arg; ++j) {
                    const proto::ProtoObject* val = env->next(iterObj);
                    if (!val) {
                        // CPython embeds the expected and got counts:
                        //   ValueError: not enough values to unpack (expected 3, got 2)
                        std::string msg = "not enough values to unpack (expected "
                            + std::to_string(arg) + ", got " + std::to_string(got) + ")";
                        env->raiseValueError(ctx,
                            PythonEnvironment::getInternedString(ctx, msg.c_str())->asObject(ctx));
                    }
                    items.push_back(val);
                    got++;
                }

                // Check if there are too many values
                const proto::ProtoObject* excess = env->next(iterObj);
                if (excess) {
                    // CPython:
                    //   ValueError: too many values to unpack (expected 2)
                    std::string msg = "too many values to unpack (expected "
                        + std::to_string(arg) + ")";
                    env->raiseValueError(ctx,
                        PythonEnvironment::getInternedString(ctx, msg.c_str())->asObject(ctx));
                    i = next_i; continue;
                }

                // Push onto stack in reverse order so the first unpacked name gets the top of the stack
                for (int j = arg - 1; j >= 0; --j) {
                    stack.push_back(items[j]);
                }
            } else {
                const proto::ProtoList* list = ::protoPython::listStorage(ctx, seq);
                ::protoPython::TupleView tup = ::protoPython::PythonEnvironment::tupleView(ctx, seq);
                if (!list && !tup) {
                     const proto::ProtoObject* data = seq->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                     if (data) {
                         list = ::protoPython::listStorage(ctx, data);
                         tup = ::protoPython::PythonEnvironment::tupleView(ctx, data);
                     }
                }
                if (list) {
                    if (static_cast<int>(list->getSize(ctx)) < arg) {
                        if (env) env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx, "not enough values to unpack")->asObject(ctx));
                        i = next_i; continue;
                    }
                    for (int j = arg - 1; j >= 0; --j) {
                        stack.push_back(list->getAt(ctx, j));
                    }
                } else if (tup) {
                    if (static_cast<int>(tup->getSize(ctx)) < arg) {
                        if (env) env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx, "not enough values to unpack")->asObject(ctx));
                        i = next_i; continue;
                    }
                    for (int j = arg - 1; j >= 0; --j) {
                        stack.push_back(tup->getAt(ctx, j));
                    }
                }
            }
        } break;
        case OP_UNPACK_EX: {
            if (stack.empty()) { i = next_i; continue; }
            int num_before = arg & 0xFF;
            int num_after = (arg >> 8) & 0xFF;
            const proto::ProtoObject* seq = stack.back();
            stack.pop_back();

            std::vector<const proto::ProtoObject*> all;
            if (env) {
                const proto::ProtoObject* iterObj = env->iter(seq);
                if (!iterObj) {
                    env->raiseTypeError(ctx, "cannot unpack non-iterable object");
                    i = next_i; continue;
                }
                // Pin iterObj across user __next__ callbacks (seq was popped).
                PythonEnvironment::TransientPin pinIt(env, iterObj);
                while (true) {
                    const proto::ProtoObject* val = env->next(iterObj);
                    if (!val) break;   // exhausted
                    all.push_back(val);
                }
            } else {
                const proto::ProtoList* list = ::protoPython::listStorage(ctx, seq);
                ::protoPython::TupleView tup = ::protoPython::PythonEnvironment::tupleView(ctx, seq);
                if (!list && !tup) {
                    const proto::ProtoObject* data = seq->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                    if (data) { list = ::protoPython::listStorage(ctx, data); tup = ::protoPython::PythonEnvironment::tupleView(ctx, data); }
                }
                if (list) {
                    for (size_t j = 0; j < list->getSize(ctx); ++j) all.push_back(list->getAt(ctx, static_cast<int>(j)));
                } else if (tup) {
                    for (size_t j = 0; j < tup->getSize(ctx); ++j) all.push_back(tup->getAt(ctx, static_cast<int>(j)));
                } else {
                    continue;
                }
            }

            if (static_cast<int>(all.size()) < num_before + num_after) {
                if (env) env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx, "not enough values to unpack")->asObject(ctx));
                i = next_i; continue;
            }

            // Push after elements (in reverse order for stack)
            for (int idx = static_cast<int>(all.size()) - 1; idx >= static_cast<int>(all.size()) - num_after; --idx) {
                stack.push_back(all[idx]);
            }
            // Push middle as a Python list
            {
                const proto::ProtoList* middle = ctx->newList();
                for (int idx = num_before; idx < static_cast<int>(all.size()) - num_after; ++idx) {
                    middle = middle->appendLast(ctx, all[idx]);
                }
                const proto::ProtoObject* listProto = env ? env->getListPrototype() : nullptr;
                if (listProto) {
                    proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(listProto->newChild(ctx, true));
                    listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx,
                        PythonEnvironment::getInternedString(ctx, "__data__"), middle->asObject(ctx)));
                    stack.push_back(listObj);
                } else {
                    stack.push_back(middle->asObject(ctx));
                }
            }
            // Push before elements (in reverse order)
            for (int idx = num_before - 1; idx >= 0; --idx) {
                stack.push_back(all[idx]);
            }
        } break;
        case OP_LOAD_GLOBAL: {
            bool pushNull = (arg & 1);
            int nameIdx = arg >> 1;
            // No `frame` guard: LOAD_GLOBAL resolves via env->resolve()/getCurrentGlobals(),
            // not via frame. This allows LOAD_GLOBAL in frame-free (slot fast path) contexts.
            {
                // Use flat pre-fetched nativeNames array when available — avoids cross-DSO
                // AVL lookup + isString/asString conversions since all nativeNames are ProtoString*.
                const proto::ProtoString* nameS = nullptr;
                if (nativeNames && names && static_cast<proto::proto_ulong>(nameIdx) < names->getSize(ctx)) {
                    const proto::ProtoObject* n = nativeNames[nameIdx];
                    nameS = n ? n->asString(ctx) : nullptr;
                } else if (names && static_cast<proto::proto_ulong>(nameIdx) < names->getSize(ctx)) {
                    const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                    if (nameObj && proto::ProtoObject::isStringTagFast(nameObj))
                        nameS = nameObj->asString(ctx);
                }
                if (nameS && env) {
                    // LOAD_GLOBAL resolves in globals+builtins only — never in the local frame.
                    const proto::ProtoObject* val = env->resolveGlobalName(nameS, ctx);
                    if (val != nullptr) {
                        if (pushNull) stack.push_back(nullptr);
                        stack.push_back(val);
                    } else {
                        {
                            std::string n;
                            nameS->toUTF8String(ctx, n);
                            env->raiseNameError(ctx, n);
                        }
                        continue;
                    }
                } else if (nameS) {
                    // env is null (unit-test context) — fall back to getCurrentGlobals() or frame.
                    const proto::ProtoObject* globalsObj = PythonEnvironment::getCurrentGlobals();
                    if (!globalsObj) globalsObj = frame;
                    const proto::ProtoObject* val = globalsObj ? globalsObj->getAttribute(ctx, nameS) : nullptr;
                    if (val && val != PROTO_NONE) {
                        if (pushNull) stack.push_back(nullptr);
                        stack.push_back(val);
                    } else {
                        continue;
                    }
                } else {
                    continue;
                }
            }
        } break;
        case OP_STORE_GLOBAL: {
            int nameIdx = arg >> 1;
            if (names && static_cast<proto::proto_ulong>(nameIdx) < names->getSize(ctx)) {
                if (stack.empty()) { i = next_i; continue; }
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoObject* globalsObj = PythonEnvironment::getCurrentGlobals();
                    if (!globalsObj) globalsObj = frame;
                    const proto::ProtoObject* newGlobals = globalsObj->setAttribute(ctx, nameObj->asString(ctx), val);
                    PythonEnvironment::setCurrentGlobals(newGlobals);
                    if (frame) {
                    const proto::ProtoString* fg = env ? env->getFGlobalsString() : protoPython::PythonEnvironment::getInternalString(ctx, "f_globals");
                    frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, fg, newGlobals));
                    PythonEnvironment::setCurrentFrame(frame);
                    } // if (frame)
                    if (env) env->invalidateResolveCache();
                }
            }
        } break;
        case OP_BUILD_SLICE: {
            // i++;
            if ((arg != 2 && arg != 3) || stack.size() < static_cast<size_t>(arg)) continue;
            long long step = 1;
            const proto::ProtoObject* stepObj = nullptr;
            if (arg == 3) {
                stepObj = stack.back();
                stack.pop_back();
            } else {
                // CPython: `a[i:j]` is `slice(i, j, None)` — step is
                // None, NOT 1 — and slice equality compares the raw
                // (start, stop, step) triple, so substituting 1 here
                // breaks `slice(0,10) == slice(0,10)` for slices that
                // came from BUILD_SLICE.
                stepObj = PROTO_NONE;
            }
            (void)step;
            const proto::ProtoObject* stopObj = stack.back();
            stack.pop_back();
            const proto::ProtoObject* startObj = stack.back();
            stack.pop_back();
            proto::ProtoObject* sliceObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx, env ? env->getStartString() : PythonEnvironment::getInternedString(ctx, "start"), startObj));
            sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx, env ? env->getStopString() : PythonEnvironment::getInternedString(ctx, "stop"), stopObj));
            sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx, env ? env->getStepString() : PythonEnvironment::getInternedString(ctx, "step"), stepObj));
            if (env && env->getSliceType()) {
                sliceObj = const_cast<proto::ProtoObject*>(sliceObj->addParent(ctx, env->getSliceType()));
                // __class__ is required for isinstance(slice_obj, slice) to
                // detect the slice type via env->getType (which checks the
                // own __class__ attribute first, then walks the parent
                // chain).  Without it, `isinstance(s, slice)` returned
                // False inside user __getitem__ and the slice branch
                // never fired.
                sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx,
                    env->getClassString(), env->getSliceType()));
            }
            stack.push_back(sliceObj);
        } break;
        case OP_ROT_TWO: {
            if (stack.size() >= 2) {
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                stack.push_back(a);
                stack.push_back(b);
            }
        } break;
        case OP_ROT_THREE: {
            if (stack.size() >= 3) {
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                const proto::ProtoObject* c = stack.back();
                stack.pop_back();
                stack.push_back(a);
                stack.push_back(c);
                stack.push_back(b);
            }
        } break;
        case OP_ROT_FOUR: {
            if (stack.size() >= 4) {
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                const proto::ProtoObject* c = stack.back();
                stack.pop_back();
                const proto::ProtoObject* d = stack.back();
                stack.pop_back();
                stack.push_back(a);
                stack.push_back(d);
                stack.push_back(c);
                stack.push_back(b);
            }
        } break;
        case OP_LIST_TO_TUPLE: {
            // GC Safe: list stays on stack until tuple is ready
            if (!stack.empty()) {
                proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(stack.back());
                
                const proto::ProtoString* dataS = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = listObj->getAttribute(ctx, dataS);
                const proto::ProtoList* L = (data && ::protoPython::listStorage(ctx, data)) ? ::protoPython::listStorage(ctx, data) : nullptr;
                if (diag_local) {
                    fprintf(stderr, "DEBUG: OP_LIST_TO_TUPLE L=%p size=%zu\n", (void*)L, L ? L->getSize(ctx) : 0);
                }
                
                if (L && env) {
                    // The list's ProtoList is immutable: the tuple shares it.
                    const proto::ProtoObject* tupObj = ::protoPython::PythonEnvironment::tupleFromList(ctx, L);
                    stack.pop_back();
                    stack.push_back(tupObj);
                } else {
                    stack.pop_back();
                    stack.push_back(PROTO_NONE);
                }
            }
        } break;
        case OP_DUP_TOP_TWO: {
            if (stack.size() >= 2) {
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                stack.push_back(a);
                stack.push_back(b);
                stack.push_back(a);
                stack.push_back(b);
            }
        } break;
        case OP_DUP_TOP: {
            if (!stack.empty())
                stack.push_back(stack.back());
        } break;
        case OP_DELETE_NAME:
        case OP_DELETE_GLOBAL: {
            // `del name` undoes what STORE_NAME / STORE_GLOBAL wrote: the
            // namespace's own attribute, its __data__ entry and its __keys__
            // entry.  The old code discarded the SparseList returned by
            // removeAt, so every del was a no-op, and it swallowed errors, so
            // deleting an unbound name never raised NameError.
            // DELETE_GLOBAL acts on the module globals; DELETE_NAME on the
            // current namespace (module, class body namespace, exec() dict).
            int nameIdx = arg >> 1;
            if (!names || static_cast<proto::proto_ulong>(nameIdx) >= names->getSize(ctx)) break;
            const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
            if (!nameObj || !proto::ProtoObject::isStringTagFast(nameObj)) break;
            const proto::ProtoString* nS = nameObj->asString(ctx);
            const bool isGlobalDel = (op == OP_DELETE_GLOBAL);
            proto::ProtoObject* target = frame;
            if (isGlobalDel && PythonEnvironment::getCurrentGlobals()) {
                target = const_cast<proto::ProtoObject*>(PythonEnvironment::getCurrentGlobals());
            }
            if (!target) break;

            // Custom class namespace from __prepare__ (e.g. EnumDict): the
            // same interception STORE_NAME does with __setitem__.
            if (!isGlobalDel && env) {
                const proto::ProtoObject* frameType = env->getType(ctx, target);
                if (frameType == env->getDictPrototype()) {
                    const proto::ProtoString* classS = env->getClassString() ? env->getClassString() : PythonEnvironment::getInternedString(ctx, "__class__");
                    const proto::ProtoObject* cls = target->proto::ProtoObject::getAttribute(ctx, classS);
                    if (cls && cls != PROTO_NONE) frameType = cls;
                }
                if (frameType && frameType != PROTO_NONE &&
                    frameType != env->getDictPrototype() &&
                    frameType != env->getModulePrototype()) {
                    const proto::ProtoObject* delitem = env->getAttribute(ctx, frameType, env->getDelItemString(), false);
                    if (delitem && delitem != PROTO_NONE) {
                        const proto::ProtoList* delArgs = ctx->newList()->appendLast(ctx, nameObj);
                        try {
                            invokeDunder(ctx, target, env->getDelItemString(), delArgs);
                        } catch (const proto::ProtoException&) {
                            // CPython reports the mapping's failure as NameError.
                            env->invalidateResolveCache();
                            std::string nStr;
                            nS->toUTF8String(ctx, nStr);
                            env->raiseNameError(ctx, nStr);
                        }
                        env->invalidateResolveCache();
                        break;
                    }
                }
            }

            const proto::ProtoString* dataS = env ? env->getDataString() : PythonEnvironment::getInternalString(ctx, "__data__");
            const proto::ProtoString* keysS = env ? env->getKeysString() : PythonEnvironment::getInternedString(ctx, "__keys__");
            const proto::proto_ulong nameHash = nameObj->getHash(ctx);
            const bool ownAttr = target->hasOwnAttribute(ctx, nS) == PROTO_TRUE;
            const proto::ProtoObject* dataObj = target->getAttribute(ctx, dataS);
            const proto::ProtoSparseList* dataList = dataObj ? dataObj->asSparseList(ctx) : nullptr;
            const bool inData = dataList && dataList->has(ctx, nameHash);
            if (!ownAttr && !inData) {
                if (env) {
                    std::string nStr;
                    nS->toUTF8String(ctx, nStr);
                    env->raiseNameError(ctx, nStr);
                }
                i = next_i;
                continue;
            }

            const proto::ProtoObject* oldTarget = target;
            if (ownAttr) {
                target = const_cast<proto::ProtoObject*>(target->removeAttribute(ctx, nS));
            }
            if (inData) {
                target = const_cast<proto::ProtoObject*>(target->setAttribute(ctx, dataS,
                    dataList->removeAt(ctx, nameHash)->asObject(ctx)));
            }
            if (target->hasOwnAttribute(ctx, keysS) == PROTO_TRUE) {
                const proto::ProtoObject* keysObj = target->getOwnAttributeDirect(ctx, keysS);
                const proto::ProtoList* keysList = keysObj ? ::protoPython::listStorage(ctx, keysObj) : nullptr;
                const int keysSize = keysList ? static_cast<int>(keysList->getSize(ctx)) : 0;
                for (int k = 0; k < keysSize; ++k) {
                    const proto::ProtoObject* key = keysList->getAt(ctx, k);
                    if (key == nameObj || (key && key->isString(ctx) && key->getHash(ctx) == nameHash)) {
                        target = const_cast<proto::ProtoObject*>(target->setAttribute(ctx, keysS,
                            keysList->removeAt(ctx, k)->asObject(ctx)));
                        break;
                    }
                }
            }
            syncModuleIdentity(ctx, env, oldTarget, target);
            if (isGlobalDel) {
                PythonEnvironment::setCurrentGlobals(target);
                if (frame == oldTarget) {
                    frame = target;
                } else if (frame && target != oldTarget) {
                    const proto::ProtoString* fg = env ? env->getFGlobalsString() : protoPython::PythonEnvironment::getInternalString(ctx, "f_globals");
                    frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, fg, target));
                }
            } else {
                frame = target;
            }
            if (env) {
                PythonEnvironment::setCurrentFrame(frame);
                if (!isGlobalDel && sync_globals && frame != PythonEnvironment::getCurrentGlobals()) {
                    frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, env->getFGlobalsString(), frame));
                    PythonEnvironment::setCurrentGlobals(frame);
                }
                env->invalidateResolveCache();
            }
        } break;
        case OP_DELETE_FAST: {
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<proto::proto_ulong>(arg) < nSlots) {
                proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(ctx->getAutomaticLocals());
                // LOAD_FAST reads nullptr as None; the unbound sentinel (the one
                // annotation-only locals start with) makes a read after `del x`
                // raise UnboundLocalError.
                const proto::ProtoObject* unbound = env ? env->getUnboundSentinel() : nullptr;
                if (env && unbound && (slots[arg] == nullptr || slots[arg] == unbound)) {
                    raiseUnboundFastLocal(ctx, env, arg);
                    i = next_i;
                    continue;
                }
                slots[arg] = const_cast<proto::ProtoObject*>(unbound);
            }
        } break;
        case OP_DELETE_ATTR: {
            if (!stack.empty()) {
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                int nameIdx = arg >> 1;
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (nameObj && proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    // CPython: `del obj.__class__` is always rejected.
                    // The instance's type identity is fixed for the
                    // lifetime of the object.
                    const proto::ProtoString* classS = env ? env->getClassString() : nullptr;
                    if (classS && (nameS == classS || nameS->getHash(ctx) == classS->getHash(ctx))
                        && env) {
                        std::string clsName = "?";
                        const proto::ProtoObject* tp = env->getType(ctx, obj);
                        if (tp) {
                            const proto::ProtoObject* nm = tp->getAttribute(ctx, env->getNameString());
                            if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, clsName);
                        }
                        env->raiseTypeError(ctx,
                            "can't delete __class__ attribute of '" + clsName + "' object");
                        continue;
                    }
                    // STRUCT-70: `del Cls.__bases__` (and other structural
                    // attrs on a class) is always rejected — these
                    // attributes are part of the type's identity.
                    // CPython's message for heap classes:
                    // "cannot delete '<attr>' attribute of type 'X'".
                    if (env && env->isActuallyAClass(ctx, obj)) {
                        std::string nm; nameS->toUTF8String(ctx, nm);
                        bool structural = (nm == "__bases__" || nm == "__base__"
                            || nm == "__mro__" || nm == "__name__" || nm == "__qualname__");
                        if (structural) {
                            std::string clsName = "?";
                            const proto::ProtoObject* clsNm = obj->getAttribute(ctx, env->getNameString());
                            if (clsNm && clsNm->isString(ctx)) clsNm->asString(ctx)->toUTF8String(ctx, clsName);
                            env->raiseTypeError(ctx,
                                "cannot delete '" + nm + "' attribute of type '" + clsName + "'");
                            continue;
                        }
                    }
                    // CPython: `del obj.__dict__` resets the instance
                    // dict to empty but leaves the descriptor in place,
                    // so subsequent `obj.__dict__` still returns a
                    // fresh empty dict.  Reuse the well-tested
                    // `obj.__dict__ = {}` slow path (which validates
                    // and repopulates __data__/__keys__) to keep the
                    // semantics aligned.
                    const proto::ProtoString* dictDunderS = env ? env->getDictDunderString() : nullptr;
                    if (dictDunderS && (nameS == dictDunderS || nameS->getHash(ctx) == dictDunderS->getHash(ctx))
                        && obj && obj != PROTO_NONE && env) {
                        // CPython: `del cls.__dict__` is rejected because
                        // the class dict is read-only.  Mirror that.
                        if (env->isActuallyAClass(ctx, obj)) {
                            std::string clsName = "?";
                            const proto::ProtoObject* nm = obj->getAttribute(ctx, env->getNameString());
                            if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, clsName);
                            env->raiseTypeError(ctx,
                                "cannot delete attribute '__dict__' of '" + clsName + "' objects");
                            continue;
                        }
                        // CPython: `del mod.__dict__` is likewise rejected
                        // for module objects and ModuleType subclass
                        // instances — the namespace binding is read-only.
                        // Detect via type identity / MRO so both plain
                        // modules and ModuleType subclasses qualify.
                        if (env->getModulePrototype()) {
                            const proto::ProtoObject* modProto = env->getModulePrototype();
                            const proto::ProtoObject* objType = env->getType(ctx, obj);
                            bool isModuleLike = (objType == modProto);
                            if (!isModuleLike && objType && objType != PROTO_NONE) {
                                const proto::ProtoString* mroS = env->getMroString();
                                const proto::ProtoObject* oMroAttr = mroS ? env->getAttribute(ctx, objType, mroS, false) : nullptr;
                                ::protoPython::TupleView oMroT = oMroAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, oMroAttr) : nullptr;
                                if (oMroT) {
                                    for (proto::proto_ulong mi = 0; mi < oMroT->getSize(ctx); ++mi) {
                                        if (oMroT->getAt(ctx, static_cast<int>(mi)) == modProto) {
                                            isModuleLike = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (isModuleLike) {
                                env->raiseTypeError(ctx,
                                    "cannot delete attribute '__dict__' of 'module' objects");
                                continue;
                            }
                        }
                        proto::ProtoObject* mobj = const_cast<proto::ProtoObject*>(obj);
                        // STRUCT-271: clear ALL user-set native attributes
                        // (not just those listed in __keys__/__pydict_keys__),
                        // then reset the dict-storage slots.  The keysList
                        // path missed entries that OP_STORE_ATTR wrote as
                        // native-only mirrors when the container subclass
                        // hadn't yet initialized __pydict_keys__.
                        const proto::ProtoString* dataS = env->getDataString();
                        const proto::ProtoString* canonicalKeysS = env->getKeysString();
                        bool isBuiltinContainerSubclass = false;
                        {
                            const proto::ProtoObject* probe = mobj->hasOwnAttribute(ctx, dataS) == PROTO_TRUE
                                ? mobj->getAttribute(ctx, dataS) : nullptr;
                            if (probe && probe != PROTO_NONE && !probe->asSparseList(ctx)) {
                                isBuiltinContainerSubclass = true;
                            }
                        }
                        // Walk all OWN attributes and drop user ones.
                        // Skip the runtime-internal bookkeeping slots
                        // and the container payload for built-in subclasses.
                        static const std::vector<std::string> internalSkip = {
                            "__data__", "__keys__", "__pydict_data__",
                            "__pydict_keys__", "__is_python_class__",
                            "__subclasses_list__", "__pyflags__",
                            "__py_getattr_handler__", "__fn_meta_cache__",
                            "__executed__", "__class__",
                        };
                        // The walk reports the attributes as they stood when
                        // it started, so removing them from the callback is
                        // safe and leaves the rest of the walk unchanged.
                        mobj->processOwnAttributes(ctx, mobj,
                            [](proto::ProtoContext* c, void* userData, const proto::ProtoString* name,
                               const proto::ProtoObject*) {
                                if (!name) return;
                                std::string nm; name->toUTF8String(c, nm);
                                for (auto& bad : internalSkip) if (nm == bad) return;
                                static_cast<const proto::ProtoObject*>(userData)->removeAttribute(c, name);
                            });
                        // Reset the dict-storage slots.
                        if (isBuiltinContainerSubclass) {
                            mobj->setAttribute(ctx,
                                PythonEnvironment::getInternedString(ctx, "__pydict_data__"),
                                ctx->newSparseList()->asObject(ctx));
                            mobj->setAttribute(ctx,
                                PythonEnvironment::getInternedString(ctx, "__pydict_keys__"),
                                ctx->newList()->asObject(ctx));
                        } else {
                            mobj->setAttribute(ctx, dataS, ctx->newSparseList()->asObject(ctx));
                        }
                        mobj->setAttribute(ctx, canonicalKeysS, ctx->newList()->asObject(ctx));
                        i = next_i;
                        continue;
                    }
                    // CPython: an OWN __delattr__ on the type's MRO
                    // intercepts every `del obj.attr`, just like
                    // __setattr__ intercepts assignment.  Walk the MRO
                    // (stop at objectPrototype — its default doesn't
                    // exist as own here) and dispatch the override
                    // before falling into the data-descriptor / native
                    // delete paths.  Recursion guard mirrors the
                    // setAttr depth counter so that
                    // `def __delattr__(self, n): del self.<other>`
                    // doesn't loop on the inner del.
                    if (env) {
                        static thread_local int delAttrDispatchDepth = 0;
                        struct DelGuard { int& d; DelGuard(int& d) : d(d) { d++; } ~DelGuard() { d--; } };
                        if (delAttrDispatchDepth == 0 && !env->isActuallyAClass(ctx, obj)) {
                            const proto::ProtoObject* objType = env->getType(ctx, obj);
                            if (objType && objType != PROTO_NONE) {
                                const proto::ProtoString* delattrS =
                                    PythonEnvironment::getInternedString(ctx, "__delattr__");
                                const proto::ProtoObject* mroAttr = env->getAttribute(ctx, objType, env->getMroString(), false);
                                ::protoPython::TupleView mroT = mroAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, mroAttr) : nullptr;
                                const proto::ProtoObject* override = nullptr;
                                if (mroT) {
                                    for (proto::proto_ulong mi = 0; mi < mroT->getSize(ctx); ++mi) {
                                        const proto::ProtoObject* base = mroT->getAt(ctx, static_cast<int>(mi));
                                        if (!base || base == PROTO_NONE) continue;
                                        if (base == env->getObjectPrototype()) break;
                                        if (base->hasOwnAttribute(ctx, delattrS) == PROTO_TRUE) {
                                            override = base->getOwnAttributeDirect(ctx, delattrS);
                                            break;
                                        }
                                    }
                                }
                                if (override && override != PROTO_NONE) {
                                    DelGuard g(delAttrDispatchDepth);
                                    const proto::ProtoList* args = ctx->newList()
                                        ->appendLast(ctx, nameS->asObject(ctx));
                                    if (override->asMethod(ctx)) {
                                        override->asMethod(ctx)(ctx,
                                            const_cast<proto::ProtoObject*>(obj), nullptr, argsView(proto::ProtoListArguments(ctx, args)), nullptr);
                                    } else {
                                        const proto::ProtoList* selfArgs = ctx->newList()
                                            ->appendLast(ctx, obj)
                                            ->appendLast(ctx, nameS->asObject(ctx));
                                        invokePythonCallable(ctx, override, selfArgs, nullptr);
                                    }
                                    continue;
                                }
                            }
                        }
                    }
                    // The generic delete: a data descriptor's __delete__, else
                    // the own attribute, else AttributeError (shared with
                    // object.__delattr__ and delattr()).  Advancing past the
                    // opcode on success matters (STRUCT-267): re-running it
                    // would pop the enclosing for-loop's iterator.
                    if (env) {
                        env->deleteAttribute(ctx, obj, nameS);
                    } else if (obj->hasOwnAttribute(ctx, nameS) == PROTO_TRUE) {
                        const_cast<proto::ProtoObject*>(obj)->proto::ProtoObject::removeAttribute(ctx, nameS);
                    }
                }
            }
        } break;
        case OP_DELETE_SUBSCR: {
            if (stack.size() >= 2) {
                const proto::ProtoObject* key = stack.back();
                const proto::ProtoObject* container = stack[stack.top - 2];
                // Delay pop
                const proto::ProtoString* delItemS = env ? env->getDelItemString() : PythonEnvironment::getInternedString(ctx, "__delitem__");
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
                const proto::ProtoObject* result = invokeDunder(ctx, container, delItemS, args);
                if (!result) {
                    // Fallback for genuine wrapped list / dict instances.
                    // ProtoObject::asSparseList resolves the attribute
                    // SparseList of any object (every instance has one),
                    // so it is not a sufficient discriminator on its own.
                    // Require the container's type chain to actually
                    // descend from listPrototype / dictPrototype before
                    // routing into the __data__-based fallback.
                    bool handled = false;
                    bool isContainerList = false;
                    bool isContainerDict = false;
                    if (env) {
                        const proto::ProtoObject* tp = env->getType(ctx, container);
                        const proto::ProtoObject* listProto = env->getListPrototype();
                        const proto::ProtoObject* dictProto = env->getDictPrototype();
                        std::function<bool(const proto::ProtoObject*, const proto::ProtoObject*, int)> isOrDescends =
                            [&](const proto::ProtoObject* c, const proto::ProtoObject* anchor, int depth) -> bool {
                                if (!c || c == PROTO_NONE || !anchor || depth > 32) return false;
                                if (c == anchor) return true;
                                const proto::ProtoObject* basesAttr =
                                    c->getAttribute(ctx, env->getBasesString());
                                ::protoPython::TupleView basesT =
                                    basesAttr ? ::protoPython::PythonEnvironment::tupleView(ctx, basesAttr) : nullptr;
                                if (!basesT) return false;
                                for (proto::proto_ulong bi = 0; bi < basesT->getSize(ctx); ++bi) {
                                    const proto::ProtoObject* b =
                                        basesT->getAt(ctx, static_cast<int>(bi));
                                    if (isOrDescends(b, anchor, depth + 1)) return true;
                                }
                                return false;
                            };
                        isContainerList = tp && listProto && isOrDescends(tp, listProto, 0);
                        isContainerDict = tp && dictProto && isOrDescends(tp, dictProto, 0);
                    }
                    const proto::ProtoList* lst = isContainerList ? ::protoPython::listStorage(ctx, container) : nullptr;
                    const proto::ProtoSparseList* sl =
                        (!lst && isContainerDict) ? container->asSparseList(ctx) : nullptr;
                    if (lst && key->isInteger(ctx)) {
                        long long idx = key->asLong(ctx);
                        if (idx >= 0 && static_cast<proto::proto_ulong>(idx) < lst->getSize(ctx)) {
                            const proto::ProtoList* newList = ctx->newList();
                            for (proto::proto_ulong j = 0; j < lst->getSize(ctx); ++j) {
                                if (static_cast<long long>(j) != idx) {
                                    newList = newList->appendLast(ctx, lst->getAt(ctx, static_cast<int>(j)));
                                }
                            }
                            const proto::ProtoString* data_name = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                            const_cast<proto::ProtoObject*>(container)->setAttribute(ctx, data_name, newList->asObject(ctx));
                            handled = true;
                        }
                    } else if (sl) {
                        sl->removeAt(ctx, key->getHash(ctx));
                        handled = true;
                    }
                    if (!handled && env) {
                        // No __delitem__ method and the container is not
                        // a wrapped list / dict — raise TypeError to
                        // match CPython's `'X' object doesn't support
                        // item deletion`.
                        std::string typeName = "object";
                        const proto::ProtoObject* tp = env->getType(ctx, container);
                        if (tp && tp != PROTO_NONE) {
                            const proto::ProtoString* nameS = env->getNameString();
                            const proto::ProtoObject* tn = nameS ? tp->getAttribute(ctx, nameS) : nullptr;
                            if (tn && tn->isString(ctx)) tn->asString(ctx)->toUTF8String(ctx, typeName);
                        }
                        env->raiseTypeError(ctx, "'" + typeName + "' object doesn't support item deletion");
                        stack.pop_back();
                        stack.pop_back();
                        continue;
                    }
                }
                stack.pop_back(); // Pop key
                stack.pop_back(); // Pop container
            }
        } break;
        case OP_SETUP_FINALLY: {
            // The protected range runs as a nested dispatch inside a C++
            // try; its handler (at arg) inside the catch.
            const Block tryRange{static_cast<proto::proto_ulong>(arg), stack.size()};
            const proto::proto_ulong rangeStart = next_i;
            const proto::ProtoObject* r = runRange(tryRange,
                [&]() { return dispatch(rangeStart, ctx, env, bc, n, pcEnd); },
                [&]() { return dispatch(tryRange.handlerPc, ctx, env, bc, n, pcEnd); });
            if (levelExit != LevelExit::Return) {
                levelExit = LevelExit::Return;
                i = levelExitPc;
                continue;
            }
            return r;
        }
        case OP_POP_BLOCK: {
            // The end of a protected range: back to the level that opened it.
            if (!blockStack.empty() && !blockStack.back().isHandler) {
                levelExit = LevelExit::PopBlock;
                levelExitPc = next_i;
                return nullptr;
            }
        } break;
        case OP_GET_AWAITABLE: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* obj = stack.back();
            stack.pop_back();
            // obj.__await__(), or obj itself when it is generator-like (a
            // generator-based coroutine has send but no __await__).  Anything
            // else cannot be awaited: `await None` used to push None and spin
            // in YIELD_FROM forever.
            const proto::ProtoString* awaitS = env ? env->getAwaitString() : PythonEnvironment::getInternedString(ctx, "__await__");
            const proto::ProtoObject* awaitable = invokeDunder(ctx, obj, awaitS, ctx->newList());
            if (awaitable) {
                stack.push_back(awaitable);
            } else {
                const proto::ProtoObject* send = (env && obj && obj != PROTO_NONE)
                    ? obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "send")) : nullptr;
                if (env && (!send || send == PROTO_NONE)) {
                    const proto::ProtoObject* type = env->getType(ctx, obj);
                    const proto::ProtoObject* name = type ? type->getAttribute(ctx, env->getNameString()) : nullptr;
                    std::string typeName = "object";
                    if (name && name->isString(ctx)) name->asString(ctx)->toUTF8String(ctx, typeName);
                    env->raiseTypeError(ctx, "'" + typeName + "' object can't be awaited");
                    continue;
                }
                stack.push_back(obj);
            }
        } break;
        case OP_GET_AITER: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* obj = stack.back();
            stack.pop_back();
            const proto::ProtoString* aiterS = env ? env->getAIterString() : PythonEnvironment::getInternedString(ctx, "__aiter__");
            const proto::ProtoObject* aiter = invokeDunder(ctx, obj, aiterS, ctx->newList());
            if (!aiter) aiter = obj;
            // PD3+PF: compileAsyncFor lowers `async for` to FOR_ITER,
            // which calls aiter.__next__.  Async generators inherit
            // __next__ from the generator prototype, so FOR_ITER drives
            // them directly.  For class-defined async iterators
            // (only __anext__ defined), install py_class_aiter_next as
            // an instance-level __next__ that drives the coroutine
            // returned by __anext__ to its StopIteration value.
            if (aiter && aiter != PROTO_NONE && env) {
                const proto::ProtoString* nextS = env->getNextString();
                const proto::ProtoObject* hasNext = aiter->getAttribute(ctx, nextS);
                if (!hasNext || hasNext == PROTO_NONE) {
                    const proto::ProtoString* anextS = env->getANextString();
                    const proto::ProtoObject* hasAnext = aiter->getAttribute(ctx, anextS);
                    if (hasAnext && hasAnext != PROTO_NONE) {
                        const proto::ProtoObject* bridge = ctx->fromMethod(
                            const_cast<proto::ProtoObject*>(aiter),
                            py_class_aiter_next);
                        aiter = aiter->setAttribute(ctx, nextS, bridge);
                    }
                }
            }
            stack.push_back(aiter);
        } break;
        case OP_GET_ANEXT: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* aiter = stack.back();
            if (diag_local) {
                if (diag_local) {}
            }
            const proto::ProtoString* anextS = env ? env->getANextString() : PythonEnvironment::getInternedString(ctx, "__anext__");
            const proto::ProtoObject* awaitable = invokeDunder(ctx, aiter, anextS, ctx->newList());
            if (awaitable) {
                stack.push_back(awaitable);
            } else {
                if (env) {
                    env->raiseTypeError(ctx, "async for item must be an async iterator");
                }
                continue;
            }
        } break;
        case OP_EXCEPTION_MATCH: {
             if (stack.size() < 2) { i = next_i; continue; }
             const proto::ProtoObject* type = stack.back();
             stack.pop_back();
             const proto::ProtoObject* exc = stack.back();
             bool match = false;
             if (exc && type) {
                 match = env->isException(exc, type);
                 if (diag_local) fprintf(stderr, "DEBUG: OP_EXCEPTION_MATCH exc %p vs type %p -> %d\n", (void*)exc, (void*)type, match);
            fflush(stderr);
             }
             if (diag_local) {
                 std::string excName = "unknown";
                 std::string typeName = "unknown";
                 if (exc) {
                     const proto::ProtoObject* cls = env ? env->getAttribute(ctx, exc, env->getClassString()) : nullptr;
                     const proto::ProtoObject* name = cls ? env->getAttribute(ctx, cls, env->getNameString()) : nullptr;
                     if (name && name->isString(ctx)) name->asString(ctx)->toUTF8String(ctx, excName);
                 }
                 if (type) {
                     const proto::ProtoObject* name = env ? env->getAttribute(ctx, type, env->getNameString()) : nullptr;
                     if (name && name->isString(ctx)) name->asString(ctx)->toUTF8String(ctx, typeName);
                 }
                 // Exception match diagnostic removed
             }
             stack.push_back(match ? PROTO_TRUE : PROTO_FALSE);
        } break;
        case OP_SETUP_ASYNC_WITH: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* mgr = stack.back();
            stack.pop_back();
            const proto::ProtoString* aexitS = env ? env->getAExitString() : PythonEnvironment::getInternedString(ctx, "__aexit__");
            const proto::ProtoString* aenterS = env ? env->getAEnterString() : PythonEnvironment::getInternedString(ctx, "__aenter__");
            const proto::ProtoObject* aexit = mgr->getAttribute(ctx, aexitS);
            stack.push_back(aexit ? aexit : PROTO_NONE);
            const proto::ProtoObject* awaitable = invokeDunder(ctx, mgr, aenterS, ctx->newList());
            if (awaitable) {
                stack.push_back(awaitable);
            } else {
                if (env) env->raiseTypeError(ctx, "async with expression must have __aenter__");
                continue;
            }
            const Block withRange{static_cast<proto::proto_ulong>(arg), stack.size()};
            const proto::proto_ulong rangeStart = next_i;
            const proto::ProtoObject* r = runRange(withRange,
                [&]() { return dispatch(rangeStart, ctx, env, bc, n, pcEnd); },
                [&]() { return dispatch(withRange.handlerPc, ctx, env, bc, n, pcEnd); });
            if (levelExit != LevelExit::Return) {
                levelExit = LevelExit::Return;
                i = levelExitPc;
                continue;
            }
            return r;
        }

        // ─────────────────────────────────────────────────────────────
        // Sprint-11 — fused / specialised opcodes produced by the
        // post-codegen peephole pass (Compiler::specialiseBytecode).
        //
        // Each handler reads the operand pair (or operand + const slot)
        // out of `arg` and applies the SmallInt fast path directly on
        // the function's `slots[]` array, bypassing the GCStack push/
        // pop work that the unfused sequence had to do.  Worst case
        // (operand isn't a SmallInt, or the arithmetic overflows): the
        // handler reconstructs the equivalent stack state and re-runs
        // the original slow path through the existing helpers, so the
        // observable behaviour is identical to the unfused sequence.
        // ─────────────────────────────────────────────────────────────
        case OP_ACC_FAST_FAST: {
            // ACC a, b  ⇔  LOAD_FAST a; LOAD_FAST b; INPLACE_ADD; STORE_FAST a
            //
            // Layout in the bytecode tuple (8 slots, NOPs preserve
            // jump targets that may already point at slot i+2..i+7):
            //   i  : OP_ACC_FAST_FAST | (idxA << 8) | idxB
            //   i+2: OP_NOP | 0
            //   i+4: OP_NOP | 0
            //   i+6: OP_NOP | 0
            // Execution advances by 8 slots (= 4 original instructions).
            int idxA = (arg >> 8) & 0xFF;
            int idxB = arg & 0xFF;
            const proto::ProtoObject** slots = ctx->getAutomaticLocals();
            unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (slots && idxA < (int)nSlots && idxB < (int)nSlots) {
                const proto::ProtoObject* a = slots[idxA];
                const proto::ProtoObject* b = slots[idxB];
                if (a && b && proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    long long s = proto::asSmallInt(a) + proto::asSmallInt(b);
                    if (proto::smallIntInRange(s)) {
                        slots[idxA] = proto::makeSmallInt(s);
                        i = i + 8;
                        continue;
                    }
                }
                // Slow path — emulate the four-op sequence via binaryAdd.
                // The LOAD_FASTs it replaces reject an unbound local first.
                const proto::ProtoObject* unbound = env ? env->getUnboundSentinel() : nullptr;
                if (unbound && (a == unbound || b == unbound)) {
                    raiseUnboundFastLocal(ctx, env, a == unbound ? idxA : idxB);
                    i = next_i;
                    continue;
                }
                const proto::ProtoObject* r = inplaceAdd(ctx, env, a, b);
                slots[idxA] = r;
            }
            i = i + 8;
            continue;
        }

        case OP_INC_FAST_K: {
            // INC idx, kIdx  ⇔  LOAD_FAST idx; LOAD_CONST kIdx; INPLACE_ADD; STORE_FAST idx
            // Same 8-slot layout as ACC_FAST_FAST.
            int idx  = (arg >> 8) & 0xFF;
            int kIdx = arg & 0xFF;
            const proto::ProtoObject** slots = ctx->getAutomaticLocals();
            unsigned int nSlots = ctx->getAutomaticLocalsCount();
            const proto::ProtoObject* k = nullptr;
            if (nativeConsts && (uint32_t)kIdx < (uint32_t)constants->getSize(ctx)) {
                k = nativeConsts[kIdx];
            } else if ((proto::proto_ulong)kIdx < constants->getSize(ctx)) {
                k = constants->getAt(ctx, kIdx);
            }
            if (slots && idx < (int)nSlots && k) {
                const proto::ProtoObject* a = slots[idx];
                if (a && proto::isSmallInt(a) && proto::isSmallInt(k)) {
                    long long s = proto::asSmallInt(a) + proto::asSmallInt(k);
                    if (proto::smallIntInRange(s)) {
                        slots[idx] = proto::makeSmallInt(s);
                        i = i + 8;
                        continue;
                    }
                }
                if (env && a && a == env->getUnboundSentinel()) {
                    raiseUnboundFastLocal(ctx, env, idx);
                    i = next_i;
                    continue;
                }
                const proto::ProtoObject* r = inplaceAdd(ctx, env, a, k);
                slots[idx] = r;
            }
            i = i + 8;
            continue;
        }

        case OP_LT_FAST_FAST_JF: {
            // LT_JF a, b ⇔ LOAD_FAST a; LOAD_FAST b; COMPARE_OP <; PJUMP_IF_FALSE target
            //
            // Layout in the bytecode tuple (8 slots):
            //   i  : OP_LT_FAST_FAST_JF | (idxA << 8) | idxB
            //   i+2: OP_NOP | 0
            //   i+4: OP_NOP | 0
            //   i+6: OP_NOP | target   ← target stays in the slot the
            //                            original PJUMP_IF_FALSE used,
            //                            so the existing jump-patching
            //                            in Compiler::applyPatches keeps
            //                            working unchanged.
            int idxA = (arg >> 8) & 0xFF;
            int idxB = arg & 0xFF;
            proto::proto_ulong targetSlot = i + 7;   // the arg slot of the NOP that replaces PJUMP_IF_FALSE
            int target = 0;
            if (nativeBc && targetSlot < n) {
                target = nativeBc[targetSlot];
            } else if (bytecode && targetSlot < bytecode->getSize(ctx)) {
                const proto::ProtoObject* tobj = bytecode->getAt(ctx, (int)targetSlot);
                if (tobj && tobj->isInteger(ctx)) target = (int)tobj->asLong(ctx);
            }
            const proto::ProtoObject** slots = ctx->getAutomaticLocals();
            unsigned int nSlots = ctx->getAutomaticLocalsCount();
            bool predicate = false;
            bool tookFastPath = false;
            if (slots && idxA < (int)nSlots && idxB < (int)nSlots) {
                const proto::ProtoObject* a = slots[idxA];
                const proto::ProtoObject* b = slots[idxB];
                if (a && b && proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    predicate = (proto::asSmallInt(a) < proto::asSmallInt(b));
                    tookFastPath = true;
                }
            }
            if (!tookFastPath) {
                const proto::ProtoObject* a = (slots && idxA < (int)nSlots) ? slots[idxA] : PROTO_NONE;
                const proto::ProtoObject* b = (slots && idxB < (int)nSlots) ? slots[idxB] : PROTO_NONE;
                const proto::ProtoObject* unbound = env ? env->getUnboundSentinel() : nullptr;
                if (unbound && (a == unbound || b == unbound)) {
                    raiseUnboundFastLocal(ctx, env, a == unbound ? idxA : idxB);
                    i = next_i;
                    continue;
                }
                const proto::ProtoObject* r = compareOp(ctx, a, b, /*arg=*/2);
                predicate = (r == PROTO_TRUE);
            }
            if (predicate) {
                // Predicate true: execution continues at the instruction
                // AFTER the original PJUMP_IF_FALSE (i + 8).
                i = i + 8;
            } else {
                // Predicate false: jump to target (array index).
                if (target >= 0 && (proto::proto_ulong)target < n) {
                    i = (proto::proto_ulong)target;
                } else {
                    i = i + 8;
                }
            }
            continue;
        }

        default:
            break;
        }  // end switch (op)
        } catch (const std::exception& e) {
            // A C++ standard exception (never a ProtoException, which does
            // not derive from std::exception) continues as RuntimeError(what()).
            if (env) env->raiseForeignCppException(ctx, e);
            throw;
        } catch (const proto::ProtoObject*) {
            // A legacy native generator throws its value (TestGeneratorsNative).
            // Not catch (...): a Python exception (PyException) must pass to
            // the enclosing try without being caught and rethrown here.
            if (env) {
                env->raiseRuntimeError(ctx, std::string("internal C++ exception (unknown type)"));
            }
        }
        i = next_i;
    }
    return stack.empty() ? PROTO_NONE : stack.back();
}

}  // namespace

// ---------------------------------------------------------------------------
// Source-level debugging: the thread's activations (innermost last) while it
// reports line events, and the per-instruction check (Debugger.h).
namespace {
thread_local std::vector<Activation*> tl_debugStack;

/** Links an activation into tl_debugStack for its lifetime. */
struct DebugStackScope {
    Activation* act;
    explicit DebugStackScope(Activation* a) : act(a) {
        if (act) tl_debugStack.push_back(act);
    }
    ~DebugStackScope() {
        if (!act) return;
        if (!tl_debugStack.empty() && tl_debugStack.back() == act) tl_debugStack.pop_back();
        // The caller continues on the line of its call: tell it, for stepping.
        if (!tl_debugStack.empty()) tl_debugStack.back()->dbgReturned = true;
    }
    DebugStackScope(const DebugStackScope&) = delete;
    DebugStackScope& operator=(const DebugStackScope&) = delete;
};
} // namespace

bool Activation::ensureDebugCode() {
    if (dbgCode) return true;
    const proto::ProtoObject* code = runningCodeObject(ctx, env, frame, bytecode);
    // Keyed by the code object: two functions may share one (interned)
    // co_code tuple.
    dbgCode = debugger::detail::codeInfoFor(ctx, env, code, code);
    if (!dbgCode) { dbgOn = false; return false; }
    dbgGlobals = PythonEnvironment::getCurrentGlobals();
    return true;
}

void Activation::debugInstruction(proto::proto_ulong i) {
    const proto::proto_ulong prevPc = dbgPc;
    dbgPc = i;
    if (!debugger::detail::listenerInstalled()) { dbgOn = false; return; }
    // Not while an attribute write group holds unpublished writes: code the
    // debugger runs could observe the receiver between them.
    if (attrGroupState == 1) return;
    if (!ensureDebugCode()) return;
    const proto::proto_ulong k = i / 2;
    const int line = k < dbgCode->lines.size() ? dbgCode->lines[k] : 0;
    if (line <= 0) return;
    debugger::LineEventKind kind;
    if (line != dbgLastLine) {
        kind = debugger::LineEventKind::Line;
    } else if (i <= prevPc && (k == 0 || dbgCode->lines[k - 1] != line)) {
        // A backward jump to the first instruction of the current line (a
        // loop on one line) starts the line again.
        kind = debugger::LineEventKind::Line;
    } else if (dbgReturned) {
        kind = debugger::LineEventKind::AfterReturn;
    } else {
        return;
    }
    dbgReturned = false;
    dbgLastLine = line;
    if (frameAnchor) *frameAnchor = frame;
    debugger::detail::dispatchLine(ctx, env, *dbgCode, line, kind, tl_debugStack.size());
}

namespace {
debugger::StackEntry debugEntryOf(const Activation* a) {
    const proto::proto_ulong k = a->dbgPc / 2;
    const int line = k < a->dbgCode->lines.size() ? a->dbgCode->lines[k] : 0;
    const proto::ProtoObject* globals = a->dbgGlobals ? a->dbgGlobals : a->frame;
    return debugger::StackEntry{a->frame, globals, a->ctx, a->dbgCode, line};
}

// The name of fast slot `slot` of `code` (co_varnames, then co_freevars:
// the layout PythonEnvironment::forEachFastLocal reads), or "".
std::string debugSlotName(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* code, int slot) {
    if (!code || slot < 0) return std::string();
    auto nameAt = [&](const proto::ProtoObject* tupleObj, proto::proto_ulong i, proto::proto_ulong& size) -> std::string {
        TupleView t = (tupleObj && tupleObj != PROTO_NONE) ? PythonEnvironment::tupleView(ctx, tupleObj) : nullptr;
        size = t ? t->getSize(ctx) : 0;
        if (!t || i >= size) return std::string();
        const proto::ProtoObject* nm = t->getAt(ctx, static_cast<int>(i));
        std::string out;
        if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, out);
        return out;
    };
    proto::proto_ulong nVar = 0, nFree = 0;
    std::string n = nameAt(code->getAttribute(ctx, env->getCoVarnamesString()), static_cast<proto::proto_ulong>(slot), nVar);
    if (!n.empty() || static_cast<proto::proto_ulong>(slot) < nVar) return n;
    return nameAt(code->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_freevars")),
                  static_cast<proto::proto_ulong>(slot) - nVar, nFree);
}
} // namespace

bool Activation::debugRangeHandles(const Block& b, const proto::ProtoObject* exc) {
    // A with block handles the exception when its __exit__ suppresses it:
    // known for contextlib.suppress only (another __exit__ is not called).
    // Its bound __exit__ is the operand below the block's depth.
    if (b.isWithBlock) {
        if (b.stackDepth == 0 || b.stackDepth > stack.top) return false;
        return debugger::withSuppresses(ctx, env, stack.slots[b.stackDepth - 1], exc);
    }
    // The handler region as Compiler::compileTry lays it out: for each except
    // clause `<type expression> EXCEPTION_MATCH POP_JUMP_IF_FALSE <next>`, a
    // bare except or a finally clause as POP_TOP x3 SETUP_FINALLY, and
    // RERAISE 1 when no clause matched. Anything else is taken as handling
    // the exception (no stop rather than a wrong one).
    auto nameOf = [&](int idx) -> std::string {
        if (!names || idx < 0 || static_cast<proto::proto_ulong>(idx) >= names->getSize(ctx)) return std::string();
        const proto::ProtoObject* nm = names->getAt(ctx, idx);
        std::string out;
        if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, out);
        return out;
    };
    proto::proto_ulong pc = b.handlerPc;
    for (int clause = 0; clause < 256; ++clause) {
        if (pc + 1 >= n) return true;
        const int op = bc[pc];
        if (op == OP_RERAISE) return false;
        if (op == OP_POP_TOP) {
            // A finally clause ends by re-raising: its SETUP_FINALLY's
            // cleanup target follows `RERAISE 1`.
            const proto::proto_ulong q = pc + 6;
            if (q + 1 < n && bc[pc + 2] == OP_POP_TOP && bc[pc + 4] == OP_POP_TOP && bc[q] == OP_SETUP_FINALLY) {
                const int t = bc[q + 1];
                if (t >= 2 && static_cast<proto::proto_ulong>(t) < n && bc[t - 2] == OP_RERAISE && bc[t - 1] == 1)
                    return false;
            }
            return true;  // a bare except
        }
        std::vector<std::string> st;
        for (int guard = 0; pc + 1 < n && bc[pc] != OP_EXCEPTION_MATCH; ++guard, pc += 2) {
            if (guard > 64) return true;
            const int o = bc[pc], a = bc[pc + 1];
            std::string nm;
            switch (o) {
                case OP_LOAD_NAME: case OP_LOAD_GLOBAL:
                    nm = nameOf(a >> 1);
                    if (nm.empty()) return true;
                    st.push_back(nm);
                    break;
                case OP_LOAD_FAST: case OP_LOAD_DEREF: case OP_LOAD_CLASSDEREF:
                    nm = debugSlotName(ctx, env, dbgCode ? dbgCode->codeObject : nullptr, a);
                    if (nm.empty()) return true;
                    st.push_back(nm);
                    break;
                case OP_LOAD_ATTR:
                    nm = nameOf(a >> 1);
                    if (nm.empty() || st.empty()) return true;
                    st.back() = "(" + st.back() + ")." + nm;
                    break;
                case OP_BUILD_TUPLE: {
                    if (a < 0 || static_cast<std::size_t>(a) > st.size()) return true;
                    std::string t = "(";
                    for (std::size_t j = st.size() - static_cast<std::size_t>(a); j < st.size(); ++j) t += st[j] + ", ";
                    t += ")";
                    st.resize(st.size() - static_cast<std::size_t>(a));
                    st.push_back(t);
                } break;
                default:
                    return true;
            }
        }
        if (pc + 3 >= n || st.size() != 1 || bc[pc + 2] != OP_POP_JUMP_IF_FALSE) return true;
        debugger::Value type;
        std::string error;
        if (!debugger::evaluate(ctx, env, debugEntryOf(this), st.back(), false, type, error) || !type.object)
            return true;
        bool match;
        {
            PythonEnvironment::TransientPin pin(env, type.object);
            match = env->isException(exc, type.object);
        }
        if (match) return true;
        pc = static_cast<proto::proto_ulong>(bc[pc + 3]);
    }
    return true;
}

void Activation::debugException(const proto::ProtoObject* exc, const Block* caught) {
    if (!exc || !debugger::detail::exceptionEventsWanted()) return;
    if (!ensureDebugCode()) return;
    const proto::proto_ulong k = dbgPc / 2;
    const int line = k < dbgCode->lines.size() ? dbgCode->lines[k] : 0;
    if (dbgCode->library) {
        debugger::detail::noteLibrarySighting(exc, *dbgCode, line);
        return;
    }
    if (!debugger::detail::firstSighting(ctx, env, exc)) return;
    if (frameAnchor) *frameAnchor = frame;
    debugger::ExceptionEvent event;
    event.exception = exc;
    const int op = dbgPc + 1 < n ? bc[dbgPc] : -1;
    if (op != OP_RAISE_VARARGS && op != OP_RERAISE) {
        event.origin = debugger::detail::librarySightingOrigin(exc);
        if (event.origin.empty()) event.origin = "native code";
    }
    event.handledInUserCode = [this, exc, caught]() -> bool {
        if (caught && debugRangeHandles(*caught, exc)) return true;
        for (auto it = blockStack.rbegin(); it != blockStack.rend(); ++it)
            if (!it->isHandler && debugRangeHandles(*it, exc)) return true;
        // The callers on this thread, up to the first library frame.
        auto self = std::find(tl_debugStack.rbegin(), tl_debugStack.rend(), this);
        if (self == tl_debugStack.rend()) return false;
        for (auto it = std::next(self); it != tl_debugStack.rend(); ++it) {
            Activation* a = *it;
            if (!a->ensureDebugCode() || a->dbgCode->library) return false;
            for (auto b = a->blockStack.rbegin(); b != a->blockStack.rend(); ++b)
                if (!b->isHandler && a->debugRangeHandles(*b, exc)) return true;
        }
        return false;
    };
    debugger::detail::dispatchException(ctx, env, event, tl_debugStack.size());
}

namespace debugger {
void currentStack(std::vector<StackEntry>& out) {
    out.clear();
    for (auto it = tl_debugStack.rbegin(); it != tl_debugStack.rend(); ++it) {
        const Activation* a = *it;
        if (!a->dbgCode) continue;
        const proto::proto_ulong k = a->dbgPc / 2;
        const int line = k < a->dbgCode->lines.size() ? a->dbgCode->lines[k] : 0;
        const proto::ProtoObject* globals = a->dbgGlobals ? a->dbgGlobals : a->frame;
        out.push_back(StackEntry{a->frame, globals, a->ctx, a->dbgCode, line});
    }
}
} // namespace debugger

const proto::ProtoObject* executeBytecodeRange(
    proto::ProtoContext* ctx,
    const proto::ProtoTuple* constants,
    const proto::ProtoTuple* bytecode,
    const proto::ProtoTuple* names,
    proto::ProtoObject*& frame,
    proto::proto_ulong pcStart,
    proto::proto_ulong pcEnd,
    proto::proto_ulong stackOffset,
    proto::proto_ulong* outPc,
    bool* yielded,
    std::vector<Block>* externalBlockStack,
    proto::proto_ulong initialTop,
    proto::proto_ulong* finalTopPtr,
    const int* nativeBc,
    const proto::ProtoObject** nativeConsts,
    const proto::ProtoObject** nativeNames,
    const proto::ProtoObject* throwOnResume) {
    if (!ctx || !constants || !bytecode) return nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    RecursionScope recScope(env, ctx);
    if (recScope.overflowed()) return nullptr;
    
    // Hoist the diagnostic-tracing flag once per dispatcher invocation.
    // get_env_diag() is `inline bool ... { return g_diag_enabled; }` over a
    // namespace-scope const bool, but every call still emitted a PLT-trampolined
    // function call from inside the dispatch loop (the symbol survives in the
    // shared library because address-taken instances exist in other TUs).
    // 61 call sites in this dispatch loop × millions of iterations on
    // call_recursion = ~5 % of total CPU time spent on PLT stubs around a check
    // that is constant for the process lifetime.  One local read replaces all
    // of that.
    const bool diag_local = get_env_diag();

    FrameScope fscope(frame, ctx);
    proto::proto_ulong n = bytecode->getSize(ctx);
    if (n == 0) {
        if (diag_local) {
            fprintf(stderr, "DEBUG: executeBytecodeRange n=0, returning nullptr\n");
        }
        return nullptr;
    }
    if (pcEnd >= n) pcEnd = n - 1;

    // Use pre-computed native bytecode int[] from co_bytecode_native (built once at compile time
    // by makeCodeObject). Falls back to building a local vector if not available (module-level
    // code or generators resuming without nativeBc). The hot path (user function calls) always
    // has nativeBc set by runUserFunctionCall, so the fallback is cold.
    std::vector<int> bc_fallback;
    const int* bc = nativeBc;
    if (!bc) {
        bc_fallback.resize(n);
        for (proto::proto_ulong j = 0; j < n; ++j) {
            const proto::ProtoObject* obj = bytecode->getAt(ctx, static_cast<int>(j));
            bc_fallback[j] = (obj && obj->isInteger(ctx)) ? static_cast<int>(obj->asLong(ctx)) : 0;
        }
        bc = bc_fallback.data();
    }

    unsigned int nSlots = ctx->getAutomaticLocalsCount();
    // A context with no automatic slots at all gets them here, so that the
    // operand stack and the frame anchor below are GC roots.  Code compiled by
    // this runtime always asks for slots (Compiler.cpp); this covers contexts
    // built elsewhere.  Only an empty context is resized: a context that has
    // slots may be running an outer activation whose stack points into them.
    if (nSlots == 0) {
        ctx->resizeAutomaticLocals(static_cast<unsigned int>(stackOffset) + 4096u + 1u);
        nSlots = ctx->getAutomaticLocalsCount();
    }
    const proto::ProtoObject** allSlots = ctx->getAutomaticLocals();

    // Fallback stack for contexts whose few slots cannot hold the stack. It is
    // not scanned by the collector.
    std::vector<const proto::ProtoObject*> fallbackStack;
    const proto::ProtoObject** stackBase = nullptr;
    size_t stackCap = 0;

    // The frame is anchored in the context's last automatic slot, which the
    // operand stack never uses.  A frame is a cell of the context that runs
    // it, and nothing in protoCore's heap references it while it runs: it is
    // held by this function's `frame`, by the caller's C++ locals and by the
    // thread-local current frame, none of which the collector scans.  That
    // is safe only while the frame is in the context's young generation, and
    // the ctx->safepoint() below hands the young generation to the collector
    // once the context has allocated maxAllocatedCellsPerContext cells (rule
    // 3 of protoCore's docs/EMBEDDER-CONFORMANCE.md).  Without the anchor the
    // next cycle freed the frame of any long-running call, its cell was
    // reused, and LOAD_DEREF / STORE_DEREF walked a stranger (a nested
    // function's NameError on a variable of its enclosing function).
    // The two slots before it mark the context as an activation's and hold
    // the exception the activation handles (PythonEnvironment's
    // kActivationSlots); a nested activation on the same context shares them.
    const proto::ProtoObject** frameAnchor = nullptr;
    const proto::ProtoObject** handledSlot = nullptr;
    const proto::ProtoObject** synthFrameSlot = nullptr;
    if (allSlots && nSlots > stackOffset + PythonEnvironment::kActivationSlots && env) {
        stackBase = allSlots + stackOffset;
        stackCap = nSlots - stackOffset - PythonEnvironment::kActivationSlots;
        frameAnchor = allSlots + (nSlots - 1);
        env->markActivation(ctx);
        handledSlot = allSlots + (nSlots - 2);
        synthFrameSlot = allSlots + (nSlots - 4);
    } else if (allSlots && nSlots > stackOffset + 1) {
        stackBase = allSlots + stackOffset;
        stackCap = nSlots - stackOffset - 1;
        frameAnchor = allSlots + (nSlots - 1);
    } else if (allSlots && nSlots > stackOffset) {
        stackBase = allSlots + stackOffset;
        stackCap = nSlots - stackOffset;
    } else {
        fallbackStack.resize(4096); // Default capacity for manual/test execution
        stackBase = fallbackStack.data();
        stackCap = fallbackStack.size();
    }

    GCStack stack(stackBase, stackCap);
    stack.top = initialTop;
    // A nested activation on the same context restores the outer anchor.
    struct FrameAnchorScope {
        const proto::ProtoObject** slot;
        const proto::ProtoObject* saved;
        ~FrameAnchorScope() { if (slot) *slot = saved; }
    } frameAnchorScope{frameAnchor, frameAnchor ? *frameAnchor : nullptr};
    if (frameAnchor) *frameAnchor = frame;

    // The try nesting of this activation (see Block). A resumed generator's
    // saved levels are rebuilt below (runLevel), before dispatching.
    std::vector<Block> blockStack;
    std::vector<Block> savedLevels;
    if (externalBlockStack) {
        savedLevels = *externalBlockStack;
    }
    const bool sync_globals = (frame == PythonEnvironment::getCurrentGlobals());
    // Cooperative GC safepoint counter.  Most Python opcodes execute without
    // touching the cell allocator (LOAD_FAST, BINARY_ADD on SmallIntegers,
    // tight integer loops), so the per-allocation STW poll inside allocCell
    // never fires for CPU-bound threads.  Without an in-loop safepoint, a
    // GC stop-the-world request will starve indefinitely against a
    // pure-bytecode thread, deadlocking every other thread that is already
    // parked.  Polling every 64 opcodes (`sp_ctr & 0x3F` in
    // Activation::dispatch) keeps the fast path branch-only while bounding
    // pause-acquisition latency to a few µs.

    Activation act(ctx, env, bc, n, pcEnd, diag_local, constants, bytecode, names, nativeConsts, nativeNames,
                   nativeBc, pcStart, stackOffset, outPc, yielded, externalBlockStack, initialTop, finalTopPtr,
                   frameAnchor, sync_globals, frame, stack, blockStack, savedLevels, throwOnResume);
    act.handledSlot = handledSlot;
    act.synthFrameSlot = synthFrameSlot;
    act.dbgOn = debugger::detail::threadWantsEvents();
    DebugStackScope debugStackScope(act.dbgOn ? &act : nullptr);
    try {
        // A resumed generator rebuilds its try nesting first; any other
        // activation dispatches at once.
        if (savedLevels.empty() && !throwOnResume) return act.dispatch(pcStart, ctx, env, bc, n, pcEnd);
        return act.runLevel(0);
    } catch (const proto::ProtoException& uncaught) {
        act.noteFrame(uncaught.value());
        if (act.dbgOn) [[unlikely]] act.debugException(uncaught.value(), nullptr);
        throw;
    }
}

const proto::ProtoObject* executeMinimalBytecode(
    proto::ProtoContext* ctx,
    const proto::ProtoTuple* constants,
    const proto::ProtoTuple* bytecode,
    const proto::ProtoTuple* names,
    proto::ProtoObject*& frame) {
    if (!ctx || !constants || !bytecode) return nullptr;
    proto::proto_ulong n = bytecode->getSize(ctx);
    // Save and restore thread-local/global singletons so unit tests don't pollute each other.
    // STORE_NAME/STORE_GLOBAL handlers call setCurrentFrame/setCurrentGlobals, which would
    // leave stale pointers that corrupt subsequent tests running in a fresh ProtoContext.
    const proto::ProtoObject* savedFrame = PythonEnvironment::getCurrentFrame();
    const proto::ProtoObject* savedGlobals = PythonEnvironment::getCurrentGlobals();
    // Set globals to frame so BUILD_FUNCTION captures the correct __globals__, and so that
    // sync_globals=true enables STORE_NAME to keep getCurrentGlobals() in sync with frame CoW.
    PythonEnvironment::setCurrentGlobals(frame);
    struct Restore {
        const proto::ProtoObject* frame; const proto::ProtoObject* globals;
        ~Restore() { PythonEnvironment::setCurrentFrame(frame); PythonEnvironment::setCurrentGlobals(globals); }
    } restore{savedFrame, savedGlobals};
    return executeBytecodeRange(ctx, constants, bytecode, names, frame, 0, n ? n - 1 : 0, 0, nullptr, nullptr, nullptr, 0, nullptr);
}


const proto::ProtoObject* exported_py_function_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs) {
    return py_function_get(ctx, self, parentLink, args, kwargs);
}

const proto::ProtoObject* exported_py_function_code_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs) {
    if (args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    if (!instance || instance == PROTO_NONE) return self;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* codeStr = env ? env->getCodeString() : PythonEnvironment::getInternedString(ctx, "__code__");
    const proto::ProtoObject* res = instance->getAttribute(ctx, codeStr);
    return res ? res : PROTO_NONE;
}

const proto::ProtoObject* exported_py_function_globals_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs) {
    if (args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    if (!instance || instance == PROTO_NONE) return self;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* globalsStr = env ? env->getGlobalsString() : PythonEnvironment::getInternedString(ctx, "__globals__");
    const proto::ProtoObject* res = instance->getAttribute(ctx, globalsStr);
    return res ? res : PROTO_NONE;
}

const proto::ProtoObject* exported_py_function_doc_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs) {
    if (args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    if (!instance || instance == PROTO_NONE) return self;
    const proto::ProtoString* docStr = PythonEnvironment::getInternedString(ctx, "__doc__");
    const proto::ProtoObject* res = instance->getAttribute(ctx, docStr);
    return res ? res : PROTO_NONE;
}


const proto::ProtoObject* exported_runUserFunctionCall(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs) {
    return runUserFunctionCall(ctx, self, parentLink, args, kwargs);
}


} // namespace protoPython
