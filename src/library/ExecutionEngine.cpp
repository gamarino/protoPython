#include <protoPython/ExecutionEngine.h>
#include <protoPython/Compiler.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/MemoryManager.hpp>
#include <protoCore.h>
#include <proto_internal.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace protoPython {
static bool get_env_diag();


namespace {
struct Block {
    unsigned long handlerPc;
    size_t stackDepth;
};

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
    const proto::ProtoObject* co_automatic_obj = codeObj->getAttribute(ctx, co_automatic_name);

    const proto::ProtoList* co_varnames = co_varnames_obj && co_varnames_obj->asList(ctx) ? co_varnames_obj->asList(ctx) : nullptr;
    int nparams_count = (co_nparams_obj && co_nparams_obj->isInteger(ctx)) ? static_cast<int>(co_nparams_obj->asLong(ctx)) : 0;
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

    // Bind parameters
    unsigned int nSlots = calleeCtx->getAutomaticLocalsCount();
    proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());

    if (nSlots > 0 && slots) {
        // 1. Positional arguments
        unsigned long nparams_to_bind = (co_varnames) ? co_varnames->getSize(calleeCtx) : 0;
        // Adjust nparams for VARARGS/VARKEYWORDS if they are included in varnames
        if (co_flags & CO_VARARGS) nparams_to_bind--;
        if (co_flags & CO_VARKEYWORDS) nparams_to_bind--;

        for (unsigned long i = 0; i < nparams_to_bind && i < argCount; ++i) {
            slots[i] = const_cast<proto::ProtoObject*>(args->getAt(calleeCtx, static_cast<int>(i)));
        }

        // 3. *args
        if (co_flags & CO_VARARGS) {
            int varargIdx = static_cast<int>(nparams_to_bind);
            const proto::ProtoList* starArgs = calleeCtx->newList();
            if (argCount > nparams_to_bind) {
                for (unsigned long i = nparams_to_bind; i < argCount; ++i) {
                    starArgs = starArgs->appendLast(calleeCtx, args->getAt(calleeCtx, static_cast<int>(i)));
                }
            }
            const proto::ProtoObject* tup = calleeCtx->newTupleFromList(starArgs)->asObject(calleeCtx);
            if (varargIdx < static_cast<int>(nSlots)) slots[varargIdx] = const_cast<proto::ProtoObject*>(tup);
        }

        // 4. **kwargs
        if (co_flags & CO_VARKEYWORDS) {
            int kwargIdx = (co_flags & CO_VARARGS) ? static_cast<int>(nparams_to_bind) + 1 : static_cast<int>(nparams_to_bind);
            proto::ProtoObject* kwDict = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
            if (env && env->getDictPrototype()) kwDict = const_cast<proto::ProtoObject*>(kwDict->addParent(calleeCtx, env->getDictPrototype()));
            
            const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(calleeCtx, "__data__");
            
            const proto::ProtoSparseList* data = calleeCtx->newSparseList();
            const proto::ProtoTuple* kwNames = env ? env->getCurrentKwNames() : nullptr;
            
            if (kwargs) {
                auto it = kwargs->getIterator(calleeCtx);
                while (it && it->hasNext(calleeCtx)) {
                    unsigned long key = it->nextKey(calleeCtx);
                    const proto::ProtoObject* val = it->nextValue(calleeCtx);
                    
                    // Only add if not already bound to a positional param
                    bool alreadyBound = false;
                    for (unsigned long i = 0; i < nparams_to_bind; ++i) {
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
            if (kwargIdx < static_cast<int>(nSlots)) slots[kwargIdx] = kwDict;
        }
    }

    // 5. Build Execution Frame (for locals()/sys._getframe)
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
    if (env) {
        frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, env->getFramePrototype()));
        const proto::ProtoObject* closure = self->getAttribute(calleeCtx, env->getClosureString());
        if (closure && closure != PROTO_NONE) {
            frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, closure));
        }
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFCodeString(), codeObj));
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFGlobalsString(), globalsObj));
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
        result = runCodeObject(calleeCtx, codeObj, frame);
    }
    promote(calleeCtx, result);
    return result;
}

static const proto::ProtoObject* runBoundMethodCall(proto::ProtoContext* ctx,
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
    if (!env) return self;

    if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-diag] py_function_get: binding " << self << " to " << instance << "\n";

    proto::ProtoObject* bound = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    // Set __self__, __func__, and __call__
    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(ctx, env->getSelfDunderString(), instance));
    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(ctx, env->getFuncDunderString(), self));
    bound = const_cast<proto::ProtoObject*>(bound->setAttribute(ctx, env->getCallString(),
        ctx->fromMethod(bound, runBoundMethodCall)));
    
    // Also set __class__ to something reasonable if possible, but for now just Return
    return bound;
}

/** Create a callable object with __code__, __globals__, and __call__. */
static proto::ProtoObject* createUserFunction(proto::ProtoContext* ctx, const proto::ProtoObject* codeObj, proto::ProtoObject* globalsFrame, const proto::ProtoObject* closureFrame = nullptr) {
    if (!ctx || !codeObj || !globalsFrame) return nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* fn = ctx->newObject(true);
    fn = fn->setAttribute(ctx, env ? env->getCodeString() : proto::ProtoString::fromUTF8String(ctx, "__code__"), codeObj);
    fn = fn->setAttribute(ctx, env ? env->getGlobalsString() : proto::ProtoString::fromUTF8String(ctx, "__globals__"), globalsFrame);
    if (closureFrame && env) {
        fn = fn->setAttribute(ctx, env->getClosureString(), closureFrame);
    }
    fn = fn->setAttribute(ctx, env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(fn), runUserFunctionCall));
    fn = fn->setAttribute(ctx, env ? env->getGetDunderString() : proto::ProtoString::fromUTF8String(ctx, "__get__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(fn), py_function_get));
    return const_cast<proto::ProtoObject*>(fn);
}

static const proto::ProtoObject* runUserClassCall(proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs) {
    if (!ctx || !self) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    
    // Create new object instance
    proto::ProtoObject* obj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    
    // Class acts as prototype/parent
    obj = const_cast<proto::ProtoObject*>(obj->addParent(ctx, self));
    
    // Step V74: Explicitly set __class__ to the class itself
    const proto::ProtoString* classS = env ? env->getClassString() : proto::ProtoString::fromUTF8String(ctx, "__class__");
    obj = const_cast<proto::ProtoObject*>(obj->setAttribute(ctx, classS, self));
    
    // Invoke __init__ if present
    const proto::ProtoString* initS = env ? env->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__");
    
    // Step V75: Use env->getAttribute to get a BOUND method, ensuring 'self' is passed!
    const proto::ProtoObject* initM = env ? env->getAttribute(ctx, obj, initS) : obj->getAttribute(ctx, initS);
    if (initM && initM != PROTO_NONE) {
        if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-diag] runUserClassCall: calling __init__ for " << obj << "\n";
        invokeCallable(ctx, initM, args, kwargs);
    } else {
        if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-diag] runUserClassCall: __init__ not found for " << obj << "\n";
    }
    
    return obj;
}


/** Return true if obj is an embedded value (e.g. small int, bool); do not call getAttribute on it. */
static bool isEmbeddedValue(const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    return (reinterpret_cast<uintptr_t>(obj) & 0x3FUL) == POINTER_TAG_EMBEDDED_VALUE;
}

