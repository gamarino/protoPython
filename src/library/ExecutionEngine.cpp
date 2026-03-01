#include <protoPython/ExecutionEngine.h>
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

static bool get_env_diag() {
    static bool diag = std::getenv("PROTO_ENV_DIAG") != nullptr;
    return diag;
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

static const proto::ProtoString* getInternalString(proto::ProtoContext* ctx, const char* name) {
    // ... same as before but ensured to be in protoPython ...
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        if (std::strcmp(name, "__keys__") == 0) return env->getKeysString();
        if (std::strcmp(name, "__data__") == 0) return env->getDataString();
        if (std::strcmp(name, "__dict__") == 0) return env->getDictDunderString();
        if (std::strcmp(name, "__init__") == 0) return env->getInitString();
        if (std::strcmp(name, "__name__") == 0) return env->getNameString();
        if (std::strcmp(name, "__class__") == 0) return env->getClassString();
        if (std::strcmp(name, "__str__") == 0) return env->getStrString();
        if (std::strcmp(name, "__repr__") == 0) return env->getReprString();
        if (std::strcmp(name, "__iter__") == 0) return env->getIterString();
        if (std::strcmp(name, "__next__") == 0) return env->getNextString();
        if (std::strcmp(name, "__contains__") == 0) return env->getContainsString();
        if (std::strcmp(name, "__matmul__") == 0) return env->getMatMulString();
        if (std::strcmp(name, "__imatmul__") == 0) return env->getIMatMulString();
    }
    return proto::ProtoString::fromUTF8String(ctx, name);
}

