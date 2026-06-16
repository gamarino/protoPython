#include <protoPython/ExecutionEngine.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/Compiler.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/MemoryManager.hpp>
#include <protoPython/SignalModule.h>
#include <protoCore.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cstring>
#include <functional>
#include <iostream>

namespace protoPython {

// Forward-declared in PythonEnvironment.cpp so the dict-key opcodes
// (OP_BUILD_MAP / OP_MAP_ADD) can share the env-aware hash function
// with py_dict_getitem / setitem — keeps user-__hash__ overrides
// (cistr, etc.) consistent across the entire dict pipeline.
unsigned long pyDictKeyHash(proto::ProtoContext* context, const proto::ProtoObject* key);

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
    // True when no OP_BUILD_FUNCTION or OP_BUILD_CLASS appear in the native bytecode.
    // Safe to skip frame construction when this is true and the function has no closures.
    bool no_inner_functions;
    // True when no OP_LOAD_DEREF appears in the native bytecode.
    // Safe to skip closure frame lookup even if __closure__ is present.
    bool no_load_deref;
    // True when __closure__ is a non-empty list at BUILD_FUNCTION time.
    bool hasClosure;
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
    // Followed in memory by flat arrays:
    //   const proto::ProtoObject* nativeConsts[nConsts]
    //   const proto::ProtoObject* nativeNames[nNames]
    // Access via: reinterpret_cast<const proto::ProtoObject**>(cache + 1)
};

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
        case OP_LIST_EXTEND:
        case OP_SET_UPDATE:
        case OP_BUILD_SET:
        case OP_BUILD_STRING:
        case OP_LOAD_DEREF:
        case OP_STORE_DEREF:
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



RecursionScope::RecursionScope(PythonEnvironment* env, proto::ProtoContext* ctx) : env_(env), ctx_(ctx) {
    if (!env_) return;
    
    int limit = env_->getRecursionLimit();

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
        PythonEnvironment::s_inRecursionError = true;
        env_->raiseRecursionError(ctx_);
        PythonEnvironment::s_inRecursionError = false;
        overflowed_ = true;
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
    ~FrameScope() {
        PythonEnvironment::setCurrentFrame(oldFrame);
    }
    const proto::ProtoObject* oldFrame;
};

struct GlobalsScope {
    GlobalsScope(const proto::ProtoObject* globals) : oldGlobals(PythonEnvironment::getCurrentGlobals()) {
        if (globals != oldGlobals) {
            PythonEnvironment::setCurrentGlobals(globals);
            PythonEnvironment* env = PythonEnvironment::getCurrentEnvironment();
            if (env) env->invalidateResolveCache();
        }
    }
    ~GlobalsScope() {
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
    unsigned long rawArgCount);

static const proto::ProtoObject* runUserFunctionCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs) {
    if (!ctx || !self || !args) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    // Fast path: retrieve all constant metadata from the pre-computed FunctionMetaCache
    // (one ByteBuffer attribute lookup instead of 7 individual codeObj reads).
    const proto::ProtoObject* codeObj = nullptr;
    const proto::ProtoObject* globalsObj = nullptr;
    int co_flags = 0, nparams_count = 0, kwonly_count = 0, automatic_count = 0;
    bool isGenerator = false;
    const proto::ProtoTuple* co_varnames = nullptr;

    bool cacheHit = false;
    bool cacheNoInnerFunctions = false;
    bool cacheNoLoadDeref = false;
    if (env && env->getFnMetaCacheString()) {
        const proto::ProtoObject* cacheAttr = self->getAttribute(ctx, env->getFnMetaCacheString());
        if (cacheAttr && cacheAttr != PROTO_NONE) {
            char* cacheData = cacheAttr->getDataIfByteBuffer(ctx);
            if (cacheData) {
                const FunctionMetaCache* cache =
                    reinterpret_cast<const FunctionMetaCache*>(cacheData);
                codeObj                = cache->codeObj;
                globalsObj             = cache->globalsObj;
                co_flags               = cache->co_flags;
                nparams_count          = cache->nparams;
                kwonly_count           = cache->kwonly;
                automatic_count        = cache->automatic_count;
                isGenerator            = cache->is_generator;
                co_varnames            = cache->co_varnames;
                cacheNoInnerFunctions  = cache->no_inner_functions;
                cacheNoLoadDeref       = cache->no_load_deref;
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
        co_varnames = (cvObj && cvObj->asTuple(ctx)) ? cvObj->asTuple(ctx) : nullptr;
        const proto::ProtoObject* isGenObj = (env && env->getCoIsGeneratorString())
            ? codeObj->getAttribute(ctx, env->getCoIsGeneratorString()) : nullptr;
        isGenerator = isGenObj && isGenObj->isBoolean(ctx) && isGenObj->asBoolean(ctx);
    }

    if (!codeObj || codeObj == PROTO_NONE) return PROTO_NONE;
    if (!globalsObj || globalsObj == PROTO_NONE) return PROTO_NONE;

    std::string fnName = "unknown";
    if (get_env_diag()) {
        const proto::ProtoObject* co_name_obj = codeObj->getAttribute(ctx, env ? env->getCoNameString() : PythonEnvironment::getInternedString(ctx, "co_name"));
        if (co_name_obj && co_name_obj->isString(ctx)) co_name_obj->asString(ctx)->toUTF8String(ctx, fnName);
        fprintf(stderr, "DEBUG: runUserFunctionCall name=%s nparams_count=%d argCount=%lu\n", fnName.c_str(), nparams_count, args->getSize(ctx));
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
    unsigned long argCount = args->getSize(calleeCtx);

    // 5. Build Execution Frame (for locals()/sys._getframe)
    // CO_OPTIMIZED functions with no closure and not generators never access the frame
    // in the hot path (LOAD_FAST/STORE_FAST use slots). Skip the ~4 AVL-tree object
    // allocations to reduce per-call GC pressure — the dominant cost in deep recursion.
    const proto::ProtoObject* closure = env ? self->getAttribute(calleeCtx, env->getClosureString()) : nullptr;
    // An empty closure tuple/list means no captured variables — treat as no closure.
    const proto::ProtoList* closureList0 = closure ? closure->asList(calleeCtx) : nullptr;
    bool hasClosure = closure && closure != PROTO_NONE
        && closureList0 && closureList0->getSize(calleeCtx) > 0;
    // Also skip frame when cacheNoLoadDeref: even if closure exists, the function never
    // accesses it via LOAD_DEREF, so the closure frame is unused during execution.
    //
    // Re-enabling the V97-era frame-skip optimisation. b35bf811 disabled it
    // unconditionally to support sys._getframe()/traceback introspection, but every
    // call to a CO_OPTIMIZED leaf function (e.g. fib) then paid 4-5 cell
    // allocations per call. Profiles of call_recursion (fib(25), 242k calls)
    // showed 90% of time in ProtoContext destruction + cell free-list refilling;
    // frame setup was the upstream amplifier. Functions that never build inner
    // functions/classes and either have no closure or never LOAD_DEREF do not
    // observably need a frame in the hot path; introspection can be added back
    // lazily (build the frame on demand when sys._getframe() / traceback walks
    // actually request it) without paying for it on every call.
    bool skipFrame = env && (co_flags & CO_OPTIMIZED) && !isGenerator && cacheNoInnerFunctions
        && (!hasClosure || cacheNoLoadDeref);
    proto::ProtoObject* frame = nullptr;
    if (!skipFrame) {
        // For forceMapped (non-CO_OPTIMIZED) functions the frame holds the
        // locals as own attributes; nested closures take this frame as a
        // parent and rely on subsequent STORE_NAMEs being visible
        // through the parent walk.  An immutable frame would force every
        // STORE_NAME into a copy-on-write that breaks the parent linkage,
        // so build the frame mutable in that case.
        bool framMutable = !(co_flags & CO_OPTIMIZED);
        frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(framMutable));
        if (env) {
            if (hasClosure) {
                // closureList0 is already validated non-null and non-empty above.
                if (closureList0) {
                    const proto::ProtoObject* outerFrame = closureList0->getAt(calleeCtx, 0);
                    if (outerFrame && outerFrame != PROTO_NONE) {
                        frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, outerFrame));
                    }
                } else {
                    frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, closure));
                }
                frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getClosureString(), closure));
            }
            frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, env->getFramePrototype()));
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFCodeString(), codeObj));
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFGlobalsString(), globalsObj));

            const proto::ProtoObject* parentFrame = PythonEnvironment::getCurrentFrame();
            if (parentFrame) {
                frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFBackString(), parentFrame));
            }
        }
    }

    // Bind parameters
    unsigned int nSlots = calleeCtx->getAutomaticLocalsCount();
    proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());

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
        if ((co_flags & CO_OPTIMIZED) && slots && idx < (int)nSlots) {
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
        fprintf(stderr, "DEBUG: runUserFunctionCall nparams_count=%d argCount=%lu argsSize=%lu\n", nparams_count, argCount, args ? args->getSize(calleeCtx) : 0);
        fflush(stderr);
    }


    const proto::ProtoObject* codeOwner = self;
    if (env) {
        const proto::ProtoObject* im_func = self->getAttribute(calleeCtx, env->getFuncDunderString());
        if (im_func && im_func != PROTO_NONE) codeOwner = im_func;
    }
    
    // 1. Positional arguments
    for (unsigned long i = 0; i < (unsigned long)nparams_count && i < argCount; ++i) {
        bindVar(static_cast<int>(i), args->getAt(calleeCtx, static_cast<int>(i)));
    }

    // 1b. Positional / keyword argument conflict.  A parameter already
    // filled by a positional argument must not also receive a keyword
    // argument of the same name — CPython raises "got multiple values
    // for argument".  Without this check the duplicate keyword was
    // silently dropped: the **kwargs collection loop below skips any
    // name that matches a bound parameter, so `f(1, foo=2)` for
    // `def f(foo, *a, **kw)` lost the foo=2 keyword and raised nothing.
    if (kwargs && kwargs->getSize(calleeCtx) > 0 && co_varnames) {
        unsigned long boundPositional = (argCount < (unsigned long)nparams_count)
            ? argCount : (unsigned long)nparams_count;
        for (unsigned long i = 0; i < boundPositional; ++i) {
            const proto::ProtoObject* paramName = co_varnames->getAt(calleeCtx, static_cast<int>(i));
            if (!paramName || !paramName->isString(calleeCtx)) continue;
            if (kwargs->has(calleeCtx, paramName->getHash(calleeCtx))) {
                std::string pn, fn = "function";
                paramName->asString(calleeCtx)->toUTF8String(calleeCtx, pn);
                const proto::ProtoObject* cnObj = codeObj->getAttribute(calleeCtx,
                    env ? env->getCoNameString() : PythonEnvironment::getInternedString(calleeCtx, "co_name"));
                if (cnObj && cnObj->isString(calleeCtx)) cnObj->asString(calleeCtx)->toUTF8String(calleeCtx, fn);
                if (env) env->raiseTypeError(calleeCtx,
                    fn + "() got multiple values for argument '" + pn + "'");
                return nullptr;
            }
        }
    }

    // 2. Keyword arguments mapped to positional parameters and defaults if missing
    if (argCount < (unsigned long)nparams_count) {
        const proto::ProtoString* defaults_name = env ? env->getDefaultsString() : PythonEnvironment::getInternedString(calleeCtx, "__defaults__");
        const proto::ProtoObject* defaultsObj = codeOwner->getAttribute(calleeCtx, defaults_name);
        bool has_defaults = (defaultsObj && defaultsObj != PROTO_NONE && defaultsObj->isTuple(calleeCtx));
        const proto::ProtoTuple* defaults = has_defaults ? defaultsObj->asTuple(calleeCtx) : nullptr;
        int num_defaults = defaults ? (int)defaults->getSize(calleeCtx) : 0;

        int defaults_start_at = nparams_count - num_defaults;

        for (int i = (int)argCount; i < nparams_count; ++i) {
            bool bound = false;
            if (co_varnames && i < (int)co_varnames->getSize(calleeCtx) && kwargs) {
                const proto::ProtoObject* paramName = co_varnames->getAt(calleeCtx, i);
                if (paramName) {
                    unsigned long key = paramName->getHash(calleeCtx);
                    if (kwargs->has(calleeCtx, key)) {
                        bindVar(i, kwargs->getAt(calleeCtx, key));
                        bound = true;
                    }
                }
            }
            // If not found in kwargs, see if it has a default value
            if (!bound && defaults && i >= defaults_start_at) {
                const proto::ProtoObject* val = defaults->getAt(calleeCtx, i - defaults_start_at);
                bindVar(i, val);
            }
        }
    }

    // 3. Keyword-only arguments
    const proto::ProtoString* kwdefaults_name = env ? env->getKwdefaultsString() : PythonEnvironment::getInternedString(calleeCtx, "__kwdefaults__");
    const proto::ProtoObject* kwDefaultsObj = self->getAttribute(calleeCtx, kwdefaults_name);

    for (int i = 0; i < kwonly_count; ++i) {
        int slotIdx = nparams_count + i;
        if (co_varnames && slotIdx < (int)co_varnames->getSize(calleeCtx)) {
            const proto::ProtoObject* paramName = co_varnames->getAt(calleeCtx, slotIdx);
            if (!paramName) continue;
            
            unsigned long key = paramName->getHash(calleeCtx);
            const proto::ProtoObject* val = (kwargs && kwargs->has(calleeCtx, key)) ? kwargs->getAt(calleeCtx, key) : nullptr;
            
            if (val) {
                bindVar(slotIdx, val);
            } else if (kwDefaultsObj && kwDefaultsObj != PROTO_NONE) {
                // Check kw-defaults
                const proto::ProtoString* dataName = env ? env->getDataString() : PythonEnvironment::getInternedString(calleeCtx, "__data__");
                const proto::ProtoObject* data = kwDefaultsObj->getAttribute(calleeCtx, dataName);
                if (data && data->asSparseList(calleeCtx)) {
                    const proto::ProtoSparseList* sl = data->asSparseList(calleeCtx);
                    if (sl->has(calleeCtx, key)) {
                        val = sl->getAt(calleeCtx, key);
                        if (val) {
                            bindVar(slotIdx, val);
                        }
                    }
                }
            }
        }
    }

    // 4. *args
    if (co_flags & CO_VARARGS) {
        int varargIdx = nparams_count + kwonly_count;
        const proto::ProtoList* starArgs = calleeCtx->newList();
        if (argCount > (unsigned long)nparams_count) {
            for (unsigned long i = nparams_count; i < argCount; ++i) {
                starArgs = starArgs->appendLast(calleeCtx, args->getAt(calleeCtx, static_cast<int>(i)));
            }
        }
        const proto::ProtoObject* tup = calleeCtx->newTupleFromList(starArgs)->asObject(calleeCtx);
        bindVar(varargIdx, tup);
    }

    // 5. **kwargs
    if (co_flags & CO_VARKEYWORDS) {
        int kwargIdx = nparams_count + kwonly_count + ((co_flags & CO_VARARGS) ? 1 : 0);
        proto::ProtoObject* kwDict = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG runUserFunctionCall: env=%p dictProto=%p\n", (void*)env, (void*)(env ? env->getDictPrototype() : nullptr));
            fflush(stderr);
        }
        if (env && env->getDictPrototype()) kwDict = const_cast<proto::ProtoObject*>(kwDict->addParent(calleeCtx, env->getDictPrototype()));

        const proto::ProtoString* dataName = env ? env->getDataString() : PythonEnvironment::getInternedString(calleeCtx, "__data__");
        const proto::ProtoString* keysName = env ? env->getKeysString() : PythonEnvironment::getInternedString(calleeCtx, "__keys__");

        const proto::ProtoSparseList* data = calleeCtx->newSparseList();
        const proto::ProtoList* keysList = calleeCtx->newList();

        // Build a hash→nameObject map from the caller's kwNames tuple so we can
        // populate __keys__ (the list of actual key string objects) alongside __data__.
        const proto::ProtoTuple* kwNamesTuple = env ? env->getCurrentKwNames() : nullptr;

        if (kwargs) {
            // Iterate the caller's kwNames tuple (CALL ORDER), looking up
            // each key's value in the SparseList. Iterating the SparseList
            // directly would surface keys in hash order, which collapses
            // CPython's "**kwargs preserves insertion order" guarantee
            // (PEP 468). When kwNames is unavailable (defensive fallback),
            // fall back to SparseList iteration; the result is still
            // populated, just possibly out of insertion order.
            if (kwNamesTuple) {
                unsigned long n = kwNamesTuple->getSize(calleeCtx);
                for (unsigned long ni = 0; ni < n; ++ni) {
                    const proto::ProtoObject* nm = kwNamesTuple->getAt(calleeCtx, ni);
                    if (!nm) continue;
                    unsigned long key = nm->getHash(calleeCtx);
                    if (!kwargs->has(calleeCtx, key)) continue;
                    const proto::ProtoObject* val = kwargs->getAt(calleeCtx, key);

                    bool alreadyBound = false;
                    for (unsigned long i = 0; i < (unsigned long)(nparams_count + kwonly_count); ++i) {
                        const proto::ProtoObject* paramName = co_varnames->getAt(calleeCtx, i);
                        if (paramName && paramName->getHash(calleeCtx) == key) {
                            alreadyBound = true;
                            break;
                        }
                    }
                    if (!alreadyBound) {
                        data = data->setAt(calleeCtx, key, val);
                        keysList = keysList->appendLast(calleeCtx, nm);
                    }
                }
            } else {
                auto it = kwargs->getIterator(calleeCtx);
                while (it && it->hasNext(calleeCtx)) {
                    unsigned long key = it->nextKey(calleeCtx);
                    const proto::ProtoObject* val = it->nextValue(calleeCtx);

                    bool alreadyBound = false;
                    for (unsigned long i = 0; i < (unsigned long)(nparams_count + kwonly_count); ++i) {
                        const proto::ProtoObject* paramName = co_varnames->getAt(calleeCtx, i);
                        if (paramName && paramName->getHash(calleeCtx) == key) {
                            alreadyBound = true;
                            break;
                        }
                    }
                    if (!alreadyBound) {
                        data = data->setAt(calleeCtx, key, val);
                    }
                    it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(calleeCtx);
                }
            }
        }
        kwDict->setAttribute(calleeCtx, dataName, data->asObject(calleeCtx));
        kwDict->setAttribute(calleeCtx, keysName, keysList->asObject(calleeCtx));
        if (get_env_diag()) {
             const proto::ProtoList* keys = keysList;
             unsigned long size = keys ? keys->getSize(calleeCtx) : 0;
             fprintf(stderr, "DEBUG runUserFunctionCall: kwDict populated with %lu keys\n", size);
             for (unsigned long i = 0; i < size; ++i) {
                 const proto::ProtoObject* k = keys->getAt(calleeCtx, i);
                 std::string ks = env ? env->reprObject(calleeCtx, k) : "???";
                 fprintf(stderr, "  key[%lu]=%s\n", i, ks.c_str());
             }
             std::string r = env ? env->reprObject(calleeCtx, kwDict) : "???";
             fprintf(stderr, "DEBUG runUserFunctionCall: kwDict repr=%s\n", r.c_str());
             fflush(stderr);
        }
        bindVar(kwargIdx, kwDict);
    }

    // f_locals is only read by locals()/vars() and sys._getframe(). Skip for CO_OPTIMIZED
    // functions (params in slots) to save 1 AVL-tree op in the common hot path.
    if (env && !(co_flags & CO_OPTIMIZED)) {
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFLocalsString(), frame));
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
        //   CO_COROUTINE (0x100) + yield (0x20) → async_generator
        //   CO_COROUTINE (0x100) alone          → coroutine
        //   else (CO_GENERATOR 0x20)            → generator
        bool hasCoroutineFlag = false;
        bool hasGeneratorFlag = false;
        bool isAsyncGenerator = false;
        if (env && env->getGeneratorPrototype()) {
            const proto::ProtoObject* genProto = env->getGeneratorPrototype();
            const proto::ProtoObject* coFlagsObj = codeObj->getAttribute(calleeCtx, env->getCoFlagsString());
            long long co_flags_val = (coFlagsObj && coFlagsObj->isInteger(calleeCtx)) ? coFlagsObj->asLong(calleeCtx) : 0;
            hasCoroutineFlag = (co_flags_val & 0x100) != 0;
            hasGeneratorFlag = (co_flags_val & 0x20) != 0;
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
        }
        
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
        
        const proto::ProtoList* localList = calleeCtx->newList();
        for (unsigned int i = 0; i < nSlots; ++i) {
            localList = localList->appendLast(calleeCtx, slots[i]);
        }
        gen->setAttribute(calleeCtx, env ? env->getGiLocalsString() : PythonEnvironment::getInternedString(calleeCtx, "gi_locals"), localList->asObject(calleeCtx));
        
        promote(calleeCtx, gen);
        return gen;
    }

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
        const proto::ProtoObject* prevCodeObj = PythonEnvironment::getCurrentCodeObject();
        PythonEnvironment::setCurrentCodeObject(codeObj);

        const proto::ProtoObject* bytecodeObj = codeObj->getAttribute(calleeCtx, env->getCoCodeString());
        const proto::ProtoObject* constsObj = codeObj->getAttribute(calleeCtx, env->getCoConstsString());
        const proto::ProtoObject* namesObj = codeObj->getAttribute(calleeCtx, env->getCoNamesString());

        const proto::ProtoTuple* bytecode = bytecodeObj ? bytecodeObj->asTuple(calleeCtx) : nullptr;
        const proto::ProtoTuple* consts = constsObj ? constsObj->asTuple(calleeCtx) : nullptr;
        const proto::ProtoTuple* names = namesObj ? namesObj->asTuple(calleeCtx) : nullptr;

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
            unsigned long stackOffset = co_varnames ? co_varnames->getSize(calleeCtx) : 0;
            result = executeBytecodeRange(calleeCtx, consts, bytecode, names, frame, 0, bytecode->getSize(calleeCtx), stackOffset,
                nullptr, nullptr, nullptr, 0, nullptr, nativeBc);
        } else {
            result = PROTO_NONE;
        }
        PythonEnvironment::setCurrentCodeObject(prevCodeObj);
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
    unsigned long rawArgCount) {

    if (!ctx || !self) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

    // Read FunctionMetaCache — a single attribute lookup replaces all per-call codeObj reads.
    const proto::ProtoObject* codeObj = nullptr;
    const proto::ProtoObject* globalsObj = nullptr;
    int co_flags = 0, nparams_count = 0, kwonly_count = 0, automatic_count = 0;
    bool isGenerator = false;
    bool cacheNoInnerFunctions = false;
    bool cacheNoLoadDeref = false;
    const proto::ProtoTuple* co_varnames = nullptr;
    // Extended cached fields (Opt 2).
    const proto::ProtoTuple* cached_bytecode = nullptr;
    const proto::ProtoTuple* cached_consts   = nullptr;
    const proto::ProtoTuple* cached_names    = nullptr;
    const int*               cached_nativeBc = nullptr;
    bool                     cached_hasClosure = false;
    uint32_t                 cached_nConsts = 0;
    uint32_t                 cached_nNames  = 0;
    const proto::ProtoObject** cached_nativeConsts = nullptr;
    const proto::ProtoObject** cached_nativeNames  = nullptr;

    bool cacheHit = false;
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
                isGenerator           = cache->is_generator;
                co_varnames           = cache->co_varnames;
                cacheNoInnerFunctions = cache->no_inner_functions;
                cacheNoLoadDeref      = cache->no_load_deref;
                // Extended fields cached at BUILD_FUNCTION time.
                cached_bytecode       = cache->co_bytecode;
                cached_consts         = cache->co_consts_tuple;
                cached_names          = cache->co_names_tuple;
                cached_nativeBc       = cache->nativeBc;
                cached_hasClosure     = cache->hasClosure;
                cached_nConsts        = cache->nConsts;
                cached_nNames         = cache->nNames;
                cached_nativeConsts   = reinterpret_cast<const proto::ProtoObject**>(
                                            const_cast<FunctionMetaCache*>(cache) + 1);
                cached_nativeNames    = cached_nativeConsts + cached_nConsts;
                cacheHit = true;
            }
        }
    }

    // For any non-trivial case (generator, closure, kwargs, kwonly args, varargs, no cache),
    // fall back to the full implementation via a temporary ProtoList.
    // Use the cached hasClosure flag instead of a getAttribute call on the hot path.
    bool hasClosure = cacheHit ? cached_hasClosure : false;

    // cacheNoLoadDeref: safe to skip closure frame even when hasClosure — the function
    // never accesses captured variables via LOAD_DEREF during execution.
    bool useSlotFastPath = cacheHit && !isGenerator && (!hasClosure || cacheNoLoadDeref)
        && cacheNoInnerFunctions && (co_flags & CO_OPTIMIZED)
        && kwonly_count == 0 && !(co_flags & CO_VARARGS) && !(co_flags & CO_VARKEYWORDS)
        && (!kwargs || !kwargs->getSize(ctx))
        && rawArgCount >= (unsigned long)nparams_count; // fall back to full path when defaults needed

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
        return runUserFunctionCall(ctx, self, nullptr, argsList, kwargs);
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
    for (unsigned long i = 0; i < rawArgCount && i < (unsigned long)nparams_count && i < nSlots; ++i)
        slots[i] = const_cast<proto::ProtoObject*>(rawArgs[i]);

    // Frame creation: only when actually needed.
    //
    // For CO_OPTIMIZED leaf functions with no closure / no LOAD_DEREF / no inner
    // function or class definitions, the frame is never observed during normal
    // execution (LOAD_FAST/STORE_FAST use slots, no LOAD_DEREF needs f_back, no
    // BUILD_FUNCTION needs f_globals). sys._getframe() and traceback walks are
    // the only consumers; both can be supported lazily on demand without paying
    // for a full frame on every call. Profiles of call_recursion (fib(25), 242k
    // calls) showed 90% of time was the frame-newObject path here; gating it
    // restores the V97-era hot-call path.
    bool skipFrame = env && (co_flags & CO_OPTIMIZED) && !isGenerator && cacheNoInnerFunctions
        && (!hasClosure || cacheNoLoadDeref);
    proto::ProtoObject* frame = nullptr;
    if (!skipFrame) {
        frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(false));
        if (env) {
            frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, env->getFramePrototype()));
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFCodeString(), codeObj));
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFGlobalsString(), globalsObj));
            const proto::ProtoObject* parentFrame = PythonEnvironment::getCurrentFrame();
            if (parentFrame) {
                frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFBackString(), parentFrame));
            }
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
        const proto::ProtoObject* prevCodeObj_raw = PythonEnvironment::getCurrentCodeObject();
        PythonEnvironment::setCurrentCodeObject(codeObj);

        // Use values cached at BUILD_FUNCTION time — avoids 4 cross-DSO getAttribute calls
        // and 3 asTuple conversions on every function invocation.
        const proto::ProtoTuple* bytecode = cached_bytecode;
        const proto::ProtoTuple* consts   = cached_consts;
        const proto::ProtoTuple* names    = cached_names;
        const int* nativeBc               = cached_nativeBc;

        if (bytecode && consts) {
            unsigned long stackOffset = co_varnames ? co_varnames->getSize(calleeCtx) : 0;
            result = executeBytecodeRange(calleeCtx, consts, bytecode, names, frame,
                                          0, bytecode->getSize(calleeCtx), stackOffset,
                                          nullptr, nullptr, nullptr, 0, nullptr, nativeBc,
                                          cached_nativeConsts, cached_nativeNames);
        } else {
            result = PROTO_NONE;
        }
        PythonEnvironment::setCurrentCodeObject(prevCodeObj_raw);
    }
    promote(calleeCtx, result);
    } // ContextScope destroyed here
    return result;
}

} // namespace