static const proto::ProtoObject* binaryAdd(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return ctx->fromInteger(a->asLong(ctx) + b->asLong(ctx));
    if (a->isDouble(ctx) || b->isDouble(ctx)) {
        if ((a->isDouble(ctx) || a->isInteger(ctx)) && (b->isDouble(ctx) || b->isInteger(ctx)))
             return ctx->fromDouble(a->asDouble(ctx) + b->asDouble(ctx));
    }
    if (a->isString(ctx) && b->isString(ctx)) {
        std::string s1, s2;
        a->asString(ctx)->toUTF8String(ctx, s1);
        b->asString(ctx)->toUTF8String(ctx, s2);
        return ctx->fromUTF8String((s1 + s2).c_str());
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binarySubtract(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return ctx->fromInteger(a->asLong(ctx) - b->asLong(ctx));
    if (a->isDouble(ctx) || b->isDouble(ctx)) {
        if ((a->isDouble(ctx) || a->isInteger(ctx)) && (b->isDouble(ctx) || b->isInteger(ctx)))
            return ctx->fromDouble(a->asDouble(ctx) - b->asDouble(ctx));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryMultiply(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* r = a->multiply(ctx, b);
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryUnaryNegative(proto::ProtoContext* ctx, const proto::ProtoObject* a) {
    if (a->isInteger(ctx)) return ctx->fromInteger(-a->asLong(ctx));
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
    if (isEmbeddedValue(a) || isEmbeddedValue(b)) {
        double aa = a->isDouble(ctx) ? a->asDouble(ctx) : static_cast<double>(a->asLong(ctx));
        double bb = b->isDouble(ctx) ? b->asDouble(ctx) : static_cast<double>(b->asLong(ctx));
        return ctx->fromDouble(aa / bb);
    }
    const proto::ProtoObject* r = a->divide(ctx, b);
    return r ? r : PROTO_NONE;
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
    if (a->isInteger(ctx) && b->isInteger(ctx)) {
        return ctx->fromInteger(a->asLong(ctx) % b->asLong(ctx));
    }
    if (a->isDouble(ctx) || b->isDouble(ctx)) {
        double aa = a->isDouble(ctx) ? a->asDouble(ctx) : static_cast<double>(a->asLong(ctx));
        double bb = b->isDouble(ctx) ? b->asDouble(ctx) : static_cast<double>(b->asLong(ctx));
        return ctx->fromDouble(std::fmod(aa, bb));
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
                const proto::ProtoString* strS = env ? env->getStrString() : proto::ProtoString::fromUTF8String(ctx, "__str__");
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
    const proto::ProtoObject* r = a->modulo(ctx, b);
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryPower(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
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

static const proto::ProtoObject* binaryFloorDivide(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) || a->isDouble(ctx)) {
        if ((b->isInteger(ctx) && b->asLong(ctx) == 0) || (b->isDouble(ctx) && b->asDouble(ctx) == 0.0)) {
            PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
            return PROTO_NONE;
        }
    }
    if (a->isInteger(ctx) && b->isInteger(ctx)) {
        long long aa = a->asLong(ctx);
        long long bb = b->asLong(ctx);
        return ctx->fromInteger(aa / bb);
    }
    double aa = a->isDouble(ctx) ? a->asDouble(ctx) : static_cast<double>(a->asLong(ctx));
    double bb = b->isDouble(ctx) ? b->asDouble(ctx) : static_cast<double>(b->asLong(ctx));
    return ctx->fromInteger(static_cast<long long>(std::floor(aa / bb)));
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
        if (!lst) {
            // Try dictionary keys
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoString* keys_name = env ? env->getKeysString() : proto::ProtoString::fromUTF8String(ctx, "__keys__");
            const proto::ProtoObject* keysObj = b->getAttribute(ctx, keys_name);
            if (keysObj) lst = keysObj->asList(ctx);
        }
        
        if (lst) {
            for (size_t i = 0; i < lst->getSize(ctx); ++i) {
                if (a->compare(ctx, lst->getAt(ctx, i)) == 0) {
                    found = true;
                    break;
                }
            }
        } else if (b->isString(ctx) && a->isString(ctx)) {
            std::string s_sub, s_full;
            a->asString(ctx)->toUTF8String(ctx, s_sub);
            b->asString(ctx)->toUTF8String(ctx, s_full);
            found = (s_full.find(s_sub) != std::string::npos);
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
    bool res = false;
    if (!obj || obj == PROTO_NONE) res = false;
    else if (obj == PROTO_FALSE) res = false;
    else if (obj == PROTO_TRUE) res = true;
    else {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env && obj == env->getNonePrototype()) res = false;
        else if (obj->isInteger(ctx)) res = (obj->asLong(ctx) != 0);
        else if (obj->isDouble(ctx)) res = (obj->asDouble(ctx) != 0.0);
        else if (obj->isString(ctx)) res = (obj->asString(ctx)->getSize(ctx) > 0);
        else res = true;
    }
    
    return res;
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
    if (!callable) {
        if (env) env->raiseTypeError(ctx, "object is not callable (nullptr)");
        return nullptr;
    }

    RecursionScope recScope(env, ctx);
    if (recScope.overflowed()) return nullptr;

    if (callable->isMethod(ctx)) {
        const auto* cell = proto::toImpl<const proto::ProtoMethodCell>(callable);
        return cell->method(ctx, const_cast<proto::ProtoObject*>(cell->self), nullptr, args, kwargs);
    }
    const proto::ProtoObject* callAttr = callable->getAttribute(ctx, env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__"));
    if (!callAttr || !callAttr->asMethod(ctx)) {
        if (env) env->raiseTypeError(ctx, "object is not callable");
        return nullptr; // Return nullptr on error
    }
    const proto::ProtoObject* result = callAttr->asMethod(ctx)(ctx, callable, nullptr, args, kwargs);
    return result;
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

    if (!codeObj || !frame || !pcObj || !stackObj) return PROTO_NONE;

    unsigned long pc = (pcObj && pcObj->isInteger(ctx)) ? static_cast<unsigned long>(pcObj->asLong(ctx)) : 0;
    const proto::ProtoList* co_code_list = codeObj->getAttribute(ctx, env->getCoCodeString())->asList(ctx);
    if (!co_code_list) return PROTO_NONE;
    
    if (pc >= co_code_list->getSize(ctx)) {
        env->raiseStopIteration(ctx, PROTO_NONE);
        return PROTO_NONE;
    }

    // 3. Restore stack
    std::vector<const proto::ProtoObject*> stack;
    const proto::ProtoList* slist = stackObj ? stackObj->asList(ctx) : nullptr;
    
    if (slist) {
        unsigned long size = slist->getSize(ctx);
        for (unsigned long i = 0; i < size; ++i) {
             const proto::ProtoObject* item = slist->getAt(ctx, static_cast<int>(i));
             stack.push_back(item);
        }
    }

    // 4. If sendVal is provided, push it (unless it's the very first call and None)
    if (pc > 0) {
        stack.push_back(sendVal);
    } else if (sendVal != PROTO_NONE) {
        env->raiseTypeError(ctx, "can't send non-None value to a just-started generator");
        return PROTO_NONE;
    }

    // 5. Run
    self->setAttribute(ctx, env->getGiRunningString(), PROTO_TRUE);
    
    if (throwExc) {
        env->setPendingException(throwExc);
    }
    
    unsigned long nextPc = pc; // Default to current if we exception immediately
    bool yielded = false;
    
    const proto::ProtoObject* result = nullptr;
    {
        const proto::ProtoObject* co_varnames_obj = codeObj->getAttribute(ctx, env->getCoVarnamesString());
        const proto::ProtoList* co_varnames = co_varnames_obj ? co_varnames_obj->asList(ctx) : nullptr;
        const proto::ProtoObject* co_automatic_obj = codeObj->getAttribute(ctx, env->getCoAutomaticCountString());
        int automatic_count = co_automatic_obj ? static_cast<int>(co_automatic_obj->asLong(ctx)) : 0;
        
        const proto::ProtoList* localNames = ctx->newList();
        if (co_varnames) {
            unsigned long vSize = co_varnames->getSize(ctx);
            for (int i = 0; i < automatic_count; ++i) {
                const proto::ProtoObject* name = (i < static_cast<int>(vSize)) ? co_varnames->getAt(ctx, i) : PROTO_NONE;
                localNames = localNames->appendLast(ctx, name);
            }
        }
        
        ContextScope scope(ctx->space, ctx, nullptr, localNames, nullptr, nullptr);
        proto::ProtoContext* calleeCtx = scope.context();
        
        // Restore locals from gi_locals
        const proto::ProtoObject* localsObj = self->getAttribute(ctx, env->getGiLocalsString());
        const proto::ProtoList* savedLocals = localsObj ? localsObj->asList(ctx) : nullptr;

        if (savedLocals) {
            proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());
            for (unsigned int i = 0; i < calleeCtx->getAutomaticLocalsCount() && i < savedLocals->getSize(ctx); ++i) {
                slots[i] = const_cast<proto::ProtoObject*>(savedLocals->getAt(ctx, i));
            }
        }
        
        const proto::ProtoObject* globals = frame->getAttribute(calleeCtx, env->getFGlobalsString());
        if (!globals) globals = env->getGlobals();
        GlobalsScope gscope(globals);
        
        const proto::ProtoList* co_consts = codeObj->getAttribute(calleeCtx, env->getCoConstsString())->asList(calleeCtx);
        const proto::ProtoList* co_names = codeObj->getAttribute(calleeCtx, env->getCoNamesString())->asList(calleeCtx);
        
        unsigned long stackOffset = co_varnames ? co_varnames->getSize(calleeCtx) : 0;
        result = executeBytecodeRange(calleeCtx, 
            co_consts,
            co_code_list,
            co_names,
            frame,
            pc,
            co_code_list->getSize(calleeCtx),
            stackOffset,
            &stack,
            &nextPc,
            &yielded);
            
        const proto::ProtoList* newLocals = calleeCtx->newList();
        const proto::ProtoObject** updatedSlots = calleeCtx->getAutomaticLocals();
        for (unsigned int i = 0; i < calleeCtx->getAutomaticLocalsCount(); ++i) {
            newLocals = newLocals->appendLast(calleeCtx, updatedSlots[i]);
        }
        self->setAttribute(calleeCtx, env->getGiLocalsString(), newLocals->asObject(calleeCtx));
    }

    // 8. Clear running (already done above but let's be explicit)
    self->setAttribute(ctx, env->getGiRunningString(), PROTO_FALSE);
    self->setAttribute(ctx, env->getGiPCString(), ctx->fromInteger(nextPc));

    // 6. Save stack back
    const proto::ProtoList* newStack = ctx->newList();
    for (const auto* obj : stack)
        newStack = newStack->appendLast(ctx, obj);
    self->setAttribute(ctx, env->getGiStackString(), newStack->asObject(ctx));

    if (!yielded && !env->hasPendingException()) {
        env->raiseStopIteration(ctx, result);
        return PROTO_NONE;
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
        unsigned long pc = static_cast<unsigned long>(pcObj->asLong(ctx));
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

static bool get_env_diag() {
    static bool diag = std::getenv("PROTO_ENV_DIAG") != nullptr;
    return diag;
}

static void checkSTW(proto::ProtoContext* ctx) {
    if (ctx && ctx->space && ctx->space->stwFlag.load()) {
        if (ctx->space->gcThread && std::this_thread::get_id() == ctx->space->gcThread->get_id()) return;
        ctx->space->parkedThreads++;
        {
            std::unique_lock<std::recursive_mutex> lock(proto::ProtoSpace::globalMutex);
            ctx->space->gcCV.notify_all();
            ctx->space->stopTheWorldCV.wait(lock, [ctx] { return !ctx->space->stwFlag.load(); });
        }
        ctx->space->parkedThreads--;
    }
}

static const proto::ProtoObject* invokeDunder(proto::ProtoContext* ctx, const proto::ProtoObject* container, const proto::ProtoString* name, const proto::ProtoList* args) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* method = env ? env->getAttribute(ctx, container, name) : container->getAttribute(ctx, name);
    if (!method || method == PROTO_NONE) return nullptr;

    if (get_env_diag()) {
        std::string* nPtr = new std::string();
        name->toUTF8String(ctx, *nPtr);
        // log removed
        delete nPtr;
    }

    return invokeCallable(ctx, method, args);
}

namespace {
struct GCStack {
    proto::ProtoObject** slots;
    size_t top;
    size_t capacity;

    GCStack(proto::ProtoObject** s, size_t cap) : slots(s), top(0), capacity(cap) {}

    void push_back(const proto::ProtoObject* obj) {
        if (top >= capacity) {
            // log removed
            throw std::runtime_error("Python stack overflow");
        }
        slots[top++] = const_cast<proto::ProtoObject*>(obj);
    }
    void pop_back() { if (top > 0) top--; }
    proto::ProtoObject*& back() { 
        if (top == 0) throw std::runtime_error("Python stack underflow");
        return slots[top - 1]; 
    }
    const proto::ProtoObject* back() const { return top > 0 ? slots[top - 1] : nullptr; }
    size_t size() const { return top; }
    bool empty() const { return top == 0; }
    proto::ProtoObject*& operator[](size_t i) { return slots[i]; }
    const proto::ProtoObject* operator[](size_t i) const { return slots[i]; }
    const proto::ProtoObject** data() const { return const_cast<const proto::ProtoObject**>(slots); }
    void reserve(size_t) {}
};
}

const proto::ProtoObject* executeBytecodeRange(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode,
    const proto::ProtoList* names,
    proto::ProtoObject*& frame,
    unsigned long pcStart,
    unsigned long pcEnd,
    unsigned long stackOffset,
    std::vector<const proto::ProtoObject*>* externalStack,
    unsigned long* outPc,
    bool* yielded) {
    if (!ctx || !constants || !bytecode) return nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env && std::getenv("PROTO_THREAD_DIAG")) {
        // log removed
    }
    
    FrameScope fscope(frame);
    unsigned long n = bytecode->getSize(ctx);
    if (n == 0) return nullptr;
    if (pcEnd >= n) pcEnd = n - 1;
    if (std::getenv("PROTO_ENV_DIAG")) {
        std::cerr << "[proto-diag] executeBytecodeRange: pcStart=" << pcStart << " pcEnd=" << pcEnd << " bytecodeSize=" << n << "\n" << std::flush;
    }

    unsigned int nSlots = ctx->getAutomaticLocalsCount();
    proto::ProtoObject** allSlots = const_cast<proto::ProtoObject**>(ctx->getAutomaticLocals());
    
    // Fallback stack for contexts without pre-allocated slots (e.g. some unit tests)
    std::vector<const proto::ProtoObject*> fallbackStack;
    proto::ProtoObject** stackBase = nullptr;
    size_t stackCap = 0;
    
    if (allSlots && nSlots > stackOffset) {
        stackBase = allSlots + stackOffset;
        stackCap = nSlots - stackOffset;
    } else {
        fallbackStack.resize(1024); // Default capacity for manual/test execution
        stackBase = const_cast<proto::ProtoObject**>(fallbackStack.data());
        stackCap = fallbackStack.size();
    }
    
    GCStack stack(stackBase, stackCap);

    if (externalStack) {
        for (const auto* obj : *externalStack) {
            stack.push_back(obj);
        }
    }
    
    std::vector<Block> blockStack;
    const bool sync_globals = (frame == PythonEnvironment::getCurrentGlobals());
    for (unsigned long i = pcStart; i <= pcEnd; i += 2) {
        if (env && env->hasPendingException()) {
            if (!blockStack.empty()) {
                Block b = blockStack.back();
                blockStack.pop_back();
                while (stack.size() > b.stackDepth) stack.pop_back();
                // Push the exception object to stack for handlers to inspect
                const proto::ProtoObject* exc = env->peekPendingException();
                if (exc) stack.push_back(exc);
                env->clearPendingException();
                i = b.handlerPc - 2; // -2 because loop will i += 2
                continue;
            }
            return nullptr;
        }
        if ((i & 0x7FF) == 0) checkSTW(ctx);
        const proto::ProtoObject* instr = bytecode->getAt(ctx, static_cast<int>(i));
        if (!instr || !instr->isInteger(ctx)) {
            continue;
        }
        int op = static_cast<int>(instr->asLong(ctx));
        int arg = (i + 1 < n && bytecode->getAt(ctx, static_cast<int>(i + 1))->isInteger(ctx))
            ? static_cast<int>(bytecode->getAt(ctx, static_cast<int>(i + 1))->asLong(ctx)) : 0;
        if (std::getenv("PROTO_ENV_DIAG")) {
            std::cerr << "[proto-diag] execute: i=" << i << " op=" << op << " arg=" << arg << "\n" << std::flush;
        }


        if (op == OP_LOAD_CONST) {
            if (static_cast<unsigned long>(arg) < constants->getSize(ctx)) {
                stack.push_back(constants->getAt(ctx, arg));
            }
        } else if (op == OP_RETURN_VALUE) {
            if (stack.empty()) return PROTO_NONE;
            const proto::ProtoObject* ret = stack.back();
            ctx->returnValue = ret;
            if (outPc) *outPc = pcEnd + 1; // Mark finished
            return ret;  /* exit block immediately; destructor will promote */
        } else if (op == OP_YIELD_VALUE) {
            if (stack.empty()) return PROTO_NONE;
            const proto::ProtoObject* ret = stack.back();
            stack.pop_back();
            ctx->returnValue = ret;
            if (yielded) *yielded = true;
            if (outPc) *outPc = i + 1; // Resume at NEXT instruction
            if (externalStack) {
                externalStack->clear();
                for (size_t j = 0; j < stack.size(); ++j) externalStack->push_back(stack[j]);
            }
            return ret;
        } else if (op == OP_GET_YIELD_FROM_ITER) {
            if (stack.empty()) continue;
            const proto::ProtoObject* obj = stack.back();
            stack.pop_back();
            const proto::ProtoObject* iterator = nullptr;
            const proto::ProtoObject* iterMethod = env ? env->getAttribute(ctx, obj, env->getIterString()) : obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"));
            if (iterMethod && iterMethod != PROTO_NONE) {
                iterator = invokePythonCallable(ctx, iterMethod, ctx->newList(), nullptr);
            } else {
                iterator = obj;
            }
            stack.push_back(iterator);
        } else if (op == OP_YIELD_FROM) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* sendVal = stack.back();
            stack.pop_back();
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
                const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(ctx, "__next__");
                result = subIter->call(ctx, nullptr, nextS, subIter, ctx->newList(), nullptr);
            }

            if (env && env->hasPendingException()) {
                const proto::ProtoObject* exc = env->peekPendingException();
                if (get_env_diag()) {
                }
                if (env->isStopIteration(ctx, exc)) {
                    const proto::ProtoObject* stopVal = env->getStopIterationValue(ctx, exc);
                    if (get_env_diag()) {
                        proto::ProtoObjectPointer p; p.oid = stopVal;
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
                if (outPc) *outPc = i - 1; // RESTAY at current instruction (the OP_YIELD_FROM itself)
                if (externalStack) {
                    externalStack->clear();
                    for (size_t j = 0; j < stack.size(); ++j) externalStack->push_back(stack[j]);
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
                        std::cerr << "[proto-diag] OP_LOAD_NAME: name='" << nStr << "'\n" << std::flush;
                    }
                    const proto::ProtoObject* val = nullptr;
                    bool found = false;
                    if (frame->hasAttribute(ctx, nameS) == PROTO_TRUE) {
                        val = frame->getAttribute(ctx, nameS);
                        found = true;
                    }
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::cerr << "[proto-diag] OP_LOAD_NAME: val=" << val << " found=" << (found ? "1" : "0") << "\n" << std::flush;
                    }

                    if (found) {
                        stack.push_back(val);
                    } else if (env) {
                        const proto::ProtoObject* r = env->resolve(nameS, ctx);
                        if (r != nullptr || (nStr == "None" && r == PROTO_NONE)) { // Special case for None literal if needed, but resolve should return it
                            if (r == nullptr && nStr == "None") r = PROTO_NONE; 
                            if (r) {
                                stack.push_back(r);
                            } else {
                                env->raiseNameError(ctx, nStr);
                                return nullptr;
                            }
                        } else {
                            env->raiseNameError(ctx, nStr);
                            return nullptr;
                        }
                    } else {
                        std::cerr << "Engine Error: env is NULL in OP_LOAD_NAME for '" << nStr << "'\n";
                        return nullptr;
                    }
                } else {
                    stack.push_back(PROTO_NONE);
                }
            }
        } else if (op == OP_STORE_NAME) {
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                if (stack.empty()) continue;
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (nameObj->isString(ctx)) {
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::string n;
                        nameObj->asString(ctx)->toUTF8String(ctx, n);
                        std::cerr << "[proto-diag] OP_STORE_NAME: name='" << n << "' val=" << val << "\n" << std::flush;
                    }
                    // Update frame (CoW support)
                    const proto::ProtoObject* newFrame = frame->setAttribute(ctx, nameObj->asString(ctx), val);
                    frame = const_cast<proto::ProtoObject*>(newFrame);
                    
                    if (env) {
                        PythonEnvironment::setCurrentFrame(frame);
                        if (sync_globals) PythonEnvironment::setCurrentGlobals(frame);
                        env->invalidateResolveCache();
                    }
                }
            }
        } else if (op == OP_LOAD_FAST) {
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<unsigned long>(arg) < nSlots) {
                const proto::ProtoObject** slots = ctx->getAutomaticLocals();
                const proto::ProtoObject* val = slots[arg];
                stack.push_back(val ? val : PROTO_NONE);
            } else {
                if (get_env_diag()) {
                    // LOAD_FAST range diagnostic removed
                }
                stack.push_back(PROTO_NONE);
            }
        } else if (op == OP_STORE_FAST) {
            if (stack.empty()) continue;
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<unsigned long>(arg) < nSlots) {
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(ctx->getAutomaticLocals());
                slots[arg] = const_cast<proto::ProtoObject*>(val);
            }
        } else if (op == OP_BINARY_ADD) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryAdd(ctx, a, b);
            stack.push_back(r);
        } else if (op == OP_INPLACE_ADD) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* iadd = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getIAddString() : proto::ProtoString::fromUTF8String(ctx, "__iadd__"));
            if (iadd && iadd->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = iadd->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binaryAdd(ctx, a, b);
                stack.push_back(r);
            }
        } else if (op == OP_BINARY_SUBTRACT) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binarySubtract(ctx, a, b);
            stack.push_back(r);
        } else if (op == OP_INPLACE_SUBTRACT) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* isub = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getISubString() : proto::ProtoString::fromUTF8String(ctx, "__isub__"));
            if (isub && isub->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = isub->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binarySubtract(ctx, a, b);
                stack.push_back(r);
            }
        } else if (op == OP_BINARY_MULTIPLY) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryMultiply(ctx, a, b);
            stack.push_back(r);
        } else if (op == OP_INPLACE_MULTIPLY) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* imul = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getIMulString() : proto::ProtoString::fromUTF8String(ctx, "__imul__"));
            if (imul && imul->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = imul->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binaryMultiply(ctx, a, b);
                stack.push_back(r);
            }
        } else if (op == OP_BINARY_TRUE_DIVIDE) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryTrueDivide(ctx, a, b);
            stack.push_back(r);
        } else if (op == OP_BINARY_MODULO) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryModulo(ctx, a, b);
            stack.push_back(r);
        } else if (op == OP_BINARY_POWER) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryPower(ctx, a, b);
            stack.push_back(r);
        } else if (op == OP_BINARY_FLOOR_DIVIDE) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryFloorDivide(ctx, a, b);
            stack.push_back(r);
        } else if (op == OP_INPLACE_TRUE_DIVIDE) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* itruediv = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getITrueDivString() : proto::ProtoString::fromUTF8String(ctx, "__itruediv__"));
            if (itruediv && itruediv->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = itruediv->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binaryTrueDivide(ctx, a, b);
                stack.push_back(r);
            }
        } else if (op == OP_INPLACE_FLOOR_DIVIDE) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* ifloordiv = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getIFloorDivString() : proto::ProtoString::fromUTF8String(ctx, "__ifloordiv__"));
            if (ifloordiv && ifloordiv->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ifloordiv->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binaryFloorDivide(ctx, a, b);
                stack.push_back(r);
            }
        } else if (op == OP_INPLACE_MODULO) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* imod = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getIModString() : proto::ProtoString::fromUTF8String(ctx, "__imod__"));
            if (imod && imod->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = imod->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binaryModulo(ctx, a, b);
                stack.push_back(r);
            }
        } else if (op == OP_INPLACE_POWER) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* ipow = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getIPowString() : proto::ProtoString::fromUTF8String(ctx, "__ipow__"));
            if (ipow && ipow->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ipow->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* r = binaryPower(ctx, a, b);
                stack.push_back(r);
            }
        } else if (op == OP_INPLACE_LSHIFT) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* ilshift = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getILShiftString() : proto::ProtoString::fromUTF8String(ctx, "__ilshift__"));
            if (ilshift && ilshift->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ilshift->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = static_cast<long long>(static_cast<unsigned long long>(av) << bv);
                stack.push_back(ctx->fromInteger(av));
            }
        } else if (op == OP_INPLACE_RSHIFT) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* irshift = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getIRShiftString() : proto::ProtoString::fromUTF8String(ctx, "__irshift__"));
            if (irshift && irshift->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = irshift->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = av >> bv;
                stack.push_back(ctx->fromInteger(av));
            }
        } else if (op == OP_INPLACE_AND) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* iand = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getIAndString() : proto::ProtoString::fromUTF8String(ctx, "__iand__"));
            if (iand && iand->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = iand->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                stack.push_back(ctx->fromInteger(a->asLong(ctx) & b->asLong(ctx)));
            } else {
                const proto::ProtoObject* andM = a->getAttribute(ctx, env ? env->getAndString() : proto::ProtoString::fromUTF8String(ctx, "__and__"));
                if (andM && andM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = andM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    if (result) stack.push_back(result);
                } else {
                    const proto::ProtoObject* randM = b->getAttribute(ctx, env ? env->getRAndString() : proto::ProtoString::fromUTF8String(ctx, "__rand__"));
                    if (randM && randM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = randM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        if (result) stack.push_back(result);
                    }
                }
            }
        } else if (op == OP_INPLACE_OR) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* ior = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getIOrString() : proto::ProtoString::fromUTF8String(ctx, "__ior__"));
            if (ior && ior->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ior->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                stack.push_back(ctx->fromInteger(a->asLong(ctx) | b->asLong(ctx)));
            } else {
                const proto::ProtoObject* orM = a->getAttribute(ctx, env ? env->getOrString() : proto::ProtoString::fromUTF8String(ctx, "__or__"));
                if (orM && orM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = orM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    if (result) stack.push_back(result);
                } else {
                    const proto::ProtoObject* rorM = b->getAttribute(ctx, env ? env->getROrString() : proto::ProtoString::fromUTF8String(ctx, "__ror__"));
                    if (rorM && rorM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = rorM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        if (result) stack.push_back(result);
                    }
                }
            }
        } else if (op == OP_INPLACE_XOR) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* ixor = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getIXorString() : proto::ProtoString::fromUTF8String(ctx, "__ixor__"));
            if (ixor && ixor->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                const proto::ProtoObject* result = ixor->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else if (a->isInteger(ctx) && b->isInteger(ctx)) {
                stack.push_back(ctx->fromInteger(a->asLong(ctx) ^ b->asLong(ctx)));
            } else {
                const proto::ProtoObject* xorM = a->getAttribute(ctx, env ? env->getXorString() : proto::ProtoString::fromUTF8String(ctx, "__xor__"));
                if (xorM && xorM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = xorM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    if (result) stack.push_back(result);
                } else {
                    const proto::ProtoObject* rxorM = b->getAttribute(ctx, env ? env->getRXorString() : proto::ProtoString::fromUTF8String(ctx, "__rxor__"));
                    if (rxorM && rxorM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = rxorM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        if (result) stack.push_back(result);
                    }
                }
            }
        } else if (op == OP_BINARY_LSHIFT) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = static_cast<long long>(static_cast<unsigned long long>(av) << bv);
                stack.push_back(ctx->fromInteger(av));
            }
        } else if (op == OP_BINARY_RSHIFT) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                if (bv < 0 || bv >= 64) av = 0;
                else av = av >> bv;
                stack.push_back(ctx->fromInteger(av));
            }
        } else if (op == OP_BINARY_AND) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                stack.push_back(ctx->fromInteger(av & bv));
            } else {
                const proto::ProtoObject* andM = a->getAttribute(ctx, env ? env->getAndString() : proto::ProtoString::fromUTF8String(ctx, "__and__"));
                if (andM && andM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = andM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    if (result) stack.push_back(result);
                } else {
                    const proto::ProtoObject* randM = b->getAttribute(ctx, env ? env->getRAndString() : proto::ProtoString::fromUTF8String(ctx, "__rand__"));
                    if (randM && randM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = randM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        if (result) stack.push_back(result);
                    }
                }
            }
        } else if (op == OP_BINARY_OR) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                stack.push_back(ctx->fromInteger(av | bv));
            } else {
                const proto::ProtoObject* orM = a->getAttribute(ctx, env ? env->getOrString() : proto::ProtoString::fromUTF8String(ctx, "__or__"));
                if (orM && orM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = orM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    if (result) stack.push_back(result);
                } else {
                    const proto::ProtoObject* rorM = b->getAttribute(ctx, env ? env->getROrString() : proto::ProtoString::fromUTF8String(ctx, "__ror__"));
                    if (rorM && rorM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = rorM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        if (result) stack.push_back(result);
                    }
                }
            }
        } else if (op == OP_BINARY_XOR) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx) && b->isInteger(ctx)) {
                long long av = a->asLong(ctx);
                long long bv = b->asLong(ctx);
                stack.push_back(ctx->fromInteger(av ^ bv));
            } else {
                const proto::ProtoObject* xorM = a->getAttribute(ctx, env ? env->getXorString() : proto::ProtoString::fromUTF8String(ctx, "__xor__"));
                if (xorM && xorM->asMethod(ctx)) {
                    const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, b);
                    const proto::ProtoObject* result = xorM->asMethod(ctx)(ctx, a, nullptr, oneArg, nullptr);
                    if (result) stack.push_back(result);
                } else {
                    const proto::ProtoObject* rxorM = b->getAttribute(ctx, env ? env->getRXorString() : proto::ProtoString::fromUTF8String(ctx, "__rxor__"));
                    if (rxorM && rxorM->asMethod(ctx)) {
                        const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, a);
                        const proto::ProtoObject* result = rxorM->asMethod(ctx)(ctx, b, nullptr, oneArg, nullptr);
                        if (result) stack.push_back(result);
                    }
                }
            }
        } else if (op == OP_UNARY_NEGATIVE) {
            if (stack.empty()) continue;
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx))
                stack.push_back(ctx->fromInteger(-a->asLong(ctx)));
            else if (a->isDouble(ctx))
                stack.push_back(ctx->fromDouble(-a->asDouble(ctx)));
        } else if (op == OP_UNARY_NOT) {
            if (stack.empty()) continue;
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            stack.push_back(isTruthy(ctx, a) ? PROTO_FALSE : PROTO_TRUE);
        } else if (op == OP_UNARY_INVERT) {
            if (stack.empty()) continue;
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            if (a->isInteger(ctx)) {
                long long n = a->asLong(ctx);
                stack.push_back(ctx->fromInteger(static_cast<long long>(~static_cast<unsigned long long>(n))));
            } else {
                const proto::ProtoObject* inv = a->getAttribute(ctx, env ? env->getInvertString() : proto::ProtoString::fromUTF8String(ctx, "__invert__"));
                if (inv && inv->asMethod(ctx)) {
                    const proto::ProtoList* noArgs = ctx->newList();
                    const proto::ProtoObject* result = inv->asMethod(ctx)(ctx, a, nullptr, noArgs, nullptr);
                    if (result) stack.push_back(result);
                }
            }
        } else if (op == OP_RAISE_VARARGS) {
            const proto::ProtoObject* exc = nullptr;
            if (arg == 1) {
                if (!stack.empty()) {
                    exc = stack.back();
                    stack.pop_back();
                }
            } else if (arg == 0) {
                // Re-raise: if there's an exception on the stack (TOS), use it.
                // This is common in 'with' cleanup handlers.
                if (!stack.empty()) exc = stack.back();
            }
            if (env) {
                if (exc) env->setPendingException(exc);
            }
            continue;
        } else if (op == OP_SETUP_WITH) {
            if (stack.size() < 1) continue;
            const proto::ProtoObject* manager = stack.back();
            stack.pop_back();
            
            const proto::ProtoString* enterS = env ? env->getEnterString() : proto::ProtoString::fromUTF8String(ctx, "__enter__");
            const proto::ProtoString* exitS = env ? env->getExitString() : proto::ProtoString::fromUTF8String(ctx, "__exit__");
            
            const proto::ProtoObject* exitM = env ? env->getAttribute(ctx, manager, exitS) : manager->getAttribute(ctx, exitS);
            stack.push_back(exitM ? exitM : (const proto::ProtoObject*)PROTO_NONE);
            
            const proto::ProtoObject* enterResult = PROTO_NONE;
            const proto::ProtoObject* enterM = env ? env->getAttribute(ctx, manager, enterS) : manager->getAttribute(ctx, enterS);
            if (enterM && enterM != PROTO_NONE) {
                const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
                enterResult = invokeCallable(ctx, enterM, emptyL);
            } else {
                enterResult = manager;
            }
            
            // Push block pointing to handler at arg (absolute PC)
            blockStack.push_back({static_cast<unsigned long>(arg), stack.size()});
            
            stack.push_back(enterResult);
        } else if (op == OP_WITH_CLEANUP) {
            // Stack: [..., __exit__, (None or Exc)]
            if (stack.size() < 2) continue;
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
            if (stack.empty()) continue;
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* pos = isEmbeddedValue(a) ? nullptr : a->getAttribute(ctx, env ? env->getPosString() : proto::ProtoString::fromUTF8String(ctx, "__pos__"));
            if (pos && pos->asMethod(ctx)) {
                const proto::ProtoList* noArgs = ctx->newList();
                const proto::ProtoObject* result = pos->asMethod(ctx)(ctx, a, nullptr, noArgs, nullptr);
                if (result) stack.push_back(result);
            } else {
                stack.push_back(a);
            }
        } else if (op == OP_NOP) {
        } else if (op == OP_COMPARE_OP) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = compareOp(ctx, a, b, arg);
            if (r) stack.push_back(r);
        } else if (op == OP_POP_JUMP_IF_FALSE) {
            if (stack.empty()) continue;
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (!isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 2;
        } else if (op == OP_POP_JUMP_IF_TRUE) {
            if (stack.empty()) continue;
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 2;
        } else if (op == OP_LIST_APPEND) {
            if (stack.size() >= static_cast<size_t>(arg)) {
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                proto::ProtoObject* lstObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg]);
                const proto::ProtoObject* data = lstObj->getAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"));
                if (data && data->asList(ctx)) {
                    const proto::ProtoList* lst = data->asList(ctx);
                    lst = lst->appendLast(ctx, val);
                    const proto::ProtoObject* newLst = lstObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), lst->asObject(ctx));
                    stack[stack.size() - arg] = const_cast<proto::ProtoObject*>(newLst);
                }
            }
        } else if (op == OP_MAP_ADD) {
            if (stack.size() >= static_cast<size_t>(arg)) {
                const proto::ProtoObject* key = stack.back();
                stack.pop_back();
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                proto::ProtoObject* mapObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg]);
                const proto::ProtoString* dataString = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                const proto::ProtoObject* data = mapObj->getAttribute(ctx, dataString);
                if (data && data->asSparseList(ctx)) {
                    const proto::ProtoSparseList* sl = data->asSparseList(ctx);
                    unsigned long h = key->getHash(ctx);
                    bool isNew = !sl->has(ctx, h);
                    sl = sl->setAt(ctx, h, val);
                    const proto::ProtoObject* newMap = mapObj->setAttribute(ctx, dataString, sl->asObject(ctx));
                    stack[stack.size() - arg] = const_cast<proto::ProtoObject*>(newMap);
                    if (isNew) {
                         const proto::ProtoString* keysString = proto::ProtoString::fromUTF8String(ctx, "__keys__");
                         const proto::ProtoObject* keysObj = newMap->getAttribute(ctx, keysString);
                         const proto::ProtoList* keys = (keysObj && keysObj->asList(ctx)) ? keysObj->asList(ctx) : ctx->newList();
                         keys = keys->appendLast(ctx, key);
                         const proto::ProtoObject* finalMap = newMap->setAttribute(ctx, keysString, keys->asObject(ctx));
                         stack[stack.size() - arg] = const_cast<proto::ProtoObject*>(finalMap);
                    }
                }
            }
        } else if (op == OP_SET_ADD) {
            // GC Safe: val stays on stack until added
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                const proto::ProtoObject* val = stack.back();
                // TOS-arg is the set object
                const proto::ProtoObject* setObj = stack[stack.size() - arg - 1];
                const proto::ProtoString* dataString = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                const proto::ProtoObject* data = setObj->getAttribute(ctx, dataString);
                const proto::ProtoSet* s = (data && data->asSet(ctx)) ? data->asSet(ctx) : ctx->newSet();
                s = s->add(ctx, val);
                setObj->setAttribute(ctx, dataString, s->asObject(ctx));
                stack.pop_back();
            }
        } else if (op == OP_BUILD_SET) {
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
            
            setObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), data->asObject(ctx));
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
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    unsigned long h = nameObj->getHash(ctx);
                    const proto::ProtoObject* val = PROTO_NONE;
                    
                    std::vector<const proto::ProtoObject*> worklist;
                    const proto::ProtoList* ps = frame->getParents(ctx);
                    if (ps) {
                        for (unsigned long j = 0; j < ps->getSize(ctx); ++j) worklist.push_back(ps->getAt(ctx, j));
                    }
                    
                    bool found = false;
                    while (!worklist.empty()) {
                        const proto::ProtoObject* curr = worklist.back();
                        worklist.pop_back();
                        if (!curr || curr == PROTO_NONE) continue;
                        
                        val = curr->getAttribute(ctx, nameS);
                        if (val && val != PROTO_NONE) { found = true; break; }
                        
                        const proto::ProtoString* dName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                        const proto::ProtoObject* dataObj = curr->getAttribute(ctx, dName);
                        if (dataObj && dataObj->asSparseList(ctx)) {
                            val = dataObj->asSparseList(ctx)->getAt(ctx, h);
                            if (val && val != PROTO_NONE) { found = true; break; }
                        }

                        const proto::ProtoList* parents = curr->getParents(ctx);
                        if (parents) {
                            for (unsigned long j = 0; j < parents->getSize(ctx); ++j) { worklist.push_back(parents->getAt(ctx, j)); }
                        }
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
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    std::string* sPtr = new std::string();
                    nameS->toUTF8String(ctx, *sPtr);
                    std::unique_ptr<std::string> sGuard(sPtr);
                    std::string& s = *sPtr;
                    unsigned long h = nameObj->getHash(ctx);
                    
                    std::vector<proto::ProtoObject*> worklist;
                    const proto::ProtoList* ps = frame ? frame->getParents(ctx) : nullptr;
                    if (ps) {
                        for (unsigned long j = 0; j < ps->getSize(ctx); ++j) 
                            worklist.push_back(const_cast<proto::ProtoObject*>(ps->getAt(ctx, j)));
                    }
                    
                    bool found = false;
                    while (!worklist.empty()) {
                        proto::ProtoObject* curr = worklist.back();
                        worklist.pop_back();
                        if (!curr || curr == PROTO_NONE) continue;
                        
                        if (std::getenv("PROTO_ENV_DIAG")) {
                            // log removed
                        }
                        const proto::ProtoString* dName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
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
                        const proto::ProtoList* parents = curr->getParents(ctx);
                        if (parents) {
                            for (unsigned long j = 0; j < parents->getSize(ctx); ++j) {
                                worklist.push_back(const_cast<proto::ProtoObject*>(parents->getAt(ctx, j)));
                            }
                        }
                    }
                    if (!found) {
                        if (env) {
                            env->raiseNameError(ctx, "nonlocal " + s + " not found");
                        }
                    }
                    delete sPtr;
                }
            }
        } else if (op == OP_JUMP_ABSOLUTE) {
            if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 2;
        } else if (op == OP_LOAD_ATTR) {
            if (names && stack.size() >= 1 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* attrName = nameObj->asString(ctx);
                    if (obj->hasAttribute(ctx, attrName)) {
                        const proto::ProtoObject* val = env ? env->getAttribute(ctx, obj, attrName) : obj->getAttribute(ctx, attrName);
                        stack.push_back(val);
                    } else {
                        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                        std::string* attrPtr = new std::string();
                        attrName->toUTF8String(ctx, *attrPtr);
                        if (env) env->raiseAttributeError(ctx, obj, *attrPtr);
                        delete attrPtr;
                        continue;
                    }
                }
            }
        } else if (op == OP_STORE_ATTR) {
            if (names && stack.size() >= 2 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::string n;
                        nameS->toUTF8String(ctx, n);
                        std::cerr << "[proto-diag] OP_STORE_ATTR: obj=" << obj << " name='" << n << "' val=" << val << "\n";
                    }
                    if (env) {
                        obj = const_cast<proto::ProtoObject*>(env->setAttribute(ctx, obj, nameS, val));
                    } else {
                        proto::ProtoObject* mutableObj = const_cast<proto::ProtoObject*>(obj);
                        obj = const_cast<proto::ProtoObject*>(mutableObj->setAttribute(ctx, nameS, val));
                    }
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
            
            listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), lst->asObject(ctx)));
            if (env && env->getListPrototype()) {
                listObj = const_cast<proto::ProtoObject*>(listObj->addParent(ctx, env->getListPrototype()));
                listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx, env->getClassString(), env->getListPrototype()));
            }
            
            const proto::ProtoObject* finalList = listObj;
            for (int j = 0; j < arg + 2; ++j) stack.pop_back();
            stack.push_back(finalList);
        } else if (op == OP_BINARY_SUBSCR) {
            // i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* key = stack.back();
            stack.pop_back();
            const proto::ProtoObject* container = stack.back();
            stack.pop_back();
            
            const proto::ProtoString* getItemS = env ? env->getGetItemString() : proto::ProtoString::fromUTF8String(ctx, "__getitem__");
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
            const proto::ProtoObject* result = invokeDunder(ctx, container, getItemS, args);
            
            if (result) {
                stack.push_back(result);
            } else {
                // Fallback for minimal objects without __getitem__ (e.g. built-in lists/tuples if dunder is missing)
                const proto::ProtoObject* data = container->getAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"));
                if (data) {
                    if (data->asList(ctx) && key->isInteger(ctx)) {
                        long long idx = key->asLong(ctx);
                        const proto::ProtoList* list = data->asList(ctx);
                        if (idx >= 0 && static_cast<unsigned long>(idx) < list->getSize(ctx)) {
                            stack.push_back(list->getAt(ctx, static_cast<int>(idx)));
                        } else {
                            stack.push_back(PROTO_NONE);
                        }
                    } else if (data->asList(ctx) && env && env->getSliceType() && key->isInstanceOf(ctx, env->getSliceType()) == PROTO_TRUE) {
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
                        stack.push_back(newListObj);
                    } else if (data->asSparseList(ctx)) {
                        unsigned long h = key->getHash(ctx);
                        const proto::ProtoObject* val = data->asSparseList(ctx)->getAt(ctx, h);
                        if (val) stack.push_back(val);
                        else stack.push_back(PROTO_NONE);
                    }
                } else {
                    stack.push_back(PROTO_NONE);
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
            
            const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
            const proto::ProtoString* keysName = env ? env->getKeysString() : proto::ProtoString::fromUTF8String(ctx, "__keys__");
            
            dictObj = const_cast<proto::ProtoObject*>(dictObj->setAttribute(ctx, dataName, data->asObject(ctx)));
            stack.back() = dictObj;
            dictObj = const_cast<proto::ProtoObject*>(dictObj->setAttribute(ctx, keysName, keys->asObject(ctx)));
            stack.back() = dictObj;
            
            const proto::ProtoObject* finalDict = dictObj;
            for (int k = 0; k < 2 * arg + 3; ++k) stack.pop_back();
            stack.push_back(finalDict);
        } else if (op == OP_STORE_SUBSCR) {
            // i++;
            if (stack.size() < 3) continue;
            const proto::ProtoObject* value = stack.back();
            stack.pop_back();
            const proto::ProtoObject* key = stack.back();
            stack.pop_back();
            proto::ProtoObject* container = const_cast<proto::ProtoObject*>(stack.back());
            stack.pop_back();
            const proto::ProtoString* setItemS = env ? env->getSetItemString() : proto::ProtoString::fromUTF8String(ctx, "__setitem__");
            const proto::ProtoObject* setitem = container->getAttribute(ctx, setItemS);
            if (setitem && setitem != PROTO_NONE) {
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, value);
                invokeDunder(ctx, container, setItemS, args);
            } else {
                // Fallback for native objects (maps/lists) without __setitem__
                if (container->asSparseList(ctx)) {
                    container->asSparseList(ctx)->setAt(ctx, key->getHash(ctx), value);
                } else if (container->asList(ctx) && key->isInteger(ctx)) {
                    long long idx = key->asLong(ctx);
                    container->asList(ctx)->setAt(ctx, static_cast<int>(idx), value);
                }
            }
        } else if (op == OP_CALL_FUNCTION_KW) {
            if (stack.size() < 2) continue; // at least callable and names_tuple
            const proto::ProtoObject* namesTupleObj = stack.back();
            stack.pop_back();
            const proto::ProtoTuple* namesTuple = namesTupleObj->asTuple(ctx);
            if (!namesTuple) continue;
            int nkw = namesTuple->getSize(ctx);
            int npos = arg - nkw;
            if (stack.size() < static_cast<size_t>(arg) + 1) continue;

            std::vector<const proto::ProtoObject*> kwVals(nkw);
            for (int k = nkw - 1; k >= 0; --k) {
                kwVals[k] = stack.back();
                stack.pop_back();
            }
            std::vector<const proto::ProtoObject*> posArgs(npos);
            for (int p = npos - 1; p >= 0; --p) {
                posArgs[p] = stack.back();
                stack.pop_back();
            }
            const proto::ProtoObject* callable = stack.back();
            stack.pop_back();

            const proto::ProtoList* plArgs = ctx->newList();
            for (int p = 0; p < npos; ++p) plArgs = plArgs->appendLast(ctx, posArgs[p]);

            const proto::ProtoSparseList* kwMap = ctx->newSparseList();
            for (int k = 0; k < nkw; ++k) {
                const proto::ProtoObject* nameStr = namesTuple->getAt(ctx, k);
                if (nameStr->isString(ctx))
                    kwMap = kwMap->setAt(ctx, nameStr->getHash(ctx), kwVals[k]);
            }

            if (env) env->pushKwNames(namesTuple);
            const proto::ProtoObject* result = invokeCallable(ctx, callable, plArgs, kwMap);
            if (env) env->popKwNames();
            if (result) {
                stack.push_back(result);
            } else if (env && env->hasPendingException()) {
                continue;
            } else {
                stack.push_back(env ? env->getNonePrototype() : PROTO_NONE);
            }
        } else if (op == OP_CALL_FUNCTION) {
            if (stack.size() < static_cast<size_t>(arg) + 1) continue;
            std::vector<const proto::ProtoObject*> argVec(arg);
            for (int j = arg - 1; j >= 0; --j) {
                argVec[j] = stack.back();
                stack.pop_back();
            }
            const proto::ProtoObject* callable = stack.back();
            stack.pop_back();
            if (std::getenv("PROTO_ENV_DIAG")) {
                std::cerr << "[proto-diag] OP_CALL_FUNCTION: callable=" << callable << " isMethod=" << (callable && callable->isMethod(ctx)) << "\n" << std::flush;
            }
            const proto::ProtoList* args = ctx->newList();
            for (int j = 0; j < arg; ++j)
                args = args->appendLast(ctx, argVec[j]);
            const proto::ProtoObject* result = invokeCallable(ctx, callable, args);
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (result) {
                stack.push_back(result);
            } else if (env && env->hasPendingException()) {
                // If it returns nullptr and has exception, it's an error.
                continue;
            } else {
                // Return value was None (nullptr) but no exception.
                stack.push_back(env ? env->getNonePrototype() : PROTO_NONE);
            }
        } else if (op == OP_CALL_FUNCTION_EX) {
            const proto::ProtoObject* kwargs = (arg & 1) ? stack.back() : nullptr;
            if (arg & 1) stack.pop_back();
            const proto::ProtoObject* starargs = stack.back();
            stack.pop_back();
            const proto::ProtoObject* callable = stack.back();
            stack.pop_back();
            
            const proto::ProtoList* posArgs = nullptr;
            if (starargs && starargs->asList(ctx)) {
                posArgs = starargs->asList(ctx);
            } else if (starargs && starargs != PROTO_NONE) {
                // Fallback: use getIter
                if (env) {
                    const proto::ProtoObject* it = env->iter(starargs);
                    if (it) {
                        const proto::ProtoList* L = ctx->newList();
                        while (const proto::ProtoObject* next = env->next(it)) {
                            L = L->appendLast(ctx, next);
                        }
                        posArgs = L;
                    }
                }
            }
            if (!posArgs) posArgs = ctx->newList();
            
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
                 const proto::ProtoObject* keysListObj = kwargs->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"));
                 if (keysListObj && keysListObj->asList(ctx)) {
                     env->pushKwNames(ctx->newTupleFromList(keysListObj->asList(ctx)));
                     pushed = true;
                 }
            }

            const proto::ProtoObject* result = invokePythonCallable(ctx, callable, posArgs, kwArgs);
            if (pushed && env) env->popKwNames();
            if (result) {
                stack.push_back(result);
            } else if (env && env->hasPendingException()) {
                continue;
            } else {
                stack.push_back(env ? env->getNonePrototype() : PROTO_NONE);
            }
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
            
            tupObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), tup->asObject(ctx));
            
            const proto::ProtoObject* finalTup = tupObj;
            for (int j = 0; j < arg + 2; ++j) stack.pop_back();
            stack.push_back(finalTup);
        } else if (op == OP_BUILD_FUNCTION) {
            if (!stack.empty() && frame) {
                const proto::ProtoObject* codeObj = stack.back();
                stack.pop_back();
                proto::ProtoObject* fn = createUserFunction(ctx, codeObj, const_cast<proto::ProtoObject*>(PythonEnvironment::getCurrentGlobals()), frame);
                if (fn) {
                    // Store flags if needed, though they are usually in code object
                    stack.push_back(fn);
                }
            }
        } else if (op == OP_BUILD_CLASS) {
            // No argument for BUILD_CLASS
            if (stack.size() >= 3 && frame) {
                const proto::ProtoObject* body = stack.back();
                stack.pop_back();
                const proto::ProtoObject* bases = stack.back();
                stack.pop_back();
                const proto::ProtoObject* name = stack.back();
                stack.pop_back();
                
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                const proto::ProtoString* nameS = env ? env->getNameString() : proto::ProtoString::fromUTF8String(ctx, "__name__");
                const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__");
                
                proto::ProtoObject* ns = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, nameS, name));
                if (env) {
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, env->getFBackString(), PythonEnvironment::getCurrentFrame()));
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, env->getFGlobalsString(), PythonEnvironment::getCurrentGlobals()));
                    ns = const_cast<proto::ProtoObject*>(ns->setAttribute(ctx, env->getFLocalsString(), ns));
                }
                
                // Invoke body with ns as locals
                if (body) {
                    const proto::ProtoObject* codeObj = body->getAttribute(ctx, env ? env->getCodeString() : proto::ProtoString::fromUTF8String(ctx, "__code__"));
                    if (codeObj && codeObj != PROTO_NONE) {
                        runCodeObject(ctx, codeObj, ns);
                    } else {
                        const proto::ProtoObject* callM = body->getAttribute(ctx, callS);
                        if (callM && callM->asMethod(ctx)) {
                            callM->asMethod(ctx)(ctx, body, nullptr, ctx->newList(), nullptr);
                        }
                    }
                }
                
                // Create class object (prototype)
                proto::ProtoObject* targetClass = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                if (env && env->getObjectPrototype()) {
                    targetClass = const_cast<proto::ProtoObject*>(targetClass->addParent(ctx, env->getObjectPrototype()));
                }
                if (env && env->getTypePrototype()) {
                    targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, env->getClassString(), env->getTypePrototype()));
                }
                // Step V75: Explicitly set __name__ on the class object
                targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, nameS, name));
                
                // Set runUserClassCall as __call__
                targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, callS, ctx->fromMethod(targetClass, runUserClassCall)));
                
                if (bases && bases->asTuple(ctx)) {
                    auto bt = bases->asTuple(ctx);
                    for (size_t j = 0; j < bt->getSize(ctx); ++j) {
                        targetClass = const_cast<proto::ProtoObject*>(targetClass->addParent(ctx, bt->getAt(ctx, j)));
                    }
                }
                
                // Copy ns attributes to targetClass
                const proto::ProtoObject* codeObj = body ? body->getAttribute(ctx, env ? env->getCodeString() : proto::ProtoString::fromUTF8String(ctx, "__code__")) : nullptr;
                if (codeObj && codeObj != PROTO_NONE) {
                    const proto::ProtoObject* co_names_obj = codeObj->getAttribute(ctx, env ? env->getCoNamesString() : proto::ProtoString::fromUTF8String(ctx, "co_names"));
                    if (co_names_obj && co_names_obj->asList(ctx)) {
                        const proto::ProtoList* co_names = co_names_obj->asList(ctx);
                        for (size_t i = 0; i < co_names->getSize(ctx); ++i) {
                            const proto::ProtoObject* knObj = co_names->getAt(ctx, i);
                            if (knObj && knObj->isString(ctx)) {
                                const proto::ProtoString* k = knObj->asString(ctx);
                                if (env && (k == env->getFBackString() || k == env->getFLocalsString() || k == env->getFGlobalsString() || k == env->getFCodeString())) {
                                    continue;
                                }
                                const proto::ProtoObject* attrVal = ns->getAttribute(ctx, k);
                                if (attrVal && attrVal != PROTO_NONE) {
                                    targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, k, attrVal));
                                }
                            }
                        }
                    }
                }
                
                // Also copy any attributes that might have been set manually (if possible)
                // For now, the co_names approach is much safer than the hash cast.
                
                // Set __call__ to support instantiation
                if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-diag] OP_BUILD_CLASS: setting __call__ for " << targetClass << "\n";
                targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, callS, ctx->fromMethod(targetClass, runUserClassCall)));

                stack.push_back(targetClass);
            }
        }
        else if (op == OP_GET_ITER) {
            if (stack.empty()) continue;
            const proto::ProtoObject* iterable = stack.back();
            stack.pop_back();
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(ctx, "__iter__");
            
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
            
            const proto::ProtoObject* iterM = env ? env->getAttribute(ctx, iterable, iterS) : iterable->getAttribute(ctx, iterS);
            
            if (!iterM || !iterM->asMethod(ctx)) {
                stack.push_back(PROTO_NONE);
                continue;
            }
            const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, emptyL, nullptr);
            if (it) {
                if (get_env_diag()) {
                }
                stack.push_back(it);
            } else {
                if (get_env_diag()) {
                }
                stack.push_back(iterable);
            }
        } else if (op == OP_FOR_ITER) {
            if (stack.empty()) continue;
            const proto::ProtoObject* iterator = stack.back();

            // FAST PATH: RangeIterator
            proto::ProtoObjectPointer ptr{iterator};
            if (ptr.op.pointer_tag == POINTER_TAG_RANGE_ITERATOR) {
                 // Use const_cast because we need to modify the iterator state (current)
                 auto* impl = const_cast<proto::ProtoRangeIteratorImplementation*>(proto::toImpl<proto::ProtoRangeIteratorImplementation>(iterator));
                  const proto::ProtoObject* val = impl->implNext(ctx); // Direct C++ call
                  if (val) {
                      stack.push_back(val);
                  } else {
                     stack.pop_back();
                     if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                        i = static_cast<unsigned long>(arg) - 2;
                 }
                 continue;
            }

            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(ctx, "__next__");
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
            
            const proto::ProtoObject* nextM = env ? env->getAttribute(ctx, iterator, nextS) : iterator->getAttribute(ctx, nextS);
            if (!nextM || !nextM->asMethod(ctx)) {
                if (get_env_diag()) {
                }
                stack.pop_back();
                if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                    i = static_cast<unsigned long>(arg) - 2;
                continue;
            }
            
            const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, iterator, nullptr, emptyL, nullptr);
            if (env && env->hasPendingException()) {
                const proto::ProtoObject* exc = env->takePendingException();
                if (env->isStopIteration(ctx, exc)) {
                    if (get_env_diag()) {
                    }
                    stack.pop_back();
                    if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                        i = static_cast<unsigned long>(arg) - 2;
                    continue;
                } else {
                    env->setPendingException(exc);
                    continue;
                }
            }
            if (val != nullptr) {
                if (get_env_diag()) {
                }
                stack.push_back(val);
            } else {
                if (get_env_diag()) {
                }
                // nullptr returned by native __next__ signals exhaustion in protoPython protocol
                stack.pop_back();
                if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                    i = static_cast<unsigned long>(arg) - 2;
            }
        } else if (op == OP_UNPACK_SEQUENCE) {
            if (get_env_diag()) {
                if (get_env_diag()) {
                }
            }
            if (stack.empty() || arg <= 0) continue;
            const proto::ProtoObject* seq = stack.back();
            stack.pop_back();
            const proto::ProtoList* list = nullptr;
            const proto::ProtoTuple* tup = nullptr;
            if (seq->asList(ctx)) list = seq->asList(ctx);
            else if (seq->asTuple(ctx)) tup = seq->asTuple(ctx);
            else {
                const proto::ProtoObject* data = seq->getAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"));
                if (data) {
                    if (data->asList(ctx)) list = data->asList(ctx);
                    else if (data->asTuple(ctx)) tup = data->asTuple(ctx);
                }
            }
            if (list) {
                if (get_env_diag()) {
                }
                if (static_cast<int>(list->getSize(ctx)) < arg) continue;
                for (int j = arg - 1; j >= 0; --j) {
                    const proto::ProtoObject* elem = list->getAt(ctx, j);
                    if (get_env_diag()) {
                        // log removed
                    }
                    stack.push_back(elem);
                }
            } else if (tup) {
                if (get_env_diag()) {
                }
                if (static_cast<int>(tup->getSize(ctx)) < arg) continue;
                for (int j = arg - 1; j >= 0; --j) {
                    const proto::ProtoObject* elem = tup->getAt(ctx, j);
                    if (get_env_diag()) {
                        // log removed
                    }
                    stack.push_back(elem);
                }
            }
        } else if (op == OP_LOAD_GLOBAL) {
                if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::string n;
                        nameS->toUTF8String(ctx, n);
                        std::cerr << "[proto-diag] OP_LOAD_GLOBAL: name='" << n << "'\n" << std::flush;
                    }
                    const proto::ProtoObject* val = frame->getAttribute(ctx, nameS);
                    bool found = (val != nullptr);
                    if (!found) {
                        const proto::ProtoString* dName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
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
                                std::string n;
                                nameS->toUTF8String(ctx, n);
                                env->raiseNameError(ctx, n);
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
                if (stack.empty()) continue;
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (nameObj->isString(ctx)) {
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::string n;
                        nameObj->asString(ctx)->toUTF8String(ctx, n);
                        std::cerr << "[proto-diag] OP_STORE_GLOBAL: name='" << n << "' val=" << val << "\n" << std::flush;
                    }
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
                stack.push_back(b);
                stack.push_back(a);
                stack.push_back(c);
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
                stack.push_back(c);
                stack.push_back(b);
                stack.push_back(a);
                stack.push_back(d);
            }
        } else if (op == OP_LIST_EXTEND) {
            if (stack.size() >= static_cast<size_t>(arg + 1)) {
                // stack[-2] is listObj, stack[-1] is iterable
                const proto::ProtoObject* iterable = stack.back();
                proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg - 1]);
                
                const proto::ProtoString* dataS = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
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
                
                const proto::ProtoString* dataS = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                const proto::ProtoString* keysS = env ? env->getKeysString() : proto::ProtoString::fromUTF8String(ctx, "__keys__");
                
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
                
                const proto::ProtoString* dataS = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                const proto::ProtoObject* data = setObj->getAttribute(ctx, dataS);
                const proto::ProtoSet* s = (data && data->asSet(ctx)) ? data->asSet(ctx) : nullptr;
                
                if (s && env) {
                    const proto::ProtoObject* iter = env->iter(iterable);
                    while (iter) {
                        const proto::ProtoObject* item = env->next(iter);
                        if (!item) break;
                        s = s->add(ctx, item);
                    }
                    setObj->setAttribute(ctx, dataS, s->asObject(ctx));
                }
                stack.pop_back();
            }
        } else if (op == OP_LIST_TO_TUPLE) {
            // GC Safe: list stays on stack until tuple is ready
            if (!stack.empty()) {
                proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(stack.back());
                
                const proto::ProtoString* dataS = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                const proto::ProtoObject* data = listObj->getAttribute(ctx, dataS);
                const proto::ProtoList* L = (data && data->asList(ctx)) ? data->asList(ctx) : nullptr;
                
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
            // i++;
            if (frame) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj && nameObj->isString(ctx)) {
                    frame->setAttribute(ctx, nameObj->asString(ctx), nullptr);
                    // Also check __data__ if frame is a dict
                    const proto::ProtoString* data_name = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                    const proto::ProtoObject* data = frame->getAttribute(ctx, data_name);
                    if (data && data->asSparseList(ctx)) {
                        data->asSparseList(ctx)->removeAt(ctx, nameObj->getHash(ctx));
                    }
                }
            }
            if (env) env->invalidateResolveCache();
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
                    obj->setAttribute(ctx, nameObj->asString(ctx), nullptr);
                }
            }
        } else if (op == OP_DELETE_SUBSCR) {
            if (stack.size() >= 2) {
                const proto::ProtoObject* key = stack.back();
                stack.pop_back();
                const proto::ProtoObject* container = stack.back();
                stack.pop_back();
                const proto::ProtoString* delItemS = env ? env->getDelItemString() : proto::ProtoString::fromUTF8String(ctx, "__delitem__");
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
                const proto::ProtoObject* result = invokeDunder(ctx, container, delItemS, args);
                if (!result) {
                    // Fallback for list/dict
                    const proto::ProtoString* data_name = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
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
                                const proto::ProtoString* data_name = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                                const_cast<proto::ProtoObject*>(container)->setAttribute(ctx, data_name, newList->asObject(ctx));
                            }
                        } else if (data->asSparseList(ctx)) {
                            data->asSparseList(ctx)->removeAt(ctx, key->getHash(ctx));
                        }
                    }
                }
            }
        } else if (op == OP_SETUP_FINALLY) {
             blockStack.push_back({static_cast<unsigned long>(arg), stack.size()});
        } else if (op == OP_POP_BLOCK) {
            if (!blockStack.empty()) blockStack.pop_back();
        } else if (op == OP_GET_AWAITABLE) {
            if (stack.empty()) continue;
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
            if (stack.empty()) continue;
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
            if (stack.empty()) continue;
            const proto::ProtoObject* aiter = stack.back();
            const proto::ProtoString* anextS = env ? env->getANextString() : proto::ProtoString::fromUTF8String(ctx, "__anext__");
            const proto::ProtoObject* awaitable = invokeDunder(ctx, aiter, anextS, ctx->newList());
            if (awaitable) {
                stack.push_back(awaitable);
            } else {
                if (env) env->raiseTypeError(ctx, "async for item must be an async iterator");
                return PROTO_NONE;
            }
        } else if (op == OP_EXCEPTION_MATCH) {
             if (stack.size() < 2) continue;
             const proto::ProtoObject* type = stack.back();
             stack.pop_back();
             const proto::ProtoObject* exc = stack.back();
             bool match = false;
             if (exc && type) {
                 match = env->isException(exc, type);
                 if (match) {
                     env->clearPendingException();
                 }
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
            if (stack.empty()) continue;
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
                return PROTO_NONE;
            }
            blockStack.push_back({static_cast<unsigned long>(arg), stack.size()});
        }
    }
    return stack.empty() ? PROTO_NONE : stack.back();
}

const proto::ProtoObject* executeMinimalBytecode(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode,
    const proto::ProtoList* names,
    proto::ProtoObject*& frame) {
    if (!ctx || !constants || !bytecode) return nullptr;
    unsigned long n = bytecode->getSize(ctx);
    return executeBytecodeRange(ctx, constants, bytecode, names, frame, 0, n ? n - 1 : 0, 0);
}

} // namespace protoPython