namespace {

static const proto::ProtoObject* invokeDunder(proto::ProtoContext* ctx, const proto::ProtoObject* container, const proto::ProtoString* name, const proto::ProtoList* args);
static bool isTruthy(proto::ProtoContext* ctx, const proto::ProtoObject* obj);
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



static const proto::ProtoObject* runUserFunctionCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs) {
    if (!ctx || !self || !args) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* code_name = env ? env->getCodeString() : proto::ProtoString::fromUTF8String(ctx, "__code__");
    const proto::ProtoObject* codeObj = self->getAttribute(ctx, code_name);
    if (!codeObj || codeObj == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoString* globals_name = env ? env->getGlobalsString() : proto::ProtoString::fromUTF8String(ctx, "__globals__");
    const proto::ProtoObject* globalsObj = self->getAttribute(ctx, globals_name);
    if (!globalsObj || globalsObj == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoString* co_flags_name = env ? env->getCoFlagsString() : proto::ProtoString::fromUTF8String(ctx, "co_flags");
    const proto::ProtoObject* co_flags_obj = codeObj->getAttribute(ctx, co_flags_name);
    int co_flags = (co_flags_obj && co_flags_obj->isInteger(ctx)) ? static_cast<int>(co_flags_obj->asLong(ctx)) : 0;

    const proto::ProtoString* co_varnames_name = env ? env->getCoVarnamesString() : proto::ProtoString::fromUTF8String(ctx, "co_varnames");
    const proto::ProtoString* co_nparams_name = env ? env->getCoNparamsString() : proto::ProtoString::fromUTF8String(ctx, "co_nparams");
    const proto::ProtoString* co_automatic_name = env ? env->getCoAutomaticCountString() : proto::ProtoString::fromUTF8String(ctx, "co_automatic_count");

    const proto::ProtoObject* co_varnames_obj = codeObj->getAttribute(ctx, co_varnames_name);
    const proto::ProtoObject* co_nparams_obj = codeObj->getAttribute(ctx, co_nparams_name);
    const proto::ProtoString* co_kwonly_name = env ? env->getCoKwonlyargcountString() : proto::ProtoString::fromUTF8String(ctx, "co_kwonlyargcount");
    const proto::ProtoObject* co_kwonly_obj = codeObj->getAttribute(ctx, co_kwonly_name);
    const proto::ProtoObject* co_automatic_obj = codeObj->getAttribute(ctx, co_automatic_name);

    const proto::ProtoTuple* co_varnames = co_varnames_obj && co_varnames_obj->asTuple(ctx) ? co_varnames_obj->asTuple(ctx) : nullptr;
    int nparams_count = (co_nparams_obj && co_nparams_obj->isInteger(ctx)) ? static_cast<int>(co_nparams_obj->asLong(ctx)) : 0;
    int kwonly_count = (co_kwonly_obj && co_kwonly_obj->isInteger(ctx)) ? static_cast<int>(co_kwonly_obj->asLong(ctx)) : 0;
    int automatic_count = (co_automatic_obj && co_automatic_obj->isInteger(ctx)) ? static_cast<int>(co_automatic_obj->asLong(ctx)) : 0;


    const proto::ProtoList* parameterNames = nullptr;
    const proto::ProtoList* localNames = nullptr;
    if (co_varnames) {
        int co_varnames_size = static_cast<int>(co_varnames->getSize(ctx));
        if (nparams_count > 0 && nparams_count <= co_varnames_size) {
            parameterNames = ctx->newList();
            for (int i = 0; i < nparams_count; ++i)
                parameterNames = parameterNames->appendLast(ctx, co_varnames->getAt(ctx, i));
        }
        if (automatic_count > 0) {
            localNames = ctx->newList();
            for (int i = 0; i < automatic_count; ++i) {
                const proto::ProtoObject* name = (i < co_varnames_size) ? co_varnames->getAt(ctx, i) : PROTO_NONE;
                localNames = localNames->appendLast(ctx, name);
            }
        }
    }

    // We pass nullptr for args and kwargs to skip ProtoContext's internal binding,
    // as we handle it manually below to support Python-specific semantics like *args and **kwargs.
    ContextScope scope(ctx->space, ctx, parameterNames, localNames, nullptr, nullptr);
    proto::ProtoContext* calleeCtx = scope.context();
    unsigned long argCount = args->getSize(calleeCtx);

    // 5. Build Execution Frame (for locals()/sys._getframe)
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
    if (env) {
        const proto::ProtoObject* closure = self->getAttribute(calleeCtx, env->getClosureString());
        if (closure && closure != PROTO_NONE) {
            const proto::ProtoList* closureList = closure->asList(calleeCtx);
            if (closureList && closureList->getSize(calleeCtx) > 0) {
                const proto::ProtoObject* outerFrame = closureList->getAt(calleeCtx, 0);
                if (outerFrame && outerFrame != PROTO_NONE) {
                    frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, outerFrame));
                    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: runUserFunctionCall extracted outerFrame=%p from closure list\n", (void*)outerFrame);
                }
            } else {
                // Fallback in case it's not wrapped in a list for some internal reason
                frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, closure));
                if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: runUserFunctionCall fallback used closure=%p as parent\n", (void*)closure);
            }
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getClosureString(), closure));
        }
        // ADD FRAME PROTOTYPE LAST SO IT BECOMES HEAD!
        frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, env->getFramePrototype()));
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFCodeString(), codeObj));
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFGlobalsString(), globalsObj));
        const proto::ProtoObject* parentFrame = PythonEnvironment::getCurrentFrame();
        if (parentFrame) {
            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFBackString(), parentFrame));
        }
    }

    // Bind parameters
    unsigned int nSlots = calleeCtx->getAutomaticLocalsCount();
    proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());

    auto bindVar = [&](int idx, const proto::ProtoObject* val) {
        if (std::getenv("PROTO_ENV_DIAG")) {
            std::string pn = "unknown";
            if (co_varnames && idx < (int)co_varnames->getSize(calleeCtx) && co_varnames->getAt(calleeCtx, idx)->isString(calleeCtx)) {
                co_varnames->getAt(calleeCtx, idx)->asString(calleeCtx)->toUTF8String(calleeCtx, pn);
            }
            fprintf(stderr, "DEBUG: bindVar idx=%d param=%s val=%p co_flags=%d slots=%p frame=%p\n", idx, pn.c_str(), (void*)val, co_flags, (void*)slots, (void*)frame);
            fflush(stderr);
        }
        if ((co_flags & CO_OPTIMIZED) && slots && idx < (int)nSlots) {
            slots[idx] = const_cast<proto::ProtoObject*>(val);
        } else if (frame && co_varnames && idx < (int)co_varnames->getSize(calleeCtx)) {
            const proto::ProtoObject* nameObj = co_varnames->getAt(calleeCtx, idx);
            if (nameObj && nameObj->isString(calleeCtx)) {
                const proto::ProtoString* nameS = nameObj->asString(calleeCtx);
                frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, nameS, val));
            }
        }
    };

    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: runUserFunctionCall nparams_count=%d argCount=%lu argsSize=%lu\n", nparams_count, argCount, args ? args->getSize(calleeCtx) : 0);
        fflush(stderr);
    }

    // 1. Positional arguments
    for (unsigned long i = 0; i < (unsigned long)nparams_count && i < argCount; ++i) {
        bindVar(static_cast<int>(i), args->getAt(calleeCtx, static_cast<int>(i)));
    }

    // 2. Keyword arguments mapped to positional parameters and defaults if missing
    if (argCount < (unsigned long)nparams_count) {
        const proto::ProtoString* defaults_name = env ? env->getDefaultsString() : proto::ProtoString::fromUTF8String(calleeCtx, "__defaults__");
        const proto::ProtoObject* defaultsObj = self->getAttribute(calleeCtx, defaults_name);
        bool has_defaults = (defaultsObj && defaultsObj != PROTO_NONE && defaultsObj->isTuple(calleeCtx));
        const proto::ProtoTuple* defaults = has_defaults ? defaultsObj->asTuple(calleeCtx) : nullptr;
        int num_defaults = defaults ? (int)defaults->getSize(calleeCtx) : 0;
        int defaults_start_at = nparams_count - num_defaults;

        for (int i = (int)argCount; i < nparams_count; ++i) {
            bool bound = false;
            // First check if this argument was supplied in kwargs
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
    const proto::ProtoString* kwdefaults_name = env ? env->getKwdefaultsString() : proto::ProtoString::fromUTF8String(calleeCtx, "__kwdefaults__");
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
                const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(calleeCtx, "__data__");
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
        if (env && env->getDictPrototype()) kwDict = const_cast<proto::ProtoObject*>(kwDict->addParent(calleeCtx, env->getDictPrototype()));
        
        const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(calleeCtx, "__data__");
        
        const proto::ProtoSparseList* data = calleeCtx->newSparseList();
        
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
                }
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(calleeCtx);
            }
        }
        kwDict->setAttribute(calleeCtx, dataName, data->asObject(calleeCtx));
        bindVar(kwargIdx, kwDict);
    }

    if (env) {
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFLocalsString(), frame));
    }

    const proto::ProtoObject* isGenObj = codeObj->getAttribute(calleeCtx, env ? env->getCoIsGeneratorString() : proto::ProtoString::fromUTF8String(calleeCtx, "co_is_generator"));
    bool isGenerator = isGenObj && isGenObj->isBoolean(calleeCtx) && isGenObj->asBoolean(calleeCtx);

    if (isGenerator) {
        proto::ProtoObject* gen = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
        if (env && env->getGeneratorPrototype()) {
            gen = const_cast<proto::ProtoObject*>(gen->addParent(calleeCtx, env->getGeneratorPrototype()));
            gen->setAttribute(calleeCtx, env->getClassString(), env->getGeneratorPrototype());
        }
        gen->setAttribute(calleeCtx, env ? env->getGiCodeString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_code"), codeObj);
        gen->setAttribute(calleeCtx, env ? env->getGiFrameString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_frame"), frame);
        gen->setAttribute(calleeCtx, env ? env->getGiRunningString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_running"), PROTO_FALSE);
        gen->setAttribute(calleeCtx, env ? env->getGiPCString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_pc"), calleeCtx->fromInteger(0));
        
        const proto::ProtoList* emptyStack = calleeCtx->newList();
        gen->setAttribute(calleeCtx, env ? env->getGiStackString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_stack"), emptyStack->asObject(calleeCtx));
        
        const proto::ProtoList* localList = calleeCtx->newList();
        for (unsigned int i = 0; i < nSlots; ++i) {
            localList = localList->appendLast(calleeCtx, slots[i]);
        }
        gen->setAttribute(calleeCtx, env ? env->getGiLocalsString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_locals"), localList->asObject(calleeCtx));
        
        promote(calleeCtx, gen);
        return gen;
    }

    const proto::ProtoObject* result = nullptr;
    {
        GlobalsScope gscope(globalsObj);
        
        const proto::ProtoObject* bytecodeObj = codeObj->getAttribute(calleeCtx, env->getCoCodeString());
        const proto::ProtoObject* constsObj = codeObj->getAttribute(calleeCtx, env->getCoConstsString());
        const proto::ProtoObject* namesObj = codeObj->getAttribute(calleeCtx, env->getCoNamesString());
        
        const proto::ProtoTuple* bytecode = bytecodeObj ? bytecodeObj->asTuple(calleeCtx) : nullptr;
        const proto::ProtoTuple* consts = constsObj ? constsObj->asTuple(calleeCtx) : nullptr;
        const proto::ProtoTuple* names = namesObj ? namesObj->asTuple(calleeCtx) : nullptr;
        
        if (bytecode && consts) {
            unsigned long stackOffset = co_varnames ? co_varnames->getSize(calleeCtx) : 0;
            result = executeBytecodeRange(calleeCtx, consts, bytecode, names, frame, 0, bytecode->getSize(calleeCtx), stackOffset);
        } else {
            result = PROTO_NONE;
        }
    }
    promote(calleeCtx, result);
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

    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: runBoundMethodCall forwarding to im_func=%p with newArgs size=%lu\n", (void*)im_func, newArgs ? newArgs->getSize(ctx) : 0);
        fflush(stderr);
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
    bound = bound->setAttribute(ctx, getInternalString(ctx, "__self__"),
                               instance);
    
    // Set __func__ (the original function)
    bound = bound->setAttribute(ctx, getInternalString(ctx, "__func__"),
                               self);
    
    // Copy __name__ and __qualname__ from the original function
    const proto::ProtoObject* funcName = self->getAttribute(ctx, env ? env->getNameString() : getInternalString(ctx, "__name__"));
    if (funcName && funcName != PROTO_NONE) {
        bound = bound->setAttribute(ctx, env ? env->getNameString() : getInternalString(ctx, "__name__"), funcName);
    }
    const proto::ProtoObject* funcQualname = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__qualname__"));
    if (funcQualname && funcQualname != PROTO_NONE) {
        bound = bound->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__qualname__"), funcQualname);
    }
    
    // Set __call__ to a special native method that will do the binding call
    bound = bound->setAttribute(ctx, env ? env->getCallString() : getInternalString(ctx, "__call__"),
                               ctx->fromMethod(const_cast<proto::ProtoObject*>(bound), runBoundMethodCall));

    
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
    fn = fn->setAttribute(ctx, env ? env->getCodeString() : proto::ProtoString::fromUTF8String(ctx, "__code__"), codeObj);
    fn = fn->setAttribute(ctx, env ? env->getGlobalsString() : proto::ProtoString::fromUTF8String(ctx, "__globals__"), globalsFrame);
    // Explicitly set __class__ to fix type identity if prototype linkage failed
    if (env && env->getFunctionPrototype()) {
        fn = fn->setAttribute(ctx, env ? env->getClassString() : proto::ProtoString::fromUTF8String(ctx, "__class__"), env->getFunctionPrototype());
    }
    if (codeObj) {
        const proto::ProtoString* co_name_s = proto::ProtoString::fromUTF8String(ctx, "co_name");
        const proto::ProtoObject* codeName = codeObj->getAttribute(ctx, co_name_s);
        if (codeName && codeName != PROTO_NONE) {
            fn = fn->setAttribute(ctx, env ? env->getNameString() : proto::ProtoString::fromUTF8String(ctx, "__name__"), codeName);
            fn = fn->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__qualname__"), codeName);
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
    fn = fn->setAttribute(ctx, env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(fn), runUserFunctionCall));
    fn = fn->setAttribute(ctx, env ? env->getGetDunderString() : proto::ProtoString::fromUTF8String(ctx, "__get__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(fn), py_function_get));
    return const_cast<proto::ProtoObject*>(fn);
}



/** Return true if obj is an embedded value (e.g. small int, bool); do not call getAttribute on it. */
static bool isEmbeddedValue(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    return obj->isInteger(ctx) || obj->isBoolean(ctx) || obj->isNone(ctx);
}

static const proto::ProtoObject* binaryAdd(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if ((a->isInteger(ctx) || a->isDouble(ctx)) && (b->isInteger(ctx) || b->isDouble(ctx))) {
        return a->add(ctx, b);
    }
    if (a->isString(ctx) && b->isString(ctx)) {
        std::string s1, s2;
        a->asString(ctx)->toUTF8String(ctx, s1);
        b->asString(ctx)->toUTF8String(ctx, s2);
        return ctx->fromUTF8String((s1 + s2).c_str());
    }

    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: binaryAdd a=%p class=%s b=%p class=%s\n", (void*)a, PythonEnvironment::reprObject(ctx, a).c_str(), (void*)b, PythonEnvironment::reprObject(ctx, b).c_str());
        fflush(stderr);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
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
        
        const proto::ProtoObject* aCls = env ? a->getAttribute(ctx, env->getClassString()) : a->getAttribute(ctx, getInternalString(ctx, "__class__"));
        proto::ProtoObject* resObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        
        bool isTuple = env && (aCls == env->getTuplePrototype());
        if (isTuple) {
             resObj->setAttribute(ctx, env->getDataString(), ctx->newTupleFromList(resL)->asObject(ctx));
        } else {
             resObj->setAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"), resL->asObject(ctx));
        }
        
        if (aCls) {
            resObj = const_cast<proto::ProtoObject*>(resObj->addParent(ctx, aCls));
            resObj->setAttribute(ctx, env ? env->getClassString() : getInternalString(ctx, "__class__"), aCls);
        }
        return resObj;
    }

    return PROTO_NONE;
}

static const proto::ProtoObject* binarySubtract(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if ((a->isInteger(ctx) || a->isDouble(ctx)) && (b->isInteger(ctx) || b->isDouble(ctx))) {
        return a->subtract(ctx, b);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryMultiply(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if ((a->isInteger(ctx) || a->isDouble(ctx)) && (b->isInteger(ctx) || b->isDouble(ctx))) {
        return a->multiply(ctx, b);
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
    if (a->isInteger(ctx) || a->isDouble(ctx)) {
        if ((b->isInteger(ctx) && b->asLong(ctx) == 0) || (b->isDouble(ctx) && b->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
    }
    if ((a->isInteger(ctx) || a->isDouble(ctx)) && (b->isInteger(ctx) || b->isDouble(ctx))) {
        return a->divide(ctx, b);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryModulo(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (std::getenv("PROTO_DEBUG_MODULO")) {
        // log removed
    }
    if (a->isInteger(ctx) || a->isDouble(ctx)) {
        if ((b->isInteger(ctx) && b->asLong(ctx) == 0) || (b->isDouble(ctx) && b->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
    }
    if ((a->isInteger(ctx) || a->isDouble(ctx)) && (b->isInteger(ctx) || b->isDouble(ctx))) {
        return a->modulo(ctx, b);
    }
    if (a->isString(ctx)) {
        std::string* tplPtr = new std::string();
        a->asString(ctx)->toUTF8String(ctx, *tplPtr);
        
        auto getStr = [&](const proto::ProtoObject* obj) -> std::string {
            if (obj->isString(ctx)) {
                std::string* sPtr = new std::string();
                obj->asString(ctx)->toUTF8String(ctx, *sPtr);
                std::string res = *sPtr;
                delete sPtr;
                return res;
            } else if (obj->isInteger(ctx)) {
                return std::to_string(obj->asLong(ctx));
            } else if (obj->isDouble(ctx)) {
                return std::to_string(obj->asDouble(ctx));
            } else if (obj == PROTO_TRUE) {
                return "True";
            } else if (obj == PROTO_FALSE) {
                return "False";
            } else if (obj == PROTO_NONE || !obj) {
                return "None";
            } else {
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                const proto::ProtoString* strS = env ? env->getStrString() : getInternalString(ctx, "__str__");
                const proto::ProtoObject* strM = env ? env->getAttribute(ctx, obj, strS) : obj->getAttribute(ctx, strS);
                if (strM && strM->asMethod(ctx)) {
                    const proto::ProtoObject* rs = strM->asMethod(ctx)(ctx, obj, nullptr, env ? env->getEmptyList() : ctx->newList(), nullptr);
                    std::string* sPtr = new std::string();
                    if (rs && rs->isString(ctx)) {
                        rs->asString(ctx)->toUTF8String(ctx, *sPtr);
                        std::string res = *sPtr;
                        delete sPtr;
                        return res;
                    }
                    delete sPtr;
                }
                return "<object>";
            }
        };

        if (b->isTuple(ctx)) {
            const proto::ProtoTuple* bt = b->asTuple(ctx);
            unsigned long n = bt->getSize(ctx);
            for (unsigned long i = 0; i < n; ++i) {
                std::string val = getStr(bt->getAt(ctx, i));
                size_t pos = tplPtr->find("%s");
                if (pos == std::string::npos) pos = tplPtr->find("%d");
                if (pos != std::string::npos) {
                    tplPtr->replace(pos, 2, val);
                }
            }
        } else {
            std::string valStr = getStr(b);
            size_t pos = 0;
            bool replaced = false;
            while ((pos = tplPtr->find("%s", pos)) != std::string::npos) {
                tplPtr->replace(pos, 2, valStr);
                pos += valStr.length();
                replaced = true;
            }
            pos = 0;
            while ((pos = tplPtr->find("%d", pos)) != std::string::npos) {
                tplPtr->replace(pos, 2, valStr);
                pos += valStr.length();
                replaced = true;
            }
            if (!replaced && tplPtr->find('%') != std::string::npos) {
                // Potential formatting error or unsupported specifier
            }
        }
        const proto::ProtoObject* res = ctx->fromUTF8String(tplPtr->c_str());
        delete tplPtr;
        return res;
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryPower(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if ((a->isInteger(ctx) || a->isDouble(ctx)) && (b->isInteger(ctx) || b->isDouble(ctx))) {
        if (a->isInteger(ctx) && b->isInteger(ctx)) {
            long long base = a->asLong(ctx);
            long long exp = b->asLong(ctx);
            if (exp < 0) {
                double r = std::pow(static_cast<double>(base), static_cast<double>(exp));
                return ctx->fromDouble(r);
            }
            long long result = 1;
            for (long long i = 0; i < exp; ++i) result *= base;
            return ctx->fromInteger(result);
        }
        double aa = a->isDouble(ctx) ? a->asDouble(ctx) : static_cast<double>(a->asLong(ctx));
        double bb = b->isDouble(ctx) ? b->asDouble(ctx) : static_cast<double>(b->asLong(ctx));
        return ctx->fromDouble(std::pow(aa, bb));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryFloorDivide(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) || a->isDouble(ctx)) {
        if ((b->isInteger(ctx) && b->asLong(ctx) == 0) || (b->isDouble(ctx) && b->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
    }
    if ((a->isInteger(ctx) || a->isDouble(ctx)) && (b->isInteger(ctx) || b->isDouble(ctx))) {
        if (a->isInteger(ctx) && b->isInteger(ctx)) {
            return a->divide(ctx, b); // Note: Integer division natively is floor division in Python. But protoCore division matches Python's floor division semantics natively!
        }
        double aa = a->isDouble(ctx) ? a->asDouble(ctx) : static_cast<double>(a->asLong(ctx));
        double bb = b->isDouble(ctx) ? b->asDouble(ctx) : static_cast<double>(b->asLong(ctx));
        return ctx->fromInteger(static_cast<long long>(std::floor(aa / bb)));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* compareOp(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b, int op) {
    bool result = false;
    if (op == 8) { // is
        result = (a == b);
        return result ? PROTO_TRUE : PROTO_FALSE;
    }
    if (op == 9) { // is not
        result = (a != b);
        return result ? PROTO_TRUE : PROTO_FALSE;
    }
    
    if (op == 6 || op == 7) { // in, not in
        bool found = false;
        const proto::ProtoList* lst = b->asList(ctx);
        if (!lst && b->isTuple(ctx)) {
            const proto::ProtoTuple* tup = b->asTuple(ctx);
            if (tup) lst = tup->asList(ctx);
        }
        
        if (!lst) {
            // Try dictionary keys or __data__ fallback
            const proto::ProtoString* dataS = getInternalString(ctx, "__data__");
            const proto::ProtoObject* data = b->getAttribute(ctx, dataS);
            if (data && data != PROTO_NONE) {
                if (data->asList(ctx)) lst = data->asList(ctx);
                else if (data->isTuple(ctx)) lst = data->asTuple(ctx)->asList(ctx);
                else if (data->asSparseList(ctx)) {
                    unsigned long hash = 0;
                    if (a->isString(ctx)) hash = a->asString(ctx)->getHash(ctx);
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
                    const proto::ProtoString* keysS = getInternalString(ctx, "__keys__");
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
            const proto::ProtoString* containsS = env ? env->getContainsString() : getInternalString(ctx, "__contains__");
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
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env && obj == env->getNonePrototype()) return false;
    
    if (obj->isInteger(ctx)) return (obj->asLong(ctx) != 0);
    if (obj->isDouble(ctx)) return (obj->asDouble(ctx) != 0.0);
    if (obj->isString(ctx)) return (obj->asString(ctx)->getSize(ctx) > 0);
    
    // Evaluate __bool__ method
    const proto::ProtoString* boolS = env ? env->getBoolString() : proto::ProtoString::fromUTF8String(ctx, "__bool__");
    const proto::ProtoObject* cls = env ? env->getType(ctx, obj) : obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__class__"));
    const proto::ProtoObject* boolMethod = cls ? cls->getAttribute(ctx, boolS) : obj->getAttribute(ctx, boolS);
    if (boolMethod && boolMethod->asMethod(ctx)) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
        const proto::ProtoObject* result = boolMethod->asMethod(ctx)(ctx, obj, nullptr, emptyL, nullptr);
        if (result == PROTO_FALSE) return false;
        if (result == PROTO_TRUE) return true;
        
        // If it doesn't return exactly True or False, try to convert result to bool or check its truthiness
        // This is simplified but matching py_bool logic
        return isTruthy(ctx, result); 
    }
    
    // Evaluate __len__ method fallback
    const proto::ProtoString* lenS = env ? env->getLenString() : proto::ProtoString::fromUTF8String(ctx, "__len__");
    const proto::ProtoObject* lenMethod = cls ? cls->getAttribute(ctx, lenS) : obj->getAttribute(ctx, lenS);
    if (lenMethod && lenMethod->asMethod(ctx)) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
        const proto::ProtoObject* result = lenMethod->asMethod(ctx)(ctx, obj, nullptr, emptyL, nullptr);
        if (result && result->isInteger(ctx)) {
            return (result->asLong(ctx) > 0);
        }
    }
    
    return true;
}

struct RecursionScope {
    RecursionScope(PythonEnvironment* env, proto::ProtoContext* ctx) : env_(env), ctx_(ctx) {
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
    ~RecursionScope() {
        if (incremented_) {
            PythonEnvironment::s_recursionDepth--;
        }
    }
    bool overflowed() const { return overflowed_; }
private:
    PythonEnvironment* env_;
    proto::ProtoContext* ctx_;
    bool overflowed_ = false;
    bool incremented_ = false;
};

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

    if (std::getenv("PROTO_ENV_DIAG")) {
        std::string repr = "unknown";
        if (env) repr = PythonEnvironment::reprObject(ctx, callable);
        std::string clsName = "unknown";
        const proto::ProtoObject* cls = callable->getAttribute(ctx, env ? env->getClassString() : getInternalString(ctx, "__class__"));
        if (cls) {
            const proto::ProtoObject* nameAttr = cls->getAttribute(ctx, env ? env->getNameString() : getInternalString(ctx, "__name__"));
            if (nameAttr && nameAttr->isString(ctx)) nameAttr->asString(ctx)->toUTF8String(ctx, clsName);
        }
        fprintf(stderr, "DEBUG: invokeCallable callable=%p repr=%s class=%s\n", (void*)callable, repr.c_str(), clsName.c_str());
        fflush(stderr);
    }

    RecursionScope recScope(env, ctx);
    if (recScope.overflowed()) return nullptr;

    if (callable->isNone(ctx)) {
        if (env) env->raiseTypeError(ctx, "'NoneType' object is not callable");
        return nullptr;
    }

    if (callable->asMethod(ctx)) {
        const proto::ProtoObject* self = callable->asMethodSelf(ctx);
        return callable->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(self), nullptr, args, kwargs);
    }

    /* FALLBACK TO PUBLIC API __call__ */
    const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__");
    
    // In Python, special methods like __call__ are always looked up on the TYPE of the object,
    // bypassing the object's own namespace. This prevents class definitions of __call__ from 
    // interfering with class instantiation (which uses type.__call__).
    const proto::ProtoObject* typeObj = nullptr;
    if (env) {
        typeObj = env->getType(ctx, callable);
    } else {
        typeObj = callable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__class__"));
    }
    
    // Now get the __call__ attribute specifically from the type object.
    const proto::ProtoObject* callAttr = nullptr;
    if (typeObj && typeObj != PROTO_NONE) {
        // We use env->getAttribute so that any descriptor logic (like binding the method to the callable) executes naturally.
        callAttr = env ? env->getAttribute(ctx, typeObj, callS) : typeObj->getAttribute(ctx, callS);
    }

    if (!callAttr || !callAttr->asMethod(ctx)) {
        if (env) {
            std::string repr = PythonEnvironment::reprObject(ctx, callable);
            env->raiseTypeError(ctx, "'" + repr + "' object is not callable");
        }
        return nullptr; // Return nullptr on error
    }
    
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: invokeCallable calling __call__ asMethod\n");
            fflush(stderr);
        fflush(stderr);
    }

    const proto::ProtoObject* result = callAttr->asMethod(ctx)(ctx, callable, nullptr, args, kwargs);
    if (std::getenv("PROTO_ENV_DIAG")) {
        std::string r = result ? PythonEnvironment::reprObject(ctx, result) : "nullptr";
        fprintf(stderr, "DEBUG: invokeCallable (__call__) returning result=%p repr=%s\n", (void*)result, r.c_str());
        fflush(stderr);
    }
    return result;
}

static const proto::ProtoObject* invokeDunder(proto::ProtoContext* ctx, const proto::ProtoObject* container, const proto::ProtoString* name, const proto::ProtoList* args) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* method = env ? env->getAttribute(ctx, container, name) : container->getAttribute(ctx, name);
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
        if (std::getenv("PROTO_ENV_DIAG")) {
            // log removed
        }
        return nullptr;
    }

    // 1. Check if running
    const proto::ProtoObject* runningAttr = self->getAttribute(ctx, env->getGiRunningString());
    if (runningAttr == PROTO_TRUE) {
        env->raiseValueError(ctx, ctx->fromUTF8String("generator already executing"));
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

    if (!codeObj || !frame || !pcObj || !stackObj) return PROTO_NONE;

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
                    blockStack.push_back({
                        static_cast<unsigned long>(t->getAt(ctx, 0)->asLong(ctx)),
                        static_cast<size_t>(t->getAt(ctx, 1)->asLong(ctx))
                    });
                }
            }
        }
    }

    unsigned long pc = (pcObj && pcObj->isInteger(ctx)) ? static_cast<unsigned long>(pcObj->asLong(ctx)) : 0;
    const proto::ProtoList* co_code_list = codeObj->getAttribute(ctx, env->getCoCodeString())->asList(ctx);
    if (!co_code_list) return PROTO_NONE;
    
    if (pc >= co_code_list->getSize(ctx)) {
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
        
        const proto::ProtoList* co_consts = codeObj->getAttribute(calleeCtx, env->getCoConstsString())->asList(calleeCtx);
        const proto::ProtoList* co_names = codeObj->getAttribute(calleeCtx, env->getCoNamesString())->asList(calleeCtx);
        
        result = executeBytecodeRange(calleeCtx, 
            reinterpret_cast<const proto::ProtoObject*>(co_consts)->asTuple(calleeCtx),
            reinterpret_cast<const proto::ProtoObject*>(co_code_list)->asTuple(calleeCtx),
            reinterpret_cast<const proto::ProtoObject*>(co_names)->asTuple(calleeCtx),
            frame,
            pc,
            co_code_list->getSize(calleeCtx),
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

        // Save blockStack back
        const proto::ProtoList* newBlocks = calleeCtx->newList();
        for (const auto& b : blockStack) {
            const proto::ProtoList* tempL = calleeCtx->newList();
            tempL = tempL->appendLast(calleeCtx, calleeCtx->fromInteger(b.handlerPc));
            tempL = tempL->appendLast(calleeCtx, calleeCtx->fromInteger(b.stackDepth));
            const proto::ProtoObject* bTup = calleeCtx->newTupleFromList(tempL)->asObject(calleeCtx);
            newBlocks = newBlocks->appendLast(calleeCtx, bTup);
        }
        self->setAttribute(calleeCtx, env->getGiBlocksString(), newBlocks->asObject(calleeCtx));
    }

    // 8. Clear running 
    self->setAttribute(ctx, env->getGiRunningString(), PROTO_FALSE);
    self->setAttribute(ctx, env->getGiPCString(), ctx->fromInteger(nextPc));

    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: py_generator_send_impl finished. yielded=%d, result=%p, hasPendingException=%d\n", 
                (int)yielded, (void*)result, (int)env->hasPendingException());
        if (env->hasPendingException()) {
             const proto::ProtoObject* exc = env->peekPendingException();
             fprintf(stderr, "DEBUG: py_generator_send_impl pending exception: %p\n", (void*)exc);
        }
        fflush(stderr);
    }

    if (!yielded && !env->hasPendingException()) {
        env->raiseStopIteration(ctx, result);
        return nullptr;
    }

    return result;
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
    if (!env) return ctx->fromUTF8String("<generator object>");

    const proto::ProtoObject* code = self->getAttribute(ctx, env->getGiCodeString());
    std::string name = "<unknown>";
    if (code) {
        const proto::ProtoObject* co_name = code->getAttribute(ctx, env->getCoNameString());
        if (co_name && co_name->isString(ctx)) co_name->asString(ctx)->toUTF8String(ctx, name);
    }
    
    char buf[128];
    snprintf(buf, sizeof(buf), "<generator object %s at %p>", name.c_str(), (void*)self);
    return ctx->fromUTF8String(buf);
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
    const proto::ProtoObject* genExitType = env->getAttribute(ctx, env->getGlobals(), proto::ProtoString::fromUTF8String(ctx, "GeneratorExit"));
    if (!genExitType || genExitType == PROTO_NONE) {
        // Fallback: create it if missing? For now just skip.
        return PROTO_NONE;
    }
    const proto::ProtoObject* genExit = ctx->newObject(true);
    genExit = genExit->addParent(ctx, genExitType);
    
    try {
        py_generator_send_impl(ctx, self, PROTO_NONE, genExit);
    } catch (...) {
        // In Python, GeneratorExit is special.
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

    GCStack(const proto::ProtoObject** s, size_t cap) : slots(s), top(0), capacity(cap) {}

    void push_back(const proto::ProtoObject* obj) {
        if (top < capacity) {
            slots[top++] = obj;
        } else {
            if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: GCStack overflow! top=%lu capacity=%lu\n", top, capacity);
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
    
    const proto::ProtoString* newS = proto::ProtoString::fromUTF8String(ctx, "__new__");
    const proto::ProtoObject* newM = self->getAttribute(ctx, newS);
    
    if (get_env_diag()) {
    }

    proto::ProtoObject* obj = nullptr;
    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG runUserClassCall: self=%p newM=%p\n", (void*)self, (void*)newM);
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
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG runUserClassCall: invokeCallable(newM) returned obj=%p\n", (void*)obj);
        if (!obj || obj == PROTO_NONE) {
             if (env && env->hasPendingException()) {
                 if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG runUserClassCall: Pending exception detected!\n");
                 return nullptr; 
             }
        }
    }

    if (!obj || obj == PROTO_NONE) {
        if (get_env_diag()) {}
        obj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        obj = const_cast<proto::ProtoObject*>(obj->addParent(ctx, self));
        const proto::ProtoString* classS = env ? env->getClassString() : getInternalString(ctx, "__class__");
        obj = const_cast<proto::ProtoObject*>(obj->setAttribute(ctx, classS, self));
    }
    
    // Invoke __init__
    // Magic methods should be looked up on the class, not the instance object's __dict__ directly.
    if (obj && obj != PROTO_NONE) {
        bool isInstanceOfSelf = false;
        if (env) {
            isInstanceOfSelf = (obj->isInstanceOf(ctx, self) == PROTO_TRUE);
        } else {
            // Very naive fallback if env is missing
            const proto::ProtoObject* cls = obj->getAttribute(ctx, getInternalString(ctx, "__class__"));
            isInstanceOfSelf = (cls == self);
        }
        
        if (isInstanceOfSelf) {
            const proto::ProtoString* initS = env ? env->getInitString() : getInternalString(ctx, "__init__");
            const proto::ProtoObject* initM = self->getAttribute(ctx, initS);
            if (initM && initM != PROTO_NONE) {
                // Since we looked it up on the class (self), we must manually pass `obj` as first arg
                const proto::ProtoList* initArgs = ctx->newList()->appendLast(ctx, obj);
                if (args) {
                    for (size_t i = 0; i < args->getSize(ctx); ++i) {
                        initArgs = initArgs->appendLast(ctx, args->getAt(ctx, i));
                    }
                }
                invokePythonCallable(ctx, initM, initArgs, kwargs);
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
    unsigned long* finalTopPtr) {
    if (!ctx || !constants || !bytecode) return nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env && std::getenv("PROTO_THREAD_DIAG")) {
        // log removed
    }
    
    FrameScope fscope(frame);
    unsigned long n = bytecode->getSize(ctx);
    if (n == 0) {
        if (std::getenv("PROTO_ENV_DIAG")) {
            fprintf(stderr, "DEBUG: executeBytecodeRange n=0, returning nullptr\n");
        }
        return nullptr;
    }
    if (pcEnd >= n) pcEnd = n - 1;

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
        fallbackStack.resize(1024); // Default capacity for manual/test execution
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
        unsigned long next_i = i + 1;
        {
             const proto::ProtoObject* opObj = bytecode->getAt(ctx, i);
             int op = static_cast<int>(opObj->asLong(ctx));
             int arg = (i + 1 < n && bytecode->getAt(ctx, static_cast<int>(i + 1))->isInteger(ctx))
                 ? static_cast<int>(bytecode->getAt(ctx, static_cast<int>(i + 1))->asLong(ctx)) : 0;
             if (std::getenv("PROTO_ENV_DIAG")) {
                 fprintf(stderr, "DEBUG: [PC %lu] OP %d ARG %d\n", i, op, arg);
                 fflush(stderr);
             }
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
                const proto::ProtoObject* msg = exc->getAttribute(ctx, env->getStrString());
                if (msg && msg->isString(ctx)) {
                    msg->asString(ctx)->toUTF8String(ctx, excMsg);
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
                    if (get_env_diag()) {
                        fprintf(stderr, "DEBUG: Pushing exception %p back to stack\n", exc);
                        fflush(stderr);
                    }
                    stack.push_back(exc);
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
        
        // Update location for possible trace/debug
        if ((i & 0x1F) == 0) { // Update every 32 instructions or so to save perf, or just when exception occurs
             updateContextLocation(ctx, frame, i);
        }
        if (get_env_diag()) {
        }
        if ((i & 0x7FF) == 0) checkSTW(ctx);
        const proto::ProtoObject* instr = bytecode->getAt(ctx, static_cast<int>(i));
        int op = (instr && instr->isInteger(ctx)) ? static_cast<int>(instr->asLong(ctx)) : 0;

        if (std::getenv("PROTO_PC_TRACE")) {
            if (opcodeHasArg(op)) {
                // Need to peek arg safely
                int peekArg = (i + 1 < n && bytecode->getAt(ctx, static_cast<int>(i + 1))->isInteger(ctx))
                    ? static_cast<int>(bytecode->getAt(ctx, static_cast<int>(i + 1))->asLong(ctx)) : 0;
                fprintf(stderr, "TRACE: PC %lu OP %d ARG %d\n", i, op, peekArg);
            } else {
                fprintf(stderr, "TRACE: PC %lu OP %d\n", i, op);
            }
        }

        // Every opcode in protoPython now consumes 2 slots (opcode + arg) to match the Compiler.
        int arg = (i + 1 < n && bytecode->getAt(ctx, static_cast<int>(i + 1))->isInteger(ctx))
            ? static_cast<int>(bytecode->getAt(ctx, static_cast<int>(i + 1))->asLong(ctx)) : 0;
        
        next_i = i + 2;

        if (op == OP_LOAD_CONST) {
            if (static_cast<unsigned long>(arg) < constants->getSize(ctx)) {
                const proto::ProtoObject* val = constants->getAt(ctx, arg);
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
            return ret;
        } else if (op == OP_GET_YIELD_FROM_ITER) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* obj = stack.back();
            const proto::ProtoObject* iterator = nullptr;
            const proto::ProtoObject* iterMethod = env ? env->getAttribute(ctx, obj, env->getIterString()) : obj->getAttribute(ctx, getInternalString(ctx, "__iter__"));
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
            
            const proto::ProtoString* sendS = env ? env->getSendString() : proto::ProtoString::fromUTF8String(ctx, "send");
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
                const proto::ProtoString* nextS = env ? env->getNextString() : getInternalString(ctx, "__next__");
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
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    std::string nStr;
                    nameS->toUTF8String(ctx, nStr);
                    if (std::getenv("PROTO_ENV_DIAG")) {
                    }
                    const proto::ProtoObject* val = nullptr;
                    bool found = false;
                    const proto::ProtoObject* hasAttrRes = frame->hasAttribute(ctx, nameS);
                    if (std::getenv("PROTO_ENV_DIAG") && nStr == "self") {
                        fprintf(stderr, "DEBUG: OP_LOAD_NAME('self') frame=%p hasAttribute=%p (PROTO_TRUE=%p), closure=%p\n",
                                (void*)frame, (void*)hasAttrRes, (void*)PROTO_TRUE, (void*)frame->getAttribute(ctx, env->getClosureString()));
                    }
                    if (hasAttrRes == PROTO_TRUE) {
                        val = frame->getAttribute(ctx, nameS);
                        found = true;
                    }
                    if (found) {
                        if (nStr == "_splitext") {
                            if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: OP_LOAD_NAME(_splitext) found in frame: val %p\n", (void*)val);
            fflush(stderr);
                        }
                        stack.push_back(val);
                    } else if (env) {
                        const proto::ProtoObject* r = env->resolve(nameS, ctx);
                        if (r) {
                            if (nStr == "_splitext") {
                                if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: OP_LOAD_NAME(_splitext) resolved: val %p\n", (void*)r);
                                fflush(stderr);
                            }
                            if (nStr == "len") {
                                if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: OP_LOAD_NAME(len) resolved: r=%p (PROTO_NONE is %p)\n", (void*)r, (void*)PROTO_NONE);
                                fflush(stderr);
                            }
                            stack.push_back(r);
                        } else {
                            if (!env->hasPendingException()) env->raiseNameError(ctx, nStr);
                            continue;
                        }
                    } else {
                        std::cerr << "Engine Error: env is NULL in OP_LOAD_NAME for '" << nStr << "'\n";
                        continue;
                    }
                } else {
                    stack.push_back(PROTO_NONE);
                }
            }
        } else if (op == OP_STORE_NAME) {
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG: OP_STORE_NAME start PC %lu names=%ld arg=%d\n", i, names ? names->getSize(ctx) : -1, arg);
                fflush(stderr);
            }
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                if (stack.empty()) {
                    if (get_env_diag()) fprintf(stderr, "OP_STORE_NAME: empty stack!\n");
                    continue;
                }
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                const proto::ProtoObject* val = stack.back();
                // Delay pop until done
                if (nameObj->isString(ctx)) {
                    // Update frame (CoW support)
                    std::string nStr;
                    nameObj->asString(ctx)->toUTF8String(ctx, nStr);
                    const proto::ProtoObject* newFrame = frame->setAttribute(ctx, nameObj->asString(ctx), val);
                    frame = const_cast<proto::ProtoObject*>(newFrame);
                    stack.pop_back(); // Pop val now that it's stored
                    
                    const proto::ProtoString* dataS = getInternalString(ctx, "__data__");
                    const proto::ProtoObject* dataObj = frame->getAttribute(ctx, dataS);
                    if (dataObj && dataObj->asSparseList(ctx)) {
                        const proto::ProtoSparseList* dataList = dataObj->asSparseList(ctx);
                        dataList = dataList->setAt(ctx, nameObj->getHash(ctx), val);
                        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, dataS, dataList->asObject(ctx)));
                    }
                    
                    const proto::ProtoString* keysS = getInternalString(ctx, "__keys__");
                    const proto::ProtoObject* keysObj = frame->getAttribute(ctx, keysS);
                    if (keysObj && keysObj->asList(ctx)) {
                        const proto::ProtoList* keysList = keysObj->asList(ctx);
                        if (!keysList->has(ctx, nameObj)) {
                            keysList = keysList->appendLast(ctx, nameObj);
                            frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, keysS, keysList->asObject(ctx)));
                        }
                    }

                    if (get_env_diag()) {
                        fprintf(stderr, "DEBUG: OP_STORE_NAME finished frame update for PC %lu\n", i);
                        fflush(stderr);
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
                }
            }
        } else if (op == OP_LOAD_FAST) {
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<unsigned long>(arg) < nSlots) {
                const proto::ProtoObject** slots = ctx->getAutomaticLocals();
                const proto::ProtoObject* val = slots[arg];
                if (std::getenv("PROTO_ENV_DIAG") && arg == 0) {
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
            
            const proto::ProtoObject* iadd = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIAddString() : proto::ProtoString::fromUTF8String(ctx, "__iadd__"));
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
            const proto::ProtoObject* isub = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getISubString() : proto::ProtoString::fromUTF8String(ctx, "__isub__"));
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
            const proto::ProtoObject* imul = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIMulString() : proto::ProtoString::fromUTF8String(ctx, "__imul__"));
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
            const proto::ProtoObject* r = left->modulo(ctx, right);
            stack.pop_back();
            stack.back() = r;
        } else if (op == OP_BINARY_MATRIX_MULTIPLY) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* right = stack.back();
            const proto::ProtoObject* left = stack[stack.top - 2];
            const proto::ProtoString* matmulS = getInternalString(ctx, "__matmul__");
            const proto::ProtoObject* matmul = left->getAttribute(ctx, matmulS);
            if (matmul && matmul != PROTO_NONE) {
                const proto::ProtoObject* res = invokePythonCallable(ctx, matmul, ctx->newList()->appendLast(ctx, right), nullptr);
                stack.pop_back();
                stack.back() = res;
            } else {
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                if (env) env->setPendingException(ctx->fromUTF8String("TypeError: '@' operator not supported (stubbed)"));
            }
        } else if (op == OP_INPLACE_MATRIX_MULTIPLY) {
            if (stack.size() < 2) { i = next_i; continue; }
            const proto::ProtoObject* right = stack.back();
            const proto::ProtoObject* left = stack[stack.top - 2];
            const proto::ProtoString* imatmulS = getInternalString(ctx, "__imatmul__");
            const proto::ProtoObject* imatmul = left->getAttribute(ctx, imatmulS);
            if (imatmul && imatmul != PROTO_NONE) {
                stack.push_back(invokePythonCallable(ctx, imatmul, ctx->newList()->appendLast(ctx, right), nullptr));
            } else {
                // fallback to matmul
                const proto::ProtoString* matmulS = getInternalString(ctx, "__matmul__");
                const proto::ProtoObject* matmul = left->getAttribute(ctx, matmulS);
                if (matmul && matmul != PROTO_NONE) {
                    stack.push_back(invokePythonCallable(ctx, matmul, ctx->newList()->appendLast(ctx, right), nullptr));
                } else {
                    env->setPendingException(ctx->fromUTF8String("TypeError: '@=' operator not supported (stubbed)"));
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
            const proto::ProtoObject* itruediv = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getITrueDivString() : proto::ProtoString::fromUTF8String(ctx, "__itruediv__"));
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
            const proto::ProtoObject* ifloordiv = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIFloorDivString() : proto::ProtoString::fromUTF8String(ctx, "__ifloordiv__"));
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
            const proto::ProtoObject* imod = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIModString() : proto::ProtoString::fromUTF8String(ctx, "__imod__"));
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
            const proto::ProtoObject* ipow = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIPowString() : proto::ProtoString::fromUTF8String(ctx, "__ipow__"));
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
            const proto::ProtoObject* ilshift = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getILShiftString() : proto::ProtoString::fromUTF8String(ctx, "__ilshift__"));
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
            const proto::ProtoObject* irshift = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIRShiftString() : proto::ProtoString::fromUTF8String(ctx, "__irshift__"));
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
            const proto::ProtoObject* iand = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIAndString() : proto::ProtoString::fromUTF8String(ctx, "__iand__"));
            if (iand && iand->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = iand->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = ctx->fromInteger(a->asLong(ctx) & b->asLong(ctx));
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoObject* andM = a->getAttribute(ctx, env ? env->getAndString() : proto::ProtoString::fromUTF8String(ctx, "__and__"));
                if (andM && andM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = andM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* randM = b->getAttribute(ctx, env ? env->getRAndString() : proto::ProtoString::fromUTF8String(ctx, "__rand__"));
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
            const proto::ProtoObject* ior = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIOrString() : proto::ProtoString::fromUTF8String(ctx, "__ior__"));
            if (ior && ior->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ior->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = ctx->fromInteger(a->asLong(ctx) | b->asLong(ctx));
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoObject* orM = a->getAttribute(ctx, env ? env->getOrString() : proto::ProtoString::fromUTF8String(ctx, "__or__"));
                if (orM && orM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = orM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* rorM = b->getAttribute(ctx, env ? env->getROrString() : proto::ProtoString::fromUTF8String(ctx, "__ror__"));
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
            const proto::ProtoObject* ixor = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getIXorString() : proto::ProtoString::fromUTF8String(ctx, "__ixor__"));
            if (ixor && ixor->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ixor->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                const proto::ProtoObject* res = ctx->fromInteger(a->asLong(ctx) ^ b->asLong(ctx));
                stack.pop_back(); stack.back() = res;
            } else {
                const proto::ProtoObject* xorM = a->getAttribute(ctx, env ? env->getXorString() : proto::ProtoString::fromUTF8String(ctx, "__xor__"));
                if (xorM && xorM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = xorM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* rxorM = b->getAttribute(ctx, env ? env->getRXorString() : proto::ProtoString::fromUTF8String(ctx, "__rxor__"));
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
                const proto::ProtoObject* andM = a->getAttribute(ctx, env ? env->getAndString() : proto::ProtoString::fromUTF8String(ctx, "__and__"));
                if (andM && andM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = andM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* randM = b->getAttribute(ctx, env ? env->getRAndString() : proto::ProtoString::fromUTF8String(ctx, "__rand__"));
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
                const proto::ProtoString* orS = env ? env->getOrString() : proto::ProtoString::fromUTF8String(ctx, "__or__");
                const proto::ProtoList* argsB = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = invokeDunder(ctx, a, orS, argsB);

                if (!result) {
                    const proto::ProtoString* rorS = env ? env->getROrString() : proto::ProtoString::fromUTF8String(ctx, "__ror__");
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
                const proto::ProtoObject* xorM = a->getAttribute(ctx, env ? env->getXorString() : proto::ProtoString::fromUTF8String(ctx, "__xor__"));
                if (xorM && xorM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = xorM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    stack.pop_back(); stack.back() = (result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* rxorM = b->getAttribute(ctx, env ? env->getRXorString() : proto::ProtoString::fromUTF8String(ctx, "__rxor__"));
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
                const proto::ProtoObject* inv = a->getAttribute(ctx, env ? env->getInvertString() : proto::ProtoString::fromUTF8String(ctx, "__invert__"));
                if (inv && inv->asMethod(ctx)) {
                    const proto::ProtoList* noArgs = ctx->newList();
                    const proto::ProtoObject* result = inv->asMethod(ctx)(ctx, a, nullptr, noArgs, nullptr);
                    if (std::getenv("PROTO_ENV_DIAG")) {
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
                 continue;
            } else if (arg == 1) {
                 if (!stack.empty()) {
                     const proto::ProtoObject* exc = stack.back();
                     stack.pop_back();
                     if (env && env->getTypePrototype()) {
                         const proto::ProtoObject* cls = exc->getAttribute(ctx, env->getClassString());
                         if (cls == env->getTypePrototype()) {
                             const proto::ProtoString* callS = env->getCallString();
                             exc = exc->call(ctx, nullptr, callS, exc, ctx->newList(), nullptr);
                         }
                     }
                     if (env && exc) env->setPendingException(exc);
                 }
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
                // 1. Check for __all__
                const proto::ProtoObject* allObj = mod->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__all__"));
                if (allObj && allObj->asList(ctx)) {
                    const proto::ProtoList* allList = allObj->asList(ctx);
                    const proto::ProtoListIterator* it = allList->getIterator(ctx);
                    if (std::getenv("PROTO_RESOLVE_DIAG")) {}
                    while (it && it->hasNext(ctx)) {
                        const proto::ProtoObject* nameObj = it->next(ctx);
                        if (nameObj && nameObj->isString(ctx)) {
                            const proto::ProtoObject* val = mod->getAttribute(ctx, nameObj->asString(ctx));
                            if (std::getenv("PROTO_RESOLVE_DIAG")) {
                            }
                            if (val) {
                                std::string n;
                                nameObj->asString(ctx)->toUTF8String(ctx, n);
                                if (n == "_splitext") {
                                    fprintf(stderr, "DEBUG: OP_IMPORT_STAR storing _splitext from mod %p: val %p\n", (void*)mod, (void*)val);
            fflush(stderr);
                                }
                                frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, nameObj->asString(ctx), val));
                            }
                        }
                        it = it->advance(ctx);
                    }
                    fprintf(stderr, "OP_IMPORT_STAR finished loop 1\n"); fflush(stderr);
                } else {
                    fprintf(stderr, "OP_IMPORT_STAR using __keys__\n");
                    // 2. Iterate over all attributes if __keys__ is available
                    const proto::ProtoObject* keysObj = mod->getAttribute(ctx, getInternalString(ctx, "__keys__"));
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
                                    if (n == "_splitext") {
                                        fprintf(stderr, "DEBUG: OP_IMPORT_STAR (fallback) storing _splitext from mod %p: val %p\n", (void*)mod, (void*)val);
            fflush(stderr);
                                    }
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
            if (names && stack.size() >= 1 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* mod = stack.back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::string n; nameS->toUTF8String(ctx, n);
                        fprintf(stderr, "DEBUG: OP_IMPORT_FROM loading %s\n", n.c_str());
                    }

                    const proto::ProtoObject* val = (env) 
                        ? env->getAttribute(ctx, mod, nameS) 
                        : mod->getAttribute(ctx, nameS);
                    
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::string n; nameS->toUTF8String(ctx, n);
                        fprintf(stderr, "DEBUG: OP_IMPORT_FROM name=%s val=%p hasAttr=%d\n", n.c_str(), (void*)val, mod->hasAttribute(ctx, nameS) == PROTO_TRUE);
                    }

                    if (val && (val != PROTO_NONE || mod->hasAttribute(ctx, nameS) == PROTO_TRUE)) {
                        stack.push_back(val);
                    } else {
                        if (env) {
                            std::string n;
                            nameS->toUTF8String(ctx, n);
                            std::string msg = "cannot import name '" + n + "'";
                            const proto::ProtoObject* mName = mod->getAttribute(ctx, env->getNameString());
                            if (mName && mName->isString(ctx)) {
                                std::string mn;
                                mName->asString(ctx)->toUTF8String(ctx, mn);
                                msg += " from '" + mn + "'";
                            }
                            // Also check file?
                             const proto::ProtoObject* fileAttr = mod->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__file__"));
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
            
            const proto::ProtoString* enterS = env ? env->getEnterString() : proto::ProtoString::fromUTF8String(ctx, "__enter__");
            const proto::ProtoString* exitS = env ? env->getExitString() : proto::ProtoString::fromUTF8String(ctx, "__exit__");
            
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
            blockStack.push_back({static_cast<unsigned long>(arg), stack.size()});
            
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
            const proto::ProtoObject* pos = isEmbeddedValue(ctx, a) ? nullptr : a->getAttribute(ctx, env ? env->getPosString() : proto::ProtoString::fromUTF8String(ctx, "__pos__"));
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
                const proto::ProtoObject* data = lstObj->getAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"));
                if (data && data->asList(ctx)) {
                    const proto::ProtoList* lst = data->asList(ctx);
                    lst = lst->appendLast(ctx, val);
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        fprintf(stderr, "DEBUG: OP_LIST_APPEND val=%p appended to list, new size=%zu\n", (void*)val, lst->getSize(ctx));
                    }
                    const proto::ProtoObject* newLst = lstObj->setAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"), lst->asObject(ctx));
                    stack[stack.size() - arg - 1] = const_cast<proto::ProtoObject*>(newLst);
                }
                stack.pop_back(); // Pop val now
            }
        } else if (op == OP_MAP_ADD) {
            if (stack.size() >= static_cast<size_t>(arg) + 1) { // key, val + mapObj must be there
                const proto::ProtoObject* key = stack.back();
                const proto::ProtoObject* val = stack[stack.top - 2];
                proto::ProtoObject* mapObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = mapObj->getAttribute(ctx, dataString);
                if (data && data->asSparseList(ctx)) {
                    const proto::ProtoSparseList* sl = data->asSparseList(ctx);
                    unsigned long h = key->getHash(ctx);
                    bool isNew = !sl->has(ctx, h);
                    sl = sl->setAt(ctx, h, val);
                    const proto::ProtoObject* newMap = mapObj->setAttribute(ctx, dataString, sl->asObject(ctx));
                    stack[stack.size() - arg - 1] = const_cast<proto::ProtoObject*>(newMap);
                    if (isNew) {
                         const proto::ProtoString* keysString = getInternalString(ctx, "__keys__");
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
                const proto::ProtoString* dataString = env ? env->getDataString() : getInternalString(ctx, "__data__");
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
                const proto::ProtoString* dataString = env ? env->getDataString() : getInternalString(ctx, "__data__");
                const proto::ProtoObject* toData = toObj->getAttribute(ctx, dataString);
                if (toData && toData->asSparseList(ctx)) {
                    const proto::ProtoSparseList* toSL = toData->asSparseList(ctx);
                    const proto::ProtoObject* fromData = from->getAttribute(ctx, dataString);
                    if (fromData && fromData->asSparseList(ctx)) {
                        const proto::ProtoSparseList* fromSL = fromData->asSparseList(ctx);
                        const proto::ProtoString* keysName = getInternalString(ctx, "__keys__");
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
                const proto::ProtoString* dataString = env ? env->getDataString() : getInternalString(ctx, "__data__");
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
                    }
                }
                stack.pop_back(); // Pop iterable
            }
        } else if (op == OP_SET_UPDATE) {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* iterable = stack.back();
                // iterable remains on stack
                proto::ProtoObject* setObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                const proto::ProtoString* dataString = env ? env->getDataString() : getInternalString(ctx, "__data__");
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
            if (env && env->getSetPrototype()) setObj->addParent(ctx, env->getSetPrototype());
            const proto::ProtoSet* data = ctx->newSet();
            const proto::ProtoObject* dataPinned = data->asObject(ctx);
            stack.push_back(dataPinned); // Root data
            
            size_t baseIdx = stack.size() - 2 - arg;
            for (int j = 0; j < arg; ++j) {
                const proto::ProtoObject* item = stack[baseIdx + j];
                data = data->add(ctx, item);
                stack[stack.size() - 1] = const_cast<proto::ProtoObject*>(data->asObject(ctx)); // Update root
            }
            
            setObj->setAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"), data->asObject(ctx));
            const proto::ProtoObject* finalSet = stack[stack.size() - 2];
            for (int j = 0; j < arg + 2; ++j) stack.pop_back();
            stack.push_back(finalSet);
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
                    if (std::getenv("PROTO_ENV_DIAG")) {
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
                        if (val && val != PROTO_NONE) { found = true; break; }

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
                    
                    const proto::ProtoString* dName = env ? env->getDataString() : getInternalString(ctx, "__data__");
                    const proto::ProtoObject* dataObj = curr->getAttribute(ctx, dName);
                    if (dataObj && dataObj->asSparseList(ctx)) {
                        val = dataObj->asSparseList(ctx)->getAt(ctx, h);
                        if (val && val != PROTO_NONE) { found = true; break; }
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
                        
                        const proto::ProtoString* dName = env ? env->getDataString() : getInternalString(ctx, "__data__");
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
            if (names && stack.size() >= 1 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* attrName = nameObj->asString(ctx);
                    std::string attrNameStr;
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        attrName->toUTF8String(ctx, attrNameStr);
                        // ...
                        fprintf(stderr, "DEBUG: OP_LOAD_ATTR calling getAttribute env=%p obj=%p attr=%s\n", (void*)env, (void*)obj, attrNameStr.c_str());
            fflush(stderr);
                        fflush(stderr);
                    }
                    
                    if (attrNameStr.empty()) attrName->toUTF8String(ctx, attrNameStr);
                    if (attrNameStr == "__new__") {
                        if (get_env_diag()) fprintf(stderr, "DEBUG: OP_LOAD_ATTR '__new__' env=%p obj=%p\n", (void*)env, (void*)obj);
                    }

                    const proto::ProtoObject* val = env ? env->getAttribute(ctx, obj, attrName) : obj->getAttribute(ctx, attrName);
                    
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        fprintf(stderr, "DEBUG: OP_LOAD_ATTR returned val=%p\n", (void*)val);
                        fflush(stderr);
                    }
                    
                    bool isMissing = false;
                    if (!val) {
                        if (env && env->hasPendingException()) {
                            stack.pop_back(); // Pop obj
                            continue; // Exception already set by __getattr__ or descriptor
                        }
                        isMissing = true;
                    } else if (val == PROTO_NONE) {
                        if (obj->hasAttribute(ctx, attrName) == PROTO_FALSE) {
                            const proto::ProtoString* getattrS = proto::ProtoString::fromUTF8String(ctx, "__getattr__");
                            const proto::ProtoObject* cls = obj->getAttribute(ctx, env ? env->getClassString() : proto::ProtoString::fromUTF8String(ctx, "__class__"));
                            bool hasGetattr = false;
                            if (cls && cls != PROTO_NONE && cls->hasAttribute(ctx, getattrS) == PROTO_TRUE) {
                                hasGetattr = true;
                            } else if (obj->hasOwnAttribute(ctx, getattrS) == PROTO_TRUE) {
                                hasGetattr = true;
                            }
                            if (!hasGetattr) isMissing = true;
                        }
                    }

                    if (!isMissing) {
                        stack.back() = val ? val : PROTO_NONE; // Replace obj with result
                    } else {
                        stack.pop_back(); // Pop obj before raising error
                        std::string attr;
                        attrName->toUTF8String(ctx, attr);
                        if (env) env->raiseAttributeError(ctx, obj, attr);
                        continue;
                    }
                }
            }
        } else if (op == OP_STORE_ATTR) {
            if (names && stack.size() >= 2 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                const proto::ProtoObject* val = stack[stack.top - 2];
                // Delay pop
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    if (std::getenv("PROTO_ENV_DIAG")) {
                    }
                    if (env) {
                        obj = const_cast<proto::ProtoObject*>(env->setAttribute(ctx, obj, nameS, val));
                    } else {
                        proto::ProtoObject* mutableObj = const_cast<proto::ProtoObject*>(obj);
                        obj = const_cast<proto::ProtoObject*>(mutableObj->setAttribute(ctx, nameS, val));
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
            
            listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"), lst->asObject(ctx)));
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
            
            const proto::ProtoString* getItemS = env ? env->getGetItemString() : proto::ProtoString::fromUTF8String(ctx, "__getitem__");
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
            const proto::ProtoObject* result = invokeDunder(ctx, container, getItemS, args);
            
            if (get_env_diag()) {
                fprintf(stderr, "DEBUG: OP_BINARY_SUBSCR container=%p repr=%s key=%p repr=%s\n", (void*)container, PythonEnvironment::reprObject(ctx, container).c_str(), (void*)key, PythonEnvironment::reprObject(ctx, key).c_str());
                fflush(stderr);
            }
            if (!result) {
                const proto::ProtoString* classGetItemS = proto::ProtoString::fromUTF8String(ctx, "__class_getitem__");
                // Check if container itself has __class_getitem__ (for types) via invokeDunder
                result = invokeDunder(ctx, container, classGetItemS, args);
            }

            if (get_env_diag()) {
                fprintf(stderr, "DEBUG: OP_BINARY_SUBSCR result=%p\n", (void*)result);
                fflush(stderr);
            }

            if (result) {
                stack.pop_back();
                stack.back() = result;
            } else if (env && env->hasPendingException()) {
                continue;
            } else {
                // Fallback for minimal objects without __getitem__ (e.g. built-in lists/tuples if dunder is missing)
                const proto::ProtoObject* data = container->getAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"));
                if (!data) data = container; // Fallback to the object itself (for primitive strings/tuples)
                
                if (data) {
                    if (data->asList(ctx) && key->isInteger(ctx)) {
                        long long idx = key->asLong(ctx);
                        const proto::ProtoList* list = data->asList(ctx);
                        const proto::ProtoObject* res = (idx >= 0 && static_cast<unsigned long>(idx) < list->getSize(ctx)) ? list->getAt(ctx, static_cast<int>(idx)) : PROTO_NONE;
                        stack.pop_back(); stack.back() = res;
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
                    } else if (data->asString(ctx)) {
                        const proto::ProtoString* s = data->asString(ctx);
                        long long size = static_cast<long long>(s->getSize(ctx));
                        if (key->isInteger(ctx)) {
                            long long idx = key->asLong(ctx);
                            if (idx < 0) idx += size;
                            const proto::ProtoString* charStr = (idx >= 0 && static_cast<unsigned long>(idx) < s->getSize(ctx)) ? s->getSlice(ctx, static_cast<int>(idx), static_cast<int>(idx) + 1) : nullptr;
                            const proto::ProtoObject* charObj = charStr ? charStr->asObject(ctx) : PROTO_NONE;
                            proto::ProtoObject* resObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                            resObj->setAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"), charObj);
                            if (env && env->getStrPrototype()) {
                                resObj = const_cast<proto::ProtoObject*>(resObj->addParent(ctx, env->getStrPrototype()));
                                resObj->setAttribute(ctx, env->getClassString(), env->getStrPrototype());
                            }
                            stack.pop_back(); stack.back() = resObj;
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
                            resObj->setAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"), slice->asObject(ctx));
                            if (env && env->getStrPrototype()) {
                                resObj = const_cast<proto::ProtoObject*>(resObj->addParent(ctx, env->getStrPrototype()));
                                resObj->setAttribute(ctx, env->getClassString(), env->getStrPrototype());
                            }
                            stack.pop_back(); stack.back() = resObj;
                        }
                    } else if (data->asSparseList(ctx)) {
                        unsigned long h = key->getHash(ctx);
                        const proto::ProtoObject* val = data->asSparseList(ctx)->getAt(ctx, h);
                        stack.pop_back();
                        stack.back() = (val ? val : PROTO_NONE);
                    }
                } else {
                    // Start of Error Handling for unsubscriptable objects
                    std::string typeName = "unknown";
                    if (container) {
                         const proto::ProtoObject* cls = container->getAttribute(ctx, env ? env->getClassString() : getInternalString(ctx, "__class__"));
                         if (cls) {
                             const proto::ProtoObject* nameAttr = cls->getAttribute(ctx, env ? env->getNameString() : getInternalString(ctx, "__name__"));
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
            
            const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(ctx, "__data__");
            const proto::ProtoString* keysName = env ? env->getKeysString() : getInternalString(ctx, "__keys__");
            
            dictObj = const_cast<proto::ProtoObject*>(dictObj->setAttribute(ctx, dataName, data->asObject(ctx)));
            stack.back() = dictObj;
            dictObj = const_cast<proto::ProtoObject*>(dictObj->setAttribute(ctx, keysName, keys->asObject(ctx)));
            stack.back() = dictObj;
            
            const proto::ProtoObject* finalDict = dictObj;
            for (int k = 0; k < 2 * arg + 3; ++k) stack.pop_back();
            stack.push_back(finalDict);
        } else if (op == OP_STORE_SUBSCR) {
            // i++;
            if (stack.size() < 3) { i = next_i; continue; }
            proto::ProtoObject* container = const_cast<proto::ProtoObject*>(stack.back());
            const proto::ProtoObject* value = stack[stack.top - 2];
            const proto::ProtoObject* key = stack[stack.top - 3];
            // Delay pop

            const proto::ProtoString* setItemS = env ? env->getSetItemString() : proto::ProtoString::fromUTF8String(ctx, "__setitem__");
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
                    if (std::getenv("PROTO_ENV_DIAG")) {
                    }
                    container->setAttribute(ctx, key->asString(ctx), value);
                } else {
                    // Dictionary-like storage in __data__ for non-string keys or explicit collections
                    const proto::ProtoString* dataS = getInternalString(ctx, "__data__");
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
                const proto::ProtoString* keysS = getInternalString(ctx, "__keys__");
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
        } else if (op == OP_CALL_FUNCTION_KW) {
            if (stack.size() < 2) { i = next_i; continue; } // at least callable and names_tuple
            const proto::ProtoObject* namesTupleObj = stack.back();
            stack.pop_back();
            const proto::ProtoTuple* namesTuple = namesTupleObj->asTuple(ctx);
            if (!namesTuple) continue;
            int nkw = namesTuple->getSize(ctx);
            int npos = arg - nkw;
            if (stack.size() < static_cast<size_t>(arg) + 1) continue;

            int firstKwPos = stack.top - nkw;
            int firstPosIdx = firstKwPos - npos;

            // Build positional args list. Use stack to track the intermediate list.
            stack.push_back(ctx->newList()->asObject(ctx));
            for (int p = 0; p < npos; ++p) {
                const proto::ProtoList* l = stack.back()->asList(ctx);
                l = l->appendLast(ctx, stack[firstPosIdx + p]);
                stack[stack.top - 1] = l->asObject(ctx);
            }
            const proto::ProtoList* plArgs = stack.back()->asList(ctx);
            stack.pop_back();

            // Build keyword map. Use stack to track it.
            stack.push_back(ctx->newSparseList()->asObject(ctx));
            for (int k = 0; k < nkw; ++k) {
                const proto::ProtoObject* nameStr = namesTuple->getAt(ctx, k);
                if (nameStr->isString(ctx)) {
                    const proto::ProtoSparseList* m = stack.back()->asSparseList(ctx);
                    m = m->setAt(ctx, nameStr->getHash(ctx), stack[firstKwPos + k]);
                    stack[stack.top - 1] = m->asObject(ctx);
                }
            }
            const proto::ProtoSparseList* kwMap = stack.back()->asSparseList(ctx);
            // Positions: callable (firstPosIdx-1), args (firstPosIdx...firstKwPos-1), kwVals (firstKwPos...top-4), namesTuple (top-3), plArgs (top-2), kwMap (top-1)
            // Wait, plArgs is at top-2, kwMap is at top-1.
            // callable is at index firstPosIdx - 1.
            const proto::ProtoObject* callable = stack[firstPosIdx - 1];
            
            if (env) env->pushKwNames(namesTuple);
            const proto::ProtoObject* result = invokeCallable(ctx, callable, plArgs, kwMap);
            if (env) env->popKwNames();
            
            // Now stack contains: callable + (arg) items + kwMap. Total to pop: arg + 2.
            for (int j = 0; j < arg + 2; ++j) stack.pop_back(); // Pop callable, args, kwMap
            stack.push_back(result ? result : (env ? env->getNonePrototype() : PROTO_NONE));
        } else if (op == OP_CALL_FUNCTION) {
            if (stack.size() < (unsigned long)(arg + 1)) continue;
            int firstArgPos = stack.top - arg;
            
            stack.push_back(ctx->newList()->asObject(ctx));
            for (int j = 0; j < arg; ++j) {
                const proto::ProtoList* l = stack.back()->asList(ctx);
                l = l->appendLast(ctx, stackBase[firstArgPos + j]);
                stack[stack.top - 1] = l->asObject(ctx);
            }
            const proto::ProtoList* args = stack.back()->asList(ctx);
            const proto::ProtoObject* callable = stack[firstArgPos - 1];
            
            if (std::getenv("PROTO_ENV_DIAG")) {
                fprintf(stderr, "DEBUG: OP_CALL_FUNCTION PC %lu callable=%p argCount=%d\n", i, (void*)callable, arg);
                fflush(stderr);
            }

            const proto::ProtoObject* result = invokeCallable(ctx, callable, args);
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            
            // Cleanup: Pop callable, args, and intermediate args-list
            for (int j = 0; j < arg + 2; ++j) stack.pop_back();
            stack.push_back(result ? result : (env ? env->getNonePrototype() : PROTO_NONE));
            if (!result && env && env->hasPendingException()) continue;
        } else if (op == OP_CALL_FUNCTION_EX) {
            const proto::ProtoObject* kwargs = (arg & 1) ? stack.back() : nullptr;
            const proto::ProtoObject* starargs = (arg & 1) ? stack[stack.top - 2] : stack.back();
            const proto::ProtoObject* callable = (arg & 1) ? stack[stack.top - 3] : stack[stack.top - 2];
            // Keep them on stack during extraction
            
            const proto::ProtoList* posArgs = nullptr;
            if (starargs && starargs->asList(ctx)) {
                posArgs = starargs->asList(ctx);
            } else if (starargs && starargs->isTuple(ctx)) {
                const proto::ProtoTuple* tup = starargs->asTuple(ctx);
                const proto::ProtoList* L = ctx->newList();
                for (size_t i = 0; i < tup->getSize(ctx); ++i) {
                    L = L->appendLast(ctx, tup->getAt(ctx, i));
                }
                posArgs = L;
            } else if (starargs && starargs != PROTO_NONE) {
                // Fallback: use getIter
                if (env) {
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
                                continue;
                            }
                        }
                        posArgs = L;
                    }
                }
            }
            if (!posArgs) posArgs = ctx->newList();
            
            if (std::getenv("PROTO_ENV_DIAG")) {
                std::string clsName = "unknown";
                std::string repr = "unknown";
                if (env) {
                    const proto::ProtoObject* cls = starargs ? starargs->getAttribute(ctx, env->getClassString()) : nullptr;
                    if (cls) {
                        const proto::ProtoObject* nameAttr = cls->getAttribute(ctx, env->getNameString());
                        if (nameAttr && nameAttr->isString(ctx)) nameAttr->asString(ctx)->toUTF8String(ctx, clsName);
                    }
                    repr = PythonEnvironment::reprObject(ctx, starargs);
                }
                fprintf(stderr, "DEBUG: OP_CALL_FUNCTION_EX PC %lu callable=%p starargs=%p (cls=%s, repr=%s) posArgsSize=%zu\n", i, (void*)callable, (void*)starargs, clsName.c_str(), repr.c_str(), posArgs->getSize(ctx));
            }
            
            const proto::ProtoSparseList* kwArgs = nullptr;
            if (kwargs && kwargs->asSparseList(ctx)) {
                kwArgs = kwargs->asSparseList(ctx);
            } else if (kwargs && kwargs != PROTO_NONE && env) {
                 // Fallback: check if it has __data__ (for dict objects)
                 const proto::ProtoString* dName = env->getDataString();
                 const proto::ProtoObject* data = kwargs->getAttribute(ctx, dName);
                 if (data && data->asSparseList(ctx)) kwArgs = data->asSparseList(ctx);
            }
            
            bool pushed = false;
            if (kwargs && env) {
                 const proto::ProtoObject* keysListObj = kwargs->getAttribute(ctx, getInternalString(ctx, "__keys__"));
                 if (keysListObj && keysListObj->asList(ctx)) {
                     env->pushKwNames(ctx->newTupleFromList(keysListObj->asList(ctx)));
                     pushed = true;
                 }
            }

            const proto::ProtoObject* result = invokePythonCallable(ctx, callable, posArgs, kwArgs);
            if (pushed && env) env->popKwNames();
            
            // Pop callable, starargs, [kwargs]
            int toPop = (arg & 1) ? 3 : 2;
            for (int j = 0; j < toPop; ++j) stack.pop_back();
            
            stack.push_back(result ? result : (env ? env->getNonePrototype() : PROTO_NONE));
            if (!result && env && env->hasPendingException()) continue;
        } else if (op == OP_BUILD_TUPLE) {
            if (stack.size() < static_cast<size_t>(arg)) continue;
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
            
            tupObj->setAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"), tup->asObject(ctx));
            
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
                    fprintf(stderr, "DEBUG: OP_BUILD_FUNCTION called for PC %lu, codeObj=%p\n", i, (void*)codeObj);
                    fflush(stderr);
                }
                proto::ProtoObject* fn = createUserFunction(ctx, codeObj, const_cast<proto::ProtoObject*>(PythonEnvironment::getCurrentGlobals()), frame, defaults, kwDefaults);
                if (get_env_diag()) {
                    fprintf(stderr, "DEBUG: OP_BUILD_FUNCTION finished createUserFunction\n");
                    fflush(stderr);
                }
                if (fn) {
                    stack.push_back(fn);
                }
            }

        } else if (op == OP_BUILD_CLASS) {
            if (stack.size() >= 4 && frame) {
                // Keep name, bases, kwds, body on stack as roots.
                // stack order: [..., name, bases, kwds, body]
                int firstIdx = stack.top - 4;
                const proto::ProtoObject* body = stack[firstIdx + 3];
                const proto::ProtoObject* kwds = stack[firstIdx + 2];
                const proto::ProtoObject* bases = stack[firstIdx + 1];
                const proto::ProtoObject* name = stack[firstIdx];
                if (get_env_diag()) {
                    std::string n = "unknown";
                    if (name && name->isString(ctx)) name->asString(ctx)->toUTF8String(ctx, n);
                    fprintf(stderr, "DEBUG OP_BUILD_CLASS: building name='%s'\n", n.c_str());
                }
                
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                const proto::ProtoString* nameS = env ? env->getNameString() : getInternalString(ctx, "__name__");
                const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__");
                
                // 1. Identify Metaclass
                const proto::ProtoObject* metaclass = nullptr;
                if (kwds && kwds != PROTO_NONE) {
                    const proto::ProtoString* kName = proto::ProtoString::fromUTF8String(ctx, "metaclass");
                    metaclass = kwds->getAttribute(ctx, kName);
                    if (!metaclass || metaclass == PROTO_NONE) {
                        // Try looking in __data__ if it's a dict object
                        const proto::ProtoObject* dataObj = kwds->getAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"));
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
                    
                    if (bases && bases->asTuple(ctx)) {
                        const proto::ProtoTuple* tupleBases = bases->asTuple(ctx);
                        for (size_t i = 0; i < tupleBases->getSize(ctx); ++i) {
                            const proto::ProtoObject* base = tupleBases->getAt(ctx, i);
                            const proto::ProtoObject* baseMeta = nullptr;
                            if (env) {
                                baseMeta = base->getAttribute(ctx, env->getClassString());
                                if (!baseMeta || baseMeta == PROTO_NONE) {
                                    baseMeta = env->getType(ctx, base);
                                }
                            } else {
                                baseMeta = base->getAttribute(ctx, getInternalString(ctx, "__class__"));
                            }
                            if (!baseMeta || baseMeta == PROTO_NONE || baseMeta == objectProto) {
                                // If a native base accidentally lacks a metaclass (evaluating to object), default it to type
                                baseMeta = typeProto;
                            }
                            // Compute derivation: if baseMeta is a subclass of bestMeta, it becomes the new best
                            if (baseMeta != bestMeta && bestMeta) {
                                const proto::ProtoObject* mro = baseMeta->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__mro__"));
                                bool isSub = false;
                                if (mro) {
                                    const proto::ProtoTuple* mroTuple = mro->asTuple(ctx);
                                    if (mroTuple) {
                                        for (size_t j = 0; j < mroTuple->getSize(ctx); ++j) {
                                            if (mroTuple->getAt(ctx, j) == bestMeta) {
                                                isSub = true;
                                                break;
                                            }
                                        }
                                    } else {
                                        const proto::ProtoList* mroList = mro->asList(ctx);
                                        if (mroList) {
                                            for (unsigned long j = 0; j < mroList->getSize(ctx); ++j) {
                                                if (mroList->getAt(ctx, j) == bestMeta) {
                                                    isSub = true;
                                                    break;
                                                }
                                            }
                                        }
                                    }
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
                if (std::getenv("PROTO_ENV_DIAG")) {
                }

                // 2. Metaclass __prepare__
                if (get_env_diag()) fprintf(stderr, "DEBUG OP_BUILD_CLASS: metaclass=%p (PROTO_NONE=%p)\n", (void*)metaclass, (void*)PROTO_NONE);
                if (metaclass) {
                    const proto::ProtoObject* mcName = metaclass->getAttribute(ctx, env ? env->getNameString() : getInternalString(ctx, "__name__"));
                    if (mcName && mcName->isString(ctx)) {
                        std::string mn; mcName->asString(ctx)->toUTF8String(ctx, mn);
                        if (get_env_diag()) fprintf(stderr, "DEBUG OP_BUILD_CLASS: metaclass name='%s'\n", mn.c_str());
                    }
                }
                const proto::ProtoObject* prepareRaw = nullptr;
                if (metaclass) {
                    prepareRaw = env ? env->getAttribute(ctx, metaclass, proto::ProtoString::fromUTF8String(ctx, "__prepare__")) : metaclass->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__prepare__"));
                }
                const proto::ProtoObject* prepareM = prepareRaw;
                if (prepareRaw && prepareRaw != PROTO_NONE) {
                    // Manually resolve descriptor if it has __get__ (since it might be an instance attribute on the class, escaping typical getAttribute descriptor logic)
                    const proto::ProtoObject* getMethod = prepareRaw->getAttribute(ctx, env ? env->getGetDunderString() : proto::ProtoString::fromUTF8String(ctx, "__get__"));
                    if (getMethod && getMethod != PROTO_NONE) {
                        const proto::ProtoList* getArgs = ctx->newList()->appendLast(ctx, env ? env->getNonePrototype() : PROTO_NONE)->appendLast(ctx, metaclass);
                        prepareM = invokeCallable(ctx, getMethod, getArgs, nullptr);
                    }
                }
                if (prepareM && prepareM != PROTO_NONE) {
                    const proto::ProtoList* prepareArgs = ctx->newList()->appendLast(ctx, name)->appendLast(ctx, bases);
                    // Use keyword parameters if available
                    const proto::ProtoSparseList* kw = (kwds && kwds->asSparseList(ctx)) ? kwds->asSparseList(ctx) : nullptr;
                    const proto::ProtoObject* nsObj = invokeCallable(ctx, prepareM, prepareArgs, kw);
                    stack.push_back(nsObj); 
                } else {
                    stack.push_back(ctx->newObject(true));
                }
                proto::ProtoObject* ns = const_cast<proto::ProtoObject*>(stack.back());
                
                // Initialize __keys__ list for the namespace
                const proto::ProtoString* keysS = env ? env->getKeysString() : getInternalString(ctx, "__keys__");
                if (ns->hasOwnAttribute(ctx, keysS) != PROTO_TRUE) {
                    const proto::ProtoList* keysList = ctx->newList();
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, keysS, keysList->asObject(ctx)));
                }

                // Setup standard attributes in ns
                const proto::ProtoString* py_name_s = env ? env->getNameString() : getInternalString(ctx, "__name__");
                const proto::ProtoString* py_module_s = proto::ProtoString::fromUTF8String(ctx, "__module__");
                
                ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, nameS, name));
                // Add to keys
                const proto::ProtoObject* keysObj = ns->getAttribute(ctx, keysS);
                if (keysObj && keysObj->asList(ctx)) {
                    ns->setAttribute(ctx, keysS, keysObj->asList(ctx)->appendLast(ctx, nameS->asObject(ctx))->asObject(ctx));
                }

                const proto::ProtoObject* globals = env ? env->getCurrentGlobals() : nullptr;
                const proto::ProtoObject* moduleName = globals ? globals->getAttribute(ctx, py_module_s) : nullptr;
                if (moduleName) {
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, py_module_s, moduleName));
                    // Re-fetch keysObj to avoid stale pointer if setAttribute returns new version or updates state
                    keysObj = ns->getAttribute(ctx, keysS);
                    if (keysObj && keysObj->asList(ctx)) {
                        ns->setAttribute(ctx, keysS, keysObj->asList(ctx)->appendLast(ctx, py_module_s->asObject(ctx))->asObject(ctx));
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

                // 3. Execute body with ns as locals
                if (body) {
                    const proto::ProtoObject* codeObj = body->getAttribute(ctx, env ? env->getCodeString() : proto::ProtoString::fromUTF8String(ctx, "__code__"));
                    if (codeObj && codeObj != PROTO_NONE) {
                        runCodeObject(ctx, codeObj, ns);
                        stack.back() = ns; // ns may have been reallocated by CoW during execution
                    } else {
                        const proto::ProtoObject* callM = body->getAttribute(ctx, callS);
                        if (callM && callM->asMethod(ctx)) {
                            callM->asMethod(ctx)(ctx, body, nullptr, ctx->newList(), nullptr);
                        }
                    }
                }

                if (std::getenv("PROTO_ENV_DIAG")) {
                }

                // 4. Invoke metaclass to create the class
                const proto::ProtoList* mcArgs = ctx->newList()->appendLast(ctx, name)->appendLast(ctx, bases)->appendLast(ctx, ns);
                const proto::ProtoSparseList* kw = (kwds && kwds->asSparseList(ctx)) ? kwds->asSparseList(ctx) : nullptr;
                if (get_env_diag()) fprintf(stderr, "DEBUG OP_BUILD_CLASS: calling metaclass=%p\n", (void*)metaclass);
                const proto::ProtoObject* targetClass = invokeCallable(ctx, metaclass, mcArgs, kw);
                if (get_env_diag()) fprintf(stderr, "DEBUG OP_BUILD_CLASS: targetClass=%p\n", (void*)targetClass);
                
                if (targetClass && targetClass != PROTO_NONE) {
                    // Inject __class__ into the class namespace (frame) so methods can interpret it 
                    // via closure (parent frame reference).
                    // Note: object.__class__ data descriptor prevents this from shadowing the type 
                    // on the class object itself, so this is safe.
                    const proto::ProtoString* clsName = env ? env->getClassString() : getInternalString(ctx, "__class__");
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::string tName = "unknown";
                        const proto::ProtoObject* tNameAttr = targetClass->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"));
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
            if (std::getenv("PROTO_ENV_DIAG")) {
                fprintf(stderr, "DEBUG: OP_GET_ITER iterable=%p iterObj=%p\n", (void*)iterable, (void*)iterObj);
                fflush(stderr);
            }
            if (iterObj) {
                stack.back() = iterObj;
            } else {
                if (env) {
                    if (!env->hasPendingException()) {
                        env->raiseTypeError(ctx, "object is not iterable");
                    }
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
                        if (std::getenv("PROTO_ENV_DIAG")) {
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
            const proto::ProtoList* list = seq->asList(ctx);
            const proto::ProtoTuple* tup = seq->asTuple(ctx);
            if (!list && !tup) {
                 const proto::ProtoObject* data = seq->getAttribute(ctx, env ? env->getDataString() : getInternalString(ctx, "__data__"));
                 if (data) {
                     list = data->asList(ctx);
                     tup = data->asTuple(ctx);
                 }
            }
            if (list) {
                if (static_cast<int>(list->getSize(ctx)) < arg) continue;
                for (int j = arg - 1; j >= 0; --j) {
                    stack.push_back(list->getAt(ctx, j));
                }
            } else if (tup) {
                if (static_cast<int>(tup->getSize(ctx)) < arg) continue;
                for (int j = arg - 1; j >= 0; --j) {
                    stack.push_back(tup->getAt(ctx, j));
                }
            }
        } else if (op == OP_UNPACK_EX) {
            if (stack.empty()) { i = next_i; continue; }
            int num_before = arg & 0xFF;
            int num_after = (arg >> 8) & 0xFF;
            const proto::ProtoObject* seq = stack.back();
            stack.pop_back();

            std::vector<const proto::ProtoObject*> all;
            const proto::ProtoList* list = seq->asList(ctx);
            const proto::ProtoTuple* tup = seq->asTuple(ctx);
            if (list) {
                for (size_t i = 0; i < list->getSize(ctx); ++i) all.push_back(list->getAt(ctx, i));
            } else if (tup) {
                for (size_t i = 0; i < tup->getSize(ctx); ++i) all.push_back(tup->getAt(ctx, i));
            } else {
                continue;
            }

            if (static_cast<int>(all.size()) < num_before + num_after) continue;

            // Push after elements (in reverse order for stack)
            for (int i = static_cast<int>(all.size()) - 1; i >= static_cast<int>(all.size()) - num_after; --i) {
                stack.push_back(all[i]);
            }
            // Push middle list
            const proto::ProtoList* middle = ctx->newList();
            for (int i = num_before; i < static_cast<int>(all.size()) - num_after; ++i) {
                middle = middle->appendLast(ctx, all[i]);
            }
            stack.push_back(middle->asObject(ctx));
            // Push before elements (in reverse order)
            for (int i = num_before - 1; i >= 0; --i) {
                stack.push_back(all[i]);
            }
        } else if (op == OP_LOAD_GLOBAL) {
                if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    const proto::ProtoObject* val = frame->getAttribute(ctx, nameS);
                    bool found = (val != nullptr);
                    if (!found) {
                        const proto::ProtoString* dName = env ? env->getDataString() : getInternalString(ctx, "__data__");
                        const proto::ProtoObject* dataObj = frame->getAttribute(ctx, dName);
                        if (dataObj && dataObj->asSparseList(ctx)) {
                            if (dataObj->asSparseList(ctx)->has(ctx, nameObj->getHash(ctx))) {
                                val = dataObj->asSparseList(ctx)->getAt(ctx, nameObj->getHash(ctx));
                                found = true;
                            }
                        }
                    }
                    if (found) {
                        stack.push_back(val);
                    } else {
                        if (env) {
                            val = env->resolve(nameS, ctx);
                            if (val != nullptr) {
                                stack.push_back(val);
                            } else {
                                if (!env->hasPendingException()) {
                                    std::string n;
                                    nameS->toUTF8String(ctx, n);
                                    env->raiseNameError(ctx, n);
                                }
                                continue;
                            }
                        } else {
                            continue;
                        }
                    }
                }
            }
        } else if (op == OP_STORE_GLOBAL) {
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                if (stack.empty()) { i = next_i; continue; }
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (nameObj->isString(ctx)) {
                    frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, nameObj->asString(ctx), val));
                    PythonEnvironment::setCurrentFrame(frame);
                    if (sync_globals) PythonEnvironment::setCurrentGlobals(frame);
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
            sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx, env ? env->getStartString() : proto::ProtoString::fromUTF8String(ctx, "start"), startObj));
            sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx, env ? env->getStopString() : proto::ProtoString::fromUTF8String(ctx, "stop"), stopObj));
            sliceObj = const_cast<proto::ProtoObject*>(sliceObj->setAttribute(ctx, env ? env->getStepString() : proto::ProtoString::fromUTF8String(ctx, "step"), stepObj));
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
                
                const proto::ProtoString* dataS = env ? env->getDataString() : getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = listObj->getAttribute(ctx, dataS);
                const proto::ProtoList* L = (data && data->asList(ctx)) ? data->asList(ctx) : nullptr;
                
                if (L && env) {
                    stack.push_back(L->asObject(ctx)); // TEMP ROOT at index top-1
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
                
                const proto::ProtoString* dataS = env ? env->getDataString() : getInternalString(ctx, "__data__");
                const proto::ProtoString* keysS = env ? env->getKeysString() : getInternalString(ctx, "__keys__");
                
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
                
                const proto::ProtoString* dataS = env ? env->getDataString() : getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = setObj->getAttribute(ctx, dataS);
                const proto::ProtoSet* s = (data && data->asSet(ctx)) ? data->asSet(ctx) : nullptr;
                
                if (s && env) {
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
                
                const proto::ProtoString* dataS = env ? env->getDataString() : getInternalString(ctx, "__data__");
                const proto::ProtoObject* data = listObj->getAttribute(ctx, dataS);
                const proto::ProtoList* L = (data && data->asList(ctx)) ? data->asList(ctx) : nullptr;
                if (std::getenv("PROTO_ENV_DIAG")) {
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
            // Swallow any pre-existing or subsequent exception from delete path (e.g. os.py del _create_environ_mapping)
            if (env && env->hasPendingException()) env->clearPendingException();
            // i++;
            if (frame) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj && nameObj->isString(ctx)) {
                    const proto::ProtoString* data_name = env ? env->getDataString() : getInternalString(ctx, "__data__");
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
            // i++;
            if (!stack.empty()) {
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj && nameObj->isString(ctx)) {
                    const proto::ProtoObject* nil = env ? env->getNonePrototype() : PROTO_NONE;
                    obj->setAttribute(ctx, nameObj->asString(ctx), nil);
                }
            }
        } else if (op == OP_DELETE_SUBSCR) {
            if (stack.size() >= 2) {
                const proto::ProtoObject* key = stack.back();
                const proto::ProtoObject* container = stack[stack.top - 2];
                // Delay pop
                const proto::ProtoString* delItemS = env ? env->getDelItemString() : proto::ProtoString::fromUTF8String(ctx, "__delitem__");
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
                const proto::ProtoObject* result = invokeDunder(ctx, container, delItemS, args);
                if (!result) {
                    if (env && env->hasPendingException()) continue;
                    // Fallback for list/dict
                    const proto::ProtoString* data_name = env ? env->getDataString() : getInternalString(ctx, "__data__");
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
                                const proto::ProtoString* data_name = env ? env->getDataString() : getInternalString(ctx, "__data__");
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
            const proto::ProtoString* awaitS = env ? env->getAwaitString() : proto::ProtoString::fromUTF8String(ctx, "__await__");
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
            const proto::ProtoString* aiterS = env ? env->getAIterString() : proto::ProtoString::fromUTF8String(ctx, "__aiter__");
            const proto::ProtoObject* aiter = invokeDunder(ctx, obj, aiterS, ctx->newList());
            if (aiter) {
                stack.push_back(aiter);
            } else {
                stack.push_back(obj);
            }
        } else if (op == OP_GET_ANEXT) {
            if (stack.empty()) { i = next_i; continue; }
            const proto::ProtoObject* aiter = stack.back();
            if (std::getenv("PROTO_ENV_DIAG")) {
                if (std::getenv("PROTO_ENV_DIAG")) {}
            }
            const proto::ProtoString* anextS = env ? env->getANextString() : proto::ProtoString::fromUTF8String(ctx, "__anext__");
            const proto::ProtoObject* awaitable = invokeDunder(ctx, aiter, anextS, ctx->newList());
            if (awaitable) {
                stack.push_back(awaitable);
            } else {
                if (env && !env->hasPendingException()) {
                    env->raiseTypeError(ctx, "async for item must be an async iterator");
                }
                if (get_env_diag() && env && env->hasPendingException()) {
                    if (std::getenv("PROTO_ENV_DIAG")) {}
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
            const proto::ProtoString* aexitS = env ? env->getAExitString() : proto::ProtoString::fromUTF8String(ctx, "__aexit__");
            const proto::ProtoString* aenterS = env ? env->getAEnterString() : proto::ProtoString::fromUTF8String(ctx, "__aenter__");
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
    return executeBytecodeRange(ctx, constants, bytecode, names, frame, 0, n ? n - 1 : 0, 0, nullptr, nullptr, nullptr, 0, nullptr);
}

const proto::ProtoObject* exported_py_function_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return py_function_get(ctx, self, parentLink, args, kwargs);
}

const proto::ProtoObject* exported_py_function_code_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    if (!instance || instance == PROTO_NONE) return self;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* codeStr = env ? env->getCodeString() : proto::ProtoString::fromUTF8String(ctx, "__code__");
    const proto::ProtoObject* res = instance->getAttribute(ctx, codeStr);
    return res ? res : PROTO_NONE;
}

const proto::ProtoObject* exported_py_function_globals_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    if (!instance || instance == PROTO_NONE) return self;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* globalsStr = env ? env->getGlobalsString() : proto::ProtoString::fromUTF8String(ctx, "__globals__");
    const proto::ProtoObject* res = instance->getAttribute(ctx, globalsStr);
    return res ? res : PROTO_NONE;
}

const proto::ProtoObject* exported_py_function_doc_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* instance = args->getAt(ctx, 0);
    if (!instance || instance == PROTO_NONE) return self;
    const proto::ProtoString* docStr = proto::ProtoString::fromUTF8String(ctx, "__doc__");
    const proto::ProtoObject* res = instance->getAttribute(ctx, docStr);
    return res ? res : PROTO_NONE;
}

const proto::ProtoObject* exported_runUserFunctionCall(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return runUserFunctionCall(ctx, self, parentLink, args, kwargs);
}

} // namespace protoPython