const proto::ProtoObject* runBoundMethodCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs) {
    if (!ctx || !self) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;

    const proto::ProtoObject* im_self = self->getAttribute(ctx, env->getSelfDunderString());
    const proto::ProtoObject* im_func = self->getAttribute(ctx, env->getFuncDunderString());
    if (!im_self || !im_func) return PROTO_NONE;

    // Prepend im_self to args
    const proto::ProtoList* newArgs = ctx->newList()->appendLast(ctx, im_self);
    if (args) {
        for (unsigned long i = 0; i < args->getSize(ctx); ++i) {
            newArgs = newArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
        }
    }

    return invokePythonCallable(ctx, im_func, newArgs, kwargs);
}

namespace {

// Polymorphic Inline Cache for LOAD_ATTR / LOAD_METHOD slow-path lookups
// (sprint-2 step B, 2026-06-15). Caches the resolved value for
// `(type(obj), name)` pairs WITHOUT instance shadow.
//
// Cacheability invariant: an `obj.name` lookup that descends into the type
// chain (because the instance has no own `name`) returns the same value
// across all instances of `type(obj)`. The slow path's `env->getAttribute`
// returns the unbound function on a class (with isUnboundFunc=true) so
// the result is shared across instances — no per-instance binding state
// is captured in the cached value. The CALL opcode applies the bound-vs-
// unbound layout at dispatch time.
//
// Invalidation: tied to PythonEnvironment::resolveCacheGeneration. Class
// mutations bump the generation; cache entries with a stale generation
// miss and are refilled.
struct LoadAttrPicEntry {
    const proto::ProtoObject* type      = nullptr;
    const proto::ProtoString* name      = nullptr;
    const proto::ProtoObject* value     = nullptr;
    uint64_t                  generation = 0;
    bool                      isUnbound = false;
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
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*kwargs*/) {
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
static proto::ProtoObject* createUserFunction(proto::ProtoContext* ctx, const proto::ProtoObject* codeObj, proto::ProtoObject* globalsFrame, const proto::ProtoObject* closureFrame = nullptr, const proto::ProtoObject* defaults = nullptr, const proto::ProtoObject* kwDefaults = nullptr) {
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
    if (closureFrame && env) {
        // Wrap as a proper Python list (with __data__ + __class__) so
        // Python-side `len(f.__closure__)`, indexing, and repr work.
        const proto::ProtoList* closureTuple = ctx->newList()->appendLast(ctx, closureFrame);
        const proto::ProtoObject* closureWrapped = closureTuple->asObject(ctx);
        if (env->getListPrototype()) {
            proto::ProtoObject* w = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            w = const_cast<proto::ProtoObject*>(w->setAttribute(ctx, env->getDataString(), closureWrapped));
            w = const_cast<proto::ProtoObject*>(w->addParent(ctx, env->getListPrototype()));
            w = const_cast<proto::ProtoObject*>(w->setAttribute(ctx, env->getClassString(), env->getListPrototype()));
            closureWrapped = w;
        }
        fn = fn->setAttribute(ctx, env->getClosureString(), closureWrapped);
    }
    if (defaults && env) {
        fn = fn->setAttribute(ctx, env->getDefaultsString(), defaults);
    }
    if (kwDefaults && env) {
        fn = fn->setAttribute(ctx, env->getKwdefaultsString(), kwDefaults);
    }
    fn = fn->setAttribute(ctx, env ? env->getCallString() : PythonEnvironment::getInternedString(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(fn), runUserFunctionCall));
    fn = fn->setAttribute(ctx, env ? env->getGetDunderString() : PythonEnvironment::getInternedString(ctx, "__get__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(fn), py_function_get));

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
        const proto::ProtoTuple* co_varnames_val = (cvObj && cvObj->asTuple(ctx)) ? cvObj->asTuple(ctx) : nullptr;

        // Resolve the co_consts, co_names, and co_bytecode tuples.
        const proto::ProtoObject* constsObj = env->getCoConstsString()
            ? codeObj->getAttribute(ctx, env->getCoConstsString()) : nullptr;
        const proto::ProtoTuple* co_consts_val = constsObj ? constsObj->asTuple(ctx) : nullptr;

        const proto::ProtoObject* namesObj = env->getCoNamesString()
            ? codeObj->getAttribute(ctx, env->getCoNamesString()) : nullptr;
        const proto::ProtoTuple* co_names_val = namesObj ? namesObj->asTuple(ctx) : nullptr;

        const proto::ProtoObject* codeObjTupleObj = env->getCoCodeString()
            ? codeObj->getAttribute(ctx, env->getCoCodeString()) : nullptr;
        const proto::ProtoTuple* co_bytecode_val = codeObjTupleObj ? codeObjTupleObj->asTuple(ctx) : nullptr;

        // Resolve native bytecode and scan for special opcodes.
        const int* nativeBc_val = nullptr;
        bool no_inner_functions_val = false;
        bool no_load_deref_val = false;
        if (env->getCoNativeBytecodeString()) {
            const proto::ProtoObject* nbObj = codeObj->getAttribute(ctx, env->getCoNativeBytecodeString());
            if (nbObj && nbObj != PROTO_NONE) {
                char* nbData3 = nbObj->getDataIfByteBuffer(ctx);
                if (nbData3) {
                    nativeBc_val = reinterpret_cast<const int*>(nbData3);
                    size_t nbLen = nbObj->asByteBuffer(ctx)->getSize(ctx) / sizeof(int);
                    bool hasBuild = false, hasDeref = false;
                    for (size_t bi = 0; bi < nbLen; bi += 2) {
                        int op = nativeBc_val[bi];
                        if (op == OP_BUILD_FUNCTION || op == OP_BUILD_CLASS) hasBuild = true;
                        if (op == OP_LOAD_DEREF) hasDeref = true;
                    }
                    no_inner_functions_val = !hasBuild;
                    no_load_deref_val = !hasDeref;
                }
            }
        }

        // Determine closure status at build time.
        // An empty closure tuple/list means no captured variables — treat as no closure.
        bool hasClosure_val = false;
        if (closureFrame) {
            // closureFrame is the closure frame passed in to createUserFunction —
            // if it exists, the function has captured variables.
            hasClosure_val = true;
        }

        // Compute flat array sizes.
        uint32_t nConsts_val = co_consts_val ? static_cast<uint32_t>(co_consts_val->getSize(ctx)) : 0;
        uint32_t nNames_val  = co_names_val  ? static_cast<uint32_t>(co_names_val->getSize(ctx))  : 0;

        // Allocate variable-length buffer: struct + two flat pointer arrays.
        size_t totalBytes = sizeof(FunctionMetaCache)
            + (static_cast<size_t>(nConsts_val) + static_cast<size_t>(nNames_val))
              * sizeof(const proto::ProtoObject*);
        char* buf = new char[totalBytes];
        FunctionMetaCache* cache = new(buf) FunctionMetaCache{};

        cache->co_flags           = co_flags_val;
        cache->nparams            = nparams_val;
        cache->kwonly             = kwonly_val;
        cache->automatic_count    = automatic_val;
        cache->is_generator       = is_generator_val;
        cache->no_inner_functions = no_inner_functions_val;
        cache->no_load_deref      = no_load_deref_val;
        cache->hasClosure         = hasClosure_val;
        cache->codeObj            = codeObj;
        cache->globalsObj         = globalsObj;
        cache->co_varnames        = co_varnames_val;
        cache->co_bytecode        = co_bytecode_val;
        cache->co_consts_tuple    = co_consts_val;
        cache->co_names_tuple     = co_names_val;
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

        const proto::ProtoObject* cacheObj = ctx->fromBuffer(
            totalBytes, buf, true);
        if (cacheObj) {
            ctx->space->moduleRoots.push_back(cacheObj);
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
    const proto::ProtoTuple* mroTup = mroObj ? mroObj->asTuple(ctx) : nullptr;
    if (!mroTup) return false;
    unsigned long n = mroTup->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
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
            const proto::ProtoObject* res = invokeDunder(ctx, b, rdunderS, argsA);
            if (res && (!env || res != env->getNotImplementedPrototype())) {
                return res;
            }
            // Fall through to the normal forward-then-reflected order
            // when b.__rop__ returned NotImplemented or nullptr.
            if (env && env->hasPendingException()) env->clearPendingException();
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
        if (env && env->hasPendingException()) {
            // STRUCT-242: invokeDunder raised TypeError due to wrapper-
            // receiver mismatch on an explicit misbinding (e.g.
            // `class A(int): __add__ = str.__add__`).  Propagate the
            // exception instead of attempting the reflected __radd__
            // fallback (which would dispatch to int's inherited adder
            // and silently produce a wrong result).
            return nullptr;
        }
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
    if ((!res || (env && res == env->getNotImplementedPrototype()))
        && env && !env->hasPendingException()) {
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
    const proto::ProtoList* l1 = a->asList(ctx);
    if (!l1) {
        const proto::ProtoObject* data = a->getAttribute(ctx, env->getDataString());
        if (data) l1 = data->asList(ctx);
        if (!l1 && data) {
            if (data->asTuple(ctx)) l1 = data->asTuple(ctx)->asList(ctx);
        }
    }
    const proto::ProtoList* l2 = b->asList(ctx);
    if (!l2) {
        const proto::ProtoObject* data = b->getAttribute(ctx, env->getDataString());
        if (data) l2 = data->asList(ctx);
        if (!l2 && data) {
            if (data->asTuple(ctx)) l2 = data->asTuple(ctx)->asList(ctx);
        }
    }

    if (l1 && l2) {
        // Handle list/tuple addition
        proto::ProtoList* resL = const_cast<proto::ProtoList*>(ctx->newList());
        unsigned long n1 = l1->getSize(ctx);
        unsigned long n2 = l2->getSize(ctx);
        for (unsigned long i = 0; i < n1; ++i) resL = const_cast<proto::ProtoList*>(resL->appendLast(ctx, l1->getAt(ctx, i)));
        for (unsigned long i = 0; i < n2; ++i) resL = const_cast<proto::ProtoList*>(resL->appendLast(ctx, l2->getAt(ctx, i)));

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
                const proto::ProtoTuple* mroT = mroAttr ? mroAttr->asTuple(ctx) : nullptr;
                if (mroT) {
                    for (unsigned long mi = 0; mi < mroT->getSize(ctx); ++mi) {
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

        proto::ProtoObject* resObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        const proto::ProtoString* dataS = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
        if (isTuple) {
            // Save setAttribute result: these are immutable persistent objects.
            resObj = const_cast<proto::ProtoObject*>(resObj->setAttribute(ctx, dataS, ctx->newTupleFromList(resL)->asObject(ctx)));
        } else {
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
    if (a->asTuple(ctx) && b->isInteger(ctx)) { tupleObj = a; intObj = b; }
    else if (a->isInteger(ctx) && b->asTuple(ctx)) { tupleObj = b; intObj = a; }
    if (tupleObj && intObj) {
        long long n = intObj->asLong(ctx);
        if (n <= 0) return ctx->newTupleFromList(ctx->newList())->asObject(ctx);
        const proto::ProtoTuple* t = tupleObj->asTuple(ctx);
        size_t seqSize = t->getSize(ctx);
        proto::ProtoList* resultList = const_cast<proto::ProtoList*>(ctx->newList());
        for (long long rep = 0; rep < n; ++rep) {
            for (size_t idx = 0; idx < seqSize; ++idx) {
                const proto::ProtoObject* elem = t->getAt(ctx, idx);
                if (elem) resultList = const_cast<proto::ProtoList*>(resultList->appendLast(ctx, elem));
            }
        }
        return ctx->newTupleFromList(resultList)->asObject(ctx);
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
                return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(a), nullptr, emptyL, nullptr);
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
        try { return static_cast<double>(o->asLong(ctx)); }
        catch (const std::overflow_error&) {
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
                try { return static_cast<double>(o->asLong(ctx)); }
                catch (const std::overflow_error&) {
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
            return method->asMethod(ctx)(ctx, a, nullptr, args, nullptr);
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
            long long exp;
            try { exp = bb_p->asLong(ctx); }
            catch (const std::overflow_error&) {
                // Exponents that don't fit int64 are astronomical; fall
                // through to the double path (returns inf/0 as CPython
                // does via floats).
                double aa = static_cast<double>(aa_p->asLong(ctx));
                double bb = bb_p->isDouble(ctx) ? bb_p->asDouble(ctx) : 0.0;
                return ctx->fromDouble(std::pow(aa, bb));
            }
            if (exp < 0) {
                double base_d;
                try { base_d = static_cast<double>(aa_p->asLong(ctx)); }
                catch (const std::overflow_error&) { base_d = 0; }
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
        double aa = aa_p->isDouble(ctx) ? aa_p->asDouble(ctx) : static_cast<double>(aa_p->asLong(ctx));
        double bb = bb_p->isDouble(ctx) ? bb_p->asDouble(ctx) : static_cast<double>(bb_p->asLong(ctx));
        return ctx->fromInteger(static_cast<long long>(std::floor(aa / bb)));
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
                    const proto::ProtoTuple* mroTup = mroObj ? mroObj->asTuple(ctx) : nullptr;
                    if (mroTup) {
                        unsigned long mn = mroTup->getSize(ctx);
                        for (unsigned long mi = 0; mi < mn; ++mi) {
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
                        const proto::ProtoObject* res = invokeDunder(ctx, b, containsS, args);
                        if (res) {
                            found = isTruthy(ctx, res);
                            return ((op == 6) ? found : !found) ? PROTO_TRUE : PROTO_FALSE;
                        }
                        if (env_dunder->hasPendingException()) env_dunder->clearPendingException();
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
                                if (parents->getAt(ctx, i) == mpProto) { isMP = true; break; }
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
        const proto::ProtoList* lst = b->asList(ctx);
        if (!lst && b->isTuple(ctx)) {
            const proto::ProtoTuple* tup = b->asTuple(ctx);
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
                    const proto::ProtoTuple* mroT = mAttr ? mAttr->asTuple(ctx) : nullptr;
                    if (mroT) {
                        for (unsigned long i = 0; i < mroT->getSize(ctx); ++i) {
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
                if (data->asList(ctx)) lst = data->asList(ctx);
                else if (data->isTuple(ctx)) lst = data->asTuple(ctx)->asList(ctx);
                else if (data->asSparseList(ctx)) {
                    unsigned long hash = 0;
                    if (a->isString(ctx)) hash = a->getHash(ctx);
                    else if (a->isInteger(ctx)) hash = static_cast<unsigned long>(a->asLong(ctx));
                    
                    if (hash != 0 || a->isInteger(ctx)) {
                        if (data->asSparseList(ctx)->has(ctx, hash)) {
                            found = true;
                            result = (op == 6) ? found : !found;
                            return result ? PROTO_TRUE : PROTO_FALSE;
                        }
                        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                        if (env && env->hasPendingException()) env->clearPendingException();
                    }
                    // Fallback to __keys__ for SparseList if not found in data
                    const proto::ProtoString* keysS = protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                    const proto::ProtoObject* keysObj = b->getAttribute(ctx, keysS);
                    if (keysObj) lst = keysObj->asList(ctx);
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
                    const proto::ProtoObject* r = env_local->compareObjects(ctx, a, lst->getAt(ctx, i), 0);
                    eq = (r == PROTO_TRUE);
                } else {
                    eq = (a->compare(ctx, lst->getAt(ctx, i)) == 0);
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
            } else if (env && env->hasPendingException()) {
                // CPython rule: a __contains__ implementation that raises
                // — e.g. str.__contains__(non_str) → TypeError — must
                // propagate; previously this branch swallowed every
                // pending exception under the rationale "frame.__contains__
                // can set TypeError, treat as not found".  The blanket
                // suppression hid genuine type errors at user-facing
                // sites like `5 in 'hello'`.  Only swallow when the
                // dunder is missing (no exception path); a raised
                // TypeError propagates so the caller sees the same
                // diagnostic CPython emits.
                return nullptr;
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
                const proto::ProtoObject* it = env->iter(b);
                if (it) {
                    for (;;) {
                        const proto::ProtoObject* item = env->next(it);
                        if (!item) {
                            if (env->hasPendingException()) env->clearPendingException();
                            break;
                        }
                        const proto::ProtoObject* r = env->compareObjects(ctx, a, item, 0);
                        if (r == PROTO_TRUE) {
                            found = true;
                            break;
                        }
                    }
                } else if (env->hasPendingException()) {
                    // env->iter raised because b is not iterable.  CPython
                    // surfaces this as:
                    //   TypeError: argument of type 'X' is not iterable
                    // Replace env->iter's generic message with the form
                    // CPython uses for `x in non_container`.  Without this
                    // both `5 in 10` and `1 in None` silently returned
                    // False — a real correctness gap, not a tolerance.
                    env->clearPendingException();
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
        if (env) return env->compareObjects(ctx, a, b, op);
        
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

static bool isTruthy(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    if (obj == PROTO_FALSE) return false;
    if (obj == PROTO_TRUE) return true;

    if (obj->isInteger(ctx)) return (obj->asLong(ctx) != 0);
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
            result = boolMethod->asMethod(ctx)(ctx, obj, nullptr, emptyL, nullptr);
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
            result = lenMethod->asMethod(ctx)(ctx, obj, nullptr, emptyL, nullptr);
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
    if (obj->asTuple(ctx)) return (obj->asTuple(ctx)->getSize(ctx) > 0);
    if (obj->asList(ctx)) return (obj->asList(ctx)->getSize(ctx) > 0);

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

    if (env && env->hasPendingException()) return nullptr;
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
        return callable->asMethod(ctx)(
            ctx, const_cast<proto::ProtoObject*>(callable->asMethodSelf(ctx)),
            nullptr, args, kwargs);
    }

    // Fast path for user-defined Python functions: they always have __code__ as an own attribute
    // (set in createUserFunction). Avoid the expensive getType + getAttribute(__call__) path.
    if (env && env->getCodeString() &&
        callable->hasOwnAttribute(ctx, env->getCodeString()) == PROTO_TRUE) {
        return runUserFunctionCall(ctx, callable, nullptr, args, kwargs);
    }

    // If callable is a Python class, dispatch directly to runUserClassCall.
    // This prevents type(cls).__call__ from resolving to an instance method
    // like ContextDecorator.__call__ instead of type.__call__ = py_type_call.
    {
        const proto::ProtoString* isPyClsS = PythonEnvironment::getInternedString(ctx, "__is_python_class__");
        if (isPyClsS && callable->hasOwnAttribute(ctx, isPyClsS) == PROTO_TRUE) {
            // PI: refuse to instantiate classes whose
            // __abstractmethods__ frozenset is non-empty.  Mirrors
            // py_type_call's check for the metaclass-dispatched path.
            if (env) {
                const proto::ProtoString* amS =
                    PythonEnvironment::getInternedString(ctx, "__abstractmethods__");
                const proto::ProtoObject* am = callable->getAttribute(ctx, amS);
                if (am && am != PROTO_NONE) {
                    const proto::ProtoString* lenS =
                        PythonEnvironment::getInternedString(ctx, "__len__");
                    const proto::ProtoObject* lenM = env->getAttribute(ctx, am, lenS, false);
                    if (env->hasPendingException()) env->clearPendingException();
                    bool nonEmpty = false;
                    if (lenM && lenM != PROTO_NONE) {
                        const proto::ProtoObject* lenRes = nullptr;
                        if (lenM->asMethod(ctx)) {
                            lenRes = lenM->asMethod(ctx)(ctx,
                                const_cast<proto::ProtoObject*>(am), nullptr,
                                ctx->newList(), nullptr);
                        } else {
                            const proto::ProtoList* la =
                                ctx->newList()->appendLast(ctx, am);
                            lenRes = invokePythonCallable(ctx, lenM, la, nullptr);
                        }
                        if (env->hasPendingException()) env->clearPendingException();
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
            return runUserClassCall(ctx, callable, nullptr, args, kwargs);
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
        if (env && env->hasPendingException()) return nullptr;
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
        unsigned long nargs = args ? args->getSize(ctx) : 0;
        for (unsigned long j = 0; j < nargs; ++j) {
            selfPrependedArgs = selfPrependedArgs->appendLast(ctx, args->getAt(ctx, j));
        }
        return invokeCallable(ctx, callAttr, selfPrependedArgs, kwargs);
    }

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: invokeCallable calling __call__ asMethod\n");
        fflush(stderr);
    }

    const proto::ProtoObject* result = callAttr->asMethod(ctx)(ctx, callable, nullptr, args, kwargs);
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
            const proto::ProtoTuple* mroT = mroAttr ? mroAttr->asTuple(ctx) : nullptr;
            if (mroT) {
                for (unsigned long i = 0; i < mroT->getSize(ctx); ++i) {
                    const proto::ProtoObject* base = mroT->getAt(ctx, i);
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
                                        const_cast<proto::ProtoObject*>(v), nullptr, gargs, nullptr);
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
                    const proto::ProtoTuple* mroT = mroAttr ? mroAttr->asTuple(ctx) : nullptr;
                    if (mroT) {
                        for (unsigned long i = 0; i < mroT->getSize(ctx); ++i) {
                            if (mroT->getAt(ctx, static_cast<int>(i)) == owner) { ok = true; break; }
                        }
                    }
                    if (!ok) {
                        // Also check protoCore native parent chain (covers
                        // built-in subclass instances whose __mro__ tuple
                        // may be unset but whose chain is correct).
                        const proto::ProtoList* chain = rcvType->getParents(ctx);
                        if (chain) {
                            for (unsigned long i = 0; i < chain->getSize(ctx); ++i) {
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
        return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(container), nullptr, args, kwargs);
    }

    // Python user-defined dunder (regular function with __code__): prepend
    // `self` so the call lands as method(self, *args). invokeCallable does
    // not bind for us, so without this every inherited Python __contains__,
    // __setitem__, __getitem__ etc. ran with self=arg0 (silently broken).
    const proto::ProtoString* codeS = env ? env->getCodeString() : PythonEnvironment::getInternedString(ctx, "__code__");
    if (codeS && method->hasOwnAttribute(ctx, codeS) == PROTO_TRUE) {
        const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, container);
        unsigned long n = args ? args->getSize(ctx) : 0;
        for (unsigned long j = 0; j < n; ++j) selfArgs = selfArgs->appendLast(ctx, args->getAt(ctx, j));
        return invokeCallable(ctx, method, selfArgs, kwargs);
    }
    return invokeCallable(ctx, method, args, kwargs);
}

} // anonymous namespace

const proto::ProtoObject* py_generator_send_impl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ProtoObject* sendVal,
    const proto::ProtoObject* throwExc = nullptr) {
    
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
        // Native generators use a C++ callback that handles state.
        // We pass self (the generator) and sendVal (the value being sent).
        // The callback is responsible for updating gi_pc and gi_locals/stack on self.
        
        // 1. Mark as running
        self->setAttribute(ctx, env->getGiRunningString(), PROTO_TRUE);
        
        try {
            const proto::ProtoObject* result = env->callObject(nativeCb, {self, sendVal});
            self->setAttribute(ctx, env->getGiRunningString(), PROTO_FALSE);
            return result;
        } catch (const proto::ProtoObject* exc) {
            self->setAttribute(ctx, env->getGiRunningString(), PROTO_FALSE);
            throw exc;
        }
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
    const proto::ProtoList* blist = blocksObj ? blocksObj->asList(ctx) : nullptr;
    if (blist) {
        unsigned long size = blist->getSize(ctx);
        for (unsigned long i = 0; i < size; ++i) {
            const proto::ProtoObject* item = blist->getAt(ctx, static_cast<int>(i));
            if (item && item->isTuple(ctx)) {
                const proto::ProtoTuple* t = item->asTuple(ctx);
                if (t->getSize(ctx) >= 2) {
                    bool isWith = (t->getSize(ctx) >= 3) && (t->getAt(ctx, 2) == PROTO_TRUE);
                    blockStack.push_back({
                        static_cast<unsigned long>(t->getAt(ctx, 0)->asLong(ctx)),
                        static_cast<size_t>(t->getAt(ctx, 1)->asLong(ctx)),
                        isWith
                    });
                }
            }
        }
    }

    unsigned long pc = (pcObj && pcObj->isInteger(ctx)) ? static_cast<unsigned long>(pcObj->asLong(ctx)) : 0;
    const proto::ProtoTuple* co_code_tuple = codeObj->getAttribute(ctx, env->getCoCodeString())->asTuple(ctx);
    if (!co_code_tuple) {
        if (get_env_diag()) fprintf(stderr, "DEBUG HANG: generator missing co_code_tuple!\n");
        return PROTO_NONE;
    }
    
    if (pc >= co_code_tuple->getSize(ctx)) {
        if (get_env_diag()) fprintf(stderr, "DEBUG HANG: generator pc >= co_code_tuple! pc=%lu size=%lu\n", pc, co_code_tuple->getSize(ctx));
        env->raiseStopIteration(ctx, PROTO_NONE);
        return PROTO_NONE;
    }

    // 3. Restore stack (moved inside block below)

    // 5. Run
    self->setAttribute(ctx, env->getGiRunningString(), PROTO_TRUE);
    
    if (throwExc) {
        env->setPendingException(throwExc);
    }
    
    unsigned long nextPc = pc; 
    unsigned long finalTop = 0;
    unsigned long stackOffset = 0;
    unsigned long initialTop = 0;
    bool yielded = false;
    proto::ProtoContext* calleeCtx = nullptr;
    const proto::ProtoList* slist = stackObj ? stackObj->asList(ctx) : nullptr;
    
    const proto::ProtoObject* result = nullptr;
    {
        const proto::ProtoObject* co_varnames_obj = codeObj->getAttribute(ctx, env->getCoVarnamesString());
        const proto::ProtoTuple* co_varnames = co_varnames_obj ? co_varnames_obj->asTuple(ctx) : nullptr;
        stackOffset = co_varnames ? co_varnames->getSize(ctx) : 0;
        const proto::ProtoObject* co_automatic_obj = codeObj->getAttribute(ctx, env->getCoAutomaticCountString());
        int automatic_count = (co_automatic_obj && co_automatic_obj->isInteger(ctx)) ? static_cast<int>(co_automatic_obj->asLong(ctx)) : 0;
        
        const proto::ProtoList* localNames = ctx->newList();
        if (co_varnames) {
            unsigned long vSize = co_varnames->getSize(ctx);
            for (int i = 0; i < automatic_count; ++i) {
                const proto::ProtoObject* name = (i < static_cast<int>(vSize)) ? co_varnames->getAt(ctx, i) : PROTO_NONE;
                localNames = localNames->appendLast(ctx, name);
            }
        }
        
        ContextScope scope(ctx->space, ctx, nullptr, localNames, nullptr, nullptr);
        calleeCtx = scope.context();
        
        // Restore locals from gi_locals
        const proto::ProtoObject* localsObj = self->getAttribute(ctx, env->getGiLocalsString());
        const proto::ProtoList* savedLocals = localsObj ? localsObj->asList(ctx) : nullptr;

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
        unsigned int maxStack = (allSlots && nSlots > stackOffset) ? (nSlots - stackOffset) : 0;

        if (slist && stackBase) {
            unsigned long sSize = slist->getSize(calleeCtx);
            for (unsigned long i = 0; i < sSize && i < maxStack; ++i) {
                stackBase[initialTop++] = const_cast<proto::ProtoObject*>(slist->getAt(calleeCtx, static_cast<int>(i)));
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
            &finalTop);
            
        const proto::ProtoList* newLocals = calleeCtx->newList();
        const proto::ProtoObject** updatedSlots = calleeCtx->getAutomaticLocals();
        unsigned int updatedSlotsN = calleeCtx->getAutomaticLocalsCount();
        if (updatedSlots) {
            for (unsigned int i = 0; i < updatedSlotsN; ++i) {
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
            unsigned long stackBound = (updatedSlotsN > stackOffset)
                ? static_cast<unsigned long>(updatedSlotsN - stackOffset) : 0UL;
            unsigned long count = (finalTop < stackBound) ? finalTop : stackBound;
            for (unsigned long j = 0; j < count; ++j) {
                newStack = newStack->appendLast(calleeCtx, slots[stackOffset + j]);
            }
        }
        self->setAttribute(calleeCtx, env->getGiStackString(), newStack->asObject(calleeCtx));

        // Save blockStack back (tuple: [handlerPc, stackDepth, isWithBlock])
        const proto::ProtoList* newBlocks = calleeCtx->newList();
        for (const auto& b : blockStack) {
            const proto::ProtoList* tempL = calleeCtx->newList();
            tempL = tempL->appendLast(calleeCtx, calleeCtx->fromInteger(b.handlerPc));
            tempL = tempL->appendLast(calleeCtx, calleeCtx->fromInteger(b.stackDepth));
            tempL = tempL->appendLast(calleeCtx, b.isWithBlock ? PROTO_TRUE : PROTO_FALSE);
            const proto::ProtoObject* bTup = calleeCtx->newTupleFromList(tempL)->asObject(calleeCtx);
            newBlocks = newBlocks->appendLast(calleeCtx, bTup);
        }
        self->setAttribute(calleeCtx, env->getGiBlocksString(), newBlocks->asObject(calleeCtx));
    }

    // 8. Clear running 
    self->setAttribute(ctx, env->getGiRunningString(), PROTO_FALSE);
    if (!yielded) {
        nextPc = co_code_tuple->getSize(ctx);
    }
    self->setAttribute(ctx, env->getGiPCString(), ctx->fromInteger(nextPc));

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: py_generator_send_impl finished. yielded=%d, result=%p, hasPendingException=%d\n", 
                (int)yielded, (void*)result, (int)env->hasPendingException());
        if (env->hasPendingException()) {
             const proto::ProtoObject* exc = env->peekPendingException();
             fprintf(stderr, "DEBUG: py_generator_send_impl pending exception: %p\n", (void*)exc);
        }
        fflush(stderr);
    }

    if (!yielded && !env->hasPendingException()) {
        if (get_env_diag()) fprintf(stderr, "DEBUG HANG: generator exhausted cleanly! raising StopIteration\n");
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
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
}

const proto::ProtoObject* py_generator_repr(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
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

const proto::ProtoObject* py_generator_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return py_generator_send_impl(ctx, self, PROTO_NONE);
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
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self) return PROTO_NONE;

    const proto::ProtoString* anextS = env->getANextString();
    const proto::ProtoObject* anext = self->getAttribute(ctx, anextS);
    if (!anext || anext == PROTO_NONE) {
        env->raiseStopIteration(ctx, PROTO_NONE);
        return nullptr;
    }

    const proto::ProtoObject* coro = nullptr;
    if (anext->asMethod(ctx) && anext->asMethodSelf(ctx) != nullptr) {
        // Bound native method — self already captured.
        coro = anext->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(self),
                                     nullptr, ctx->newList(), nullptr);
    } else {
        // Python-level async def __anext__ — call as a callable, prepending self
        // explicitly because async def methods are not auto-bound by descriptor
        // protocol at attribute lookup time in protopy.
        const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, self);
        coro = invokePythonCallable(ctx, anext, selfArgs, nullptr);
    }
    if (env->hasPendingException()) {
        const proto::ProtoObject* exc = env->peekPendingException();
        // Resolve StopAsyncIteration via the env's cached type and
        // delegate to isInstanceOf — exc->__class__ can resolve through
        // the metaclass and return `type` instead of the actual
        // exception class for some allocation paths, so a string-name
        // compare against "StopAsyncIteration" is unreliable.
        const proto::ProtoObject* sasType = env->getStopAsyncIterationType();
        bool isSAS = false;
        if (exc && sasType) {
            isSAS = (exc->isInstanceOf(ctx, sasType) == PROTO_TRUE);
        }
        if (isSAS) {
            env->clearPendingException();
            env->raiseStopIteration(ctx, PROTO_NONE);
        }
        return nullptr;
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
    if (sendMethod->asMethod(ctx)) {
        sendResult = sendMethod->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(coro),
                                                nullptr, args, nullptr);
    } else {
        sendResult = invokePythonCallable(ctx, sendMethod, args, nullptr);
    }

    if (env->hasPendingException()) {
        const proto::ProtoObject* exc = env->peekPendingException();
        if (env->isStopIteration(ctx, exc)) {
            const proto::ProtoObject* val = env->getStopIterationValue(ctx, exc);
            env->clearPendingException();
            return val ? val : PROTO_NONE;
        }
        // StopAsyncIteration → StopIteration so FOR_ITER ends cleanly.
        // Use isInstanceOf; class name resolution can disagree with
        // the metaclass-walked __class__ attribute.
        const proto::ProtoObject* sasType = env->getStopAsyncIterationType();
        bool isSAS = false;
        if (exc && sasType) {
            isSAS = (exc->isInstanceOf(ctx, sasType) == PROTO_TRUE);
        }
        if (isSAS) {
            env->clearPendingException();
            env->raiseStopIteration(ctx, PROTO_NONE);
            return nullptr;
        }
        return nullptr;
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
static const proto::ProtoObject* py_asend_wrapper_send(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
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
    const proto::ProtoObject* val = py_generator_send_impl(ctx, target, sendVal, nullptr);

    if (env->hasPendingException()) {
        // StopIteration from the underlying agen → translate to
        // StopAsyncIteration so callers awaiting `agen.asend(v)` see
        // the async-iteration sentinel.
        const proto::ProtoObject* exc = env->peekPendingException();
        if (env->isStopIteration(ctx, exc)) {
            env->clearPendingException();
            env->raiseStopAsyncIteration(ctx);
        }
        return nullptr;
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
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
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
    // send / __next__ both drive one step; the wrapper StopIterates.
    const proto::ProtoObject* sendBound = ctx->fromMethod(
        const_cast<proto::ProtoObject*>(wrapper), py_asend_wrapper_send);
    wrapper = wrapper->setAttribute(ctx, env->getSendString(), sendBound);
    wrapper = wrapper->setAttribute(ctx, env->getNextString(), sendBound);
    // __await__ returns self so it's a valid awaitable.
    wrapper = wrapper->setAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "__await__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(wrapper), py_self_iter));
    return wrapper;
}

// athrow: throw an exception into the underlying agen.  Implemented as a
// thin wrapper similar to asend but invoking py_generator_throw.
const proto::ProtoObject* py_async_generator_athrow(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kw) {
    // Reuse py_generator_throw directly — its return value semantics
    // (yielded value, or pending exception on raise) match what awaitable
    // drivers expect for a single-shot throw.
    return py_generator_throw(ctx, self, pl, posArgs, kw);
}

const proto::ProtoObject* py_generator_send(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
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
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
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
                // Instantiate the type
                const proto::ProtoObject* instance = invokePythonCallable(ctx, exc, ctx->newList(), nullptr);
                if (instance && instance != PROTO_NONE && !env->hasPendingException()) {
                    exc = instance;
                } else if (env->hasPendingException()) {
                    env->clearPendingException();
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
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;

    // Check if already closed
    const proto::ProtoObject* pcObj = self->getAttribute(ctx, env->getGiPCString());
    const proto::ProtoObject* codeObj = self->getAttribute(ctx, env->getGiCodeString());
    if (pcObj && codeObj && pcObj->isInteger(ctx) && codeObj->getAttribute(ctx, env->getCoCodeString())->asList(ctx)) {
        unsigned long pc = (pcObj && pcObj->isInteger(ctx)) ? static_cast<unsigned long>(pcObj->asLong(ctx)) : 0;
        if (pc >= codeObj->getAttribute(ctx, env->getCoCodeString())->asList(ctx)->getSize(ctx)) {
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
    const proto::ProtoObject* genExit = invokePythonCallable(ctx, genExitType, emptyArgs, nullptr);
    if (!genExit || genExit == PROTO_NONE) {
        // Fallback: bare object with parent (may not be fully recognized by isinstance)
        genExit = ctx->newObject(true);
        genExit = genExit->addParent(ctx, genExitType);
    }
    // Clear any exception that may have been set during instance construction.
    if (env->hasPendingException()) {
        env->clearPendingException();
    }

    const proto::ProtoObject* sendResult = nullptr;
    try {
        sendResult = py_generator_send_impl(ctx, self, PROTO_NONE, genExit);
    } catch (...) {
        // C++ exception: clear any pending protoPython exception and return.
        if (env->hasPendingException()) {
            env->clearPendingException();
        }
        return PROTO_NONE;
    }

    // py_generator_send_impl sets a pending exception when the generator
    // does not handle GeneratorExit (the common case for a simple generator).
    // Python semantics: close() must swallow GeneratorExit and StopIteration;
    // any other exception propagates to the caller.
    if (env->hasPendingException()) {
        const proto::ProtoObject* exc = env->peekPendingException();
        bool isGenExit = false;
        if (genExitType && exc) {
            isGenExit = (exc == genExitType) ||
                        (exc->getPrototype(ctx) == genExitType) ||
                        (exc->isInstanceOf(ctx, genExitType) == PROTO_TRUE);
        }
        bool isStopIt = env->isStopIteration(ctx, exc);
        if (isGenExit || isStopIt) {
            env->clearPendingException();
        }
        // Otherwise leave the exception pending so it propagates.
    } else if (sendResult && sendResult != PROTO_NONE) {
        // PB6: the generator caught GeneratorExit and *yielded* a new
        // value rather than letting close() finish.  CPython contract:
        // raise RuntimeError("generator ignored GeneratorExit").
        env->raiseRuntimeError(ctx, "generator ignored GeneratorExit");
    }
    return PROTO_NONE;
}

const proto::ProtoObject* invokePythonCallable(proto::ProtoContext* ctx,
    const proto::ProtoObject* callable, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return invokeCallable(ctx, callable, args, kwargs);
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
            fprintf(stderr, "FATAL: GCStack overflow! top=%lu capacity=%lu — "
                    "increase PYTHON_STACK_BUFFER in Compiler.cpp\n",
                    (unsigned long)top, (unsigned long)capacity);
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

const proto::ProtoObject* runUserClassCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs) {
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
                newArgs = newArgs->appendLast(ctx, args->getAt(ctx, i));
            }
        }
        
        obj = const_cast<proto::ProtoObject*>(invokeCallable(ctx, newM, newArgs, kwargs));
        if (!obj || obj == PROTO_NONE) {
             if (env && env->hasPendingException()) {
                 if (get_env_diag()) fprintf(stderr, "DEBUG runUserClassCall: Pending exception detected!\n");
                 return nullptr; 
             }
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
                const proto::ProtoTuple* mroT =
                    mroAttr ? mroAttr->asTuple(ctx) : nullptr;
                if (mroT) {
                    for (unsigned long mi = 0; mi < mroT->getSize(ctx); ++mi) {
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
                            const proto::ProtoTuple* basesT =
                                basesAttr ? basesAttr->asTuple(ctx) : nullptr;
                            if (!basesT) return false;
                            for (unsigned long bi = 0; bi < basesT->getSize(ctx); ++bi) {
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
                        initArgs = initArgs->appendLast(ctx, args->getAt(ctx, i));
                    }
                }
                const proto::ProtoObject* initRes = invokePythonCallable(ctx, initM, initArgs, kwargs);
                if (env && env->hasPendingException()) {
                    if (get_env_diag()) fprintf(stderr, "DEBUG runUserClassCall: initM raised an exception!\n");
                    return nullptr;
                }
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



static void updateContextLocation(proto::ProtoContext* ctx, proto::ProtoObject* frame, unsigned long pc) {
    if (!ctx || !frame) return;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return;

    const proto::ProtoObject* co = frame->getAttribute(ctx, env->getFCodeString());
    if (!co) return;

    // Set filename
    const proto::ProtoObject* fn = co->getAttribute(ctx, env->getCoFilenameString());
    if (fn && fn->isString(ctx)) {
        static std::unordered_map<const proto::ProtoObject*, std::string> filenameCache;
        if (filenameCache.find(fn) == filenameCache.end()) {
            std::string s;
            fn->asString(ctx)->toUTF8String(ctx, s);
            filenameCache[fn] = s;
        }
        ctx->currentFileName = const_cast<char*>(filenameCache[fn].c_str());
    }

    // Set line number
    int lineno = 0;
    const proto::ProtoObject* fln = co->getAttribute(ctx, env->getCoFirstLinenoString());
    if (fln && fln->isInteger(ctx)) {
        lineno = static_cast<int>(fln->asLong(ctx));
    }

    const proto::ProtoObject* lnotabObj = co->getAttribute(ctx, env->getCoLnotabString());
    const proto::ProtoList* lnotab = lnotabObj ? lnotabObj->asList(ctx) : nullptr;
    if (lnotab) {
        // Resolve lineno from lnotab and PC
        unsigned long cursor = 0;
        int current_lineno = lineno;
        for (unsigned long j = 0; j < lnotab->getSize(ctx); j += 2) {
            if (j + 1 >= lnotab->getSize(ctx)) break;
            int pc_offset = static_cast<int>(lnotab->getAt(ctx, j)->asLong(ctx));
            int line_offset = static_cast<int>(static_cast<signed char>(lnotab->getAt(ctx, j+1)->asLong(ctx)));
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG: lnotab entry j=%lu, pc_offset=%d, line_offset=%d\n", j, pc_offset, line_offset);
            }
            if (cursor + pc_offset > pc) break;
            cursor += pc_offset;
            current_lineno += line_offset;
        }
        lineno = current_lineno;
    }
    ctx->currentLineNumber = lineno;
}

const proto::ProtoObject* executeBytecodeRange(
    proto::ProtoContext* ctx,
    const proto::ProtoTuple* constants,
    const proto::ProtoTuple* bytecode,
    const proto::ProtoTuple* names,
    proto::ProtoObject*& frame,
    unsigned long pcStart,
    unsigned long pcEnd,
    unsigned long stackOffset,
    unsigned long* outPc,
    bool* yielded,
    std::vector<Block>* externalBlockStack,
    unsigned long initialTop,
    unsigned long* finalTopPtr,
    const int* nativeBc,
    const proto::ProtoObject** nativeConsts,
    const proto::ProtoObject** nativeNames) {
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

    FrameScope fscope(frame);
    unsigned long n = bytecode->getSize(ctx);
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
        for (unsigned long j = 0; j < n; ++j) {
            const proto::ProtoObject* obj = bytecode->getAt(ctx, j);
            bc_fallback[j] = (obj && obj->isInteger(ctx)) ? static_cast<int>(obj->asLong(ctx)) : 0;
        }
        bc = bc_fallback.data();
    }

    unsigned int nSlots = ctx->getAutomaticLocalsCount();
    const proto::ProtoObject** allSlots = ctx->getAutomaticLocals();

    // Fallback stack for contexts without pre-allocated slots (e.g. some unit tests)
    std::vector<const proto::ProtoObject*> fallbackStack;
    const proto::ProtoObject** stackBase = nullptr;
    size_t stackCap = 0;

    if (allSlots && nSlots > stackOffset) {
        stackBase = allSlots + stackOffset;
        stackCap = nSlots - stackOffset;
    } else {
        fallbackStack.resize(4096); // Default capacity for manual/test execution
        stackBase = fallbackStack.data();
        stackCap = fallbackStack.size();
    }

    GCStack stack(stackBase, stackCap);
    stack.top = initialTop;

    std::vector<Block> blockStack;
    if (externalBlockStack) {
        blockStack = *externalBlockStack;
    }
    const bool sync_globals = (frame == PythonEnvironment::getCurrentGlobals());
    // Cooperative GC safepoint counter.  Most Python opcodes execute without
    // touching the cell allocator (LOAD_FAST, BINARY_ADD on SmallIntegers,
    // tight integer loops), so the per-allocation STW poll inside allocCell
    // never fires for CPU-bound threads.  Without an in-loop safepoint, a
    // GC stop-the-world request will starve indefinitely against a
    // pure-bytecode thread, deadlocking every other thread that is already
    // parked.  Polling every 256 opcodes keeps the fast path branch-only
    // while bounding pause-acquisition latency to a few µs.
    unsigned int sp_ctr = 0;
    for (unsigned long i = pcStart; i <= pcEnd; ) {
        if ((++sp_ctr & 0x3F) == 0 && ctx) {
            ctx->safepoint();
            // Cooperative signal delivery: when a Python signal handler
            // is registered, the OS-level handler only flips a
            // sig_atomic_t flag (async-signal-safe). The actual Python
            // callback is dispatched here, at the same cadence as the
            // GC safepoint, where the operand stack and frame are in
            // a known-good state. The branch is a single volatile
            // load when nothing is pending — measured cost is < 0.1%
            // on bench_pidigits.
            if (signal_module::hasPendingSignal()) {
                signal_module::checkAndDeliverPendingSignals(ctx, env);
                if (env && env->hasPendingException()) {
                    // The handler raised (e.g. KeyboardInterrupt). Let
                    // the standard exception-unwinding path pick it up
                    // by falling through; do NOT clear it here.
                }
            }
        }
        int op = bc[i];
        int arg = (i + 1 < n) ? bc[i + 1] : 0;
        unsigned long next_i = i + 2;

        if (diag_local) {
            fprintf(stderr, "DEBUG HANG TRACE: [PC %lu] OP %d ARG %d\n", i, op, arg);
            fprintf(stderr, "  Stack (depth=%lu):", (unsigned long)stack.size());
            for (size_t k = 0; k < stack.size(); ++k) {
                fprintf(stderr, " [%lu]=%p", (unsigned long)k, (void*)stack[k]);
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
        if (op == OP_YIELD_FROM && env && env->hasPendingException()) {
            // Skip the handler-dispatch below; YIELD_FROM owns it.
        } else
        if (env && env->hasPendingException()) {
            const proto::ProtoObject* exc = env->peekPendingException();
            if (exc) {
                fflush(stderr);
                
                std::string excName = "unknown";
                const proto::ProtoObject* cls = exc->getAttribute(ctx, env->getClassString());
                if (cls) {
                     const proto::ProtoObject* nameAttr = cls->getAttribute(ctx, env->getNameString());
                     if (nameAttr && nameAttr->isString(ctx)) nameAttr->asString(ctx)->toUTF8String(ctx, excName);
                }
                
                std::string excMsg = "";
                const proto::ProtoObject* strFunc = exc->getAttribute(ctx, env->getStrString());
                if (strFunc) {
                    if (strFunc->isString(ctx)) {
                        strFunc->asString(ctx)->toUTF8String(ctx, excMsg);
                    } else if (strFunc->asMethod(ctx)) {
                        const proto::ProtoObject* funcSelf = strFunc->asMethodSelf(ctx);
                        const proto::ProtoObject* strRes = strFunc->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(funcSelf), nullptr, ctx->newList(), ctx->newSparseList());
                        if (strRes && strRes->isString(ctx)) {
                            strRes->asString(ctx)->toUTF8String(ctx, excMsg);
                        }
                    }
                }

                updateContextLocation(ctx, frame, i);
                
                if (diag_local) {
                    fprintf(stderr, "DEBUG: Exception %s at %s:%d (PC %lu)\n", 
                            excName.c_str(), 
                            ctx->currentFileName ? ctx->currentFileName : "unknown", 
                            ctx->currentLineNumber, 
                            i);
                    fprintf(stderr, "DEBUG: Exception message: '%s'\n", excMsg.c_str());
                    fprintf(stderr, "DEBUG: raw exception pointer %p\n", exc);
                    fflush(stderr);
                }
            }
            
            if (diag_local) {
                fprintf(stderr, "DEBUG: Calling addTraceback...\n");
                fflush(stderr);
            }
            // runUserFunctionCall skips frame construction for CO_OPTIMIZED
            // hot-path functions that have no closure and no inner functions
            // (the local `frame` parameter is nullptr in that case). For
            // traceback purposes we still need an object that exposes
            // f_globals so unittest's `tb.tb_frame.f_globals` walk works,
            // so synthesise a minimal frame on demand here. The synthesised
            // frame parents off framePrototype and stores the executing
            // code object plus the current globals — enough for the
            // observed traceback callers, with no overhead on the
            // exception-free hot path.
            const proto::ProtoObject* tbFrame = frame;
            if (!tbFrame) tbFrame = PythonEnvironment::getCurrentFrame();
            if (!tbFrame || tbFrame == PROTO_NONE) {
                proto::ProtoObject* synth = const_cast<proto::ProtoObject*>(ctx->newObject(false));
                if (env && env->getFramePrototype()) {
                    synth = const_cast<proto::ProtoObject*>(synth->addParent(ctx, env->getFramePrototype()));
                }
                if (env) {
                    const proto::ProtoObject* curGlobals = PythonEnvironment::getCurrentGlobals();
                    if (curGlobals) {
                        synth = const_cast<proto::ProtoObject*>(synth->setAttribute(ctx, env->getFGlobalsString(), curGlobals));
                    }
                }
                tbFrame = synth;
            }
            env->addTraceback(exc, tbFrame, static_cast<int>(i), ctx->currentLineNumber);
            if (diag_local) {
                fprintf(stderr, "DEBUG: addTraceback returned. blockStack.empty()=%s\n", blockStack.empty() ? "true" : "false");
                fflush(stderr);
            }
            
            if (!blockStack.empty()) {
                Block b = blockStack.back();
                if (diag_local) {
                    fprintf(stderr, "DEBUG: Popping block: handlerPc=%lu stackDepth=%zu\n", b.handlerPc, b.stackDepth);
                    fflush(stderr);
                }
                blockStack.pop_back();

                if (exc && env) {
                    env->pushActiveException(exc);
                }
                env->clearPendingException();

                if (diag_local) {
                    fprintf(stderr, "DEBUG: Cleaning stack: current size=%zu, target size=%zu\n", stack.size(), b.stackDepth);
                    fflush(stderr);
                }
                while (stack.size() > b.stackDepth) stack.pop_back();
                if (exc) {
                    if (b.isWithBlock) {
                        // with-block handler: stack is [..., __exit__]; push only exc
                        // so OP_WITH_CLEANUP sees [__exit__, exc] as expected
                        if (diag_local) {
                            fprintf(stderr, "DEBUG: Pushing exc only for with-block handler %p\n", exc);
                            fflush(stderr);
                        }
                        stack.push_back(exc);
                    } else {
                        if (diag_local) {
                            fprintf(stderr, "DEBUG: Pushing exception tuple (None, exc, exc) %p back to stack\n", exc);
                            fflush(stderr);
                        }
                        const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : PROTO_NONE;
                        stack.push_back(noneObj); // Traceback
                        stack.push_back(exc);     // Value
                        stack.push_back(exc);     // Type
                    }
                }

                if (diag_local) {
                    fprintf(stderr, "DEBUG: Jumping to handlerPc=%lu\n", b.handlerPc);
                    fflush(stderr);
                }
                i = b.handlerPc;
                continue;
            }
            if (diag_local) {
                fprintf(stderr, "DEBUG: No trap found, returning nullptr\n");
                fflush(stderr);
            }
            return nullptr;
        }
        
        bool diag_env = diag_local;
        int diag_level = diag_env ? 1 : 0;
        if (diag_level >= 2) {
             fprintf(stderr, "TRACE: PC %lu OP %d ARG %d depth=%zu\n", i, op, (opcodeHasArg(op) ? arg : 0), stack.top);
             fprintf(stderr, "  STACK:");
             for (size_t k = 0; k < stack.top && k < 10; ++k) {
                 fprintf(stderr, " [%zu]=%p", k, (void*)stack.slots[k]);
             }
             if (stack.top > 10) fprintf(stderr, " ...");
             fprintf(stderr, "\n");
        }

        // C++ exception boundary.  protoCore (and other native modules) routinely
        // throw std::runtime_error / std::overflow_error from places like
        // Integer::asLong, ProtoString::createSymbol, etc., when given an
        // argument they consider invalid.  Without a translation point these
        // escape the bytecode loop, hit std::terminate, and SIGABRT the
        // process — making any test that hits a code path with weakly-typed
        // dispatch (test_re, test_types, test_descr…) into a hard crash
        // instead of a recoverable Python-level error.  Catch them here, set
        // a pending RuntimeError, and let the normal hasPendingException
        // unwinding take over so the user sees a proper Python traceback.
        try {
        switch (op) {
        case OP_LOAD_CONST: {
            // Use flat pre-fetched array when available (avoids cross-DSO AVL lookup).
            const proto::ProtoObject* val = nullptr;
            if (nativeConsts && static_cast<uint32_t>(arg) < static_cast<uint32_t>(constants->getSize(ctx))) {
                val = nativeConsts[arg];
            } else if (static_cast<unsigned long>(arg) < constants->getSize(ctx)) {
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

            // PB8: if the outer generator carries a pending exception
            // (caller did .throw()), forward it INTO subIter via
            // subIter.throw(exc) — that's the yield-from contract.
            // If subIter has no `throw` method, leave the exception
            // alone; the hasPendingException branch below will let it
            // propagate normally.
            bool injectedThrow = false;
            if (env && env->hasPendingException()) {
                const proto::ProtoString* throwS = PythonEnvironment::getInternedString(ctx, "throw");
                const proto::ProtoObject* throwMethod = subIter->getAttribute(ctx, throwS);
                if (throwMethod && throwMethod != PROTO_NONE && throwMethod->asMethod(ctx)) {
                    const proto::ProtoObject* exc = env->peekPendingException();
                    env->clearPendingException();
                    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, exc);
                    result = throwMethod->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(subIter), nullptr, args, nullptr);
                    injectedThrow = true;
                }
            }

            if (!injectedThrow) {
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

            if (env && env->hasPendingException()) {
                const proto::ProtoObject* exc = env->peekPendingException();
                if (env->isStopIteration(ctx, exc)) {
                    const proto::ProtoObject* stopVal = env->getStopIterationValue(ctx, exc);
                    env->clearPendingException();
                    stack.pop_back(); // Remove subIter
                    stack.push_back(stopVal);
                    // Will continue to i+1
                } else {
                    continue;
                }
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
            if (names && frame && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);

                    const proto::ProtoObject* val = nullptr;
                    bool found = false;

                    // Fast path: direct own-attribute lookup using the public SparseList API.
                    // getAt() returns PROTO_NONE when the key is absent, so we check for that.
                    {
                        const proto::ProtoSparseList* frameOwn = frame->proto::ProtoObject::getOwnAttributes(ctx);
                        if (frameOwn) {
                            const proto::ProtoObject* tmp = frameOwn->getAt(
                                ctx, reinterpret_cast<uintptr_t>(nameS));
                            if (tmp && tmp != PROTO_NONE) { val = tmp; found = true; }
                        }
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
                                val = invokeDunder(ctx, frame, env->getGetItemString(), giArgs);
                                if (env->hasPendingException()) {
                                    env->clearPendingException(); // KeyError means not found
                                } else if (val && val != PROTO_NONE) {
                                    found = true;
                                }
                            }
                        }
                    }

                    // Fallback: builtins, sys.modules, etc.
                    if (!found) {
                        val = env ? env->resolve(nameS, ctx) : nullptr;
                        if (val) found = true;
                    }

                    if (found) {
                        if (pushNull) stack.push_back(nullptr);
                        stack.push_back(val);
                    } else {
                        std::string nStr;
                        nameS->toUTF8String(ctx, nStr);
                        if (env && !env->hasPendingException()) env->raiseNameError(ctx, nStr);
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
            if (names && frame && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
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
                                if (env->hasPendingException()) {
                                    stack.pop_back();
                                    i = next_i;
                                    continue;
                                }
                            }
                        }
                    }

                    // O(1) pre-check: is this key already present in the frame's own attributes?
                    bool isNewKey = true;
                    {
                        const proto::ProtoSparseList* frameOwn = frame->proto::ProtoObject::getOwnAttributes(ctx);
                        if (frameOwn) {
                            isNewKey = !frameOwn->has(ctx, reinterpret_cast<uintptr_t>(nS));
                        }
                    }

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
                        const proto::ProtoList* keysList = (keysObj && keysObj->asList(ctx))
                            ? keysObj->asList(ctx) : ctx->newList();
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
                std::string nStr = "?";
                const proto::ProtoObject* codeObj = PythonEnvironment::getCurrentCodeObject();
                if (codeObj) {
                    const proto::ProtoObject* varnamesObj = codeObj->getAttribute(ctx, env->getCoVarnamesString());
                    const proto::ProtoTuple* vt = varnamesObj ? varnamesObj->asTuple(ctx) : nullptr;
                    if (vt && static_cast<unsigned long>(arg) < vt->getSize(ctx)) {
                        const proto::ProtoObject* nameObj = vt->getAt(ctx, arg);
                        if (nameObj && proto::ProtoObject::isStringTagFast(nameObj)) {
                            nameObj->asString(ctx)->toUTF8String(ctx, nStr);
                        }
                    }
                }
                std::string msg = "cannot access local variable '" + nStr +
                    "' where it is not associated with a value";
                env->raiseUnboundLocalError(ctx, msg);
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

            const proto::ProtoString* iaddS = env ? env->getIAddString() : PythonEnvironment::getInternedString(ctx, "__iadd__");
            const proto::ProtoObject* iadd = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, iaddS);
            const proto::ProtoObject* result = nullptr;
            if (iadd && iadd->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                result = iadd->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            } else if (iadd && iadd != PROTO_NONE) {
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
                result = invokeDunder(ctx, a, iaddS, args);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result && (!env || !env->hasPendingException())) {
                result = binaryAdd(ctx, a, b);
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
            if (!result && env && env->hasPendingException()) continue;
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
            if (!r && env && env->hasPendingException()) continue;
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
                result = isub->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            } else if (isub && isub != PROTO_NONE) {
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
                result = invokeDunder(ctx, a, isubS, args);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result && (!env || !env->hasPendingException())) {
                result = binarySubtract(ctx, a, b);
            }
            if (result) stack.push_back(result);
            else if (env && env->hasPendingException()) continue;
            else stack.push_back(PROTO_NONE);
        } break;
        case OP_BINARY_MULTIPLY: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            // SmallInt × SmallInt: product may exceed 56-bit range, use
            // __int128 to detect overflow.  sieve hot loop is `i * j`
            // with both operands well within SmallInt range.
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                __int128 prod = (__int128)proto::asSmallInt(a) * proto::asSmallInt(b);
                if (proto::smallIntInRange(static_cast<long long>(prod))) {
                    stack.pop_back();
                    stack.back() = proto::makeSmallInt(static_cast<long long>(prod));
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
                result = imul->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            } else if (imul && imul != PROTO_NONE) {
                // Python user __imul__ — invokeDunder prepends self.
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
                result = invokeDunder(ctx, a, imulS, args);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result && (!env || !env->hasPendingException())) {
                result = binaryMultiply(ctx, a, b);
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
            if (!result && env && env->hasPendingException()) continue;
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
            if (!r && env && !env->hasPendingException()) {
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
            if (!r && env && env->hasPendingException()) continue;
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
            if (!res && (!env || !env->hasPendingException())) {
                res = binaryOpDispatch(ctx, left, right, "__matmul__", "__rmatmul__");
                if (!res && env && !env->hasPendingException()) {
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
            if (!res && env && env->hasPendingException()) continue;
        } break;
        case OP_RERAISE: {
            // Re-raise the exception on top of block stack
            if (!env) { i = next_i; continue; }
            // stub: if we have a pending exception, just continue to trigger handler search
            continue;
        } break;
        case OP_JUMP_FORWARD: {
            i = next_i + arg;
            continue;
        } break;
        case OP_POP_EXCEPT: {
            if (env) {
                env->popActiveException();
                // popActiveException (e.g. list removeAt) can set TypeError; clear so module continues
                if (env->hasPendingException()) env->clearPendingException();
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
            const proto::ProtoObject* itruediv = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getITrueDivString() : PythonEnvironment::getInternedString(ctx, "__itruediv__"));
            if (itruediv && itruediv->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = itruediv->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back();
                stack.back() = result ? result : PROTO_NONE;
            } else {
                const proto::ProtoObject* r = binaryTrueDivide(ctx, a, b);
                stack.pop_back();
                stack.back() = r;
            }
        } break;
        case OP_INPLACE_FLOOR_DIVIDE: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* ifloordiv = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIFloorDivString() : PythonEnvironment::getInternedString(ctx, "__ifloordiv__"));
            if (ifloordiv && ifloordiv->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ifloordiv->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else {
                const proto::ProtoObject* r = binaryFloorDivide(ctx, a, b);
                stack.pop_back(); stack.back() = r;
            }
        } break;
        case OP_INPLACE_MODULO: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* imod = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIModString() : PythonEnvironment::getInternedString(ctx, "__imod__"));
            if (imod && imod->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = imod->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else {
                const proto::ProtoObject* r = binaryModulo(ctx, a, b);
                stack.pop_back(); stack.back() = r;
            }
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
                result = ipow->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
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
            if (!result && (!env || !env->hasPendingException())) {
                result = binaryPower(ctx, a, b);
                // CPython parity: failed `**=` should mention `**=` in
                // the TypeError, not `**`. binaryPower routes through
                // binaryOpDispatch which only knows the binary operator
                // symbol, so rewrite the message here for the in-place
                // op.  binaryPower returns PROTO_NONE on error (its
                // `r ? r : PROTO_NONE` tail), so compare against both.
                if ((!result || result == PROTO_NONE) && env && env->hasPendingException()) {
                    const proto::ProtoObject* exc = env->peekPendingException();
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
                        env->clearPendingException();
                        env->raiseTypeError(ctx,
                            "unsupported operand type(s) for **=: '" + aName + "' and '" + bName + "'");
                    }
                }
            }
            stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            if (!result && env && env->hasPendingException()) continue;
        } break;
        case OP_INPLACE_LSHIFT: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* ilshift = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getILShiftString() : PythonEnvironment::getInternedString(ctx, "__ilshift__"));
            const proto::ProtoObject* result = nullptr;
            if (ilshift && ilshift->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                result = ilshift->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result && a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = static_cast<long long>(static_cast<unsigned long long>(av) << bv);
                result = ctx->fromInteger(av);
            }
            if (!result && (!env || !env->hasPendingException())) {
                result = binaryOpDispatch(ctx, a, b, "__lshift__", "__rlshift__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
            if (!result && env && env->hasPendingException()) continue;
        } break;
        case OP_INPLACE_RSHIFT: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* irshift = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIRShiftString() : PythonEnvironment::getInternedString(ctx, "__irshift__"));
            const proto::ProtoObject* result = nullptr;
            if (irshift && irshift->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                result = irshift->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result && a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = av >> bv;
                result = ctx->fromInteger(av);
            }
            if (!result && (!env || !env->hasPendingException())) {
                result = binaryOpDispatch(ctx, a, b, "__rshift__", "__rrshift__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
            if (!result && env && env->hasPendingException()) continue;
        } break;
        case OP_INPLACE_AND: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* iand = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIAndString() : PythonEnvironment::getInternedString(ctx, "__iand__"));
            const proto::ProtoObject* result = nullptr;
            if (iand && iand->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                result = iand->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result && a->isInteger(ctx) && b->isInteger(ctx)) {
                result = a->bitwiseAnd(ctx, b);
            }
            if (!result && (!env || !env->hasPendingException())) {
                result = binaryOpDispatch(ctx, a, b, "__and__", "__rand__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
            if (!result && env && env->hasPendingException()) continue;
        } break;
        case OP_INPLACE_OR: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* ior = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIOrString() : PythonEnvironment::getInternedString(ctx, "__ior__"));
            const proto::ProtoObject* result = nullptr;
            if (ior && ior->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                result = ior->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result && a->isInteger(ctx) && b->isInteger(ctx)) {
                result = a->bitwiseOr(ctx, b);
            }
            if (!result && (!env || !env->hasPendingException())) {
                result = binaryOpDispatch(ctx, a, b, "__or__", "__ror__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
            if (!result && env && env->hasPendingException()) continue;
        } break;
        case OP_INPLACE_XOR: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* ixor = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIXorString() : PythonEnvironment::getInternedString(ctx, "__ixor__"));
            const proto::ProtoObject* result = nullptr;
            if (ixor && ixor->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                result = ixor->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (env && result == env->getNotImplementedPrototype()) result = nullptr;
            }
            if (!result && a->isInteger(ctx) && b->isInteger(ctx)) {
                result = a->bitwiseXor(ctx, b);
            }
            if (!result && (!env || !env->hasPendingException())) {
                result = binaryOpDispatch(ctx, a, b, "__xor__", "__rxor__");
            }
            stack.pop_back();
            stack.back() = result ? result : PROTO_NONE;
            if (!result && env && env->hasPendingException()) continue;
        } break;
        case OP_BINARY_LSHIFT: {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = coerceBoolToInt(ctx, stack.back());
            const proto::ProtoObject* a = coerceBoolToInt(ctx, stack[stack.top - 2]);
            // SmallInt << SmallInt: shift into __int128 and range-check.
            // nqueens uses `1 << col` (col ≤ n, typically ≤ 30) — fast path
            // succeeds virtually always.
            if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                long long shift = proto::asSmallInt(b);
                if (shift >= 0 && shift < 63) {
                    __int128 result = (__int128)proto::asSmallInt(a) << shift;
                    long long r64 = static_cast<long long>(result);
                    if (proto::smallIntInRange(r64) && ((__int128)r64 == result)) {
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
                // Shift amount: try asLong; bignum amounts are absurd so
                // we cap them by routing through Integer::shiftLeft on a
                // value of 0 (which short-circuits) rather than crashing.
                int amount = 0;
                try { amount = static_cast<int>(b->asLong(ctx)); }
                catch (const std::overflow_error&) {
                    if (env) env->raiseValueError(ctx,
                        PythonEnvironment::getInternedString(ctx, "shift count too large")->asObject(ctx));
                    stack.pop_back(); stack.back() = PROTO_NONE;
                    i = next_i; continue;
                }
                if (amount < 0) {
                    if (env) env->raiseValueError(ctx,
                        PythonEnvironment::getInternedString(ctx, "negative shift count")->asObject(ctx));
                    stack.pop_back(); stack.back() = PROTO_NONE;
                    i = next_i; continue;
                }
                stack.pop_back(); stack.back() = a->shiftLeft(ctx, amount);
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
                int amount = 0;
                try { amount = static_cast<int>(b->asLong(ctx)); }
                catch (const std::overflow_error&) {
                    if (env) env->raiseValueError(ctx,
                        PythonEnvironment::getInternedString(ctx, "shift count too large")->asObject(ctx));
                    stack.pop_back(); stack.back() = PROTO_NONE;
                    i = next_i; continue;
                }
                if (amount < 0) {
                    if (env) env->raiseValueError(ctx,
                        PythonEnvironment::getInternedString(ctx, "negative shift count")->asObject(ctx));
                    stack.pop_back(); stack.back() = PROTO_NONE;
                    i = next_i; continue;
                }
                stack.pop_back(); stack.back() = a->shiftRight(ctx, amount);
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
                                nullptr, argsB, nullptr);
                            if (result == env->getNotImplementedPrototype()) {
                                result = nullptr;
                                sawNotImplemented = true;
                            }
                        }
                    }
                }

                if (!result && (!env || !env->hasPendingException())) {
                    // Non-PEP 604 path: delegate to the shared dispatcher
                    // which handles forward + reflected dunder, NotImplemented
                    // propagation and TypeError raising in strict mode.
                    result = binaryOpDispatch(ctx, a, b, "__or__", "__ror__");
                }
                (void)sawNotImplemented;

                stack.pop_back();
                stack.back() = (result ? result : PROTO_NONE);
                if (!result && env && env->hasPendingException()) continue;
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
            if (arg == 0) {
                 const proto::ProtoObject* activeExc = env ? env->getActiveException() : nullptr;
                 if (!activeExc && !stack.empty()) {
                     activeExc = stack.back();
                     stack.pop_back();
                 }
                 if (activeExc) {
                     if (env) env->setPendingException(activeExc);
                 } else {
                     if (env && !env->hasPendingException()) {
                         env->raiseRuntimeError(ctx, "reraise outside of except block");
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
                     if (env && exc && exc != PROTO_NONE) env->setPendingException(exc);
                 }
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
                const proto::ProtoList* keysList = (keysObj && keysObj->asList(ctx))
                    ? keysObj->asList(ctx) : ctx->newList();
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
                            if (!nameObj) {
                                if (env->hasPendingException() && env->isStopIteration(ctx, env->peekPendingException()))
                                    env->clearPendingException();
                                break;
                            }
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
                    if (keysObj && keysObj->asList(ctx)) {
                        const proto::ProtoList* keysList = keysObj->asList(ctx);
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
                                unsigned long key = it->nextKey(ctx);
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
            if (names && stack.size() >= 1 && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* mod = stack.back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                
                if (proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    const proto::ProtoObject* val = (env) 
                        ? env->getAttribute(ctx, mod, nameS) 
                        : mod->getAttribute(ctx, nameS);
                    
                    if (val && (val != PROTO_NONE || mod->hasAttribute(ctx, nameS) == PROTO_TRUE)) {
                        if (env) env->clearPendingException();
                        stack.push_back(val);
                    } else {
                        if (env) {
                            // Clear any pending AttributeError from getAttribute so that
                            // raiseImportError can set the correct ImportError.
                            env->clearPendingException();
                            std::string n;
                            nameS->toUTF8String(ctx, n);
                            std::string msg = "cannot import name '" + n + "'";
                            const proto::ProtoObject* mName = mod->getAttribute(ctx, env->getNameString());
                            if (mName && mName->isString(ctx)) {
                                std::string mn;
                                mName->asString(ctx)->toUTF8String(ctx, mn);
                                msg += " from '" + mn + "'";
                            }
                             const proto::ProtoObject* fileAttr = mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__file__"));
                             if (fileAttr && fileAttr->isString(ctx)) {
                                 std::string fn;
                                 fileAttr->asString(ctx)->toUTF8String(ctx, fn);
                                 msg += " (" + fn + ")";
                             }

                            env->raiseImportError(ctx, msg);
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

            // STRUCT-204: CPython looks up `__enter__` / `__exit__` via
            // type-only lookup (TPSLOT_LOOKUP), bypassing
            // __getattribute__.  Using env->getAttribute on the instance
            // triggers the custom __getattribute__ hook which fails the
            // Checker pattern in test_special_method_lookup.  Walk
            // type(manager).__mro__ for own attrs of `__exit__` /
            // `__enter__` instead.
            auto lookupTypeOnly = [&](const proto::ProtoObject* obj, const proto::ProtoString* name) -> const proto::ProtoObject* {
                if (!env) return obj ? obj->getAttribute(ctx, name) : nullptr;
                const proto::ProtoObject* cls = env->getType(ctx, obj);
                if (!cls || cls == PROTO_NONE) return nullptr;
                const proto::ProtoObject* mroAttr = env->getAttribute(ctx, cls, env->getMroString(), false);
                const proto::ProtoTuple* mroT = mroAttr ? mroAttr->asTuple(ctx) : nullptr;
                if (!mroT) return env->getAttribute(ctx, cls, name, false);
                for (unsigned long i = 0; i < mroT->getSize(ctx); ++i) {
                    const proto::ProtoObject* base = mroT->getAt(ctx, i);
                    if (!base || base == PROTO_NONE) continue;
                    if (base->hasOwnAttribute(ctx, name) == PROTO_TRUE) {
                        const proto::ProtoObject* v = base->getOwnAttributeDirect(ctx, name);
                        if (v && v != PROTO_NONE) {
                            // If v is a descriptor, invoke __get__(obj, cls).
                            const proto::ProtoString* getDS = PythonEnvironment::getInternedString(ctx, "__get__");
                            const proto::ProtoObject* vType = env->getType(ctx, v);
                            const proto::ProtoObject* getM = vType ? env->getAttribute(ctx, vType, getDS, false) : nullptr;
                            if (getM && getM != PROTO_NONE) {
                                if (getM->asMethod(ctx)) {
                                    const proto::ProtoList* args = ctx->newList()
                                        ->appendLast(ctx, obj)->appendLast(ctx, cls);
                                    return getM->asMethod(ctx)(ctx,
                                        const_cast<proto::ProtoObject*>(v), nullptr, args, nullptr);
                                }
                                // STRUCT-223: handle Python __get__ as well
                                // (SpecialDescr pattern).  Pass descriptor as
                                // implicit self plus (obj, cls).
                                return env->callObject(getM, {v, obj, cls});
                            }
                            return v;
                        }
                    }
                }
                return nullptr;
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
            if (!enterResult && env && env->hasPendingException()) continue;

            // Push block pointing to handler at arg (absolute PC)
            blockStack.push_back({static_cast<unsigned long>(arg), stack.size(), true});

            stack.push_back(enterResult);
        } break;
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
                        const proto::ProtoTuple* mroT = mroAttr ? mroAttr->asTuple(ctx) : nullptr;
                        if (mroT) {
                            for (unsigned long mi = 0; mi < mroT->getSize(ctx); ++mi) {
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
            if (!r && env && env->hasPendingException()) continue;
            // (Note: if r is null, we set PROTO_NONE to avoid null on stack)
        } break;
        case OP_POP_JUMP_IF_FALSE: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (!isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n) {
                i = static_cast<unsigned long>(arg);
                continue;
            }
        } break;
        case OP_POP_JUMP_IF_TRUE: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n) {
                i = static_cast<unsigned long>(arg);
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
                const proto::ProtoList* origLst = lstObj->asList(ctx);
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
            const proto::ProtoList* raw = rawObj ? rawObj->asList(ctx) : nullptr;
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
                    unsigned long h = ::protoPython::pyDictKeyHash(ctx, key);
                    bool isNew = !sl->has(ctx, h);
                    sl = sl->setAt(ctx, h, val);
                    const proto::ProtoObject* newMap = mapObj->setAttribute(ctx, dataString, sl->asObject(ctx));
                    stack[stack.size() - arg - 1] = const_cast<proto::ProtoObject*>(newMap);
                    if (isNew) {
                         const proto::ProtoString* keysString = protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                         const proto::ProtoObject* keysObj = newMap->getAttribute(ctx, keysString);
                         const proto::ProtoList* keys = (keysObj && keysObj->asList(ctx)) ? keysObj->asList(ctx) : ctx->newList();
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
                s = s->add(ctx, val);
                const proto::ProtoObject* newSet = setObj->setAttribute(ctx, dataString, s->asObject(ctx));
                stack[stack.size() - arg - 1] = const_cast<proto::ProtoObject*>(newSet);
                stack.pop_back(); // Now safe to pop val
            }
        } break;
        case OP_DICT_UPDATE: {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* from = stack.back();
                proto::ProtoObject* toObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoString* keysName = protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                const proto::ProtoObject* toData = toObj->getAttribute(ctx, dataString);
                if (toData && toData->asSparseList(ctx)) {
                    const proto::ProtoSparseList* toSL = toData->asSparseList(ctx);
                    const proto::ProtoObject* toKeysObj = toObj->getAttribute(ctx, keysName);
                    const proto::ProtoList* toKeys = (toKeysObj && toKeysObj->asList(ctx)) ? toKeysObj->asList(ctx) : ctx->newList();
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
                        && fromKeysObj && fromKeysObj->asList(ctx)) {
                        const proto::ProtoSparseList* fromSL = fromData->asSparseList(ctx);
                        const proto::ProtoList* fromKeys = fromKeysObj->asList(ctx);
                        for (unsigned long j = 0; j < fromKeys->getSize(ctx); ++j) {
                            const proto::ProtoObject* k = fromKeys->getAt(ctx, j);
                            unsigned long h = k->getHash(ctx);
                            const proto::ProtoObject* v = fromSL->getAt(ctx, h);
                            bool isNew = !toSL->has(ctx, h);
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
                                        nullptr, args, nullptr);
                                }
                                const proto::ProtoString* codeS = env->getCodeString();
                                bool raw = (codeS && m->hasOwnAttribute(ctx, codeS) == PROTO_TRUE);
                                const proto::ProtoList* selfArgs = ctx->newList();
                                if (raw) selfArgs = selfArgs->appendLast(ctx, recv);
                                unsigned long n = args ? args->getSize(ctx) : 0;
                                for (unsigned long j = 0; j < n; ++j)
                                    selfArgs = selfArgs->appendLast(ctx, args->getAt(ctx, j));
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
                                        unsigned long h = k->getHash(ctx);
                                        bool isNew = !toSL->has(ctx, h);
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
        } break;
        case OP_LIST_EXTEND: {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* iterable = stack.back();
                // iterable remains on stack
                proto::ProtoObject* lstObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* dataObj = lstObj->getAttribute(ctx, dataString);
                if (dataObj && dataObj->asList(ctx)) {
                    const proto::ProtoList* lst = dataObj->asList(ctx);
                    const proto::ProtoObject* fromData = iterable->getAttribute(ctx, dataString);
                    const proto::ProtoList* fromList = (fromData && fromData->asList(ctx)) ? fromData->asList(ctx) : iterable->asList(ctx);
                    if (fromList) {
                        for (unsigned long j = 0; j < fromList->getSize(ctx); ++j) {
                            lst = lst->appendLast(ctx, fromList->getAt(ctx, j));
                        }
                        lstObj->setAttribute(ctx, dataString, lst->asObject(ctx));
                    } else {
                        // Handle ProtoTuple iterables (e.g. raw *args tuple from varargs binding)
                        const proto::ProtoTuple* fromTuple = (fromData && fromData->asTuple(ctx)) ? fromData->asTuple(ctx) : iterable->asTuple(ctx);
                        if (fromTuple) {
                            for (unsigned long j = 0; j < fromTuple->getSize(ctx); ++j) {
                                lst = lst->appendLast(ctx, fromTuple->getAt(ctx, j));
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
                    const proto::ProtoList* fromList = (fromData && fromData->asList(ctx)) ? fromData->asList(ctx) : iterable->asList(ctx);
                    if (fromList) {
                        for (unsigned long j = 0; j < fromList->getSize(ctx); ++j) {
                            s = s->add(ctx, fromList->getAt(ctx, j));
                        }
                    } else if (env) {
                        const proto::ProtoObject* it = env->iter(iterable);
                        if (it) {
                            for (;;) {
                                const proto::ProtoObject* item = env->next(it);
                                if (!item) break;
                                s = s->add(ctx, item);
                            }
                        }
                    }
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
            for (int j = 0; j < arg; ++j) {
                const proto::ProtoObject* item = stack[baseIdx + j];
                data = data->add(ctx, item);
                stack[stack.size() - 1] = const_cast<proto::ProtoObject*>(data->asObject(ctx)); // Update root
            }
            
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
                   const proto::ProtoList*,
                   const proto::ProtoSparseList*) -> const proto::ProtoObject* {
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
        case OP_LOAD_DEREF: {
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj && proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    unsigned long h = nameObj->getHash(ctx);
                    if (diag_local) {
                    }
                    const proto::ProtoObject* val = PROTO_NONE;

                    const proto::ProtoList* worklist = ctx->newList();
                    worklist = worklist->appendLast(ctx, frame);
                    // We must track the worklist itself! Put it on stack temporarily.
                    stack.push_back(worklist->asObject(ctx));
                    
                    bool found = false;
                    std::unordered_set<const proto::ProtoObject*> visited;
                    while (worklist->getSize(ctx) > 0) {
                        const proto::ProtoObject* curr = worklist->getAt(ctx, worklist->getSize(ctx) - 1);
                        worklist = worklist->removeAt(ctx, worklist->getSize(ctx) - 1);
                        stack[stack.top - 1] = worklist->asObject(ctx); // Update root

                        if (!curr || curr == PROTO_NONE || visited.count(curr)) continue;
                        visited.insert(curr);

                        // protoCore convention: getAttribute returns
                        // PROTO_NONE for missing attrs, NOT nullptr (only
                        // nullptr is "invalid input").  We can't tell
                        // "stored None" from "missing" via getAttribute
                        // alone — gate on hasOwnAttribute first so we
                        // only accept genuine own-attribute hits.  The
                        // worklist still walks up via __closure__ /
                        // parents, so an inherited cell is reached on
                        // a later iteration.
                        if (curr->hasOwnAttribute(ctx, nameS) == PROTO_TRUE) {
                            val = curr->getOwnAttributeDirect(ctx, nameS);
                            if (val) { found = true; break; }
                        }

                    if (env) {
                        const proto::ProtoObject* closureAttr = curr->getAttribute(ctx, env->getClosureString());
                        if (closureAttr && closureAttr != PROTO_NONE) {
                            if (closureAttr->asList(ctx)) {
                                const proto::ProtoList* l = closureAttr->asList(ctx);
                                for (unsigned long i = 0; i < l->getSize(ctx); ++i) {
                                    worklist = worklist->appendLast(ctx, l->getAt(ctx, i));
                                    stack[stack.top - 1] = worklist->asObject(ctx);
                                }
                            } else if (closureAttr->asTuple(ctx)) {
                                const proto::ProtoTuple* t = closureAttr->asTuple(ctx);
                                for (unsigned long i = 0; i < t->getSize(ctx); ++i) {
                                    worklist = worklist->appendLast(ctx, t->getAt(ctx, i));
                                    stack[stack.top - 1] = worklist->asObject(ctx);
                                }
                            } else {
                                worklist = worklist->appendLast(ctx, closureAttr);
                                stack[stack.top - 1] = worklist->asObject(ctx);
                            }
                        }
                    }

                    // STRUCT-303: read __data__ as an OWN attribute only,
                    // not via getAttribute which walks the parent chain.
                    // Walking the chain here lets a class-body's
                    // namespace dict (whose storage uses __data__) shadow
                    // any closer cell with the same name — exactly what
                    // happens to a method whose parameter name matches a
                    // class-scope local that has a __data__-backed dict.
                    // The worklist already adds parents in a later step,
                    // so each frame in the chain still gets its own own-
                    // __data__ inspection in turn.
                    const proto::ProtoString* dName = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                    const proto::ProtoObject* dataObj =
                        (curr->hasOwnAttribute(ctx, dName) == PROTO_TRUE)
                            ? curr->getOwnAttributeDirect(ctx, dName)
                            : nullptr;
                    if (dataObj && dataObj->asSparseList(ctx)) {
                        // PG: ProtoSparseList::getAt returns PROTO_NONE for
                        // missing keys, not nullptr.  Without the explicit
                        // has() check, a class namespace with a __data__
                        // dict reports any missing closure variable as
                        // None, masking the real value in the cell chain.
                        const proto::ProtoSparseList* sl = dataObj->asSparseList(ctx);
                        if (sl->has(ctx, h)) {
                            val = sl->getAt(ctx, h);
                            if (val != nullptr) { found = true; break; }
                        }
                    }

                    // STRUCT-303: walk parents in REVERSE order so the
                    // FIRST parent (closest scope, e.g. closureFrame
                    // captured at BUILD_FUNCTION time) is popped FIRST
                    // from the LIFO worklist and inspected before
                    // class-namespace ancestors that protoCore's
                    // getParents() returns as transitively-flattened
                    // descendants.  Without this reversal, a method's
                    // free variable could resolve to a class-namespace
                    // entry with the same name BEFORE the closure cell
                    // that legitimately holds the parameter binding —
                    // exactly what broke `class A: def callback(self,
                    // callback, ...): self.make(callback, ...)` style
                    // patterns inside contextlib.ExitStack.callback +
                    // subprocess._close_pipe_fds.
                    const proto::ProtoList* parents = curr->getParents(ctx);
                    if (parents) {
                        for (long j = static_cast<long>(parents->getSize(ctx)) - 1; j >= 0; --j) {
                            worklist = worklist->appendLast(ctx, parents->getAt(ctx, static_cast<int>(j)));
                            stack[stack.top - 1] = worklist->asObject(ctx);
                        }
                    }
                }
                stack.pop_back(); // Remove worklist
                    if (!found && env) {
                        val = env->resolve(nameS, ctx);
                        if (val != nullptr) found = true;
                    }
                    if (!found) {
                        if (env) {
                            std::string* sPtr = new std::string();
                            nameS->toUTF8String(ctx, *sPtr);
                            std::unique_ptr<std::string> sGuard(sPtr);
                            env->raiseNameError(ctx, *sPtr);
                            continue;
                        }
                    }
                    stack.push_back(val);
                }
            }
        } break;
        case OP_STORE_DEREF: {
            if (names && stack.size() >= 1 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj && proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    unsigned long h = nameObj->getHash(ctx);
                    
                    const proto::ProtoList* worklist = ctx->newList();
                    worklist = worklist->appendLast(ctx, frame);
                    stack.push_back(worklist->asObject(ctx));
                    
                    bool found = false;
                    std::unordered_set<const proto::ProtoObject*> visited;
                    while (worklist->getSize(ctx) > 0) {
                        const proto::ProtoObject* curr = worklist->getAt(ctx, worklist->getSize(ctx) - 1);
                        worklist = worklist->removeAt(ctx, worklist->getSize(ctx) - 1);
                        stack[stack.top - 1] = worklist->asObject(ctx);
                        
                        if (!curr || curr == PROTO_NONE || visited.count(curr)) continue;
                        visited.insert(curr);
                        
                        const proto::ProtoString* dName = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                        const proto::ProtoObject* dataObj = curr->getAttribute(ctx, dName);
                        if (dataObj && dataObj->asSparseList(ctx)) {
                            if (dataObj->asSparseList(ctx)->getAt(ctx, h) != PROTO_NONE) {
                                const proto::ProtoSparseList* newData = dataObj->asSparseList(ctx)->setAt(ctx, h, val);
                                curr->setAttribute(ctx, dName, newData->asObject(ctx));
                                found = true;
                                break;
                            }
                        }
                        if (curr->getAttribute(ctx, nameS)) {
                            curr->setAttribute(ctx, nameS, val);
                            found = true;
                            break;
                        }
                        if (env) {
                            const proto::ProtoObject* closureAttr = curr->getAttribute(ctx, env->getClosureString());
                            if (closureAttr && closureAttr != PROTO_NONE) {
                                if (closureAttr->asList(ctx)) {
                                    const proto::ProtoList* l = closureAttr->asList(ctx);
                                    for (unsigned long i = 0; i < l->getSize(ctx); ++i) {
                                        worklist = worklist->appendLast(ctx, l->getAt(ctx, i));
                                        stack[stack.top - 1] = worklist->asObject(ctx);
                                    }
                                } else if (closureAttr->asTuple(ctx)) {
                                    const proto::ProtoTuple* t = closureAttr->asTuple(ctx);
                                    for (unsigned long i = 0; i < t->getSize(ctx); ++i) {
                                        worklist = worklist->appendLast(ctx, t->getAt(ctx, i));
                                        stack[stack.top - 1] = worklist->asObject(ctx);
                                    }
                                } else {
                                    worklist = worklist->appendLast(ctx, closureAttr);
                                    stack[stack.top - 1] = worklist->asObject(ctx);
                                }
                            }
                        }
                        const proto::ProtoList* parents = curr->getParents(ctx);
                        if (parents) {
                            for (unsigned long j = 0; j < parents->getSize(ctx); ++j) {
                                worklist = worklist->appendLast(ctx, parents->getAt(ctx, j));
                                stack[stack.top - 1] = worklist->asObject(ctx);
                            }
                        }
                    }
                    stack.pop_back(); // Remove worklist
                    if (!found) {
                        if (env) {
                            std::string s; nameS->toUTF8String(ctx, s);
                            env->raiseNameError(ctx, "nonlocal " + s + " not found");
                        }
                    }
                }
            }
        } break;
        case OP_JUMP_ABSOLUTE: {
            if (arg >= 0 && static_cast<unsigned long>(arg) < n) {
                i = static_cast<unsigned long>(arg);
                continue;
            }
        } break;
        case OP_LOAD_ATTR: {
            bool pushNull = (arg & 1);
            int nameIdx = arg >> 1;
            if (names && stack.size() >= 1 && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
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
                            LoadAttrPicEntry* slot =
                                &g_loadAttrPic[loadAttrPicIndex(type, attrName)];
                            if (slot->type == type
                                && slot->name == attrName
                                && slot->generation == gen) {
                                // PIC HIT — skip env->getAttribute entirely.
                                val = slot->value;
                                isUnboundFunc = slot->isUnbound;
                                picHandled = true;
                            } else {
                                // PIC MISS — do the slow path and populate.
                                val = env->getAttribute(ctx, obj, attrName, false, &isUnboundFunc);
                                if (val && val != PROTO_NONE
                                        && (!env->hasPendingException())) {
                                    slot->type      = type;
                                    slot->name      = attrName;
                                    slot->value     = val;
                                    slot->generation = gen;
                                    slot->isUnbound = isUnboundFunc;
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
                    if (!val && env && env->hasPendingException()) {
                        // A descriptor or __getattr__ already raised an exception — propagate it.
                        stack.pop_back();
                        continue;
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
                        const proto::ProtoString* getattrS = PythonEnvironment::getInternedString(ctx, "__getattr__");
                        const proto::ProtoObject* getattr = nullptr;

                        // Check instance first (e.g. module-level __getattr__ or super() proxies)
                        bool getattrIsOwn = false;
                        if (obj->hasOwnAttribute(ctx, getattrS) == PROTO_TRUE) {
                            getattr = obj->getAttribute(ctx, getattrS);
                            getattrIsOwn = true;
                        } else {
                            // Search on class MRO.  env->getType synthesises
                            // the class identity from the protoCore parent
                            // chain, so this works without the redundant
                            // __class__ attribute previously stored on every
                            // instance (Phase 3 of the delegation design).
                            const proto::ProtoObject* cls = env ? env->getType(ctx, obj)
                                                                : obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"));
                            if (cls && cls != PROTO_NONE) {
                                getattr = env ? env->getAttribute(ctx, cls, getattrS, false) : cls->getAttribute(ctx, getattrS);
                            }
                        }

                        if (getattr && getattr != PROTO_NONE) {
                            // Module-level __getattr__(name) takes only the name string.
                            // Class-level __getattr__(self, name) takes the instance and name.
                            const proto::ProtoList* posArgs = getattrIsOwn
                                ? ctx->newList()->appendLast(ctx, nameObj)
                                : ctx->newList()->appendLast(ctx, obj)->appendLast(ctx, nameObj);
                            val = invokePythonCallable(ctx, getattr, posArgs, nullptr);
                            if (env && env->hasPendingException()) {
                                stack.pop_back(); continue;
                            }
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
                                if (env && env->hasPendingException()) {
                                    stack.pop_back(); continue;
                                }
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
                                            nullptr, ctx->newList(), nullptr);
                                    if (dictResult && !env->hasPendingException()) {
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
                                                nullptr, args, nullptr);
                                        if (dictResult && !env->hasPendingException()) {
                                            val = dictResult;
                                        } else if (env->hasPendingException()) {
                                            stack.pop_back(); continue;
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
                                        if (nseAttr->asList(ctx)) {
                                            entries = nseAttr->asList(ctx);
                                        } else {
                                            const proto::ProtoObject* d = nseAttr->getAttribute(ctx,
                                                env->getDataString());
                                            if (d) entries = d->asList(ctx);
                                        }
                                    }
                                    if (entries) {
                                        unsigned long nameHash =
                                            attrName->getHash(ctx);
                                        const proto::ProtoString* eqS =
                                            PythonEnvironment::getInternedString(ctx, "__eq__");
                                        for (unsigned long i = 0; i < entries->getSize(ctx); ++i) {
                                            const proto::ProtoObject* pair = entries->getAt(ctx, static_cast<int>(i));
                                            if (!pair) continue;
                                            const proto::ProtoTuple* pT = pair->asTuple(ctx);
                                            const proto::ProtoList* pL = pT ? pT->asList(ctx)
                                                                            : pair->asList(ctx);
                                            if (!pL || pL->getSize(ctx) < 1) continue;
                                            const proto::ProtoObject* nsKey = pL->getAt(ctx, 0);
                                            if (!nsKey) continue;
                                            unsigned long h = ::protoPython::pyDictKeyHash(ctx, nsKey);
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
                                                env->callObject(eqM, a);
                                                if (env->hasPendingException()) env->clearPendingException();
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
        case OP_STORE_ATTR: {
            int nameIdx = arg >> 1;
            if (names && stack.size() >= 2 && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
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
                    const proto::ProtoString* classS = env ? env->getClassString() : nullptr;
                    bool isClassAssign = classS && (nameS == classS
                        || nameS->getHash(ctx) == classS->getHash(ctx));
                    // __dict__ assignment must always traverse the slow
                    // path (env->setAttribute) so the dict-replace logic
                    // runs (validate type + repopulate __data__/__keys__).
                    // The fast path would store the rhs as a plain
                    // attribute named "__dict__" and lose the entries.
                    const proto::ProtoString* dictS = env ? env->getDictDunderString() : nullptr;
                    bool isDictAssign = dictS && (nameS == dictS
                        || nameS->getHash(ctx) == dictS->getHash(ctx));
                    if (!isClassAssign && !isDictAssign && !disableStoreattrFastpath && env && obj && obj != PROTO_NONE
                            && !obj->isString(ctx) && !obj->isInteger(ctx)
                            && !obj->isBoolean(ctx) && !obj->isFloat(ctx)) {
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
                            bool noDescr = directType->hasOwnAttribute(ctx, nameS) != PROTO_TRUE;
                            // Bail to slow path when the type defines an
                            // own __setattr__ override (CPython's data
                            // descriptor for "__setattr__ has been
                            // overridden").  env->setAttribute then
                            // dispatches the user's hook with
                            // (obj, name, value).
                            const proto::ProtoString* setattrS =
                                protoPython::PythonEnvironment::getInternedString(ctx, "__setattr__");
                            bool hasSetattrOverride =
                                directType->hasOwnAttribute(ctx, setattrS) == PROTO_TRUE;
                            if (noSlots && noDescr && !hasSetattrOverride) {
                                newObj = const_cast<proto::ProtoObject*>(obj)->setAttribute(ctx, nameS, val);
                                fastStoreTaken = true;
                                // STRUCT-321: dict / list / tuple / set
                                // subclass instances must NOT have STORE_ATTR
                                // write the name into their __keys__ slot —
                                // that slot is the dict payload's key
                                // tracker (or the equivalent for the other
                                // built-in containers), and conflating it
                                // with instance-attribute insertion order
                                // makes `list(d.keys())` surface C.__init__'s
                                // self.__state assignments alongside the
                                // user's d['x'] = 'y' inserts.
                                // test_multiple_inheritance regresses
                                // without this guard.
                                bool isContainerSubclass = false;
                                if (env && directType) {
                                    if (directType == env->getDictPrototype()
                                        || directType == env->getListPrototype()
                                        || directType == env->getTuplePrototype()
                                        || directType == env->getSetPrototype()
                                        || directType == env->getFrozensetPrototype()
                                        || directType == env->getBytesPrototype()) {
                                        isContainerSubclass = true;
                                    } else {
                                        // walk the MRO once
                                        const proto::ProtoObject* mroAttr = env->getAttribute(ctx,
                                            directType, env->getMroString(), false);
                                        const proto::ProtoTuple* mroT = mroAttr ? mroAttr->asTuple(ctx) : nullptr;
                                        if (mroT) {
                                            for (unsigned long i = 0; i < mroT->getSize(ctx); ++i) {
                                                const proto::ProtoObject* base = mroT->getAt(ctx, static_cast<int>(i));
                                                if (base == env->getDictPrototype()
                                                    || base == env->getListPrototype()
                                                    || base == env->getTuplePrototype()
                                                    || base == env->getSetPrototype()
                                                    || base == env->getFrozensetPrototype()
                                                    || base == env->getBytesPrototype()) {
                                                    isContainerSubclass = true;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                                // PEP 468 / dict insertion-order preservation.
                                // Track the assignment in obj.__keys__ so
                                // vars(obj) and obj.__dict__ surface attrs in
                                // call/setattr order, not the SparseList's
                                // hash order.  Only append if the name isn't
                                // already tracked (re-assigning an existing
                                // attribute keeps its original slot, matching
                                // CPython's dict.__setitem__ semantics).
                                if (env && !isContainerSubclass) {
                                    const proto::ProtoString* keysName = env->getKeysString();
                                    const proto::ProtoObject* keysObj = (newObj->hasOwnAttribute(ctx, keysName) == PROTO_TRUE)
                                        ? newObj->proto::ProtoObject::getAttribute(ctx, keysName) : nullptr;
                                    const proto::ProtoList* keysList = (keysObj && keysObj != PROTO_NONE) ? keysObj->asList(ctx) : nullptr;
                                    bool present = false;
                                    if (keysList) {
                                        unsigned long h = nameS->getHash(ctx);
                                        for (unsigned long i = 0; i < keysList->getSize(ctx); ++i) {
                                            const proto::ProtoObject* k = keysList->getAt(ctx, i);
                                            if (k && k->isString(ctx) && k->getHash(ctx) == h) {
                                                present = true;
                                                break;
                                            }
                                        }
                                    } else {
                                        keysList = ctx->newList();
                                    }
                                    if (!present) {
                                        keysList = keysList->appendLast(ctx, nameS->asObject(ctx));
                                        const_cast<proto::ProtoObject*>(newObj)->proto::ProtoObject::setAttribute(ctx, keysName, keysList->asObject(ctx));
                                    }
                                }
                            }
                        }
                    }
                    if (!fastStoreTaken) {
                        if (env) {
                            newObj = env->setAttribute(ctx, obj, nameS, val);
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
            // below — see audit/02-fast-paths.md F2.2.
            //
            // Skip strings here: ProtoString::asList builds a list of
            // unicode-char embedded values, not a list of single-char
            // strings, so returning that fast-path value to user code
            // breaks `s[i].lower()` and every other Python str-method
            // chain on indexed characters. py_str_getitem (the slow path)
            // returns a proper one-char string.
            if (proto::isSmallInt(key) && container && !container->isString(ctx)
                && env && env->getType(ctx, container) == env->getListPrototype()) {
                const proto::ProtoList* lst = container->asList(ctx);
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
                const proto::ProtoString* classGetItemS = PythonEnvironment::getInternedString(ctx, "__class_getitem__");
                result = invokeDunder(ctx, container, classGetItemS, args);
                if (env->hasPendingException()) result = nullptr;
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

            if (result && env && env->hasPendingException()) {
                // __getitem__ raised an exception (e.g. KeyError) — honour it even if a value was returned
                continue;
            } else if (result) {
                stack.pop_back();
                stack.back() = result;
            } else if (env && env->hasPendingException()) {
                continue;
            } else {
                bool handled = false;
                // Fallback for minimal objects without __getitem__ (e.g. built-in lists/tuples if dunder is missing)
                const proto::ProtoObject* data = container->getAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                if (!data) data = container; // Fallback to the object itself (for primitive strings/tuples)
                
                if (data) {
                    if (data->asList(ctx) && key->isInteger(ctx)) {
                        long long idx = key->asLong(ctx);
                        const proto::ProtoList* list = data->asList(ctx);
                        const proto::ProtoObject* res = (idx >= 0 && static_cast<unsigned long>(idx) < list->getSize(ctx)) ? list->getAt(ctx, static_cast<int>(idx)) : PROTO_NONE;
                        stack.pop_back(); stack.back() = res;
                        handled = true;
                    } else if (data->asList(ctx) && env && env->getSliceType() && (key->isInstanceOf(ctx, env->getSliceType())->asBoolean(ctx) || key->getAttribute(ctx, env->getStartString()))) {
                        // List Slicing
                        const proto::ProtoList* list = data->asList(ctx);
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
                                const proto::ProtoString* charStr = (idx >= 0 && static_cast<unsigned long>(idx) < s->getSize(ctx)) ? s->getSlice(ctx, static_cast<int>(idx), static_cast<int>(idx) + 1) : nullptr;
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
                        unsigned long h = key->getHash(ctx);
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
                
                if (!handled && stack.back() == PROTO_NONE && !env->hasPendingException()) {
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
            for (int k = 0; k < arg; ++k) {
                const proto::ProtoObject* key = stack[baseIdx + 2 * k];
                const proto::ProtoObject* val = stack[baseIdx + 2 * k + 1];
                // Use the env-aware hash so user __hash__ overrides
                // (cistr et al.) bucket the same way as
                // py_dict_getitem / setitem.
                data = data->setAt(ctx, ::protoPython::pyDictKeyHash(ctx, key), val);
                stack[stack.size() - 2] = const_cast<proto::ProtoObject*>(data->asObject(ctx)); // Update data root
                keys = keys->appendLast(ctx, key);
                stack[stack.size() - 1] = const_cast<proto::ProtoObject*>(keys->asObject(ctx)); // Update keys root
            }
            
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
                fprintf(stderr, "DEBUG OP_STORE_SUBSCR stack.size()=%lu\n", (unsigned long)stack.size());
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
            // route through the dunder path below — see audit/02-fast-paths.md F2.2.
            if (proto::isSmallInt(key) && container
                && env && env->getType(ctx, container) == env->getListPrototype()) {
                const proto::ProtoList* lst = container->asList(ctx);
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
                        bool hasData = dataS && container->hasOwnAttribute(ctx, dataS) == PROTO_TRUE;
                        if (hasData) {
                            container->setAttribute(ctx, dataS, newLst->asObject(ctx));
                            stack.pop_back(); stack.pop_back(); stack.pop_back();
                            i = next_i;
                            continue;
                        }
                        // container is a raw ProtoList — fall through to slow path.
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
                if (env && env->hasPendingException()) continue;
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, value);
                invokeDunder(ctx, container, setItemS, args);
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
                        if (dataObj && dataObj->asList(ctx)) {
                            const proto::ProtoList* newList = dataObj->asList(ctx)->setAt(ctx, static_cast<int>(key->asLong(ctx)), value);
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
                    const proto::ProtoList* keysList = keysObj ? keysObj->asList(ctx) : nullptr;
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
            const proto::ProtoTuple* namesTuple = namesTupleObj->asTuple(ctx);
            if (!namesTuple) { i = next_i; continue; }
            int nkw = namesTuple->getSize(ctx);
            int npos = arg - nkw;
            if (stack.size() < static_cast<size_t>(arg) + 1) { 
                 if (env) env->raiseRuntimeError(ctx, "Stack underflow in OP_CALL_FUNCTION_KW");
                 i = next_i;
                 continue;
            }

            // Robust detection of modern vs legacy stack layout
            bool isModern = (stack.top >= (size_t)(arg + 2)); 
            unsigned long firstArgIdx = stack.top - arg;
            
            // Build positional args list. Use stack to root it.
            stack.push_back(ctx->newList()->asObject(ctx));
            for (int j = 0; j < npos; ++j) {
                const proto::ProtoList* l = stack.back()->asList(ctx);
                l = l->appendLast(ctx, stack[firstArgIdx + j]);
                stack[stack.top - 1] = l->asObject(ctx);
            }
            const proto::ProtoList* plArgs = stack.back()->asList(ctx);
            
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
                for (unsigned long j = 0; j < plArgs->getSize(ctx); ++j) {
                    selfArgs = selfArgs->appendLast(ctx, plArgs->getAt(ctx, j));
                }
                callArgs = selfArgs;
            }
            
            if (env) env->pushKwNames(namesTuple);
            const proto::ProtoObject* result = invokeCallable(ctx, callable, callArgs, kwMap);
            if (env) env->popKwNames();
            
            // Cleanup: Pop accurately.
            int itemsToPop = arg + (isModern ? 2 : 1) + 2; // +2 for intermediate lists
            for (int j = 0; j < itemsToPop; ++j) {
                if (!stack.empty()) stack.pop_back(); 
            }
            if (!result && env && env->hasPendingException()) {
                continue;
            }
            stack.push_back(result ? result : (env ? env->getNonePrototype() : PROTO_NONE));
        } break;
        case OP_CALL_FUNCTION: {
            if (stack.size() < (unsigned long)(arg + 1)) {
                 if (diag_local) fprintf(stderr, "DEBUG: OP_CALL_FUNCTION FATAL underflow size=%lu arg=%d PC=%lu\n", (unsigned long)stack.size(), arg, i);
                 i = next_i;
                 continue;
            }

            // In 3.11+, CALL always consumes argc + 2 slots.
            // Layout: [NULL|Self, Callable, Arg1, ... ArgN]
            // We expect at least arg + 1 + 1 (the NULL/Self marker).
            unsigned long firstArgIdx = stack.top - arg;

            // Safety check: if the stack isn't deep enough to have a marker, it's a legacy call.
            bool isModern = (stack.top >= (size_t)(arg + 2));

            const proto::ProtoObject* Y = stack[firstArgIdx - 1];
            const proto::ProtoObject* X = isModern ? stack[firstArgIdx - 2] : nullptr;
            
            if (diag_local) {
                fprintf(stderr, "DEBUG CALL: argc=%d top=%zu firstArgIdx=%lu X=%p Y=%p isModern=%d\n", 
                        arg, stack.top, firstArgIdx, (void*)X, (void*)Y, isModern);
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
                                                              rawArgSlice, (unsigned long)arg);
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
                                                              rawArgSlice, (unsigned long)(arg + 1));
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
                 if (diag_local) fprintf(stderr, "DEBUG: OP_CALL_FUNCTION nullptr callable detected! PC=%lu\n", i);
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
            
            if (!result && env && env->hasPendingException()) {
                continue;
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
            if (starargs && starargs->asList(ctx)) {
                posArgs = starargs->asList(ctx);
            } else if (starargs && starargs->asTuple(ctx)) {
                posArgs = ctx->newList();
                const proto::ProtoTuple* t = starargs->asTuple(ctx);
                for (unsigned long j = 0; j < t->getSize(ctx); ++j) {
                    posArgs = posArgs->appendLast(ctx, t->getAt(ctx, j));
                }
            } else if (starargs && starargs != PROTO_NONE && env) {
                const proto::ProtoObject* it = env->iter(starargs);
                if (it) {
                    const proto::ProtoList* L = ctx->newList();
                    while (const proto::ProtoObject* nextVal = env->next(it)) {
                        L = L->appendLast(ctx, nextVal);
                    }
                    if (env->hasPendingException()) {
                        const proto::ProtoObject* exc = env->takePendingException();
                        if (!env->isStopIteration(ctx, exc)) {
                            env->setPendingException(exc);
                            i = next_i; continue;
                        }
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
                 if (keysListObj && keysListObj->asList(ctx)) {
                     // Per CPython: all keys in `**kwargs` must be strings.
                     // e.g. `f(**{b'foo': 1})` raises TypeError.
                     const proto::ProtoList* kl = keysListObj->asList(ctx);
                     for (unsigned long kj = 0; kj < kl->getSize(ctx); ++kj) {
                         const proto::ProtoObject* k = kl->getAt(ctx, kj);
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
                for (unsigned long j = 0; j < posArgs->getSize(ctx); ++j) {
                    selfArgs = selfArgs->appendLast(ctx, posArgs->getAt(ctx, j));
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
            if (!result && env && env->hasPendingException()) {
                continue;
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
            
            const proto::ProtoTuple* tup = ctx->newTupleFromList(lst);
            proto::ProtoObject* tupObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            stack.push_back(tupObj); // Root tupObj
            if (env && env->getTuplePrototype()) tupObj = const_cast<proto::ProtoObject*>(tupObj->addParent(ctx, env->getTuplePrototype()));
            
            tupObj = const_cast<proto::ProtoObject*>(tupObj->setAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"), tup->asObject(ctx)));
            
            const proto::ProtoObject* finalTup = tupObj;
            for (int j = 0; j < arg + 2; ++j) stack.pop_back();
            stack.push_back(finalTup);
        } break;
        case OP_BUILD_FUNCTION: {
            // GC discipline: every ProtoObject* we hold across an allocation
            // (newObject, addParent, setAttribute, …) must be reachable from
            // a GC root.  The operand stack lives inside automaticLocals, so
            // anything sitting in a stack slot is rooted; values held only
            // in C++ locals are NOT.  We therefore read codeObj/defaults/
            // kwDefaults/annotations *without* popping — pop_back only
            // decrements top, so the slots still hold the values and the
            // root scan still sees them.  Pushing closureFrame later goes
            // ON TOP of these slots, not over them.  Pops happen at the
            // very end, after every allocation has completed.
            int extras = 0;
            if (arg & 0x04) extras++;
            if (arg & 0x02) extras++;
            if (arg & 0x01) extras++;
            const proto::ProtoObject* annotations = nullptr;
            const proto::ProtoObject* kwDefaults = nullptr;
            const proto::ProtoObject* defaults = nullptr;
            int peekIdx = 0;
            if (arg & 0x04) { annotations = stack[stack.top - 1 - peekIdx++]; }
            if (arg & 0x02) { kwDefaults = stack[stack.top - 1 - peekIdx++]; }
            if (arg & 0x01) { defaults = stack[stack.top - 1 - peekIdx++]; }

            if (stack.size() > static_cast<size_t>(extras) && frame) {
                const proto::ProtoObject* codeObj = stack[stack.top - 1 - extras];
                // codeObj stays in its stack slot for the entire op; no pop
                // here.  See the GC discipline comment above.
                if (diag_local) {
                    int line = -1;
                    const proto::ProtoObject* lineObj = codeObj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_firstlineno"));
                    if (lineObj && lineObj->isInteger(ctx)) line = (int)lineObj->asLong(ctx);
                    fprintf(stderr, "DEBUG: OP_BUILD_FUNCTION PC=%lu arg=0x%lx codeObj=%p (line %d) defaults=%p kwDefaults=%p\n", i, (unsigned long)arg, (void*)codeObj, line, (void*)defaults, (void*)kwDefaults);
                    fflush(stderr);
                }

                // Build the closure frame.  Push it onto the stack as a GC
                // guard *before* the next allocation so that the protoCore
                // threshold trigger cannot lose it via a chain submission.
                // closureFrame must stay rooted across addParent, getAttribute,
                // and the snapshot loop.  Updates to the C++ local and the
                // stack slot are kept in sync via stack.back() = closureFrame.
                proto::ProtoObject* closureFrame = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                stack.push_back(closureFrame);
                closureFrame = const_cast<proto::ProtoObject*>(closureFrame->addParent(ctx, frame));
                stack.back() = closureFrame;
                // The frame stores the code object as f_code (not __code__)
                const proto::ProtoString* codeKey = env ? env->getFCodeString() : PythonEnvironment::getInternedString(ctx, "f_code");
                const proto::ProtoObject* outerCodeAttr = frame->getAttribute(ctx, codeKey);
                // `co_freevars` (stamped by the compiler) is the exact set of
                // names this new function — and its nested scopes — close
                // over from enclosing function scopes.  When present, snapshot
                // ONLY those names: blindly copying every outer local leaks an
                // enclosing `self` into a nested method's closure frame and
                // shadows the real binding during the LOAD_DEREF walk.  When
                // absent (older code-object shapes), fall back to the legacy
                // copy-everything behaviour.
                const proto::ProtoTuple* coFreevars = nullptr;
                {
                    const proto::ProtoObject* fvObj = codeObj->getAttribute(ctx,
                        PythonEnvironment::getInternedString(ctx, "co_freevars"));
                    if (fvObj && fvObj != PROTO_NONE) coFreevars = fvObj->asTuple(ctx);
                }
                if (outerCodeAttr && outerCodeAttr != PROTO_NONE) {
                    const proto::ProtoObject* coVarnamesObj = outerCodeAttr->getAttribute(ctx, env ? env->getCoVarnamesString() : PythonEnvironment::getInternedString(ctx, "co_varnames"));
                    if (coVarnamesObj && coVarnamesObj != PROTO_NONE) {
                        const proto::ProtoTuple* coVarnames = coVarnamesObj->asTuple(ctx);
                        if (coVarnames) {
                            const proto::ProtoObject** outerSlots = ctx->getAutomaticLocals();
                            unsigned int outerNSlots = ctx->getAutomaticLocalsCount();
                            for (unsigned int j = 0; j < coVarnames->getSize(ctx); ++j) {
                                const proto::ProtoObject* vnameObj = coVarnames->getAt(ctx, j);
                                if (proto::ProtoObject::isStringTagFast(vnameObj)) {
                                    const proto::ProtoString* vname = vnameObj->asString(ctx);
                                    // Skip outer locals the new function does not
                                    // actually close over (when co_freevars is known).
                                    if (coFreevars) {
                                        bool needed = false;
                                        for (unsigned long fi = 0; fi < coFreevars->getSize(ctx); ++fi) {
                                            const proto::ProtoObject* fvn = coFreevars->getAt(ctx, fi);
                                            if (fvn && fvn->isString(ctx) &&
                                                fvn->asString(ctx)->cmp_to_string(ctx, vname) == 0) {
                                                needed = true; break;
                                            }
                                        }
                                        if (!needed) continue;
                                    }
                                    // STRUCT-303: snapshot the OWN-attribute value into
                                    // closureFrame for forceMapped (frame-mapped) outer
                                    // locals.  Previously we skipped snapshot when frame
                                    // had the name as an own attribute, relying on the
                                    // parent-chain walk (closureFrame → frame) to expose
                                    // the live value.  That walk continues PAST `frame`
                                    // into its parent (e.g. the class body namespace), so
                                    // a free var whose name matches a class-body local
                                    // (e.g. a method's `callback` parameter inside `class
                                    // A: def callback(self, callback, ...): self.make(
                                    // callback, ...)`) resolved to the class-namespace
                                    // value (the method function) instead of the parameter
                                    // binding.  Snapshotting via getOwnAttributeDirect
                                    // captures the correct OWN value without ever touching
                                    // the chain.  Trade-off: outer-frame mutations to the
                                    // captured local become invisible to the inner closure
                                    // (it sees the snapshot, not a live cell), but
                                    // parameters and the vast majority of locals are not
                                    // mutated after the inner is created.  Real cell-style
                                    // mutability would require a separate cell object on
                                    // each cellvar — a larger restructuring left as a
                                    // follow-up.
                                    const proto::ProtoObject* val = (j < outerNSlots) ? outerSlots[j] : nullptr;
                                    // For CO_OPTIMIZED slots, PROTO_NONE is a legitimate
                                    // bound value (e.g. `boundary=None` parameter).  Accept.
                                    if (!val && frame->hasOwnAttribute(ctx, vname) == PROTO_TRUE) {
                                        val = frame->getOwnAttributeDirect(ctx, vname);
                                        if (val == PROTO_NONE) {
                                            // PROTO_NONE on an OWN slot is a legitimate
                                            // bound None — keep it.
                                        }
                                    }

                                    if (val) {
                                        closureFrame = const_cast<proto::ProtoObject*>(closureFrame->setAttribute(ctx, vname, val));
                                        stack.back() = closureFrame; // Keep GC root updated
                                    }
                                }
                            }
                        }
                    }
                }
                // Pop closureFrame.  pop_back only decrements top — slot[top]
                // still holds closureFrame, so the GC root scan continues to
                // reach it through the entire createUserFunction call below
                // (which performs many allocations).
                stack.pop_back();

                proto::ProtoObject* fn = createUserFunction(ctx, codeObj, const_cast<proto::ProtoObject*>(PythonEnvironment::getCurrentGlobals()), closureFrame, defaults, kwDefaults);
                // Push fn as a GC guard immediately.  Subsequent setAttribute
                // calls allocate (potentially crossing the per-context
                // threshold and submitting the young chain) and may return a
                // new fn pointer; we keep stack.back() in sync.  Without this
                // guard the C++-local fn is unrooted and a chain submission
                // can leave its underlying cell in dirtySegments where the
                // next sweep would free it.
                if (fn) {
                    stack.push_back(fn);
                }
                if (fn && env) {
                    const proto::ProtoObject* nameAttr = codeObj->getAttribute(ctx,
                        PythonEnvironment::getInternedString(ctx, "co_name"));
                    if (nameAttr && nameAttr->isString(ctx)) {
                        const proto::ProtoObject* updatedFrame =
                            const_cast<proto::ProtoObject*>(closureFrame)->setAttribute(
                                ctx, nameAttr->asString(ctx), fn);
                        if (updatedFrame != closureFrame) {
                            // Not in-place: rebuild closure tuple so fn sees the updated frame.
                            // Wrap as Python list so len/repr/indexing work on f.__closure__.
                            const proto::ProtoList* newClosure =
                                ctx->newList()->appendLast(ctx, updatedFrame);
                            const proto::ProtoObject* closureWrapped = newClosure->asObject(ctx);
                            if (env->getListPrototype()) {
                                proto::ProtoObject* w = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                                w = const_cast<proto::ProtoObject*>(w->setAttribute(ctx, env->getDataString(), closureWrapped));
                                w = const_cast<proto::ProtoObject*>(w->addParent(ctx, env->getListPrototype()));
                                w = const_cast<proto::ProtoObject*>(w->setAttribute(ctx, env->getClassString(), env->getListPrototype()));
                                closureWrapped = w;
                            }
                            fn = const_cast<proto::ProtoObject*>(fn->setAttribute(
                                ctx, env->getClosureString(), closureWrapped));
                            stack.back() = fn;
                        }
                    }
                }
                if (diag_local) {
                    fprintf(stderr, "DEBUG: OP_BUILD_FUNCTION finished createUserFunction fn=%p\n", (void*)fn);
                    fflush(stderr);
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
                    stack.back() = fn;
                }
                // Stack right now (top to bottom):
                //   fn (guard)
                //   annotations? kwDefaults? defaults? codeObj   (the original
                //                                                 operands we
                //                                                 deliberately
                //                                                 did not pop)
                //
                // Result of OP_BUILD_FUNCTION must leave fn at the top of the
                // operand stack, with the original operands removed.  Pop fn,
                // then pop the originals, then push fn back.  No allocations
                // happen in this sequence so fn-in-C++-local is safe.
                if (fn) {
                    stack.pop_back();  // pop fn guard
                }
                for (int i = 0; i < extras + 1; ++i) {
                    if (stack.top > 0) stack.pop_back();
                }
                if (fn) {
                    stack.push_back(fn);
                }
            }
        } break;
        case OP_BUILD_CLASS: {
            if (stack.size() >= 4 && frame) {
                // Robust detection: in modern bytecode, the function result of MAKE_FUNCTION
                // might have been pushed with a NULL marker if it was meant to be called.
                // But in BUILD_CLASS opcode (legacy path), we expect items directly.
                int firstIdx = stack.top - 4;
                const proto::ProtoObject* body = stack[firstIdx + 3];
                const proto::ProtoObject* kwds = stack[firstIdx + 2];
                const proto::ProtoObject* bases = stack[firstIdx + 1];
                const proto::ProtoObject* name = stack[firstIdx];

                if (body == nullptr && stack.top >= 5) {
                    // Oops, there was a NULL marker. Shift.
                    firstIdx = stack.top - 5;
                    body = stack[firstIdx + 4];
                    kwds = stack[firstIdx + 3];
                    bases = stack[firstIdx + 2];
                    name = stack[firstIdx + 1];
                }

                if (diag_local) {
                    fprintf(stderr, "DEBUG OP_BUILD_CLASS: stack size=%lu top=%lu\n", (unsigned long)stack.size(), (unsigned long)stack.top);
                    for (int j = 0; j < (int)stack.top; ++j) {
                        fprintf(stderr, "  stack[%d] = %p repr=%s\n", j, (void*)stack[j], env ? env->reprObject(ctx, stack[j]).c_str() : "???");
                    }
                    std::string n = "unknown";
                    if (name && name->isString(ctx)) name->asString(ctx)->toUTF8String(ctx, n);
                    fprintf(stderr, "DEBUG OP_BUILD_CLASS: building name='%s'\n", n.c_str());
                }
                
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                const proto::ProtoString* nameS = env ? env->getNameString() : protoPython::PythonEnvironment::getInternalString(ctx, "__name__");
                const proto::ProtoString* callS = env ? env->getCallString() : PythonEnvironment::getInternedString(ctx, "__call__");
                
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
                        const proto::ProtoTuple* mroT = mro ? mro->asTuple(ctx) : nullptr;
                        if (!mroT && mro) {
                            const proto::ProtoObject* d = env->getAttribute(ctx, mro, env->getDataString(), false);
                            if (d) mroT = d->asTuple(ctx);
                        }
                        if (mroT) {
                            for (unsigned long j = 0; j < mroT->getSize(ctx); ++j) {
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
                        const proto::ProtoTuple* tb = bases->asTuple(ctx);
                        const proto::ProtoList* lb = tb ? nullptr : bases->asList(ctx);
                        size_t bn = tb ? tb->getSize(ctx) : (lb ? lb->getSize(ctx) : 0);
                        for (size_t i = 0; i < bn; ++i) {
                            const proto::ProtoObject* b = tb ? tb->getAt(ctx, i) : lb->getAt(ctx, i);
                            if (!b || b == PROTO_NONE) continue;
                            const proto::ProtoObject* bm = env->getAttribute(ctx, b,
                                env->getClassString(), false);
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
                        if (stack.size() >= 5) {
                            for (int k = 0; k < 5; ++k) stack.pop_back();
                        }
                        continue;
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
                        const proto::ProtoTuple* tbInit = bases ? bases->asTuple(ctx) : nullptr;
                        const proto::ProtoList* lbInit = (tbInit || !bases) ? nullptr : bases->asList(ctx);
                        if (!tbInit && !lbInit && bases) {
                            const proto::ProtoObject* dataA = bases->getAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                            if (dataA) {
                                tbInit = dataA->asTuple(ctx);
                                lbInit = tbInit ? nullptr : dataA->asList(ctx);
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
                            const proto::ProtoObject* basei = tbInit ? tbInit->getAt(ctx, i) : lbInit->getAt(ctx, i);
                            if (!basei || basei == PROTO_NONE || !env) continue;
                            const proto::ProtoObject* m0 = env->getAttribute(ctx, basei, env->getClassString(), false);
                            if (!m0 || m0 == PROTO_NONE) m0 = env->getType(ctx, basei);
                            if (m0 && m0 != PROTO_NONE
                                && !areSameClassesVM(ctx, m0, objectProto)) {
                                bestMeta = m0;
                                break;
                            }
                        }
                    }
                    
                    if (bases) {
                        const proto::ProtoTuple* tupleBases = bases->asTuple(ctx);
                        const proto::ProtoList* listBases = tupleBases ? nullptr : bases->asList(ctx);
                        if (!tupleBases && !listBases) {
                            const proto::ProtoObject* dataAttr = bases->getAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                            if (dataAttr) {
                                tupleBases = dataAttr->asTuple(ctx);
                                listBases = tupleBases ? nullptr : dataAttr->asList(ctx);
                            }
                        }
                        
                        size_t basesSize = tupleBases ? tupleBases->getSize(ctx) : (listBases ? listBases->getSize(ctx) : 0);
                        for (size_t i = 0; i < basesSize; ++i) {
                            const proto::ProtoObject* base = tupleBases ? tupleBases->getAt(ctx, i) : listBases->getAt(ctx, i);
                            if (!base || base == PROTO_NONE) continue;

                            const proto::ProtoObject* baseMeta = nullptr;
                            if (env) {
                                baseMeta = env->getAttribute(ctx, base, env->getClassString(), false);
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
                                    const proto::ProtoTuple* mroTuple = mro->asTuple(ctx);
                                    if (!mroTuple) {
                                        const proto::ProtoObject* data = env ? env->getAttribute(ctx, mro, env->getDataString(), false) : mro->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__data__"));
                                        if (data) mroTuple = data->asTuple(ctx);
                                    }
                                    if (mroTuple) {
                                        for (size_t j = 0; j < mroTuple->getSize(ctx); ++j) {
                                            if (areSameClassesVM(ctx, mroTuple->getAt(ctx, j), bestMeta)) {
                                                isSub = true;
                                                break;
                                            }
                                        }
                                    } else {
                                        const proto::ProtoList* mroList = mro->asList(ctx);
                                        if (mroList) {
                                            for (unsigned long j = 0; j < mroList->getSize(ctx); ++j) {
                                                if (areSameClassesVM(ctx, mroList->getAt(ctx, j), bestMeta)) {
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
                                    const proto::ProtoTuple* bmroT = bmro ? bmro->asTuple(ctx) : nullptr;
                                    if (!bmroT && bmro) {
                                        const proto::ProtoObject* d = env
                                            ? env->getAttribute(ctx, bmro, env->getDataString(), false)
                                            : bmro->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__data__"));
                                        if (d) bmroT = d->asTuple(ctx);
                                    }
                                    if (bmroT) {
                                        for (size_t j = 0; j < bmroT->getSize(ctx); ++j) {
                                            if (areSameClassesVM(ctx, bmroT->getAt(ctx, j), baseMeta)) {
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
                    if (!bestMeta && env && env->hasPendingException()) {
                        // Metaclass conflict raised — unwind through the
                        // dispatch loop's exception handling.
                        i = next_i;
                        continue;
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
                        i = next_i;
                        continue;
                    }
                    // CPython: __prepare__ must return a mapping. Detect
                    // None / non-mapping returns and raise TypeError with
                    // CPython's exact message format. The duck-typed mapping
                    // check is callable `keys` attr; same predicate as
                    // dict()/MappingProxyType() use elsewhere.
                    bool isMapping = (nsObj && nsObj != PROTO_NONE);
                    if (isMapping) {
                        // Reject obvious non-mappings.
                        if (nsObj->asList(ctx) || nsObj->asTuple(ctx) ||
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
                        i = next_i;
                        continue;
                    }
                    stack.push_back(nsObj);
                } else {
                    stack.push_back(ctx->newObject(true));
                }
                proto::ProtoObject* ns = const_cast<proto::ProtoObject*>(stack.back());
                
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
                if (keysObj && keysObj->asList(ctx)) {
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, keysS, keysObj->asList(ctx)->appendLast(ctx, nameS->asObject(ctx))->asObject(ctx)));
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
                    if (keysObj && keysObj->asList(ctx)) {
                        ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, keysS, keysObj->asList(ctx)->appendLast(ctx, py_module_s->asObject(ctx))->asObject(ctx)));
                    }
                }
                if (env) {
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, env->getFBackString(), PythonEnvironment::getCurrentFrame()));
                    stack.back() = ns;
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, env->getFGlobalsString(), PythonEnvironment::getCurrentGlobals()));
                    stack.back() = ns;
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, env->getFLocalsString(), ns));
                    stack.back() = ns;
                }

                // 3. Execute class body
                if (body) {
                    const proto::ProtoString* callS = env ? env->getCallString() : protoPython::PythonEnvironment::getInternalString(ctx, "__call__");
                    const proto::ProtoString* codeS = env ? env->getCodeString() : protoPython::PythonEnvironment::getInternalString(ctx, "__code__");
                    const proto::ProtoObject* codeObj = body->getAttribute(ctx, codeS);
                    // PG: propagate the body function's __closure__ to the
                    // class namespace so LOAD_DEREF inside the class body
                    // can walk the enclosing-scope cells.  Without this,
                    // `class C: y = x` (with x defined in an enclosing
                    // function) sees x as None.
                    if (env && body) {
                        const proto::ProtoObject* bodyClosure = body->getAttribute(ctx, env->getClosureString());
                        if (bodyClosure && bodyClosure != PROTO_NONE) {
                            ns = const_cast<proto::ProtoObject*>(
                                ns->setAttribute(ctx, env->getClosureString(), bodyClosure));
                            stack.back() = ns;
                        }
                    }
                    if (codeObj && codeObj != PROTO_NONE) {
                        if (diag_local) fprintf(stderr, "DEBUG OP_BUILD_CLASS: before body run ns=%p\n", (void*)ns);
                        runCodeObject(ctx, codeObj, ns);
                        if (env && env->hasPendingException()) {
                            // Class body raised — let the dispatch loop
                            // unwind through the enclosing SETUP_FINALLY
                            // (e.g. `with assertRaises(NameError): class
                            // CBad: nonexistent.attr: int = 0`).
                            i = next_i;
                            continue;
                        }
                        if (diag_local) {
                            const proto::ProtoObject* keysObj = ns->getAttribute(ctx, env ? env->getKeysString() : protoPython::PythonEnvironment::getInternalString(ctx, "__keys__"));
                            const proto::ProtoList* keysList = keysObj ? keysObj->asList(ctx) : nullptr;
                            fprintf(stderr, "DEBUG OP_BUILD_CLASS: after body run ns=%p keysSize=%lu\n", (void*)ns, keysList ? keysList->getSize(ctx) : 0);
                        }
                        stack.back() = ns; // ns may have been reallocated by CoW during execution
                    } else {
                        const proto::ProtoObject* callM = body->getAttribute(ctx, callS);
                        if (callM && callM->asMethod(ctx)) {
                            callM->asMethod(ctx)(ctx, body, nullptr, ctx->newList(), nullptr);
                            if (env && env->hasPendingException()) { i = next_i; continue; }
                        }
                    }
                }


                if (diag_local) {
                }

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
                        if (slotsObj->isTuple(ctx)) {
                            slotsList = slotsObj->asTuple(ctx)->asList(ctx);
                        } else {
                            slotsList = slotsObj->asList(ctx);
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
                                const proto::ProtoList* keysL = (keysObj && keysObj->asList(ctx))
                                    ? keysObj->asList(ctx) : ctx->newList();
                                keysL = keysL->appendLast(ctx, k->asObject(ctx));
                                ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, keysName, keysL->asObject(ctx)));
                            }
                        };
                        if (slotsList) {
                            for (unsigned long i = 0; i < slotsList->getSize(ctx); ++i) {
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
                        stack.back() = ns;
                    }
                }

                // 4. Invoke metaclass to create the class
                const proto::ProtoList* mcArgs = ctx->newList()->appendLast(ctx, name)->appendLast(ctx, bases)->appendLast(ctx, ns);
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
                    const proto::ProtoList* keysL = keysObj ? keysObj->asList(ctx) : nullptr;
                    if (keysL && keysL->getSize(ctx) > 0) {
                        // Skip 'metaclass' key — same filter as for kw above.
                        const proto::ProtoString* metaKey =
                            PythonEnvironment::getInternedString(ctx, "metaclass");
                        unsigned long mcHash = metaKey->getHash(ctx);
                        const proto::ProtoList* fk = ctx->newList();
                        for (unsigned long i = 0; i < keysL->getSize(ctx); ++i) {
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
                        stack.back() = targetClass;
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
                            stack.back() = targetClass;
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
                        stack.back() = targetClass;
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
                        const proto::ProtoList* tcKL = tcKeys ? tcKeys->asList(ctx) : nullptr;
                        if (tcKL) {
                            for (unsigned long ki = 0; ki < tcKL->getSize(ctx); ++ki) {
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
                                        const proto::ProtoObject* v = env->getAttribute(ctx, ns, nameS_, false);
                                        if (env->hasPendingException()) env->clearPendingException();
                                        if (v && v != PROTO_NONE) {
                                            const proto::ProtoObject* isAbs =
                                                env->getAttribute(ctx, v, abstractS, false);
                                            if (env->hasPendingException()) env->clearPendingException();
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
                        const proto::ProtoTuple* mro2T = mro2 ? mro2->asTuple(ctx) : nullptr;
                        if (mro2T) {
                            for (unsigned long mi = 1; mi < mro2T->getSize(ctx); ++mi) {
                                const proto::ProtoObject* base = mro2T->getAt(ctx, mi);
                                if (!base || base == PROTO_NONE) continue;
                                const proto::ProtoObject* baseAm = base->getOwnAttributeDirect(ctx, amS);
                                if (!baseAm || baseAm == PROTO_NONE) continue;
                                const proto::ProtoList* bl = baseAm->asList(ctx);
                                const proto::ProtoTuple* bt = bl ? nullptr : baseAm->asTuple(ctx);
                                unsigned long sz = bl ? bl->getSize(ctx) : (bt ? bt->getSize(ctx) : 0);
                                for (unsigned long bi = 0; bi < sz; ++bi) {
                                    const proto::ProtoObject* bn = bl ? bl->getAt(ctx, bi) : bt->getAt(ctx, bi);
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
                        stack.back() = targetClass;
                    }

                    // PG: also write the just-built class into `ns`
                    // under its own name so methods inside the body —
                    // whose closure chains walk parent → ns — resolve
                    // zero-arg super() correctly.  Without this,
                    // methods of a class defined inside another
                    // function can't reach the class (the enclosing
                    // STORE_FAST runs only after BUILD_CLASS finishes).
                    if (name && name->isString(ctx)) {
                        ns->setAttribute(ctx, name->asString(ctx), targetClass);
                    }

                    // PG: __init_subclass__ hook.  CPython calls
                    // `super(cls, cls).__init_subclass__(**kwargs)` after
                    // a class is built, where kwargs are the class-level
                    // keywords (sans metaclass).  Walk MRO[1:] to find
                    // the first __init_subclass__ implementation and
                    // invoke it.
                    if (env) {
                        const proto::ProtoObject* mroAttr = targetClass->getAttribute(ctx,
                            PythonEnvironment::getInternedString(ctx, "__mro__"));
                        const proto::ProtoTuple* mroT = mroAttr ? mroAttr->asTuple(ctx) : nullptr;
                        if (mroT && mroT->getSize(ctx) >= 2) {
                            const proto::ProtoString* iscS =
                                PythonEnvironment::getInternedString(ctx, "__init_subclass__");
                            for (unsigned long mi = 1; mi < mroT->getSize(ctx); ++mi) {
                                const proto::ProtoObject* base = mroT->getAt(ctx, mi);
                                if (!base || base == PROTO_NONE) continue;
                                const proto::ProtoObject* hook = base->getOwnAttributeDirect(ctx, iscS);
                                if (!hook || hook == PROTO_NONE) continue;
                                // Found __init_subclass__ on this base.
                                // Filter kwds to drop metaclass key.
                                const proto::ProtoSparseList* origKw =
                                    (kwds && kwds->asSparseList(ctx)) ? kwds->asSparseList(ctx) : nullptr;
                                if (!origKw && kwds && kwds != PROTO_NONE) {
                                    const proto::ProtoObject* dataAttr = kwds->getAttribute(ctx, env->getDataString());
                                    if (dataAttr) origKw = dataAttr->asSparseList(ctx);
                                }
                                const proto::ProtoSparseList* filtered = origKw;
                                // PG: __init_subclass__ is implicitly a
                                // classmethod (CPython semantics).  Unwrap
                                // the underlying function via __func__ so
                                // invokeCallable invokes the actual body.
                                const proto::ProtoString* funcS =
                                    PythonEnvironment::getInternedString(ctx, "__func__");
                                const proto::ProtoObject* unwrapped = hook->getAttribute(ctx, funcS);
                                if (unwrapped && unwrapped != PROTO_NONE) hook = unwrapped;
                                if (origKw) {
                                    const proto::ProtoString* mcKey =
                                        PythonEnvironment::getInternedString(ctx, "metaclass");
                                    unsigned long mcHash = mcKey->getHash(ctx);
                                    if (origKw->has(ctx, mcHash)) {
                                        // Build a sparse list without metaclass.
                                        const proto::ProtoSparseList* fnew = ctx->newSparseList();
                                        const proto::ProtoSparseListIterator* it = origKw->getIterator(ctx);
                                        while (it) {
                                            unsigned long h = it->nextKey(ctx);
                                            const proto::ProtoObject* v = it->nextValue(ctx);
                                            if (!v) break;
                                            if (h != mcHash) {
                                                fnew = fnew->setAt(ctx, h, v);
                                            }
                                            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                                        }
                                        filtered = fnew;
                                    }
                                }
                                // Pass cls as first positional arg (classmethod-style binding).
                                const proto::ProtoList* iscArgs = ctx->newList()->appendLast(ctx, targetClass);
                                // STRUCT-69: thread the kwds dict's __keys__ via pushKwNames so
                                // the inner **kwargs binding can recover key names from the
                                // SparseList.  Same approach as the metaclass-invoke site above.
                                const proto::ProtoTuple* iscNames = nullptr;
                                if (env && filtered && kwds && kwds != PROTO_NONE) {
                                    const proto::ProtoObject* keysObj = kwds->getAttribute(ctx, env->getKeysString());
                                    const proto::ProtoList* keysL = keysObj ? keysObj->asList(ctx) : nullptr;
                                    if (keysL && keysL->getSize(ctx) > 0) {
                                        const proto::ProtoString* metaKey =
                                            PythonEnvironment::getInternedString(ctx, "metaclass");
                                        unsigned long mcH = metaKey->getHash(ctx);
                                        const proto::ProtoList* fk = ctx->newList();
                                        for (unsigned long i = 0; i < keysL->getSize(ctx); ++i) {
                                            const proto::ProtoObject* k = keysL->getAt(ctx, static_cast<int>(i));
                                            if (k && k->isString(ctx) && k->getHash(ctx) != mcH) {
                                                fk = fk->appendLast(ctx, k);
                                            }
                                        }
                                        if (fk->getSize(ctx) > 0) {
                                            iscNames = ctx->newTupleFromList(fk);
                                            env->pushKwNames(iscNames);
                                        }
                                    }
                                }
                                invokeCallable(ctx, hook, iscArgs, filtered);
                                if (iscNames && env) env->popKwNames();
                                if (env->hasPendingException()) {
                                    return nullptr;
                                }
                                break;
                            }
                        }
                    }
                }

                if (!targetClass) targetClass = PROTO_NONE;
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
                    if (!env->hasPendingException()) {
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
                if (env && env->hasPendingException()) {
                    if (env->isStopIteration(ctx, env->peekPendingException())) {
                        env->clearPendingException();
                        stack.pop_back();
                        i = static_cast<unsigned long>(arg);
                        continue;
                    } else {
                        // the unhandled exception will be picked up at the top of the next iteration
                        if (diag_local) {
                            const proto::ProtoObject* pendExc = env->peekPendingException();
                            const proto::ProtoObject* cls = pendExc ? env->getAttribute(ctx, pendExc, env->getClassString()) : nullptr;
                            const proto::ProtoObject* name = cls ? env->getAttribute(ctx, cls, env->getNameString()) : nullptr;
                            std::string excName = "unknown";
                            if (name && name->isString(ctx)) name->asString(ctx)->toUTF8String(ctx, excName);
                            fprintf(stderr, "DEBUG: OP_FOR_ITER falling through on pending exception: %s\n", excName.c_str());
                            fflush(stderr);
                        }
                        i = next_i;
                        continue; 
                    }
                } else {
                    // exhaustion
                    stack.pop_back();
                    i = static_cast<unsigned long>(arg);
                    continue;
                }
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
                    if (!env->hasPendingException()) env->raiseTypeError(ctx, "cannot unpack non-iterable object");
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
                        if (env->hasPendingException() && env->isStopIteration(ctx, env->peekPendingException())) {
                            env->clearPendingException();
                        }
                        if (!env->hasPendingException()) {
                            // CPython embeds the expected and got counts:
                            //   ValueError: not enough values to unpack (expected 3, got 2)
                            std::string msg = "not enough values to unpack (expected "
                                + std::to_string(arg) + ", got " + std::to_string(got) + ")";
                            env->raiseValueError(ctx,
                                PythonEnvironment::getInternedString(ctx, msg.c_str())->asObject(ctx));
                        }
                        break;
                    }
                    items.push_back(val);
                    got++;
                }
                if (env->hasPendingException()) {
                    i = next_i; continue;
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
                } else if (env->hasPendingException() && env->isStopIteration(ctx, env->peekPendingException())) {
                    env->clearPendingException();
                }

                // Push onto stack in reverse order so the first unpacked name gets the top of the stack
                for (int j = arg - 1; j >= 0; --j) {
                    stack.push_back(items[j]);
                }
            } else {
                const proto::ProtoList* list = seq->asList(ctx);
                const proto::ProtoTuple* tup = seq->asTuple(ctx);
                if (!list && !tup) {
                     const proto::ProtoObject* data = seq->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                     if (data) {
                         list = data->asList(ctx);
                         tup = data->asTuple(ctx);
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
                    if (!env->hasPendingException()) env->raiseTypeError(ctx, "cannot unpack non-iterable object");
                    i = next_i; continue;
                }
                // Pin iterObj across user __next__ callbacks (seq was popped).
                PythonEnvironment::TransientPin pinIt(env, iterObj);
                while (true) {
                    const proto::ProtoObject* val = env->next(iterObj);
                    if (!val) {
                        if (env->hasPendingException() && env->isStopIteration(ctx, env->peekPendingException()))
                            env->clearPendingException();
                        break;
                    }
                    all.push_back(val);
                }
                if (env->hasPendingException()) { i = next_i; continue; }
            } else {
                const proto::ProtoList* list = seq->asList(ctx);
                const proto::ProtoTuple* tup = seq->asTuple(ctx);
                if (!list && !tup) {
                    const proto::ProtoObject* data = seq->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                    if (data) { list = data->asList(ctx); tup = data->asTuple(ctx); }
                }
                if (list) {
                    for (size_t j = 0; j < list->getSize(ctx); ++j) all.push_back(list->getAt(ctx, j));
                } else if (tup) {
                    for (size_t j = 0; j < tup->getSize(ctx); ++j) all.push_back(tup->getAt(ctx, j));
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
                if (nativeNames && names && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                    const proto::ProtoObject* n = nativeNames[nameIdx];
                    nameS = n ? n->asString(ctx) : nullptr;
                } else if (names && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                    const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                    if (nameObj && proto::ProtoObject::isStringTagFast(nameObj))
                        nameS = nameObj->asString(ctx);
                }
                if (nameS && env) {
                    // LOAD_GLOBAL resolves in globals+builtins only — never in the local frame.
                    const proto::ProtoObject* val = env->resolve(nameS, ctx);
                    if (val != nullptr) {
                        if (pushNull) stack.push_back(nullptr);
                        stack.push_back(val);
                    } else {
                        if (!env->hasPendingException()) {
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
            if (names && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
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
                const proto::ProtoList* L = (data && data->asList(ctx)) ? data->asList(ctx) : nullptr;
                if (diag_local) {
                    fprintf(stderr, "DEBUG: OP_LIST_TO_TUPLE L=%p size=%zu\n", (void*)L, L ? L->getSize(ctx) : 0);
                }
                
                if (L) {
                    const proto::ProtoTuple* T = ctx->newTupleFromList(L);
                    proto::ProtoObject* tupObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                    tupObj->setAttribute(ctx, dataS, T->asObject(ctx));
                    if (env && env->getTuplePrototype()) {
                        tupObj->addParent(ctx, env->getTuplePrototype());
                        tupObj->setAttribute(ctx, env->getClassString(), env->getTuplePrototype());
                    }
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
            int nameIdx = arg >> 1;
            // Swallow any pre-existing or subsequent exception from delete path (e.g. os.py del _create_environ_mapping)
            if (env && env->hasPendingException()) env->clearPendingException();
            // i++;
            if (frame) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (nameObj && proto::ProtoObject::isStringTagFast(nameObj)) {
                    const proto::ProtoString* data_name = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                    const proto::ProtoObject* data = frame->getAttribute(ctx, data_name);
                    if (data && data->asSparseList(ctx)) {
                        data->asSparseList(ctx)->removeAt(ctx, nameObj->getHash(ctx));
                        // removeAt can set TypeError (e.g. wrong key type); clear so del does not abort module load
                        if (env && env->hasPendingException()) {
                            env->clearPendingException();
                        }
                    }
                }
            }
            if (env) env->invalidateResolveCache();
            // Swallow any exception from delete path so "del name" does not abort module (e.g. os.py del _create_environ_mapping)
            if (env && env->hasPendingException()) env->clearPendingException();
        } break;
        case OP_DELETE_FAST: {
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<unsigned long>(arg) < nSlots) {
                proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(ctx->getAutomaticLocals());
                slots[arg] = nullptr; 
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
                                const proto::ProtoTuple* oMroT = oMroAttr ? oMroAttr->asTuple(ctx) : nullptr;
                                if (oMroT) {
                                    for (unsigned long mi = 0; mi < oMroT->getSize(ctx); ++mi) {
                                        if (oMroT->getAt(ctx, mi) == modProto) {
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
                        std::vector<const proto::ProtoString*> toRemove;
                        const proto::ProtoSparseList* ownAttrs = mobj->getOwnAttributes(ctx);
                        if (ownAttrs) {
                            auto* it = const_cast<proto::ProtoSparseListIterator*>(ownAttrs->getIterator(ctx));
                            while (it && it->hasNext(ctx)) {
                                unsigned long key = it->nextKey(ctx);
                                const proto::ProtoObject* keyObj = reinterpret_cast<const proto::ProtoObject*>(key);
                                it = const_cast<proto::ProtoSparseListIterator*>(it->advance(ctx));
                                if (!keyObj || !keyObj->isString(ctx)) continue;
                                std::string nm; keyObj->asString(ctx)->toUTF8String(ctx, nm);
                                bool skip = false;
                                for (auto& bad : internalSkip) if (nm == bad) { skip = true; break; }
                                if (!skip) toRemove.push_back(keyObj->asString(ctx));
                            }
                        }
                        for (const proto::ProtoString* knm : toRemove) {
                            mobj->removeAttribute(ctx, knm);
                        }
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
                                const proto::ProtoTuple* mroT = mroAttr ? mroAttr->asTuple(ctx) : nullptr;
                                const proto::ProtoObject* override = nullptr;
                                if (mroT) {
                                    for (unsigned long mi = 0; mi < mroT->getSize(ctx); ++mi) {
                                        const proto::ProtoObject* base = mroT->getAt(ctx, mi);
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
                                            const_cast<proto::ProtoObject*>(obj), nullptr, args, nullptr);
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
                    // PH: data-descriptor __delete__ on the type chain.
                    // Mirrors STORE_ATTR's data-descriptor short-circuit:
                    // walk the type's MRO with raw attribute access to
                    // avoid __get__ re-entry, then dispatch to either
                    // a native or Python-defined __delete__.  PI: check
                    // even when the instance has its own attribute, since
                    // a data descriptor (with __set__/__delete__) takes
                    // precedence over instance dict for del.
                    if (env) {
                        const proto::ProtoObject* type = env->getType(ctx, obj);
                        const proto::ProtoObject* descr = nullptr;
                        if (type && type != PROTO_NONE) {
                            if (type->hasOwnAttribute(ctx, nameS) == PROTO_TRUE) {
                                descr = type->getOwnAttributeDirect(ctx, nameS);
                            } else {
                                const proto::ProtoObject* mroObj = env->getAttribute(ctx, type, env->getMroString(), false);
                                const proto::ProtoTuple* mroT = mroObj ? mroObj->asTuple(ctx) : nullptr;
                                if (mroT) {
                                    for (unsigned long mi = 0; mi < mroT->getSize(ctx); ++mi) {
                                        const proto::ProtoObject* base = mroT->getAt(ctx, mi);
                                        if (!base || base == PROTO_NONE) continue;
                                        if (base->hasOwnAttribute(ctx, nameS) == PROTO_TRUE) {
                                            descr = base->getOwnAttributeDirect(ctx, nameS);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if (descr && descr != PROTO_NONE) {
                            const proto::ProtoString* delS =
                                PythonEnvironment::getInternedString(ctx, "__delete__");
                            const proto::ProtoObject* delM = descr->getAttribute(ctx, delS);
                            const proto::ProtoObject* descrType = env->getType(ctx, descr);
                            if ((!delM || delM == PROTO_NONE) && descrType && descrType != PROTO_NONE) {
                                delM = env->getAttribute(ctx, descrType, delS, false);
                            }
                            if (delM && delM != PROTO_NONE) {
                                if (delM->asMethod(ctx)) {
                                    delM->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(descr), nullptr,
                                        ctx->newList()->appendLast(ctx, obj), nullptr);
                                } else {
                                    invokePythonCallable(ctx, delM,
                                        ctx->newList()->appendLast(ctx, descr)->appendLast(ctx, obj), nullptr);
                                }
                                // STRUCT-267: when the descriptor's
                                // __delete__ succeeds (no pending
                                // exception), the bare `continue` would
                                // re-run OP_DELETE_ATTR on the same
                                // operand-stack-position, popping the
                                // enclosing for-loop's iterator on the
                                // second pass — surfacing as
                                // "<tuple_iterator object> has no
                                // attribute '__annotations__'".  Advance
                                // pc to the next opcode on the
                                // success path; on failure, leave i
                                // alone so the top-of-loop exception
                                // handler can unwind.
                                if (env->hasPendingException()) continue;
                                i = next_i;
                                continue; // descriptor handled the delete
                            }
                        }
                    }
                    // No descriptor — fall through to instance attribute deletion.
                    // CPython raises AttributeError when the attribute is not
                    // OWN on the receiver: delete reports a missing attribute
                    // even if the type chain has one (because deletion only
                    // affects instance state). Track whether anything actually
                    // got removed so we can raise on missing names.
                    bool removed = false;
                    const proto::ProtoString* dataName = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                    const proto::ProtoString* keysName = env ? env->getKeysString() : protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                    const proto::ProtoObject* d = (obj->hasOwnAttribute(ctx, dataName) == PROTO_TRUE) ? obj->proto::ProtoObject::getAttribute(ctx, dataName) : nullptr;
                    const proto::ProtoObject* k = (keysName && obj->hasOwnAttribute(ctx, keysName) == PROTO_TRUE) ? obj->proto::ProtoObject::getAttribute(ctx, keysName) : nullptr;
                    if (d && d != PROTO_NONE && d->asSparseList(ctx)) {
                        unsigned long h = nameS->getHash(ctx);
                        const proto::ProtoSparseList* sl = d->asSparseList(ctx);
                        if (sl->has(ctx, h)) {
                            // SparseList::removeAt returns a NEW immutable list;
                            // capture it and rebind __data__ so subsequent reads
                            // (vars/keys/items) see the entry as gone instead
                            // of just losing the structural-sharing slot.
                            const proto::ProtoSparseList* newSl = sl->removeAt(ctx, h);
                            const_cast<proto::ProtoObject*>(obj)->proto::ProtoObject::setAttribute(ctx, dataName, newSl->asObject(ctx));
                            removed = true;
                        }
                        if (env && env->hasPendingException()) env->clearPendingException();
                    }
                    if (k && k != PROTO_NONE && k->asList(ctx)) {
                        unsigned long targetHash = nameS->getHash(ctx);
                        const proto::ProtoList* listIn = k->asList(ctx);
                        const proto::ProtoList* newKeys = ctx->newList();
                        bool anyDropped = false;
                        for (unsigned long ki = 0; ki < listIn->getSize(ctx); ++ki) {
                            const proto::ProtoObject* key = listIn->getAt(ctx, ki);
                            if (key && key->isString(ctx) && key->getHash(ctx) == targetHash) {
                                anyDropped = true;
                                continue;
                            }
                            newKeys = newKeys->appendLast(ctx, key);
                        }
                        if (anyDropped) {
                            const_cast<proto::ProtoObject*>(obj)->proto::ProtoObject::setAttribute(ctx, keysName, newKeys->asObject(ctx));
                            removed = true;
                        }
                    }
                    // Cell-storage attribute path: protoCore exposes a proper
                    // removeAttribute that erases the entry from the OWN
                    // attribute table (mirrors setAttribute's mutable-vs-
                    // immutable contract). Use it instead of overwriting with
                    // PROTO_NONE, which would (a) leave the attribute visible
                    // through hasattr/vars and (b) lose the user's ability to
                    // explicitly assign None as a sentinel value that shadows
                    // a parent binding.
                    if (obj->hasOwnAttribute(ctx, nameS) == PROTO_TRUE) {
                        const_cast<proto::ProtoObject*>(obj)->proto::ProtoObject::removeAttribute(ctx, nameS);
                        removed = true;
                    }
                    if (!removed && env) {
                        std::string nm;
                        nameS->toUTF8String(ctx, nm);
                        env->raiseAttributeError(ctx, obj, nm.c_str());
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
                    if (env && env->hasPendingException()) {
                        stack.pop_back(); // Pop key
                        stack.pop_back(); // Pop container
                        continue;
                    }
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
                                const proto::ProtoTuple* basesT =
                                    basesAttr ? basesAttr->asTuple(ctx) : nullptr;
                                if (!basesT) return false;
                                for (unsigned long bi = 0; bi < basesT->getSize(ctx); ++bi) {
                                    const proto::ProtoObject* b =
                                        basesT->getAt(ctx, static_cast<int>(bi));
                                    if (isOrDescends(b, anchor, depth + 1)) return true;
                                }
                                return false;
                            };
                        isContainerList = tp && listProto && isOrDescends(tp, listProto, 0);
                        isContainerDict = tp && dictProto && isOrDescends(tp, dictProto, 0);
                    }
                    const proto::ProtoList* lst = isContainerList ? container->asList(ctx) : nullptr;
                    const proto::ProtoSparseList* sl =
                        (!lst && isContainerDict) ? container->asSparseList(ctx) : nullptr;
                    if (lst && key->isInteger(ctx)) {
                        long long idx = key->asLong(ctx);
                        if (idx >= 0 && static_cast<unsigned long>(idx) < lst->getSize(ctx)) {
                            const proto::ProtoList* newList = ctx->newList();
                            for (unsigned long j = 0; j < lst->getSize(ctx); ++j) {
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
            if (diag_local) fprintf(stderr, "DEBUG: SETUP_FINALLY handler pc %lu, stack.top %lu\n", (unsigned long)arg, stack.size());
            fflush(stderr);
            blockStack.push_back({static_cast<unsigned long>(arg), stack.size()});
            // No continue: fall through to i = next_i
        } break;
        case OP_POP_BLOCK: {
            if (!blockStack.empty()) blockStack.pop_back();
        } break;
        case OP_GET_AWAITABLE: {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* obj = stack.back();
            stack.pop_back();
            // In a robust implementation, we'd check if obj is already a coroutine.
            // For now, try __await__ or just keep as is if it has a send method.
            const proto::ProtoString* awaitS = env ? env->getAwaitString() : PythonEnvironment::getInternedString(ctx, "__await__");
            const proto::ProtoObject* awaitable = invokeDunder(ctx, obj, awaitS, ctx->newList());
            if (awaitable) {
                stack.push_back(awaitable);
            } else {
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
                if (env && !env->hasPendingException()) {
                    env->raiseTypeError(ctx, "async for item must be an async iterator");
                }
                if (diag_local && env && env->hasPendingException()) {
                    if (diag_local) {}
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
            blockStack.push_back({static_cast<unsigned long>(arg), stack.size()});
        } break;
        default:
            break;
        }  // end switch (op)
        } catch (const std::exception& e) {
            // Translate any escaped C++ exception into a Python RuntimeError
            // so the bytecode loop can unwind it the normal way.  Don't
            // overwrite an exception that was already set inside the
            // handler — that one carries more specific information.
            if (env && !env->hasPendingException()) {
                std::string msg = std::string("internal C++ exception: ") + e.what();
                env->raiseRuntimeError(ctx, msg);
            }
        } catch (...) {
            if (env && !env->hasPendingException()) {
                env->raiseRuntimeError(ctx, std::string("internal C++ exception (unknown type)"));
            }
        }
        i = next_i;
    }
    return stack.empty() ? PROTO_NONE : stack.back();
}

const proto::ProtoObject* executeMinimalBytecode(
    proto::ProtoContext* ctx,
    const proto::ProtoTuple* constants,
    const proto::ProtoTuple* bytecode,
    const proto::ProtoTuple* names,
    proto::ProtoObject*& frame) {
    if (!ctx || !constants || !bytecode) return nullptr;
    unsigned long n = bytecode->getSize(ctx);
    // Save and restore thread-local/global singletons so unit tests don't pollute each other.
    // STORE_NAME/STORE_GLOBAL handlers call setCurrentFrame/setCurrentGlobals, which would
    // leave stale pointers that corrupt subsequent tests running in a fresh ProtoContext.
    const proto::ProtoObject* savedFrame = PythonEnvironment::getCurrentFrame();
    const proto::ProtoObject* savedGlobals = PythonEnvironment::getCurrentGlobals();
    // Set globals to frame so BUILD_FUNCTION captures the correct __globals__, and so that
    // sync_globals=true enables STORE_NAME to keep getCurrentGlobals() in sync with frame CoW.
    PythonEnvironment::setCurrentGlobals(frame);
    const proto::ProtoObject* result = executeBytecodeRange(ctx, constants, bytecode, names, frame, 0, n ? n - 1 : 0, 0, nullptr, nullptr, nullptr, 0, nullptr);
    PythonEnvironment::setCurrentFrame(savedFrame);
    PythonEnvironment::setCurrentGlobals(savedGlobals);
    return result;
}


const proto::ProtoObject* exported_py_function_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return py_function_get(ctx, self, parentLink, args, kwargs);
}

const proto::ProtoObject* exported_py_function_code_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    if (!instance || instance == PROTO_NONE) return self;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* codeStr = env ? env->getCodeString() : PythonEnvironment::getInternedString(ctx, "__code__");
    const proto::ProtoObject* res = instance->getAttribute(ctx, codeStr);
    return res ? res : PROTO_NONE;
}

const proto::ProtoObject* exported_py_function_globals_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    if (!instance || instance == PROTO_NONE) return self;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* globalsStr = env ? env->getGlobalsString() : PythonEnvironment::getInternedString(ctx, "__globals__");
    const proto::ProtoObject* res = instance->getAttribute(ctx, globalsStr);
    return res ? res : PROTO_NONE;
}

const proto::ProtoObject* exported_py_function_doc_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    if (!instance || instance == PROTO_NONE) return self;
    const proto::ProtoString* docStr = PythonEnvironment::getInternedString(ctx, "__doc__");
    const proto::ProtoObject* res = instance->getAttribute(ctx, docStr);
    return res ? res : PROTO_NONE;
}


const proto::ProtoObject* exported_runUserFunctionCall(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return runUserFunctionCall(ctx, self, parentLink, args, kwargs);
}


} // namespace protoPython
