#include <protoPython/ExecutionEngine.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/Compiler.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/MemoryManager.hpp>
#include <protoCore.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cstring>
#include <iostream>

namespace protoPython {

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
    bool skipFrame = false; // env && (co_flags & CO_OPTIMIZED) && !isGenerator && cacheNoInnerFunctions && (!hasClosure || cacheNoLoadDeref);
    proto::ProtoObject* frame = nullptr;
    if (!skipFrame) {
        frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(false));
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
            auto it = kwargs->getIterator(calleeCtx);
            while (it && it->hasNext(calleeCtx)) {
                unsigned long key = it->nextKey(calleeCtx);
                const proto::ProtoObject* val = it->nextValue(calleeCtx);

                // Only add if not already bound to a positional or kwonly param
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
                    // Find the actual key name object to populate __keys__.
                    const proto::ProtoObject* keyNameObj = nullptr;
                    if (kwNamesTuple) {
                        for (int ni = 0; ni < kwNamesTuple->getSize(calleeCtx); ++ni) {
                            const proto::ProtoObject* nm = kwNamesTuple->getAt(calleeCtx, ni);
                            if (nm && nm->getHash(calleeCtx) == key) {
                                keyNameObj = nm;
                                break;
                            }
                        }
                    }
                    if (keyNameObj) {
                        keysList = keysList->appendLast(calleeCtx, keyNameObj);
                    }
                }
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(calleeCtx);
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
        proto::ProtoObject* gen = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
        if (env && env->getGeneratorPrototype()) {
            const proto::ProtoObject* genProto = env->getGeneratorPrototype();
            gen = const_cast<proto::ProtoObject*>(gen->addParent(calleeCtx, genProto));
            gen->setAttribute(calleeCtx, env->getClassString(), genProto);
        }
        gen->setAttribute(calleeCtx, env ? env->getGiCodeString() : PythonEnvironment::getInternedString(calleeCtx, "gi_code"), codeObj);
        gen->setAttribute(calleeCtx, env ? env->getGiFrameString() : PythonEnvironment::getInternedString(calleeCtx, "gi_frame"), frame);
        gen->setAttribute(calleeCtx, env ? env->getGiRunningString() : PythonEnvironment::getInternedString(calleeCtx, "gi_running"), PROTO_FALSE);
        gen->setAttribute(calleeCtx, env ? env->getGiPCString() : PythonEnvironment::getInternedString(calleeCtx, "gi_pc"), calleeCtx->fromInteger(0));
        
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
        const proto::ProtoList* argsList = ctx->newList();
        for (unsigned long i = 0; i < rawArgCount; ++i)
            argsList = argsList->appendLast(ctx, rawArgs[i]);
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

    // Frame creation for fast path (required for sys._getframe and tracebacks)
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(false));
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
    FrameScope fscope(frame);

    result = nullptr;
    {
        GlobalsScope gscope(globalsObj);

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
    
    // Use function prototype directly via newChild to ensure correct parentage
    const proto::ProtoObject* fn = nullptr;
    if (env && env->getFunctionPrototype()) {
        fn = env->getFunctionPrototype()->newChild(ctx, false);
    } else {
        fn = ctx->newObject(true); // Fallback
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
            fn = fn->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__qualname__"), codeName);
        }
    }
    
    // Add missing default function attributes required by CPython/functools
    if (env) {
        const proto::ProtoString* dictS = PythonEnvironment::getInternedString(ctx, "__dict__");
        const proto::ProtoString* annS = PythonEnvironment::getInternedString(ctx, "__annotations__");
        const proto::ProtoString* modS = PythonEnvironment::getInternedString(ctx, "__module__");
        const proto::ProtoString* docS = PythonEnvironment::getInternedString(ctx, "__doc__");
        
        const proto::ProtoObject* emptyDict1 = env->getDictPrototype() ? env->getDictPrototype()->newChild(ctx, true) : ctx->newObject(true);
        const proto::ProtoObject* emptyDict2 = env->getDictPrototype() ? env->getDictPrototype()->newChild(ctx, true) : ctx->newObject(true);
        
        fn = fn->setAttribute(ctx, dictS, emptyDict1);
        fn = fn->setAttribute(ctx, annS, emptyDict2);
        fn = fn->setAttribute(ctx, docS, PROTO_NONE);
        
        const proto::ProtoObject* modName = globalsFrame->getAttribute(ctx, env->getNameString());
        if (modName && modName != PROTO_NONE) {
            fn = fn->setAttribute(ctx, modS, modName);
        } else {
            fn = fn->setAttribute(ctx, modS, PROTO_NONE);
        }
    }
    if (closureFrame && env) {
        // Wrap closure frame in a tuple for CPython compatibility (f.__closure__ is a tuple)
        const proto::ProtoList* closureTuple = ctx->newList()->appendLast(ctx, closureFrame);
        fn = fn->setAttribute(ctx, env->getClosureString(), closureTuple->asObject(ctx));
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



static const proto::ProtoObject* binaryOpDispatch(proto::ProtoContext* ctx, const proto::ProtoObject* a, const proto::ProtoObject* b, const char* dunder, const char* rdunder) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* dunderS = PythonEnvironment::getInternedString(ctx, dunder);
    const proto::ProtoList* argsB = ctx->newList()->appendLast(ctx, b);
    const proto::ProtoObject* res = invokeDunder(ctx, a, dunderS, argsB);
    if (!res || (env && res == env->getNotImplementedPrototype())) {
        const proto::ProtoString* rdunderS = PythonEnvironment::getInternedString(ctx, rdunder);
        const proto::ProtoList* argsA = ctx->newList()->appendLast(ctx, a);
        res = invokeDunder(ctx, b, rdunderS, argsA);
    }
    return res;
}

static bool isEmbeddedValue(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    return obj->isInteger(ctx) || obj->isBoolean(ctx) || obj->isNone(ctx);
}

static const proto::ProtoObject* unwrapPrimitive(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (obj->isInteger(ctx) || obj->isDouble(ctx)) return obj;
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
    if (env) {
        const proto::ProtoObject* data = obj->getAttribute(ctx, env->getDataString());
        if (data && (data->isInteger(ctx) || data->isDouble(ctx))) return data;
    }
    return obj;
}

static const proto::ProtoObject* binaryAdd(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb = unwrapPrimitive(ctx, b);
    if ((aa->isInteger(ctx) || aa->isDouble(ctx)) && (bb->isInteger(ctx) || bb->isDouble(ctx))) {
        return aa->add(ctx, bb);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);

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
        std::string s1, s2;
        a->asString(ctx)->toUTF8String(ctx, s1);
        b->asString(ctx)->toUTF8String(ctx, s2);
        return PythonEnvironment::getInternedString(ctx, (s1 + s2).c_str())->asObject(ctx);
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
        proto::ProtoObject* resObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));

        bool isTuple = env && (aCls == env->getTuplePrototype());
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
    if ((aa->isInteger(ctx) || aa->isDouble(ctx)) && (bb->isInteger(ctx) || bb->isDouble(ctx))) {
        return aa->subtract(ctx, bb);
    }
    const proto::ProtoObject* r = binaryOpDispatch(ctx, a, b, "__sub__", "__rsub__");
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryMultiply(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb = unwrapPrimitive(ctx, b);
    if ((aa->isInteger(ctx) || aa->isDouble(ctx)) && (bb->isInteger(ctx) || bb->isDouble(ctx))) {
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
    // Try __mul__ on the left operand, then __rmul__ on the right
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* mulS = protoPython::PythonEnvironment::getInternalString(ctx, "__mul__");
    if (!a->isInteger(ctx) && !a->isDouble(ctx)) {
        const proto::ProtoObject* mul = env ? env->getAttribute(ctx, a, mulS) : a->getAttribute(ctx, mulS);
        if (mul && mul != PROTO_NONE && mul->asMethod(ctx)) {
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
            const proto::ProtoObject* r = mul->asMethod(ctx)(ctx, a, nullptr, args, nullptr);
            if (r && r != PROTO_NONE) return r;
        }
    }
    const proto::ProtoString* rmulS = protoPython::PythonEnvironment::getInternalString(ctx, "__rmul__");
    if (!b->isInteger(ctx) && !b->isDouble(ctx)) {
        const proto::ProtoObject* rmul = env ? env->getAttribute(ctx, b, rmulS) : b->getAttribute(ctx, rmulS);
        if (rmul && rmul != PROTO_NONE && rmul->asMethod(ctx)) {
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, a);
            const proto::ProtoObject* r = rmul->asMethod(ctx)(ctx, b, nullptr, args, nullptr);
            if (r && r != PROTO_NONE) return r;
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryUnaryNegative(proto::ProtoContext* ctx, const proto::ProtoObject* a) {
    if (a->isInteger(ctx)) {
        return a->multiply(ctx, ctx->fromInteger(-1));
    }
    if (a->isDouble(ctx)) return ctx->fromDouble(-a->asDouble(ctx));
    return PROTO_NONE;
}
static const proto::ProtoObject* binaryTrueDivide(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb = unwrapPrimitive(ctx, b);
    if (aa->isInteger(ctx) || aa->isDouble(ctx)) {
        if ((bb->isInteger(ctx) && bb->asLong(ctx) == 0) || (bb->isDouble(ctx) && bb->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
    }
    if ((aa->isInteger(ctx) || aa->isDouble(ctx)) && (bb->isInteger(ctx) || bb->isDouble(ctx))) {
        return aa->divide(ctx, bb);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryModulo(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb = unwrapPrimitive(ctx, b);
    if (aa->isInteger(ctx) || aa->isDouble(ctx)) {
        if ((bb->isInteger(ctx) && bb->asLong(ctx) == 0) || (bb->isDouble(ctx) && bb->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
    }
    if ((aa->isInteger(ctx) || aa->isDouble(ctx)) && (bb->isInteger(ctx) || bb->isDouble(ctx))) {
        return aa->modulo(ctx, bb);
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
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryPower(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa_p = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb_p = unwrapPrimitive(ctx, b);
    if ((aa_p->isInteger(ctx) || aa_p->isDouble(ctx)) && (bb_p->isInteger(ctx) || bb_p->isDouble(ctx))) {
        if (aa_p->isInteger(ctx) && bb_p->isInteger(ctx)) {
            long long base = aa_p->asLong(ctx);
            long long exp = bb_p->asLong(ctx);
            if (exp < 0) {
                double r = std::pow(static_cast<double>(base), static_cast<double>(exp));
                return ctx->fromDouble(r);
            }
            long long result = 1;
            for (long long i = 0; i < exp; ++i) result *= base;
            return ctx->fromInteger(result);
        }
        double aa = aa_p->isDouble(ctx) ? aa_p->asDouble(ctx) : static_cast<double>(aa_p->asLong(ctx));
        double bb = bb_p->isDouble(ctx) ? bb_p->asDouble(ctx) : static_cast<double>(bb_p->asLong(ctx));
        return ctx->fromDouble(std::pow(aa, bb));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryFloorDivide(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* aa_p = unwrapPrimitive(ctx, a);
    const proto::ProtoObject* bb_p = unwrapPrimitive(ctx, b);
    if (aa_p->isInteger(ctx) || aa_p->isDouble(ctx)) {
        if ((bb_p->isInteger(ctx) && bb_p->asLong(ctx) == 0) || (bb_p->isDouble(ctx) && bb_p->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
    }
    if ((aa_p->isInteger(ctx) || aa_p->isDouble(ctx)) && (bb_p->isInteger(ctx) || bb_p->isDouble(ctx))) {
        if (aa_p->isInteger(ctx) && bb_p->isInteger(ctx)) {
            return aa_p->divide(ctx, bb_p); 
        }
        double aa = aa_p->isDouble(ctx) ? aa_p->asDouble(ctx) : static_cast<double>(aa_p->asLong(ctx));
        double bb = bb_p->isDouble(ctx) ? bb_p->asDouble(ctx) : static_cast<double>(bb_p->asLong(ctx));
        return ctx->fromInteger(static_cast<long long>(std::floor(aa / bb)));
    }
    return PROTO_NONE;
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
            // Try dictionary keys or __data__ fallback
            const proto::ProtoString* dataS = protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
            const proto::ProtoObject* data = b->getAttribute(ctx, dataS);
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
            size_t size = lst->getSize(ctx);
            for (size_t i = 0; i < size; ++i) {
                if (a->compare(ctx, lst->getAt(ctx, i)) == 0) {
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
                // e.g. frame.__contains__ can set TypeError; treat as not found so "name in globals()" is False
                env->clearPendingException();
                found = false;
            } else if (b->isString(ctx) && a->isString(ctx)) {
                std::string s_sub, s_full;
                a->asString(ctx)->toUTF8String(ctx, s_sub);
                b->asString(ctx)->toUTF8String(ctx, s_full);
                found = (s_full.find(s_sub) != std::string::npos);
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

    // Python semantics: __bool__ takes priority over all native checks.
    // Check the class (not the instance) to avoid descriptor confusion.
    const proto::ProtoString* boolS = env ? env->getBoolString() : PythonEnvironment::getInternedString(ctx, "__bool__");
    const proto::ProtoObject* cls = env ? env->getType(ctx, obj) : nullptr;
    if (!cls) cls = obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"));

    const proto::ProtoObject* boolMethod = cls ? cls->getAttribute(ctx, boolS) : obj->getAttribute(ctx, boolS);
    if (boolMethod && boolMethod->asMethod(ctx)) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
        const proto::ProtoObject* result = boolMethod->asMethod(ctx)(ctx, obj, nullptr, emptyL, nullptr);
        if (result == PROTO_FALSE) return false;
        if (result == PROTO_TRUE) return true;
        return isTruthy(ctx, result);
    }

    // __len__ fallback (before native checks so custom containers win).
    const proto::ProtoString* lenS = env ? env->getLenString() : PythonEnvironment::getInternedString(ctx, "__len__");
    const proto::ProtoObject* lenMethod = cls ? cls->getAttribute(ctx, lenS) : obj->getAttribute(ctx, lenS);
    if (lenMethod && lenMethod->asMethod(ctx)) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
        const proto::ProtoObject* result = lenMethod->asMethod(ctx)(ctx, obj, nullptr, emptyL, nullptr);
        if (result && result->isInteger(ctx)) {
            return (result->asLong(ctx) > 0);
        }
    }

    // Native container checks for raw Proto objects that have no Python class wrapper.
    if (obj->asTuple(ctx)) return (obj->asTuple(ctx)->getSize(ctx) > 0);
    if (obj->asList(ctx)) return (obj->asList(ctx)->getSize(ctx) > 0);
    if (obj->asSparseList(ctx)) return (obj->asSparseList(ctx)->getSize(ctx) > 0);

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
        return callable->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(callable->asMethodSelf(ctx)), nullptr, args, kwargs);
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

    const proto::ProtoObject* method = env ? env->getAttribute(ctx, container, name, false) : container->getAttribute(ctx, name);

    if (!method || method == PROTO_NONE) return nullptr;

    const proto::ProtoSparseList* kwargs = env ? env->getEmptySparseList() : ctx->newSparseList();

    if (method->asMethod(ctx)) {
        return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(container), nullptr, args, kwargs);
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

        if (savedLocals) {
            proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());
            for (unsigned int i = 0; i < calleeCtx->getAutomaticLocalsCount() && i < savedLocals->getSize(ctx); ++i) {
                slots[i] = const_cast<proto::ProtoObject*>(savedLocals->getAt(ctx, i));
            }
        }
        
        // Restore stack after slots are ready
        proto::ProtoObject** stackBase = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals()) + stackOffset;
        unsigned int maxStack = calleeCtx->getAutomaticLocalsCount() - stackOffset;
        
        if (slist) {
            unsigned long sSize = slist->getSize(calleeCtx);
            for (unsigned long i = 0; i < sSize && i < maxStack; ++i) {
                stackBase[initialTop++] = const_cast<proto::ProtoObject*>(slist->getAt(calleeCtx, static_cast<int>(i)));
            }
        }
        
        if (pc > 0) {
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
        for (unsigned int i = 0; i < calleeCtx->getAutomaticLocalsCount(); ++i) {
            newLocals = newLocals->appendLast(calleeCtx, updatedSlots[i]);
        }
        self->setAttribute(calleeCtx, env->getGiLocalsString(), newLocals->asObject(calleeCtx));

        // Save stack back while calleeCtx is still alive
        const proto::ProtoList* newStack = calleeCtx->newList();
        const proto::ProtoObject** slots = calleeCtx->getAutomaticLocals();
        for (unsigned long j = 0; j < finalTop; ++j) {
            newStack = newStack->appendLast(calleeCtx, slots[stackOffset + j]);
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

const proto::ProtoObject* py_generator_send(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* val = (posArgs && posArgs->getSize(ctx) > 0) ? posArgs->getAt(ctx, 0) : PROTO_NONE;
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

    try {
        py_generator_send_impl(ctx, self, PROTO_NONE, genExit);
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
    }
    return PROTO_NONE;
}

const proto::ProtoObject* invokePythonCallable(proto::ProtoContext* ctx,
    const proto::ProtoObject* callable, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return invokeCallable(ctx, callable, args, kwargs);
}



static void checkSTW(proto::ProtoContext* ctx) {
    if (ctx && ctx->thread) {
        ctx->thread->synchToGC();
    }
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
        obj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        obj = const_cast<proto::ProtoObject*>(obj->addParent(ctx, self));
        const proto::ProtoString* classS = env ? env->getClassString() : protoPython::PythonEnvironment::getInternalString(ctx, "__class__");
        obj = const_cast<proto::ProtoObject*>(obj->setAttribute(ctx, classS, self));
    }
    
    // Invoke __init__
    // Magic methods should be looked up on the class, not the instance object's __dict__ directly.
    if (obj && obj != PROTO_NONE) {
        bool isInstanceOfSelf = false;
        if (env) {
            const proto::ProtoObject* objCls = env->getType(ctx, obj);
            isInstanceOfSelf = (objCls == self || (objCls && objCls->isInstanceOf(ctx, self) == PROTO_TRUE));
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
            // Use env->getAttribute to follow the Python MRO, not the raw protoCore chain.
            const proto::ProtoObject* initM = env ? env->getAttribute(ctx, self, initS) : self->getAttribute(ctx, initS);
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
    
    if (!env && std::getenv("PROTO_THREAD_DIAG")) {
        // log removed
    }
    
    FrameScope fscope(frame);
    unsigned long n = bytecode->getSize(ctx);
    if (n == 0) {
        if (get_env_diag()) {
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
    for (unsigned long i = pcStart; i <= pcEnd; ) {
        int op = bc[i];
        int arg = (i + 1 < n) ? bc[i + 1] : 0;
        unsigned long next_i = i + 2; 

        if (get_env_diag()) {
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
                
                if (get_env_diag()) {
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
            
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG: Calling addTraceback...\n");
                fflush(stderr);
            }
            env->addTraceback(exc, frame, static_cast<int>(i), ctx->currentLineNumber);
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG: addTraceback returned. blockStack.empty()=%s\n", blockStack.empty() ? "true" : "false");
                fflush(stderr);
            }
            
            if (!blockStack.empty()) {
                Block b = blockStack.back();
                if (get_env_diag()) {
                    fprintf(stderr, "DEBUG: Popping block: handlerPc=%lu stackDepth=%zu\n", b.handlerPc, b.stackDepth);
                    fflush(stderr);
                }
                blockStack.pop_back();

                if (exc && env) {
                    env->pushActiveException(exc);
                }
                env->clearPendingException();

                if (get_env_diag()) {
                    fprintf(stderr, "DEBUG: Cleaning stack: current size=%zu, target size=%zu\n", stack.size(), b.stackDepth);
                    fflush(stderr);
                }
                while (stack.size() > b.stackDepth) stack.pop_back();
                if (exc) {
                    if (b.isWithBlock) {
                        // with-block handler: stack is [..., __exit__]; push only exc
                        // so OP_WITH_CLEANUP sees [__exit__, exc] as expected
                        if (get_env_diag()) {
                            fprintf(stderr, "DEBUG: Pushing exc only for with-block handler %p\n", exc);
                            fflush(stderr);
                        }
                        stack.push_back(exc);
                    } else {
                        if (get_env_diag()) {
                            fprintf(stderr, "DEBUG: Pushing exception tuple (None, exc, exc) %p back to stack\n", exc);
                            fflush(stderr);
                        }
                        const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : PROTO_NONE;
                        stack.push_back(noneObj); // Traceback
                        stack.push_back(exc);     // Value
                        stack.push_back(exc);     // Type
                    }
                }

                if (get_env_diag()) {
                    fprintf(stderr, "DEBUG: Jumping to handlerPc=%lu\n", b.handlerPc);
                    fflush(stderr);
                }
                i = b.handlerPc;
                continue;
            }
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG: No trap found, returning nullptr\n");
                fflush(stderr);
            }
            return nullptr;
        }
        
        // Location updated at start of loop
        if ((i & 0x7FF) == 0) checkSTW(ctx);
        
        bool diag_env = get_env_diag();
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

        if (op == OP_LOAD_CONST) {
            // Use flat pre-fetched array when available (avoids cross-DSO AVL lookup).
            const proto::ProtoObject* val = nullptr;
            if (nativeConsts && static_cast<uint32_t>(arg) < static_cast<uint32_t>(constants->getSize(ctx))) {
                val = nativeConsts[arg];
            } else if (static_cast<unsigned long>(arg) < constants->getSize(ctx)) {
                val = constants->getAt(ctx, arg);
            }
            if (val) {
                if (get_env_diag()) {
                    fprintf(stderr, "DEBUG: LOAD_CONST arg=%d val=%p repr=%s\n", arg, (void*)val, PythonEnvironment::reprObject(ctx, val).c_str());
                    fflush(stderr);
                }
                stack.push_back(val);
            }
        } else if (op == OP_RETURN_VALUE) {
            if (stack.empty()) return PROTO_NONE;
            const proto::ProtoObject* ret = stack.back();
            ctx->returnValue = ret;
            if (outPc) *outPc = next_i; // Mark finished
            return ret;  /* exit block immediately; destructor will promote */
        } else if (op == OP_YIELD_VALUE) {
            if (stack.empty()) return PROTO_NONE;
            const proto::ProtoObject* ret = stack.back();
            stack.pop_back();
            ctx->returnValue = ret;
            if (yielded) *yielded = true;
            if (outPc) *outPc = next_i; // Resume at NEXT instruction
            if (finalTopPtr) *finalTopPtr = stack.top;
            if (externalBlockStack) *externalBlockStack = blockStack; // save try/except handlers
            return ret;
        } else if (op == OP_GET_YIELD_FROM_ITER) {
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
        } else if (op == OP_YIELD_FROM) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* sendVal = stack.back();
            const proto::ProtoObject* iterator = stack[stack.top - 2];
            // ... YIELD_FROM logic continues, but for now we just fix the pop ...
            // Wait, I should see more of YIELD_FROM to be safe.
            // Let's postpone YIELD_FROM if it's complex.
            // Actually, keep it simple for now as it's a stub or partial impl usually.
            stack.pop_back(); // Pop sendVal
            // (iterator remains on stack)
            const proto::ProtoObject* subIter = stack.back();
            
            const proto::ProtoString* sendS = env ? env->getSendString() : PythonEnvironment::getInternedString(ctx, "send");
            const proto::ProtoObject* sendMethod = subIter->getAttribute(ctx, sendS);
            const proto::ProtoObject* result = nullptr;
            
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

            if (env && env->hasPendingException()) {
                const proto::ProtoObject* exc = env->peekPendingException();
                if (get_env_diag()) {
                }
                if (env->isStopIteration(ctx, exc)) {
                    const proto::ProtoObject* stopVal = env->getStopIterationValue(ctx, exc);
                    if (get_env_diag()) {
                        // StopIteration diagnostic removed
                    }
                    if (get_env_diag()) {
                    }
                    env->clearPendingException();
                    stack.pop_back(); // Remove subIter
                    stack.push_back(stopVal);
                    // Will continue to i+1
                } else {
                    continue;
                }
            } else {
                // Yielded. subIter is still at stack.back().
                if (get_env_diag()) {
                    // Yielded result diagnostic removed
                }
                ctx->returnValue = result;
                if (yielded) *yielded = true;
                if (outPc) *outPc = next_i;
                if (finalTopPtr) *finalTopPtr = stack.top;
                if (externalBlockStack) {
                    *externalBlockStack = blockStack;
                }
                return result;
            }
        } else if (op == OP_LOAD_NAME) {
            int nameIdx = arg >> 1;
            bool pushNull = (arg & 0x01);
            if (names && frame && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (nameObj->isString(ctx)) {
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
                    if (!found) {
                        val = frame->getAttribute(ctx, nameS);
                        if (val != nullptr) found = true;
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
        } else if (op == OP_STORE_NAME) {
            int nameIdx = arg >> 1;
            if (names && frame && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                if (stack.empty()) {
                    // ... error handling ...
                    i = next_i; continue;
                }
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                const proto::ProtoObject* val = stack.back();
                // Delay pop until done
                if (nameObj->isString(ctx)) {
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
                                if (get_env_diag()) {
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
                    if (isNewKey) {
                        const proto::ProtoObject* keysObj = frame->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__keys__"));
                        const proto::ProtoList* keysList = (keysObj && keysObj->asList(ctx)) ? keysObj->asList(ctx) : ctx->newList();
                        keysList = keysList->appendLast(ctx, nameObj);
                        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__keys__"), keysList->asObject(ctx)));
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
        } else if (op == OP_LOAD_FAST) {
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<unsigned long>(arg) < nSlots) {
                const proto::ProtoObject** slots = ctx->getAutomaticLocals();
                const proto::ProtoObject* val = slots[arg];
                if (get_env_diag() && arg == 0) {
                     fprintf(stderr, "DEBUG: LOAD_FAST 0 loaded: %p\n", (void*)val);
                     fflush(stderr);
                }
                stack.push_back(val ? val : (env ? env->getNonePrototype() : PROTO_NONE));
            } else {
                if (get_env_diag()) {
                    // LOAD_FAST range diagnostic removed
                }
                stack.push_back(PROTO_NONE);
            }
        } else if (op == OP_STORE_FAST) {
            if (stack.empty()) { i = next_i; continue; }
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<unsigned long>(arg) < nSlots) {
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(ctx->getAutomaticLocals());
                slots[arg] = const_cast<proto::ProtoObject*>(val);
            }
        } else if (op == OP_BINARY_ADD) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryAdd(ctx, a, b);
            stack.pop_back(); // Pop b
            stack.back() = r; // Replace a with r
        } else if (op == OP_INPLACE_ADD) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            
            const proto::ProtoObject* iadd = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIAddString() : PythonEnvironment::getInternedString(ctx, "__iadd__"));
            if (iadd && iadd->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = iadd->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); // Pop b
                stack.back() = result ? result : PROTO_NONE;
            } else {
                const proto::ProtoObject* r = binaryAdd(ctx, a, b);
                stack.pop_back(); // Pop b
                stack.back() = r;
            }
        } else if (op == OP_BINARY_SUBTRACT) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = binarySubtract(ctx, a, b);
            stack.pop_back(); // Pop b
            stack.back() = r;
            if (!r && env && env->hasPendingException()) continue;
        } else if (op == OP_INPLACE_SUBTRACT) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* isub = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getISubString() : PythonEnvironment::getInternedString(ctx, "__isub__"));
            if (isub && isub->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = isub->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binarySubtract(ctx, a, b);
                stack.push_back(r);
            }
        } else if (op == OP_BINARY_MULTIPLY) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryMultiply(ctx, a, b);
            stack.pop_back(); // Pop b
            stack.back() = r;
        } else if (op == OP_INPLACE_MULTIPLY) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* imul = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIMulString() : PythonEnvironment::getInternedString(ctx, "__imul__"));
            if (imul && imul->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = imul->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); // Pop b
                stack.back() = result ? result : PROTO_NONE;
            } else {
                const proto::ProtoObject* r = binaryMultiply(ctx, a, b);
                stack.pop_back(); // Pop b
                stack.back() = r;
            }
        } else if (op == OP_BINARY_TRUE_DIVIDE) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryTrueDivide(ctx, a, b);
            stack.pop_back(); // Pop b
            stack.back() = r;
        } else if (op == OP_BINARY_MODULO) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* right = stack.back();
            const proto::ProtoObject* left = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryModulo(ctx, left, right);
            stack.pop_back();
            stack.back() = r;
        } else if (op == OP_BINARY_MATRIX_MULTIPLY) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* right = stack.back();
            const proto::ProtoObject* left = stack[stack.top - 2];
            const proto::ProtoString* matmulS = protoPython::PythonEnvironment::getInternalString(ctx, "__matmul__");
            const proto::ProtoObject* matmul = left->getAttribute(ctx, matmulS);
            if (matmul && matmul != PROTO_NONE) {
                const proto::ProtoObject* res = invokePythonCallable(ctx, matmul, ctx->newList()->appendLast(ctx, right), nullptr);
                stack.pop_back();
                stack.back() = res;
            } else {
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                if (env) env->setPendingException(PythonEnvironment::getInternedString(ctx, "TypeError: '@' operator not supported (stubbed)")->asObject(ctx));
            }
        } else if (op == OP_INPLACE_MATRIX_MULTIPLY) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* right = stack.back();
            const proto::ProtoObject* left = stack[stack.top - 2];
            const proto::ProtoString* imatmulS = protoPython::PythonEnvironment::getInternalString(ctx, "__imatmul__");
            const proto::ProtoObject* imatmul = left->getAttribute(ctx, imatmulS);
            if (imatmul && imatmul != PROTO_NONE) {
                stack.push_back(invokePythonCallable(ctx, imatmul, ctx->newList()->appendLast(ctx, right), nullptr));
            } else {
                // fallback to matmul
                const proto::ProtoString* matmulS = protoPython::PythonEnvironment::getInternalString(ctx, "__matmul__");
                const proto::ProtoObject* matmul = left->getAttribute(ctx, matmulS);
                if (matmul && matmul != PROTO_NONE) {
                    stack.push_back(invokePythonCallable(ctx, matmul, ctx->newList()->appendLast(ctx, right), nullptr));
                } else {
                    env->setPendingException(PythonEnvironment::getInternedString(ctx, "TypeError: '@=' operator not supported (stubbed)")->asObject(ctx));
                }
            }
        } else if (op == OP_RERAISE) {
            // Re-raise the exception on top of block stack
            if (!env) { i = next_i; continue; }
            // stub: if we have a pending exception, just continue to trigger handler search
            continue;
        } else if (op == OP_JUMP_FORWARD) {
            i = next_i + arg;
            continue;
        } else if (op == OP_POP_EXCEPT) {
            if (env) {
                env->popActiveException();
                // popActiveException (e.g. list removeAt) can set TypeError; clear so module continues
                if (env->hasPendingException()) env->clearPendingException();
            }
        } else if (op == OP_BINARY_POWER) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryPower(ctx, a, b);
            stack.pop_back();
            stack.back() = r;
        } else if (op == OP_BINARY_FLOOR_DIVIDE) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = binaryFloorDivide(ctx, a, b);
            stack.pop_back();
            stack.back() = r;
        } else if (op == OP_INPLACE_TRUE_DIVIDE) {
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
        } else if (op == OP_INPLACE_FLOOR_DIVIDE) {
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
        } else if (op == OP_INPLACE_MODULO) {
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
        } else if (op == OP_INPLACE_POWER) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* ipow = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIPowString() : PythonEnvironment::getInternedString(ctx, "__ipow__"));
            if (ipow && ipow->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ipow->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else {
                const proto::ProtoObject* r = binaryPower(ctx, a, b);
                stack.pop_back(); stack.back() = r;
            }
        } else if (op == OP_INPLACE_LSHIFT) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* ilshift = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getILShiftString() : PythonEnvironment::getInternedString(ctx, "__ilshift__"));
            if (ilshift && ilshift->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ilshift->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = static_cast<long long>(static_cast<unsigned long long>(av) << bv);
                stack.pop_back(); stack.back() = ctx->fromInteger(av);
            }
        } else if (op == OP_INPLACE_RSHIFT) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* irshift = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIRShiftString() : PythonEnvironment::getInternedString(ctx, "__irshift__"));
            if (irshift && irshift->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = irshift->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = av >> bv;
                stack.pop_back(); stack.back() = ctx->fromInteger(av);
            }
        } else if (op == OP_INPLACE_AND) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* iand = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIAndString() : PythonEnvironment::getInternedString(ctx, "__iand__"));
            if (iand && iand->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = iand->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = ctx->fromInteger(a->asLong(ctx) & b->asLong(ctx));
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoObject* andM = a->getAttribute(ctx, env ? env->getAndString() : PythonEnvironment::getInternedString(ctx, "__and__"));
                if (andM && andM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = andM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* randM = b->getAttribute(ctx, env ? env->getRAndString() : PythonEnvironment::getInternedString(ctx, "__rand__"));
                    if (randM && randM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = randM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                    }
                }
            }
        } else if (op == OP_INPLACE_OR) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* ior = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIOrString() : PythonEnvironment::getInternedString(ctx, "__ior__"));
            if (ior && ior->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ior->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = ctx->fromInteger(a->asLong(ctx) | b->asLong(ctx));
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoObject* orM = a->getAttribute(ctx, env ? env->getOrString() : PythonEnvironment::getInternedString(ctx, "__or__"));
                if (orM && orM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = orM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* rorM = b->getAttribute(ctx, env ? env->getROrString() : PythonEnvironment::getInternedString(ctx, "__ror__"));
                    if (rorM && rorM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = rorM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                    }
                }
            }
        } else if (op == OP_INPLACE_XOR) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* ixor = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIXorString() : PythonEnvironment::getInternedString(ctx, "__ixor__"));
            if (ixor && ixor->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ixor->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = ctx->fromInteger(a->asLong(ctx) ^ b->asLong(ctx));
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoObject* xorM = a->getAttribute(ctx, env ? env->getXorString() : PythonEnvironment::getInternedString(ctx, "__xor__"));
                if (xorM && xorM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = xorM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* rxorM = b->getAttribute(ctx, env ? env->getRXorString() : PythonEnvironment::getInternedString(ctx, "__rxor__"));
                    if (rxorM && rxorM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = rxorM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                    }
                }
            }
        } else if (op == OP_BINARY_LSHIFT) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = static_cast<long long>(static_cast<unsigned long long>(av) << bv);
                stack.pop_back(); stack.back() = ctx->fromInteger(av);
            }
        } else if (op == OP_BINARY_RSHIFT) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = av >> bv;
                stack.pop_back(); stack.back() = ctx->fromInteger(av);
            }
        } else if (op == OP_BINARY_AND) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                stack.pop_back(); stack.back() = ctx->fromInteger(a->asLong(ctx) & b->asLong(ctx));
            } else {
                const proto::ProtoObject* andM = a->getAttribute(ctx, env ? env->getAndString() : PythonEnvironment::getInternedString(ctx, "__and__"));
                if (andM && andM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = andM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* randM = b->getAttribute(ctx, env ? env->getRAndString() : PythonEnvironment::getInternedString(ctx, "__rand__"));
                    if (randM && randM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = randM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                    }
                }
            }
        } else if (op == OP_BINARY_OR) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = ctx->fromInteger(a->asLong(ctx) | b->asLong(ctx));
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoString* orS = env ? env->getOrString() : PythonEnvironment::getInternedString(ctx, "__or__");
                const proto::ProtoList* argsB = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = invokeDunder(ctx, a, orS, argsB);

                if (!result) {
                    const proto::ProtoString* rorS = env ? env->getROrString() : PythonEnvironment::getInternedString(ctx, "__ror__");
                    const proto::ProtoList* argsA = ctx->newList()->appendLast(ctx, a);
                    result = invokeDunder(ctx, b, rorS, argsA);
                }

                stack.pop_back();
                stack.back() = (result ? result : PROTO_NONE);
            }
        } else if (op == OP_BINARY_XOR) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = ctx->fromInteger(a->asLong(ctx) ^ b->asLong(ctx));
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoObject* xorM = a->getAttribute(ctx, env ? env->getXorString() : PythonEnvironment::getInternedString(ctx, "__xor__"));
                if (xorM && xorM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = xorM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* rxorM = b->getAttribute(ctx, env ? env->getRXorString() : PythonEnvironment::getInternedString(ctx, "__rxor__"));
                    if (rxorM && rxorM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = rxorM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                    }
                }
            }
        } else if (op == OP_UNARY_NEGATIVE) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* a = stack.back();
            if (a->isInteger(ctx)) {
                const proto::ProtoObject* res = ctx->fromInteger(-a->asLong(ctx));
                stack.back() = res;
            } else if (a->isDouble(ctx)) {
                const proto::ProtoObject* res = ctx->fromDouble(-a->asDouble(ctx));
                stack.back() = res;
            }
        } else if (op == OP_UNARY_NOT) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* a = stack.back();
            stack.back() = isTruthy(ctx, a) ? PROTO_FALSE : PROTO_TRUE;
        } else if (op == OP_UNARY_INVERT) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* a = stack.back();
            if (a->isInteger(ctx)) {
                long long n = a->asLong(ctx);
                stack.back() = ctx->fromInteger(static_cast<long long>(~static_cast<unsigned long long>(n)));
            } else {
                const proto::ProtoObject* inv = a->getAttribute(ctx, env ? env->getInvertString() : PythonEnvironment::getInternedString(ctx, "__invert__"));
                if (inv && inv->asMethod(ctx)) {
                    const proto::ProtoList* noArgs = ctx->newList();
                    const proto::ProtoObject* result = inv->asMethod(ctx)(ctx, a, nullptr, noArgs, nullptr);
                    if (get_env_diag()) {
                    }
                    stack.back() = (result ? result : PROTO_NONE);
                } else {
                    stack.pop_back();
                }
            }
        } else if (op == OP_RAISE_VARARGS) {
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
                     if (env && env->getTypePrototype()) {
                         const proto::ProtoObject* cls = exc->getAttribute(ctx, env->getClassString());
                         if (cls == env->getTypePrototype()) {
                             exc = invokePythonCallable(ctx, exc, ctx->newList(), nullptr);
                         }
                     }
                     if (env && exc && exc != PROTO_NONE) env->setPendingException(exc);
                 }
                 i = next_i;
                 continue;
            }
            i = next_i;
            continue;
        }
 else if (op == OP_IMPORT_STAR) {
            if (stack.size() < 1) { i = next_i; continue; }
            const proto::ProtoObject* mod = stack.back();
            // mod remains on stack during attribute iteration
            if (mod && mod != PROTO_NONE) {
                if (std::getenv("PROTO_RESOLVE_DIAG")) {
                }
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
                            if (nameObj->isString(ctx)) {
                                const proto::ProtoString* nameS = nameObj->asString(ctx);
                                const proto::ProtoObject* val = mod->getAttribute(ctx, nameS);
                                if (val) {
                                    frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, nameS, val));
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
                            if (nameObj && nameObj->isString(ctx)) {
                                std::string n;
                                nameObj->asString(ctx)->toUTF8String(ctx, n);
                                if (n.empty() || n[0] == '_') {
                                    it = it->advance(ctx);
                                    continue;
                                }
                                const proto::ProtoObject* val = mod->getAttribute(ctx, nameObj->asString(ctx));
                                if (val) {
                                    frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, nameObj->asString(ctx), val));
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
        } else if (op == OP_IMPORT_FROM) {
            int nameIdx = arg >> 1;
            if (names && stack.size() >= 1 && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* mod = stack.back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                
                if (nameObj->isString(ctx)) {
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
        } else if (op == OP_SETUP_WITH) {
            if (stack.size() < 1) { i = next_i; continue; }
            const proto::ProtoObject* manager = stack.back();
            stack.pop_back();
            
            const proto::ProtoString* enterS = env ? env->getEnterString() : PythonEnvironment::getInternedString(ctx, "__enter__");
            const proto::ProtoString* exitS = env ? env->getExitString() : PythonEnvironment::getInternedString(ctx, "__exit__");
            
            const proto::ProtoObject* exitM = env ? env->getAttribute(ctx, manager, exitS) : manager->getAttribute(ctx, exitS);
            stack.push_back(exitM ? exitM : (const proto::ProtoObject*)PROTO_NONE);
            
            const proto::ProtoObject* enterM = env ? env->getAttribute(ctx, manager, enterS) : manager->getAttribute(ctx, enterS);
            const proto::ProtoObject* enterResult = nullptr;
            if (enterM && enterM != PROTO_NONE) {
                const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
                enterResult = invokeCallable(ctx, enterM, emptyL);
            } else {
                enterResult = manager;
            }
            if (!enterResult && env && env->hasPendingException()) continue;
            
            // Push block pointing to handler at arg (absolute PC)
            blockStack.push_back({static_cast<unsigned long>(arg), stack.size(), true});
            
            stack.push_back(enterResult);
        } else if (op == OP_WITH_CLEANUP) {
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
        } else if (op == OP_POP_TOP) {
            if (!stack.empty()) {
                stack.pop_back();
            }
        } else if (op == OP_UNARY_POSITIVE) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* a = stack.back();
            const proto::ProtoObject* pos = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getPosString() : PythonEnvironment::getInternedString(ctx, "__pos__"));
            if (pos && pos->asMethod(ctx)) {
                const proto::ProtoList* noArgs = ctx->newList();
                const proto::ProtoObject* result = pos->asMethod(ctx)(ctx, a, nullptr, noArgs, nullptr);
                stack.back() = (result ? result : PROTO_NONE);
            } else {
                // remains a on stack
            }
        } else if (op == OP_NOP) {
        } else if (op == OP_COMPARE_OP) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* b = stack.back();
            const proto::ProtoObject* a = stack[stack.top - 2];
            const proto::ProtoObject* r = compareOp(ctx, a, b, arg);
            stack.pop_back();
            stack.back() = (r ? r : PROTO_NONE);
            if (!r && env && env->hasPendingException()) continue;
            // (Note: if r is null, we set PROTO_NONE to avoid null on stack)
        } else if (op == OP_POP_JUMP_IF_FALSE) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (!isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n) {
                i = static_cast<unsigned long>(arg);
                continue;
            }
        } else if (op == OP_POP_JUMP_IF_TRUE) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n) {
                i = static_cast<unsigned long>(arg);
                continue;
            }
        } else if (op == OP_LIST_APPEND) {
            if (stack.size() >= static_cast<size_t>(arg)) {
                const proto::ProtoObject* val = stack.back();
                // val remains on stack
                proto::ProtoObject* lstObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoObject* data = lstObj->getAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"));
                if (data && data->asList(ctx)) {
                    const proto::ProtoList* lst = data->asList(ctx);
                    lst = lst->appendLast(ctx, val);
                    if (get_env_diag()) {
                        fprintf(stderr, "DEBUG: OP_LIST_APPEND val=%p appended to list, new size=%zu\n", (void*)val, lst->getSize(ctx));
                    }
                    const proto::ProtoObject* newLst = lstObj->setAttribute(ctx, env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__"), lst->asObject(ctx));
                    stack[stack.size() - arg - 1] = const_cast<proto::ProtoObject*>(newLst);
                }
                stack.pop_back(); // Pop val now
            }
        } else if (op == OP_MAP_ADD) {
            if (stack.size() >= static_cast<size_t>(arg) + 1) { // key, val + mapObj must be there
                const proto::ProtoObject* key = stack.back();
                const proto::ProtoObject* val = stack[stack.top - 2];
                proto::ProtoObject* mapObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = mapObj->getAttribute(ctx, dataString);
                if (data && data->asSparseList(ctx)) {
                    const proto::ProtoSparseList* sl = data->asSparseList(ctx);
                    unsigned long h = key->getHash(ctx);
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
        } else if (op == OP_SET_ADD) {
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
        } else if (op == OP_DICT_UPDATE) {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* from = stack.back();
                // from remains on stack
                proto::ProtoObject* toObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* toData = toObj->getAttribute(ctx, dataString);
                if (toData && toData->asSparseList(ctx)) {
                    const proto::ProtoSparseList* toSL = toData->asSparseList(ctx);
                    const proto::ProtoObject* fromData = from->getAttribute(ctx, dataString);
                    if (fromData && fromData->asSparseList(ctx)) {
                        const proto::ProtoSparseList* fromSL = fromData->asSparseList(ctx);
                        const proto::ProtoString* keysName = protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                        const proto::ProtoObject* fromKeysObj = from->getAttribute(ctx, keysName);
                        if (fromKeysObj && fromKeysObj->asList(ctx)) {
                            const proto::ProtoList* fromKeys = fromKeysObj->asList(ctx);
                            const proto::ProtoObject* toKeysObj = toObj->getAttribute(ctx, keysName);
                            const proto::ProtoList* toKeys = (toKeysObj && toKeysObj->asList(ctx)) ? toKeysObj->asList(ctx) : ctx->newList();
                            for (unsigned long j = 0; j < fromKeys->getSize(ctx); ++j) {
                                const proto::ProtoObject* k = fromKeys->getAt(ctx, j);
                                unsigned long h = k->getHash(ctx);
                                const proto::ProtoObject* v = fromSL->getAt(ctx, h);
                                
                                bool isNew = !toSL->has(ctx, h);
                                toSL = toSL->setAt(ctx, h, v);
                                if (isNew) toKeys = toKeys->appendLast(ctx, k);
                            }
                            toObj->setAttribute(ctx, keysName, toKeys->asObject(ctx));
                            toObj->setAttribute(ctx, dataString, toSL->asObject(ctx));
                        }
                    }
                }
                stack.pop_back(); // Pop from
            }
        } else if (op == OP_LIST_EXTEND) {
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
                        }
                    }
                }
                stack.pop_back(); // Pop iterable
            }
        } else if (op == OP_SET_UPDATE) {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* iterable = stack.back();
                // iterable remains on stack
                proto::ProtoObject* setObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* dataObj = setObj->getAttribute(ctx, dataString);
                if (dataObj && dataObj->asSet(ctx)) {
                    const proto::ProtoSet* s = dataObj->asSet(ctx);
                    const proto::ProtoObject* fromData = iterable->getAttribute(ctx, dataString);
                    const proto::ProtoList* fromList = (fromData && fromData->asList(ctx)) ? fromData->asList(ctx) : iterable->asList(ctx);
                    if (fromList) {
                        for (unsigned long j = 0; j < fromList->getSize(ctx); ++j) {
                            s = s->add(ctx, fromList->getAt(ctx, j));
                        }
                        setObj->setAttribute(ctx, dataString, s->asObject(ctx));
                    }
                }
                stack.pop_back(); // Pop iterable
            }
        }
 else if (op == OP_BUILD_SET) {
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
            if (get_env_diag()) {
                const proto::ProtoObject* ftype = env ? env->getType(ctx, finalSet) : nullptr;
                std::string frepr = env ? env->reprObject(ctx, finalSet) : "???";
                fprintf(stderr, "DEBUG OP_BUILD_SET: finalSet=%p type=%p repr=%s proto=%p arg=%d\n", (void*)finalSet, (void*)ftype, frepr.c_str(), (void*)(env ? env->getSetPrototype() : nullptr), arg);
                fflush(stderr);
            }
            for (int j = 0; j < arg + 2; ++j) stack.pop_back();
            stack.push_back(finalSet);
        } else if (op == OP_FORMAT_VALUE) {
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
        } else if (op == OP_PUSH_NULL) {
            stack.push_back(nullptr);
        } else if (op == OP_BUILD_STRING) {
            if (stack.size() < static_cast<size_t>(arg)) continue;
            // GC safe: elements remain on stack until buildString returns
            const proto::ProtoObject** partsPtr = (const proto::ProtoObject**)(&stack[stack.size() - arg]);
            const proto::ProtoObject* res = env->buildString(partsPtr, arg);
            for (int j = 0; j < arg; ++j) stack.pop_back();
            stack.push_back(res);
        } else if (op == OP_LOAD_DEREF) {
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj && nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    unsigned long h = nameObj->getHash(ctx);
                    if (get_env_diag()) {
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
                        
                        val = curr->getAttribute(ctx, nameS);
                        // getAttribute returns nullptr when not found; PROTO_NONE is a valid Python None value
                        if (val != nullptr) { found = true; break; }

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

                    const proto::ProtoString* dName = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                    const proto::ProtoObject* dataObj = curr->getAttribute(ctx, dName);
                    if (dataObj && dataObj->asSparseList(ctx)) {
                        val = dataObj->asSparseList(ctx)->getAt(ctx, h);
                        // asSparseList->getAt returns nullptr when key not found
                        if (val != nullptr) { found = true; break; }
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
        } else if (op == OP_STORE_DEREF) {
            if (names && stack.size() >= 1 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj && nameObj->isString(ctx)) {
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
        } else if (op == OP_JUMP_ABSOLUTE) {
            if (arg >= 0 && static_cast<unsigned long>(arg) < n) {
                i = static_cast<unsigned long>(arg);
                continue;
            }
        } else if (op == OP_LOAD_ATTR) {
            bool pushNull = (arg & 1);
            int nameIdx = arg >> 1;
            if (names && stack.size() >= 1 && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* attrName = nameObj->asString(ctx);
                    if (get_env_diag()) {
                        std::string attrNameStr;
                        attrName->toUTF8String(ctx, attrNameStr);
                        fprintf(stderr, "DEBUG: OP_LOAD_ATTR calling getAttribute env=%p obj=%p attr=%s\n", (void*)env, (void*)obj, attrNameStr.c_str());
                        fflush(stderr);
                    }

                    // Fast path: plain instance own-attribute read (e.g. self.field).
                    // Bypasses RecursionScope, isActuallyAClass, MRO walk, descriptor
                    // protocol, and the V94 double mutable_ref resolution.
                    //
                    // V95: Uses ProtoObject::getOwnAttributeDirect() — resolves mutable state
                    // once and does a single own-attributes lookup, replacing the V94 pattern
                    // of hasOwnAttribute (no cache, 2 traversals) + getAttribute (cache hit).
                    //
                    // Invariant: co_names entries are POINTER_TAG_SYMBOL (Compiler::addName
                    // calls getInternedString → createSymbol), so the interned pointer IS the
                    // stable sparse-list key — no symbolTable mutex fires inside the call.
                    const proto::ProtoObject* val = nullptr;
                    bool fastPathTaken = false;
                    {
                        const proto::ProtoString* isPyClsS = env ? PythonEnvironment::getInternedString(ctx, "__is_python_class__") : nullptr;
                        bool objIsPyClass = isPyClsS && obj->hasOwnAttribute(ctx, isPyClsS) == PROTO_TRUE;
                        if (env && obj != PROTO_NONE && !objIsPyClass
                                && !obj->isString(ctx) && !obj->isInteger(ctx) && !obj->isBoolean(ctx)) {
                            const proto::ProtoObject* fv = obj->getOwnAttributeDirect(ctx, attrName);
                            if (fv && fv != PROTO_NONE && !fv->isMethod(ctx)) {
                                if (pushNull) {
                                    stack.back() = nullptr;
                                    stack.push_back(fv);
                                } else {
                                    stack.back() = fv;
                                }
                                fastPathTaken = true;
                            }
                        }
                    }
                    if (fastPathTaken) { i = next_i; continue; }

                    // Slow path: full Python attribute protocol (descriptors, MRO, __getattr__).
                    // Use raiseError=false so we can try __getattr__ before raising AttributeError.
                    val = env ? env->getAttribute(ctx, obj, attrName, false) : obj->getAttribute(ctx, attrName);
                    if (!val && env && env->hasPendingException()) {
                        // A descriptor or __getattr__ already raised an exception — propagate it.
                        stack.pop_back();
                        continue;
                    }

                    if (get_env_diag()) {
                        fprintf(stderr, "DEBUG: OP_LOAD_ATTR returned val=%p\n", (void*)val);
                        fflush(stderr);
                    }

                    bool isMissing = false;
                    // Short-circuit: if val is non-null and non-PROTO_NONE, the attribute was found.
                    // Only do the expensive hasAttribute check when val == PROTO_NONE to distinguish
                    // "explicitly stored None" from "attribute absent".
                    bool attrNotFound = (!val) || (val == PROTO_NONE && obj->hasAttribute(ctx, attrName) == PROTO_FALSE);
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
                            // Search on class MRO
                            const proto::ProtoObject* cls = obj->getAttribute(ctx, env ? env->getClassString() : PythonEnvironment::getInternedString(ctx, "__class__"));
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

                            if (actualVal->isMethod(ctx) && actualVal->asMethodSelf(ctx) != nullptr) {
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
                                if (!method || !selfObj) {
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
                                if (get_env_diag()) {
                                     fprintf(stderr, "DEBUG_OP_LOAD_ATTR: val is NULL, pushing None! (no pending exc)\n");
                                     fflush(stderr);
                                }
                                val = (env ? env->getNonePrototype() : PROTO_NONE);
                            }
                            stack.back() = val; // Replace obj with result
                        }
                    } else {
                        stack.pop_back(); // Pop obj before raising error
                        std::string attr;
                        attrName->toUTF8String(ctx, attr);
    if (get_env_diag()) { fprintf(stderr, "!!! ExecEngine raiseAttrError: obj=%p attr=%s\n", (void*)obj, attr.c_str()); fflush(stderr); }
                        if (env) env->raiseAttributeError(ctx, obj, attr);
                        continue;
                    }
                }
            }
        } else if (op == OP_STORE_ATTR) {
            int nameIdx = arg >> 1;
            if (names && stack.size() >= 2 && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                const proto::ProtoObject* val = stack[stack.top - 2];
                // Delay pop
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    proto::ProtoObject* oldObj = const_cast<proto::ProtoObject*>(obj);
                    const proto::ProtoObject* newObj = nullptr;
                    if (env) {
                        newObj = env->setAttribute(ctx, obj, nameS, val);
                    } else {
                        proto::ProtoObject* mutableObj = const_cast<proto::ProtoObject*>(obj);
                        newObj = mutableObj->setAttribute(ctx, nameS, val);
                    }
                    if (newObj != oldObj) {
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
        } else if (op == OP_BUILD_LIST) {
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
        } else if (op == OP_BINARY_SUBSCR) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* key = stack.back();
            const proto::ProtoObject* container = stack[stack.top - 2];

            const proto::ProtoString* getItemS = env ? env->getGetItemString() : PythonEnvironment::getInternedString(ctx, "__getitem__");
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
            if (std::getenv("PROTO_SUBSCR_DIAG")) {
                std::string ks = "?";
                if (key && key->isString(ctx)) key->asString(ctx)->toUTF8String(ctx, ks);
                const proto::ProtoObject* getItemMethod = env ? env->getAttribute(ctx, container, getItemS, false) : container->getAttribute(ctx, getItemS);
                fprintf(stderr, "DEBUG_SUBSCR: key='%s' container=%p __getitem__=%p method=%p\n",
                        ks.c_str(), (void*)container, (void*)getItemMethod,
                        getItemMethod ? (void*)getItemMethod->asMethod(ctx) : nullptr);
                fflush(stderr);
            }
            const proto::ProtoObject* result = invokeDunder(ctx, container, getItemS, args);
            
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG: OP_BINARY_SUBSCR container=%p repr=%s key=%p repr=%s\n", (void*)container, PythonEnvironment::reprObject(ctx, container).c_str(), (void*)key, PythonEnvironment::reprObject(ctx, key).c_str());
                fflush(stderr);
            }
            if (!result) {
                const proto::ProtoString* classGetItemS = PythonEnvironment::getInternedString(ctx, "__class_getitem__");
                // Check if container itself has __class_getitem__ (for types) via invokeDunder
                result = invokeDunder(ctx, container, classGetItemS, args);
            }

            if (get_env_diag()) {
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
                        
                        if (get_env_diag()) {
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
                        if (std::getenv("PROTO_SUBSCR_DIAG")) {
                            std::string ks2 = "?";
                            if (key && key->isString(ctx)) key->asString(ctx)->toUTF8String(ctx, ks2);
                            fprintf(stderr, "DEBUG_SUBSCR_FALLBACK: key='%s' hash=%lu dict=%p size=%lu has=%d\n",
                                    ks2.c_str(), h, (void*)data->asSparseList(ctx),
                                    data->asSparseList(ctx)->getSize(ctx),
                                    (int)data->asSparseList(ctx)->has(ctx, h));
                            fflush(stderr);
                        }
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
        } else if (op == OP_BUILD_MAP) {
            if (stack.size() < static_cast<size_t>(arg * 2)) continue;
            const proto::ProtoSparseList* data = ctx->newSparseList();
            stack.push_back(data->asObject(ctx)); // Root data
            const proto::ProtoList* keys = ctx->newList();
            stack.push_back(keys->asObject(ctx)); // Root keys
            
            size_t baseIdx = stack.size() - 2 - 2 * arg;
            for (int k = 0; k < arg; ++k) {
                const proto::ProtoObject* key = stack[baseIdx + 2 * k];
                const proto::ProtoObject* val = stack[baseIdx + 2 * k + 1];
                data = data->setAt(ctx, key->getHash(ctx), val);
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
        } else if (op == OP_STORE_SUBSCR) {
            // i++;
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG OP_STORE_SUBSCR stack.size()=%lu\n", (unsigned long)stack.size());
            }
            if (stack.size() < 3) { i = next_i; continue; }
            proto::ProtoObject* container = const_cast<proto::ProtoObject*>(stack[stack.top - 2]);
            const proto::ProtoObject* key = stack.back();
            const proto::ProtoObject* value = stack[stack.top - 3];
            
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG OP_STORE_SUBSCR: value=%p key=%p container=%p\n", (void*)value, (void*)key, (void*)container);
                fflush(stderr);
            }
            // Delay pop

            const proto::ProtoString* setItemS = env ? env->getSetItemString() : PythonEnvironment::getInternedString(ctx, "__setitem__");
            const proto::ProtoObject* setitem = container->getAttribute(ctx, setItemS);
            if (setitem && setitem != PROTO_NONE) {
                if (env && env->hasPendingException()) continue;
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, value);
                invokeDunder(ctx, container, setItemS, args);
            } else {
                // Fallback for objects/maps without __setitem__
                if (key->isString(ctx)) {
                    // FLAT approach: store as direct attribute if it's a string key
                    // This is ideal for Namespace objects (classes, modules)
                    if (get_env_diag()) {
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
        } else if (op == OP_CALL_FUNCTION_KW) {
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
        } else if (op == OP_CALL_FUNCTION) {
            if (stack.size() < (unsigned long)(arg + 1)) {
                 if (get_env_diag()) fprintf(stderr, "DEBUG: OP_CALL_FUNCTION FATAL underflow size=%lu arg=%d PC=%lu\n", (unsigned long)stack.size(), arg, i);
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
            
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG CALL: argc=%d top=%zu firstArgIdx=%lu X=%p Y=%p isModern=%d\n", 
                        arg, stack.top, firstArgIdx, (void*)X, (void*)Y, isModern);
            }

            const proto::ProtoObject* callable = nullptr;
            const proto::ProtoList* callArgs = nullptr;

            // User-function fast path: [NULL, func, arg1...argN] — bypass ProtoList construction.
            // Eliminates 2 cell allocations (newList + appendLast) per user-function call.
            // Opt 4: detect user functions via getOwnAttributeDirect on fnMetaCacheString instead of
            // hasOwnAttribute on codeString. Same cost, but avoids a redundant cache re-read inside
            // runUserFunctionCallRaw since the cache pointer is already resolved here.
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
            }

            const proto::ProtoObject* result = nullptr;
            if (usedFastPath) {
                result = result_fast;
            } else {
            const proto::ProtoList* args = ctx->newList();
            for (int j = 0; j < arg; ++j) {
                args = args->appendLast(ctx, stack[firstArgIdx + j]);
            }

            if (!isModern) {
                callable = Y; // In legacy, Y is the callable and there is no X.
                callArgs = args;
            } else if (X == nullptr) {
                // [NULL, Callable, Arg1...]
                callable = Y;
                callArgs = args;
            } else {
                // [Method, Self, Arg1...]
                callable = X;
                const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, Y);
                unsigned long asize = args->getSize(ctx);
                for (unsigned long j = 0; j < asize; ++j) {
                    selfArgs = selfArgs->appendLast(ctx, args->getAt(ctx, j));
                }
                callArgs = selfArgs;
            }

            if (!callable) {
                 if (get_env_diag()) fprintf(stderr, "DEBUG: OP_CALL_FUNCTION nullptr callable detected! PC=%lu\n", i);
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
        } else if (op == OP_CALL_FUNCTION_EX) {
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
            if (kwargs && env) {
                 const proto::ProtoObject* keysListObj = kwargs->getAttribute(ctx, protoPython::PythonEnvironment::getInternalString(ctx, "__keys__"));
                 if (keysListObj && keysListObj->asList(ctx)) {
                     env->pushKwNames(ctx->newTupleFromList(keysListObj->asList(ctx)));
                     pushed = true;
                 }
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
        } else if (op == OP_BUILD_TUPLE) {
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
        } else if (op == OP_BUILD_FUNCTION) {
            const proto::ProtoObject* kwDefaults = (arg & 0x02) ? stack.back() : nullptr;
            if (arg & 0x02) stack.pop_back();
            const proto::ProtoObject* defaults = (arg & 0x01) ? stack.back() : nullptr;
            if (arg & 0x01) stack.pop_back();

            if (!stack.empty() && frame) {
                const proto::ProtoObject* codeObj = stack.back();
                stack.pop_back();
                if (get_env_diag()) {
                    int line = -1;
                    const proto::ProtoObject* lineObj = codeObj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_firstlineno"));
                    if (lineObj && lineObj->isInteger(ctx)) line = (int)lineObj->asLong(ctx);
                    fprintf(stderr, "DEBUG: OP_BUILD_FUNCTION PC=%lu arg=0x%lx codeObj=%p (line %d) defaults=%p kwDefaults=%p\n", i, (unsigned long)arg, (void*)codeObj, line, (void*)defaults, (void*)kwDefaults);
                    fflush(stderr);
                }

                // Snapshot current CO_OPTIMIZED slot values into a mutable closure frame.
                // We use a mutable object as the closure so that after the function is built,
                // we can store the function under its own co_name — enabling self-referential
                // and forward-referencing closures without CPython-style cell objects.
                // All inner functions that capture this frame see the same mutable object,
                // so a later assignment (e.g. STORE_FAST inner) is reflected via the parent.
                proto::ProtoObject* closureFrame = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                closureFrame = const_cast<proto::ProtoObject*>(closureFrame->addParent(ctx, frame));
                // The frame stores the code object as f_code (not __code__)
                const proto::ProtoString* codeKey = env ? env->getFCodeString() : PythonEnvironment::getInternedString(ctx, "f_code");
                const proto::ProtoObject* outerCodeAttr = frame->getAttribute(ctx, codeKey);
                if (outerCodeAttr && outerCodeAttr != PROTO_NONE) {
                    const proto::ProtoObject* coVarnamesObj = outerCodeAttr->getAttribute(ctx, env ? env->getCoVarnamesString() : PythonEnvironment::getInternedString(ctx, "co_varnames"));
                    if (coVarnamesObj && coVarnamesObj != PROTO_NONE) {
                        const proto::ProtoTuple* coVarnames = coVarnamesObj->asTuple(ctx);
                        if (coVarnames) {
                            // Push closureFrame onto stack to keep it GC-rooted during setAttribute calls.
                            stack.push_back(closureFrame);
                            const proto::ProtoObject** outerSlots = ctx->getAutomaticLocals();
                            unsigned int outerNSlots = ctx->getAutomaticLocalsCount();
                            for (unsigned int j = 0; j < coVarnames->getSize(ctx); ++j) {
                                const proto::ProtoObject* vnameObj = coVarnames->getAt(ctx, j);
                                if (vnameObj && vnameObj->isString(ctx)) {
                                    const proto::ProtoObject* val = (j < outerNSlots) ? outerSlots[j] : nullptr;
                                    // If not in slots, try frame attributes (for non-optimized/mapped frames)
                                    if (!val) val = frame->getAttribute(ctx, vnameObj->asString(ctx));
                                    
                                    if (val && val != PROTO_NONE) {
                                        closureFrame = const_cast<proto::ProtoObject*>(closureFrame->setAttribute(ctx, vnameObj->asString(ctx), val));
                                        stack.back() = closureFrame; // Keep GC root updated
                                    }
                                }
                            }
                            stack.pop_back(); // Remove GC root
                        }
                    }
                }

                proto::ProtoObject* fn = createUserFunction(ctx, codeObj, const_cast<proto::ProtoObject*>(PythonEnvironment::getCurrentGlobals()), closureFrame, defaults, kwDefaults);
                // Store fn under its own co_name in the closure frame to enable self-referential
                // and forward-referencing inner functions. If closureFrame is truly mutable and
                // setAttribute is in-place, fn.__closure__[0] already sees this update.
                // If setAttribute returns a new object, we update the closure tuple in fn.
                if (fn && env) {
                    const proto::ProtoObject* nameAttr = codeObj->getAttribute(ctx,
                        PythonEnvironment::getInternedString(ctx, "co_name"));
                    if (nameAttr && nameAttr->isString(ctx)) {
                        const proto::ProtoObject* updatedFrame =
                            const_cast<proto::ProtoObject*>(closureFrame)->setAttribute(
                                ctx, nameAttr->asString(ctx), fn);
                        if (updatedFrame != closureFrame) {
                            // Not in-place: rebuild closure tuple so fn sees the updated frame
                            const proto::ProtoList* newClosure =
                                ctx->newList()->appendLast(ctx, updatedFrame);
                            fn = const_cast<proto::ProtoObject*>(fn->setAttribute(
                                ctx, env->getClosureString(), newClosure->asObject(ctx)));
                        }
                    }
                }
                if (get_env_diag()) {
                    fprintf(stderr, "DEBUG: OP_BUILD_FUNCTION finished createUserFunction fn=%p\n", (void*)fn);
                    fflush(stderr);
                }
                if (fn) {
                    stack.push_back(fn);
                }
            }
        } else if (op == OP_BUILD_CLASS) {
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

                if (get_env_diag()) {
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
                if (!metaclass || metaclass == PROTO_NONE) {
                    // CPython semantics: iterate all bases and find the most derived metaclass.
                    // If multiple independent metaclasses exist, Python throws TypeError, but here we just take the first strictly derived one.
                    const proto::ProtoObject* typeProto = env ? env->getTypePrototype() : nullptr;
                    const proto::ProtoObject* objectProto = env ? env->getObjectPrototype() : nullptr;
                    const proto::ProtoObject* bestMeta = typeProto;
                    
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
                            if (!baseMeta || baseMeta == PROTO_NONE || areSameClassesVM(ctx, baseMeta, objectProto)) {
                                // If a native base accidentally lacks a metaclass (evaluating to object), default it to type
                                baseMeta = typeProto;
                            }
                            // Compute derivation: if baseMeta is a subclass of bestMeta, it becomes the new best
                            if (!areSameClassesVM(ctx, baseMeta, bestMeta) && bestMeta) {
                                const proto::ProtoObject* mro = baseMeta->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__mro__"));
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
                                if (get_env_diag()) {
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
                if (get_env_diag()) {
                }

                // 2. Metaclass __prepare__
                if (get_env_diag()) fprintf(stderr, "DEBUG OP_BUILD_CLASS: metaclass=%p (PROTO_NONE=%p)\n", (void*)metaclass, (void*)PROTO_NONE);
                if (metaclass) {
                    const proto::ProtoObject* mcName = metaclass->getAttribute(ctx, env ? env->getNameString() : protoPython::PythonEnvironment::getInternalString(ctx, "__name__"));
                    if (mcName && mcName->isString(ctx)) {
                        std::string mn; mcName->asString(ctx)->toUTF8String(ctx, mn);
                        if (get_env_diag()) fprintf(stderr, "DEBUG OP_BUILD_CLASS: metaclass name='%s'\n", mn.c_str());
                    }
                }
                const proto::ProtoObject* prepareRaw = nullptr;
                if (metaclass) {
                    prepareRaw = env ? env->getAttribute(ctx, metaclass, PythonEnvironment::getInternedString(ctx, "__prepare__")) : metaclass->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__prepare__"));
                }
                const proto::ProtoObject* prepareM = prepareRaw;
                if (get_env_diag()) {
                    fprintf(stderr, "TRACE_PREPARE: metaclass=%p prepareM=%p\n", (void*)metaclass, (void*)prepareM); fflush(stderr);
                }
                if (prepareM && prepareM != PROTO_NONE) {
                    const proto::ProtoList* prepareArgs = ctx->newList()->appendLast(ctx, name)->appendLast(ctx, bases);
                    // Use keyword parameters if available
                    const proto::ProtoSparseList* kw = (kwds && kwds->asSparseList(ctx)) ? kwds->asSparseList(ctx) : nullptr;
                    const proto::ProtoObject* nsObj = invokeCallable(ctx, prepareM, prepareArgs, kw);
                    if (get_env_diag()) {
                        fprintf(stderr, "TRACE_PREPARE: invokeCallable returned nsObj=%p\n", (void*)nsObj); fflush(stderr);
                    }
                    if (!nsObj) {
                        return nullptr;
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

                const proto::ProtoObject* globals = env ? env->getCurrentGlobals() : nullptr;
                const proto::ProtoObject* moduleName = globals ? globals->getAttribute(ctx, py_module_s) : nullptr;
                if (moduleName) {
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
                    if (codeObj && codeObj != PROTO_NONE) {
                        if (get_env_diag()) fprintf(stderr, "DEBUG OP_BUILD_CLASS: before body run ns=%p\n", (void*)ns);
                        runCodeObject(ctx, codeObj, ns);
                        if (env && env->hasPendingException()) return nullptr;
                        if (get_env_diag()) {
                            const proto::ProtoObject* keysObj = ns->getAttribute(ctx, env ? env->getKeysString() : protoPython::PythonEnvironment::getInternalString(ctx, "__keys__"));
                            const proto::ProtoList* keysList = keysObj ? keysObj->asList(ctx) : nullptr;
                            fprintf(stderr, "DEBUG OP_BUILD_CLASS: after body run ns=%p keysSize=%lu\n", (void*)ns, keysList ? keysList->getSize(ctx) : 0);
                        }
                        stack.back() = ns; // ns may have been reallocated by CoW during execution
                    } else {
                        const proto::ProtoObject* callM = body->getAttribute(ctx, callS);
                        if (callM && callM->asMethod(ctx)) {
                            callM->asMethod(ctx)(ctx, body, nullptr, ctx->newList(), nullptr);
                            if (env && env->hasPendingException()) return nullptr;
                        }
                    }
                }


                if (get_env_diag()) {
                }

                // 4. Invoke metaclass to create the class
                const proto::ProtoList* mcArgs = ctx->newList()->appendLast(ctx, name)->appendLast(ctx, bases)->appendLast(ctx, ns);
                const proto::ProtoSparseList* kw = (kwds && kwds->asSparseList(ctx)) ? kwds->asSparseList(ctx) : nullptr;
                if (get_env_diag()) fprintf(stderr, "DEBUG OP_BUILD_CLASS: calling metaclass=%p\n", (void*)metaclass);
                const proto::ProtoObject* targetClass = invokeCallable(ctx, metaclass, mcArgs, kw);
                if (get_env_diag()) fprintf(stderr, "DEBUG OP_BUILD_CLASS: targetClass=%p\n", (void*)targetClass);
                
                if (targetClass && targetClass != PROTO_NONE) {
                    // Set __qualname__ if not set as an *own* attribute (inherited values from
                    // object/type prototypes must not block per-class assignment).
                    const proto::ProtoString* qualnameS = PythonEnvironment::getInternedString(ctx, "__qualname__");
                    const bool hasOwnQN = targetClass->hasOwnAttribute(ctx, qualnameS) == PROTO_TRUE;
                    if (!hasOwnQN) {
                        // Try to get __qualname__ from the class namespace (set by the class body).
                        // Only use ns's value if it's an OWN attribute; otherwise it's inherited
                        // from the dict/object prototype and would incorrectly yield "dict"/"object".
                        const bool nsHasOwnQN = ns ? (ns->hasOwnAttribute(ctx, qualnameS) == PROTO_TRUE) : false;
                        const proto::ProtoObject* nsQN = nsHasOwnQN ? ns->getAttribute(ctx, qualnameS) : nullptr;
                        if (nsQN && nsQN != PROTO_NONE && nsQN->isString(ctx)) {
                            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, qualnameS, nsQN));
                        } else {
                            // Default: qualname equals __name__ for top-level classes.
                            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, qualnameS, name));
                        }
                        stack.back() = targetClass; // update rooted reference
                    }

                    // Inject __class__ into the class namespace (frame) so methods can interpret it
                    // via closure (parent frame reference).
                    // Note: object.__class__ data descriptor prevents this from shadowing the type
                    // on the class object itself, so this is safe.
                    const proto::ProtoString* clsName = env ? env->getClassString() : protoPython::PythonEnvironment::getInternalString(ctx, "__class__");
                    if (get_env_diag()) {
                        std::string tName = "unknown";
                        const proto::ProtoObject* tNameAttr = targetClass->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"));
                        if (tNameAttr && tNameAttr->isString(ctx)) tNameAttr->asString(ctx)->toUTF8String(ctx, tName);
                        fprintf(stderr, "DEBUG: OP_BUILD_CLASS injecting __class__ = %p (name=%s) into ns = %p\n", (void*)targetClass, tName.c_str(), (void*)ns);
                    }
                    if (get_env_diag()) {
                        fprintf(stderr, "DEBUG: OP_BUILD_CLASS injecting targetClass=%p into ns=%p\n", (void*)targetClass, (void*)ns);
                    }
                    ns->setAttribute(ctx, clsName, targetClass);
                }

                if (!targetClass) targetClass = PROTO_NONE;
                for (int j = 0; j < 5; ++j) stack.pop_back(); // Pop name, bases, kwds, body, ns
                stack.push_back(targetClass);
            }
        }
        else if (op == OP_GET_ITER) {
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
        } else if (op == OP_FOR_ITER) {
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
                        if (get_env_diag()) {
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
        } else if (op == OP_UNPACK_SEQUENCE) {
            if (stack.empty() || arg <= 0) {
                i = next_i;
                continue;
            }
            const proto::ProtoObject* seq = stack.back();
            stack.pop_back();

            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (env) {
                if (get_env_diag()) fprintf(stderr, "DEBUG: UNPACK_SEQUENCE calling iter(seq)\n");
                const proto::ProtoObject* iterObj = env->iter(seq);
                if (!iterObj) {
                    if (!env->hasPendingException()) env->raiseTypeError(ctx, "cannot unpack non-iterable object");
                    i = next_i; continue;
                }
                std::vector<const proto::ProtoObject*> items;
                for (int j = 0; j < arg; ++j) {
                    const proto::ProtoObject* val = env->next(iterObj);
                    if (!val) {
                        if (env->hasPendingException() && env->isStopIteration(ctx, env->peekPendingException())) {
                            env->clearPendingException();
                        }
                        if (!env->hasPendingException()) {
                            env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx, "not enough values to unpack")->asObject(ctx));
                        }
                        break;
                    }
                    items.push_back(val);
                }
                if (env->hasPendingException()) {
                    i = next_i; continue;
                }
                
                // Check if there are too many values
                const proto::ProtoObject* excess = env->next(iterObj);
                if (excess) {
                    env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx, "too many values to unpack")->asObject(ctx));
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
        } else if (op == OP_UNPACK_EX) {
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
        } else if (op == OP_LOAD_GLOBAL) {
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
                    if (nameObj && nameObj->isString(ctx))
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
        } else if (op == OP_STORE_GLOBAL) {
            int nameIdx = arg >> 1;
            if (names && static_cast<unsigned long>(nameIdx) < names->getSize(ctx)) {
                if (stack.empty()) { i = next_i; continue; }
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (nameObj->isString(ctx)) {
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
        } else if (op == OP_BUILD_SLICE) {
            // i++;
            if ((arg != 2 && arg != 3) || stack.size() < static_cast<size_t>(arg)) continue;
            long long step = 1;
            const proto::ProtoObject* stepObj = nullptr;
            if (arg == 3) {
                stepObj = stack.back();
                stack.pop_back();
            } else {
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                stepObj = env ? env->getOneInteger() : ctx->fromInteger(1);
            }
            const proto::ProtoObject* stopObj = stack.back();
            stack.pop_back();
            const proto::ProtoObject* startObj = stack.back();
            stack.pop_back();
            proto::ProtoObject* sliceObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx, env ? env->getStartString() : PythonEnvironment::getInternedString(ctx, "start"), startObj));
            sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx, env ? env->getStopString() : PythonEnvironment::getInternedString(ctx, "stop"), stopObj));
            sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx, env ? env->getStepString() : PythonEnvironment::getInternedString(ctx, "step"), stepObj));
            if (env && env->getSliceType()) sliceObj->addParent(ctx, env->getSliceType());
            stack.push_back(sliceObj);
        } else if (op == OP_ROT_TWO) {
            if (stack.size() >= 2) {
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                stack.push_back(a);
                stack.push_back(b);
            }
        } else if (op == OP_ROT_THREE) {
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
        } else if (op == OP_ROT_FOUR) {
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
        } else if (op == OP_LIST_EXTEND) {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                // stack[-2] is listObj, stack[-1] is iterable
                const proto::ProtoObject* iterable = stack.back();
                proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                
                const proto::ProtoString* dataS = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = listObj->getAttribute(ctx, dataS);
                const proto::ProtoList* L = (data && data->asList(ctx)) ? data->asList(ctx) : nullptr;
                
                if (L && env) {
                    stack.push_back(L->asObject(ctx)); // TEMP ROOT at index top-1
                    if (get_env_diag()) fprintf(stderr, "DEBUG: LIST_EXTEND calling iter(iterable)\n");
                    const proto::ProtoObject* iter = env->iter(iterable);
                    stack.push_back(iter); // TEMP ROOT at index top-1
                    while (iter) {
                        const proto::ProtoObject* item = env->next(iter);
                        if (!item) break;
                        L = L->appendLast(ctx, item);
                        stack[stack.size() - 2] = const_cast<proto::ProtoObject*>(L->asObject(ctx)); // Update L root
                    }
                    if (env->hasPendingException()) {
                        const proto::ProtoObject* exc = env->takePendingException();
                        if (!env->isStopIteration(ctx, exc)) {
                            env->setPendingException(exc);
                            i = next_i;
                            continue;
                        }
                    }
                    listObj->setAttribute(ctx, dataS, L->asObject(ctx));
                    stack.pop_back(); // pop iter
                    stack.pop_back(); // pop L
                }
                stack.pop_back(); // pop iterable
            }
        } else if (op == OP_DICT_UPDATE) {
            // GC Safe: iterable stays on stack until updated
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* other = stack.back();
                proto::ProtoObject* dictObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                
                const proto::ProtoString* dataS = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoString* keysS = env ? env->getKeysString() : protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                
                const proto::ProtoObject* data = dictObj->getAttribute(ctx, dataS);
                const proto::ProtoSparseList* sl = (data && data->asSparseList(ctx)) ? data->asSparseList(ctx) : nullptr;
                
                if (sl && env) {
                    const proto::ProtoObject* otherData = other->getAttribute(ctx, dataS);
                    const proto::ProtoSparseList* otherSl = (otherData && otherData->asSparseList(ctx)) ? otherData->asSparseList(ctx) : nullptr;
                    if (otherSl) {
                        const proto::ProtoObject* keysObj = other->getAttribute(ctx, keysS);
                        const proto::ProtoList* otherKeys = (keysObj && keysObj->asList(ctx)) ? keysObj->asList(ctx) : nullptr;
                        if (otherKeys) {
                            for (unsigned long j = 0; j < otherKeys->getSize(ctx); ++j) {
                                const proto::ProtoObject* k = otherKeys->getAt(ctx, static_cast<int>(j));
                                unsigned long h = k->getHash(ctx);
                                const proto::ProtoObject* v = otherSl->getAt(ctx, h);
                                
                                bool isNew = !sl->has(ctx, h);
                                sl = sl->setAt(ctx, h, v);
                                if (isNew) {
                                    const proto::ProtoObject* myKeysObj = dictObj->getAttribute(ctx, keysS);
                                    const proto::ProtoList* myKeys = (myKeysObj && myKeysObj->asList(ctx)) ? myKeysObj->asList(ctx) : ctx->newList();
                                    myKeys = myKeys->appendLast(ctx, k);
                                    dictObj->setAttribute(ctx, keysS, myKeys->asObject(ctx));
                                }
                            }
                        }
                    } else {
                        // Handle generic mapping/iterable (simplified for now: expect dict-like)
                    }
                    dictObj->setAttribute(ctx, dataS, sl->asObject(ctx));
                }
                stack.pop_back();
            }
        } else if (op == OP_SET_UPDATE) {
            // GC Safe: iterable stays on stack until updated
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* iterable = stack.back();
                proto::ProtoObject* setObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                
                const proto::ProtoString* dataS = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = setObj->getAttribute(ctx, dataS);
                const proto::ProtoSet* s = (data && data->asSet(ctx)) ? data->asSet(ctx) : nullptr;
                
                if (s && env) {
                    if (get_env_diag()) fprintf(stderr, "DEBUG: SET_UPDATE calling iter(iterable)\n");
                    const proto::ProtoObject* iter = env->iter(iterable);
                    while (iter) {
                        const proto::ProtoObject* item = env->next(iter);
                        if (!item) break;
                        s = s->add(ctx, item);
                    }
                    if (env->hasPendingException()) {
                        const proto::ProtoObject* exc = env->takePendingException();
                        if (!env->isStopIteration(ctx, exc)) {
                            env->setPendingException(exc);
                            i = next_i;
                            continue;
                        }
                    }
                    setObj->setAttribute(ctx, dataS, s->asObject(ctx));
                }
                stack.pop_back();
            }
        } else if (op == OP_LIST_TO_TUPLE) {
            // GC Safe: list stays on stack until tuple is ready
            if (!stack.empty()) {
                proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(stack.back());
                
                const proto::ProtoString* dataS = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = listObj->getAttribute(ctx, dataS);
                const proto::ProtoList* L = (data && data->asList(ctx)) ? data->asList(ctx) : nullptr;
                if (get_env_diag()) {
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
        } else if (op == OP_DUP_TOP_TWO) {
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
        } else if (op == OP_DUP_TOP) {
            if (!stack.empty())
                stack.push_back(stack.back());
        } else if (op == OP_POP_TOP) {
            if (!stack.empty())
                stack.pop_back();
        } else if (op == OP_DELETE_NAME || op == OP_DELETE_GLOBAL) {
            int nameIdx = arg >> 1;
            // Swallow any pre-existing or subsequent exception from delete path (e.g. os.py del _create_environ_mapping)
            if (env && env->hasPendingException()) env->clearPendingException();
            // i++;
            if (frame) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (nameObj && nameObj->isString(ctx)) {
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
        } else if (op == OP_DELETE_FAST) {
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<unsigned long>(arg) < nSlots) {
                proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(ctx->getAutomaticLocals());
                slots[arg] = nullptr; 
            }
        } else if (op == OP_DELETE_ATTR) {
            if (!stack.empty()) {
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                int nameIdx = arg >> 1;
                const proto::ProtoObject* nameObj = names->getAt(ctx, nameIdx);
                if (nameObj && nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    // Remove from __data__ sparse list if present (dict-backed instances)
                    const proto::ProtoString* dataName = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                    const proto::ProtoString* keysName = env ? env->getKeysString() : protoPython::PythonEnvironment::getInternalString(ctx, "__keys__");
                    const proto::ProtoObject* d = (obj->hasOwnAttribute(ctx, dataName) == PROTO_TRUE) ? obj->proto::ProtoObject::getAttribute(ctx, dataName) : nullptr;
                    const proto::ProtoObject* k = (keysName && obj->hasOwnAttribute(ctx, keysName) == PROTO_TRUE) ? obj->proto::ProtoObject::getAttribute(ctx, keysName) : nullptr;
                    if (d && d != PROTO_NONE && d->asSparseList(ctx)) {
                        d->asSparseList(ctx)->removeAt(ctx, nameS->getHash(ctx));
                        if (env && env->hasPendingException()) env->clearPendingException();
                    }
                    if (k && k != PROTO_NONE && k->asList(ctx)) {
                        unsigned long targetHash = nameS->getHash(ctx);
                        const proto::ProtoList* newKeys = ctx->newList();
                        for (unsigned long ki = 0; ki < k->asList(ctx)->getSize(ctx); ++ki) {
                            const proto::ProtoObject* key = k->asList(ctx)->getAt(ctx, ki);
                            if (key && key->isString(ctx) && key->getHash(ctx) == targetHash) continue;
                            newKeys = newKeys->appendLast(ctx, key);
                        }
                        const_cast<proto::ProtoObject*>(obj)->proto::ProtoObject::setAttribute(ctx, keysName, newKeys->asObject(ctx));
                    }
                    // Mark native attribute as deleted (None) so subsequent LOAD_ATTR fails gracefully
                    const proto::ProtoObject* nil = env ? env->getNonePrototype() : PROTO_NONE;
                    const_cast<proto::ProtoObject*>(obj)->proto::ProtoObject::setAttribute(ctx, nameS, nil);
                }
            }
        } else if (op == OP_DELETE_SUBSCR) {
            if (stack.size() >= 2) {
                const proto::ProtoObject* key = stack.back();
                const proto::ProtoObject* container = stack[stack.top - 2];
                // Delay pop
                const proto::ProtoString* delItemS = env ? env->getDelItemString() : PythonEnvironment::getInternedString(ctx, "__delitem__");
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
                const proto::ProtoObject* result = invokeDunder(ctx, container, delItemS, args);
                if (!result) {
                    if (env && env->hasPendingException()) continue;
                    // Fallback for list/dict
                    const proto::ProtoString* data_name = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                    const proto::ProtoObject* data = container->getAttribute(ctx, data_name);
                    if (data) {
                        if (data->asList(ctx) && key->isInteger(ctx)) {
                            long long idx = key->asLong(ctx);
                            const proto::ProtoList* list = data->asList(ctx);
                            if (idx >= 0 && static_cast<unsigned long>(idx) < list->getSize(ctx)) {
                                const proto::ProtoList* newList = ctx->newList();
                                for (unsigned long j = 0; j < list->getSize(ctx); ++j) {
                                    if (static_cast<long long>(j) != idx) {
                                        newList = newList->appendLast(ctx, list->getAt(ctx, static_cast<int>(j)));
                                    }
                                }
                                const proto::ProtoString* data_name = env ? env->getDataString() : protoPython::PythonEnvironment::getInternalString(ctx, "__data__");
                                const_cast<proto::ProtoObject*>(container)->setAttribute(ctx, data_name, newList->asObject(ctx));
                            }
                        } else if (data->asSparseList(ctx)) {
                            data->asSparseList(ctx)->removeAt(ctx, key->getHash(ctx));
                        }
                    }
                }
                stack.pop_back(); // Pop key
                stack.pop_back(); // Pop container
            }
        } else if (op == OP_SETUP_FINALLY) {
            if (get_env_diag()) fprintf(stderr, "DEBUG: SETUP_FINALLY handler pc %lu, stack.top %lu\n", (unsigned long)arg, stack.size());
            fflush(stderr);
            blockStack.push_back({static_cast<unsigned long>(arg), stack.size()});
            // No continue: fall through to i = next_i
        } else if (op == OP_POP_BLOCK) {
            if (!blockStack.empty()) blockStack.pop_back();
        } else if (op == OP_GET_AWAITABLE) {
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
        } else if (op == OP_GET_AITER) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* obj = stack.back();
            stack.pop_back();
            const proto::ProtoString* aiterS = env ? env->getAIterString() : PythonEnvironment::getInternedString(ctx, "__aiter__");
            const proto::ProtoObject* aiter = invokeDunder(ctx, obj, aiterS, ctx->newList());
            if (aiter) {
                stack.push_back(aiter);
            } else {
                stack.push_back(obj);
            }
        } else if (op == OP_GET_ANEXT) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* aiter = stack.back();
            if (get_env_diag()) {
                if (get_env_diag()) {}
            }
            const proto::ProtoString* anextS = env ? env->getANextString() : PythonEnvironment::getInternedString(ctx, "__anext__");
            const proto::ProtoObject* awaitable = invokeDunder(ctx, aiter, anextS, ctx->newList());
            if (awaitable) {
                stack.push_back(awaitable);
            } else {
                if (env && !env->hasPendingException()) {
                    env->raiseTypeError(ctx, "async for item must be an async iterator");
                }
                if (get_env_diag() && env && env->hasPendingException()) {
                    if (get_env_diag()) {}
                }
                continue;
            }
        } else if (op == OP_EXCEPTION_MATCH) {
             if (stack.size() < 2) { i = next_i; continue; }
             const proto::ProtoObject* type = stack.back();
             stack.pop_back();
             const proto::ProtoObject* exc = stack.back();
             bool match = false;
             if (exc && type) {
                 match = env->isException(exc, type);
                 if (get_env_diag()) fprintf(stderr, "DEBUG: OP_EXCEPTION_MATCH exc %p vs type %p -> %d\n", (void*)exc, (void*)type, match);
            fflush(stderr);
             }
             if (get_env_diag()) {
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
        } else if (op == OP_SETUP_ASYNC_WITH) {
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
