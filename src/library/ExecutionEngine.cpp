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
    const proto::ProtoObject* codeObj = self->getAttribute(ctx, env ? env->getCodeString() : proto::ProtoString::fromUTF8String(ctx, "__code__"));
    if (!codeObj || codeObj == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* globalsObj = self->getAttribute(ctx, env ? env->getGlobalsString() : proto::ProtoString::fromUTF8String(ctx, "__globals__"));
    if (!globalsObj || globalsObj == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoObject* co_varnames_obj = codeObj->getAttribute(ctx, env ? env->getCoVarnamesString() : proto::ProtoString::fromUTF8String(ctx, "co_varnames"));
    const proto::ProtoObject* co_nparams_obj = codeObj->getAttribute(ctx, env ? env->getCoNparamsString() : proto::ProtoString::fromUTF8String(ctx, "co_nparams"));
    const proto::ProtoObject* co_automatic_obj = codeObj->getAttribute(ctx, env ? env->getCoAutomaticCountString() : proto::ProtoString::fromUTF8String(ctx, "co_automatic_count"));
    const proto::ProtoList* co_varnames = co_varnames_obj && co_varnames_obj->asList(ctx) ? co_varnames_obj->asList(ctx) : nullptr;
    int nparams = (co_nparams_obj && co_nparams_obj->isInteger(ctx)) ? static_cast<int>(co_nparams_obj->asLong(ctx)) : 0;
    int automatic_count = (co_automatic_obj && co_automatic_obj->isInteger(ctx)) ? static_cast<int>(co_automatic_obj->asLong(ctx)) : 0;
    if (automatic_count < 0) automatic_count = 0;
    if (nparams < 0) nparams = 0;
    if (std::getenv("PROTO_THREAD_DIAG") && args) {
        std::cerr << "[proto-thread-diag] runUserFunctionCall nparams=" << nparams << " auto=" << automatic_count << " args=" << args->getSize(ctx) << ": ";
        for (unsigned long i = 0; i < args->getSize(ctx); ++i) {
            std::cerr << args->getAt(ctx, i) << " ";
        }
        std::cerr << "\n" << std::flush;
    }
    if (std::getenv("PROTO_THREAD_DIAG")) {
        std::cerr << "[proto-thread-diag] runUserFunctionCall nparams=" << nparams << " auto=" << automatic_count << " args=" << (args ? args->getSize(ctx) : 0) << "\n" << std::flush;
    }

    const proto::ProtoList* parameterNames = nullptr;
    const proto::ProtoList* localNames = nullptr;
    if (co_varnames && automatic_count > 0 && static_cast<unsigned long>(automatic_count) <= co_varnames->getSize(ctx)) {
        if (std::getenv("PROTO_THREAD_DIAG")) {
            std::cerr << "[proto-thread-diag] varnames: ";
            for (unsigned long i = 0; i < co_varnames->getSize(ctx); ++i) {
                std::string vn; co_varnames->getAt(ctx, i)->asString(ctx)->toUTF8String(ctx, vn);
                std::cerr << vn << " ";
            }
            std::cerr << "\n" << std::flush;
        }
        parameterNames = (nparams > 0 && static_cast<unsigned long>(nparams) <= co_varnames->getSize(ctx))
            ? ctx->newList() : nullptr;
        if (parameterNames) {
            for (int i = 0; i < nparams; ++i)
                parameterNames = parameterNames->appendLast(ctx, co_varnames->getAt(ctx, i));
        }
        localNames = ctx->newList();
        for (int i = 0; i < automatic_count; ++i)
            localNames = localNames->appendLast(ctx, co_varnames->getAt(ctx, i));
    }

    ContextScope scope(ctx->space, ctx, parameterNames, localNames, args, kwargs);
    proto::ProtoContext* calleeCtx = scope.context();


    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
    if (env && env->getFramePrototype()) {
        frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, env->getFramePrototype()));
    }
    
    if (args && nparams > 0) {
        unsigned long argCount = args->getSize(calleeCtx);
        unsigned long toCopy = (argCount < static_cast<unsigned long>(nparams)) ? argCount : static_cast<unsigned long>(nparams);
        
        if (automatic_count > 0) {
            unsigned int nSlots = calleeCtx->getAutomaticLocalsCount();
            if (toCopy > nSlots) toCopy = nSlots;
            proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());
            for (unsigned long i = 0; i < toCopy; ++i) {
                slots[i] = const_cast<proto::ProtoObject*>(args->getAt(calleeCtx, static_cast<int>(i)));
            }
        } else if (co_varnames) {
            // Store in frame's __data__ or attributes
            const proto::ProtoString* dName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(calleeCtx, "__data__");
            const proto::ProtoObject* dataObj = env ? env->getAttribute(calleeCtx, frame, dName) : frame->getAttribute(calleeCtx, dName);
            
            if (get_env_diag()) {
                std::cerr << "[proto-exec-diag] Storing params in __data__ for frame " << frame << " toCopy=" << toCopy << " co_varnames size=" << (co_varnames ? co_varnames->getSize(calleeCtx) : 0) << "\n";
            }
            for (unsigned long i = 0; i < toCopy; ++i) {
                const proto::ProtoObject* nameObj = co_varnames->getAt(calleeCtx, static_cast<int>(i));
                if (nameObj->isString(calleeCtx)) {
                    const proto::ProtoObject* val = args->getAt(calleeCtx, static_cast<int>(i));
                    if (dataObj && dataObj->asSparseList(calleeCtx)) {
                        unsigned long h = nameObj->getHash(calleeCtx);
                        const proto::ProtoSparseList* data = dataObj->asSparseList(calleeCtx)->setAt(calleeCtx, h, val);
                        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, dName, data->asObject(calleeCtx)));
                    } else {
                        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, nameObj->asString(calleeCtx), val));
                    }
                }
            }
        }
    }
    if (!frame) return PROTO_NONE;
    if (env) {
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFBackString(), PythonEnvironment::getCurrentFrame()));
        const proto::ProtoObject* closure = self->getAttribute(calleeCtx, env->getClosureString());
        if (closure && closure != PROTO_NONE) {
            frame = const_cast<proto::ProtoObject*>(frame->addParent(calleeCtx, closure));
        }
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFCodeString(), codeObj));
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFGlobalsString(), globalsObj));
    }
    if (env) {
        // locals is the frame itself for now
        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(calleeCtx, env->getFLocalsString(), frame));
    }

    const proto::ProtoObject* isGenObj = codeObj->getAttribute(calleeCtx, env ? env->getCoIsGeneratorString() : proto::ProtoString::fromUTF8String(calleeCtx, "co_is_generator"));
    bool isGenerator = isGenObj && isGenObj->isBoolean(calleeCtx) && isGenObj->asBoolean(calleeCtx);

    if (isGenerator) {
        proto::ProtoObject* gen = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
        if (env && env->getGeneratorPrototype()) {
            gen = const_cast<proto::ProtoObject*>(gen->addParent(calleeCtx, env->getGeneratorPrototype()));
        }
        gen->setAttribute(calleeCtx, env ? env->getGiCodeString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_code"), codeObj);
        gen->setAttribute(calleeCtx, env ? env->getGiFrameString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_frame"), frame);
        gen->setAttribute(calleeCtx, env ? env->getGiRunningString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_running"), PROTO_FALSE);
        gen->setAttribute(calleeCtx, env ? env->getGiPCString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_pc"), calleeCtx->fromInteger(0));
        
        const proto::ProtoList* emptyStack = calleeCtx->newList();
        gen->setAttribute(calleeCtx, env ? env->getGiStackString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_stack"), reinterpret_cast<const proto::ProtoObject*>(emptyStack));
        
        const proto::ProtoList* localList = calleeCtx->newList();
        unsigned int nSlots = calleeCtx->getAutomaticLocalsCount();
        const proto::ProtoObject** slots = calleeCtx->getAutomaticLocals();
        if (slots) {
            for (unsigned int i = 0; i < nSlots; ++i) {
                localList = localList->appendLast(calleeCtx, slots[i]);
            }
        }
        gen->setAttribute(calleeCtx, env ? env->getGiLocalsString() : proto::ProtoString::fromUTF8String(calleeCtx, "gi_locals"), reinterpret_cast<const proto::ProtoObject*>(localList));
        
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
    
    // Invoke __init__ if present
    const proto::ProtoString* initS = env ? env->getInitString() : proto::ProtoString::fromUTF8String(ctx, "__init__");
    const proto::ProtoObject* initM = env ? env->getAttribute(ctx, obj, initS) : obj->getAttribute(ctx, initS);
    if (initM) {
        invokeCallable(ctx, initM, args, kwargs);
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
        return a->asString(ctx)->appendLast(ctx, b->asString(ctx))->asObject(ctx);
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
        std::cerr << "[modulo-debug] a=" << a << " isStr=" << (a ? a->isString(ctx) : 0) << " b=" << b << " isStr=" << (b ? b->isString(ctx) : 0) << "\n";
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
        std::string tpl;
        a->asString(ctx)->toUTF8String(ctx, tpl);
        
        auto getStr = [&](const proto::ProtoObject* obj) -> std::string {
            if (obj->isString(ctx)) {
                std::string s; obj->asString(ctx)->toUTF8String(ctx, s); return s;
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
                    std::string s;
                    if (rs && rs->isString(ctx)) {
                        rs->asString(ctx)->toUTF8String(ctx, s);
                        return s;
                    }
                }
                return "<object>";
            }
        };

        if (b->isTuple(ctx)) {
            const proto::ProtoTuple* bt = b->asTuple(ctx);
            unsigned long n = bt->getSize(ctx);
            for (unsigned long i = 0; i < n; ++i) {
                std::string val = getStr(bt->getAt(ctx, i));
                size_t pos = tpl.find("%s");
                if (pos == std::string::npos) pos = tpl.find("%d");
                if (pos != std::string::npos) {
                    tpl.replace(pos, 2, val);
                }
            }
        } else {
            std::string valStr = getStr(b);
            size_t pos = 0;
            bool replaced = false;
            while ((pos = tpl.find("%s", pos)) != std::string::npos) {
                tpl.replace(pos, 2, valStr);
                pos += valStr.length();
                replaced = true;
            }
            pos = 0;
            while ((pos = tpl.find("%d", pos)) != std::string::npos) {
                tpl.replace(pos, 2, valStr);
                pos += valStr.length();
                replaced = true;
            }
            if (!replaced && tpl.find('%') != std::string::npos) {
                // Potential formatting error or unsupported specifier
            }
        }
        return ctx->fromUTF8String(tpl.c_str());
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
            const proto::ProtoObject* keysObj = b->getAttribute(ctx, env ? env->getKeysString() : proto::ProtoString::fromUTF8String(ctx, "__keys__"));
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
    }
    return result ? PROTO_TRUE : PROTO_FALSE;
}

static bool isTruthy(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    if (obj == PROTO_FALSE) return false;
    if (obj == PROTO_TRUE) return true;
    if (obj->isInteger(ctx)) return obj->asLong(ctx) != 0;
    if (obj->isDouble(ctx)) return obj->asDouble(ctx) != 0.0;
    if (obj->isString(ctx)) return obj->asString(ctx)->getSize(ctx) > 0;
    return true;
}

static const proto::ProtoObject* invokeCallable(proto::ProtoContext* ctx,
    const proto::ProtoObject* callable, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs = nullptr) {
    if (callable->isMethod(ctx)) {
        const auto* cell = proto::toImpl<const proto::ProtoMethodCell>(callable);
        return cell->method(ctx, const_cast<proto::ProtoObject*>(cell->self), nullptr, args, kwargs);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (std::getenv("PROTO_THREAD_DIAG")) {
        std::cerr << "[proto-thread-diag] invokeCallable callable=" << callable << " isMethod=" << callable->isMethod(ctx) << " args=" << (args ? args->getSize(ctx) : 0) << "\n" << std::flush;
    }
    const proto::ProtoObject* callAttr = callable->getAttribute(ctx, env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__"));
    if (!callAttr || !callAttr->asMethod(ctx)) {
        if (env) env->raiseTypeError(ctx, "object is not callable");
        return PROTO_NONE;
    }
    if (std::getenv("PROTO_THREAD_DIAG")) {
         std::cerr << "[proto-thread-diag] calling __call__=" << callAttr << " method=" << (void*)callAttr->asMethod(ctx) << "\n" << std::flush;
    }
    return callAttr->asMethod(ctx)(ctx, callable, nullptr, args, kwargs);
}

} // anonymous namespace

const proto::ProtoObject* py_generator_send_impl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ProtoObject* sendVal,
    const proto::ProtoObject* throwExc = nullptr) {
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;

    // 1. Check if running
    const proto::ProtoObject* runningAttr = self->getAttribute(ctx, env->getGiRunningString());
    if (runningAttr == PROTO_TRUE) {
        env->raiseValueError(ctx, ctx->fromUTF8String("generator already executing"));
        return PROTO_NONE;
    }

    // 2. Get state
    const proto::ProtoObject* codeObj = self->getAttribute(ctx, env->getGiCodeString());
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(self->getAttribute(ctx, env->getGiFrameString()));
    const proto::ProtoObject* pcObj = self->getAttribute(ctx, env->getGiPCString());
    const proto::ProtoObject* stackObj = self->getAttribute(ctx, env->getGiStackString());

    if (!codeObj || !frame || !pcObj || !stackObj) return PROTO_NONE;

    unsigned long pc = static_cast<unsigned long>(pcObj->asLong(ctx));
    const proto::ProtoList* co_code_list = codeObj->getAttribute(ctx, env->getCoCodeString())->asList(ctx);
    if (!co_code_list) return PROTO_NONE;
    
    if (pc >= co_code_list->getSize(ctx)) {
        env->raiseStopIteration(ctx, PROTO_NONE);
        return PROTO_NONE;
    }

    // 3. Restore stack
    std::vector<const proto::ProtoObject*> stack;
    const proto::ProtoList* slist = stackObj->asList(ctx);
    if (slist) {
        for (unsigned long i = 0; i < slist->getSize(ctx); ++i)
            stack.push_back(slist->getAt(ctx, static_cast<int>(i)));
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
    
    if (get_env_diag()) {
        std::cerr << "[proto-exec-diag] Resuming generator " << self << " with code " << codeObj << " at pc=" << pc << " sendVal=" << sendVal << "\n";
    }
    
    const proto::ProtoObject* result = nullptr;
    {
        const proto::ProtoObject* co_varnames_obj = codeObj->getAttribute(ctx, env->getCoVarnamesString());
        const proto::ProtoList* co_varnames = co_varnames_obj ? co_varnames_obj->asList(ctx) : nullptr;
        const proto::ProtoObject* co_automatic_obj = codeObj->getAttribute(ctx, env->getCoAutomaticCountString());
        int automatic_count = co_automatic_obj ? static_cast<int>(co_automatic_obj->asLong(ctx)) : 0;
        
        const proto::ProtoList* localNames = ctx->newList();
        if (co_varnames) {
            for (int i = 0; i < automatic_count; ++i) {
                localNames = localNames->appendLast(ctx, co_varnames->getAt(ctx, i));
            }
        }
        
        ContextScope scope(ctx->space, ctx, nullptr, localNames, nullptr, nullptr);
        proto::ProtoContext* calleeCtx = scope.context();
        
        const proto::ProtoList* savedLocals = self->getAttribute(ctx, env->getGiLocalsString())->asList(ctx);
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

        if (get_env_diag()) {
            std::cerr << "[proto-exec-diag] Resuming generator " << self << " with code " << codeObj << " at pc=" << pc << " sendVal=" << sendVal << "\n";
            std::cerr << "[proto-exec-diag] constants=" << co_consts << " size=" << (co_consts ? co_consts->getSize(calleeCtx) : 0) << "\n";
            if (co_consts && co_consts->getSize(calleeCtx) > 0) {
                std::cerr << "[proto-exec-diag] const[0]=" << co_consts->getAt(calleeCtx, 0) << "\n";
            }
        }
        
        result = executeBytecodeRange(calleeCtx, 
            co_consts,
            co_code_list,
            co_names,
            frame,
            pc,
            co_code_list->getSize(calleeCtx),
            &stack,
            &nextPc,
            &yielded);
            
        const proto::ProtoList* newLocals = calleeCtx->newList();
        const proto::ProtoObject** updatedSlots = calleeCtx->getAutomaticLocals();
        for (unsigned int i = 0; i < calleeCtx->getAutomaticLocalsCount(); ++i) {
            newLocals = newLocals->appendLast(calleeCtx, updatedSlots[i]);
        }
        self->setAttribute(calleeCtx, env->getGiLocalsString(), reinterpret_cast<const proto::ProtoObject*>(newLocals));
    }

    self->setAttribute(ctx, env->getGiRunningString(), PROTO_FALSE);
    self->setAttribute(ctx, env->getGiPCString(), ctx->fromInteger(nextPc));

    // 6. Save stack back
    const proto::ProtoList* newStack = ctx->newList();
    for (const auto* obj : stack)
        newStack = newStack->appendLast(ctx, obj);
    self->setAttribute(ctx, env->getGiStackString(), reinterpret_cast<const proto::ProtoObject*>(newStack));

    if (!yielded && !env->hasPendingException()) {
        env->raiseStopIteration(ctx, result);
        return PROTO_NONE;
    }

    return result;
}

const proto::ProtoObject* py_generator_iter(
    proto::ProtoContext*,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
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
            std::unique_lock<std::mutex> lock(proto::ProtoSpace::globalMutex);
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
        std::string n;
        name->toUTF8String(ctx, n);
        std::cerr << "[proto-engine] invokeDunder container=" << container << " name=" << n << " method=" << method << " isMethod=" << method->isMethod(ctx) << " argsSize=" << args->getSize(ctx) << "\n" << std::flush;
    }

    return invokeCallable(ctx, method, args);
}

const proto::ProtoObject* executeBytecodeRange(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode,
    const proto::ProtoList* names,
    proto::ProtoObject*& frame,
    unsigned long pcStart,
    unsigned long pcEnd,
    std::vector<const proto::ProtoObject*>* externalStack,
    unsigned long* outPc,
    bool* yielded) {
    if (!ctx || !constants || !bytecode) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env && std::getenv("PROTO_THREAD_DIAG")) std::cerr << "[proto-thread] executeBytecodeRange FAILED to get env from ctx=" << ctx << " tid=" << std::this_thread::get_id() << "\n" << std::flush;
    
    FrameScope fscope(frame);
    unsigned long n = bytecode->getSize(ctx);
    if (n == 0) return PROTO_NONE;
    if (pcEnd >= n) pcEnd = n - 1;
    /* 64-byte aligned execution state to avoid false sharing when multiple threads run tasks. */
    alignas(64) std::vector<const proto::ProtoObject*> localStack;
    std::vector<const proto::ProtoObject*>& stack = externalStack ? *externalStack : localStack;
    std::vector<Block> blockStack;
    if (!externalStack) {
        stack.reserve(64);
    }
    const bool sync_globals = (frame == PythonEnvironment::getCurrentGlobals());
    for (unsigned long i = pcStart; i <= pcEnd; ++i) {
        if (env && env->hasPendingException()) {
            if (!blockStack.empty()) {
                Block b = blockStack.back();
                blockStack.pop_back();
                while (stack.size() > b.stackDepth) stack.pop_back();
                // Push the exception object to stack for handlers to inspect
                const proto::ProtoObject* exc = env->peekPendingException();
                if (exc) stack.push_back(exc);
                env->clearPendingException();
                i = b.handlerPc - 1; // -1 because loop will i++
                continue;
            }
            return PROTO_NONE;
        }
        if ((i & 0x7FF) == 0) checkSTW(ctx);
        const proto::ProtoObject* instr = bytecode->getAt(ctx, static_cast<int>(i));
        if (!instr || !instr->isInteger(ctx)) continue;
        int op = static_cast<int>(instr->asLong(ctx));
        int arg = (i + 1 < n && bytecode->getAt(ctx, static_cast<int>(i + 1))->isInteger(ctx))
            ? static_cast<int>(bytecode->getAt(ctx, static_cast<int>(i + 1))->asLong(ctx)) : 0;
        i++; // Standard 2-byte instruction format

        if (get_env_diag()) {
            std::cerr << "[proto-exec-diag] EXEC op=" << op << " arg=" << arg << " i=" << i << " frame=" << frame << "\n";
        }

        if (get_env_diag()) {
             std::cerr << "[proto-exec-trace] i=" << i << " op=" << op << " arg=" << arg << " stack=" << stack.size() << "\n";
        }

        if (op == OP_LOAD_CONST) {
            // i++;
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
            if (outPc) *outPc = i + 1;
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
                if (get_env_diag()) std::cerr << "[proto-exec-diag] OP_YIELD_FROM exc=" << exc << "\n";
                if (env->isStopIteration(exc)) {
                    const proto::ProtoObject* stopVal = env->getStopIterationValue(ctx, exc);
                    if (get_env_diag()) std::cerr << "[proto-exec-diag] OP_YIELD_FROM StopIteration value=" << stopVal << "\n";
                    env->clearPendingException();
                    stack.pop_back(); // Remove subIter
                    stack.push_back(stopVal);
                    // Will continue to i+1
                } else {
                    return PROTO_NONE;
                }
            } else {
                // Yielded. subIter is still at stack.back().
                if (get_env_diag()) {
                    std::cerr << "[proto-exec-diag] OP_YIELD_FROM Yielded result=" << result << "\n";
                }
                ctx->returnValue = result;
                if (yielded) *yielded = true;
                if (outPc) *outPc = i; // RESTAY
                return result;
            }
        } else if (op == OP_LOAD_NAME) {
            // i++;
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    std::string name; nameObj->asString(ctx)->toUTF8String(ctx, name);
                    const proto::ProtoString* nameS = nameObj->asString(ctx);
                    unsigned long h = nameObj->getHash(ctx);
                    
                    const proto::ProtoObject* val = PROTO_NONE;
                    const proto::ProtoObject* curr = frame;
                    while (curr && curr != PROTO_NONE) {
                        val = curr->getAttribute(ctx, nameS);
                        if (val && val != PROTO_NONE) break;
                        
                        const proto::ProtoString* dName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                        const proto::ProtoObject* dataObj = curr->getAttribute(ctx, dName);
                        if (dataObj && dataObj->asSparseList(ctx)) {
                            val = dataObj->asSparseList(ctx)->getAt(ctx, h);
                            if (val && val != PROTO_NONE) break;
                        }
                        const proto::ProtoList* parents = curr->getParents(ctx);
                        if (parents && parents->getSize(ctx) > 0) {
                            // Traverse first parent (prototype or closure)
                            curr = parents->getAt(ctx, 0);
                        } else {
                            curr = nullptr;
                        }
                    }

                    if (val && val != PROTO_NONE) {
                        stack.push_back(val);
                    } else {
                        if (env) {
                            val = env->resolve(name, ctx);
                            if (val && val != PROTO_NONE) {
                                stack.push_back(val);
                            } else {
                                env->raiseNameError(ctx, name);
                                return PROTO_NONE;
                            }
                        } else {
                            return PROTO_NONE;
                        }
                    }
                }
            }
        } else if (op == OP_STORE_NAME) {
            // i++;
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                if (stack.empty()) continue;
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (nameObj->isString(ctx)) {
                    std::string name; nameObj->asString(ctx)->toUTF8String(ctx, name);
                    if (get_env_diag()) {
                        std::cerr << "[proto-exec-diag] OP_STORE_NAME " << name << " frame=" << frame << "\n";
                    }
                    const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                    const proto::ProtoObject* dataObj = env ? env->getAttribute(ctx, frame, dataName) : frame->getAttribute(ctx, dataName);
                    if (dataObj && dataObj->asSparseList(ctx)) {
                        unsigned long h = nameObj->getHash(ctx);
                        const proto::ProtoSparseList* data = dataObj->asSparseList(ctx)->setAt(ctx, h, val);
                        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, dataName, data->asObject(ctx)));
                        
                        const proto::ProtoString* keysName = env ? env->getKeysString() : proto::ProtoString::fromUTF8String(ctx, "__keys__");
                        const proto::ProtoObject* keysObj = frame->getAttribute(ctx, keysName);
                        if (keysObj && keysObj->asList(ctx)) {
                            const proto::ProtoList* keys = keysObj->asList(ctx);
                            bool found = false;
                            for (unsigned long j = 0; j < keys->getSize(ctx); ++j) {
                                if (keys->getAt(ctx, j)->getHash(ctx) == h) { found = true; break; }
                            }
                            if (!found) {
                                keys = keys->appendLast(ctx, nameObj);
                                frame->setAttribute(ctx, keysName, keys->asObject(ctx));
                            }
                        }
                    } else {
                        frame = const_cast<proto::ProtoObject*>(frame->setAttribute(ctx, nameObj->asString(ctx), val));
                    }
                    PythonEnvironment::setCurrentFrame(frame);
                    if (sync_globals) {
                        PythonEnvironment::setCurrentGlobals(frame);
                    }
                    if (get_env_diag()) {
                        std::string n; nameObj->asString(ctx)->toUTF8String(ctx, n);
                        std::cerr << "[proto-exec-diag] OP_STORE_NAME " << n << "=" << val << " on frame=" << frame << "\n";
                    }
                    if (env) env->invalidateResolveCache();
                }
            }
        } else if (op == OP_LOAD_FAST) {
            // i++;
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<unsigned long>(arg) < nSlots) {
                const proto::ProtoObject** slots = ctx->getAutomaticLocals();
                const proto::ProtoObject* val = slots[arg];
                stack.push_back(val ? val : PROTO_NONE);
            } else {
                if (get_env_diag()) {
                    std::cerr << "[proto-exec-diag] OP_LOAD_FAST out of range arg=" << arg << " nSlots=" << nSlots << "\n";
                }
                stack.push_back(PROTO_NONE);
            }
        } else if (op == OP_STORE_FAST) {
            // i++;
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
            }
            if (env) {
                if (exc) env->setPendingException(exc);
                // Note: arg == 0 (re-raise) not yet fully implemented for all cases
            }
            return PROTO_NONE;
        } else if (op == OP_SETUP_WITH) {
            // i++;
            if (stack.size() < 1) continue;
            const proto::ProtoObject* manager = stack.back();
            stack.pop_back();
            
            const proto::ProtoString* enterS = env ? env->getEnterString() : proto::ProtoString::fromUTF8String(ctx, "__enter__");
            const proto::ProtoString* exitS = env ? env->getExitString() : proto::ProtoString::fromUTF8String(ctx, "__exit__");
            
            const proto::ProtoObject* exitM = manager->getAttribute(ctx, exitS);
            stack.push_back(exitM ? exitM : (const proto::ProtoObject*)PROTO_NONE);
            
            const proto::ProtoObject* enterM = manager->getAttribute(ctx, enterS);
            const proto::ProtoObject* enterResult = PROTO_NONE;
            if (enterM && enterM->asMethod(ctx)) {
                enterResult = enterM->asMethod(ctx)(ctx, manager, nullptr, env ? env->getEmptyList() : ctx->newList(), nullptr);
            } else {
                enterResult = manager;
            }
            stack.push_back(enterResult);
        } else if (op == OP_WITH_CLEANUP) {
            // Discard result of __exit__
            if (!stack.empty()) stack.pop_back();
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
            // i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = compareOp(ctx, a, b, arg);
            if (r) stack.push_back(r);
        } else if (op == OP_POP_JUMP_IF_FALSE) {
            // i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (!isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 1;
        } else if (op == OP_POP_JUMP_IF_TRUE) {
            // i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 1;
        } else if (op == OP_LIST_APPEND) {
            // i++;
            if (stack.size() >= static_cast<size_t>(arg)) {
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                proto::ProtoObject* lstObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg]);
                const proto::ProtoObject* data = lstObj->getAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"));
                if (data && data->asList(ctx)) {
                    const proto::ProtoList* lst = data->asList(ctx);
                    lst = lst->appendLast(ctx, val);
                    lstObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), lst->asObject(ctx));
                }
            }
        } else if (op == OP_MAP_ADD) {
            // i++;
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
                    mapObj->setAttribute(ctx, dataString, sl->asObject(ctx));
                    if (isNew) {
                         const proto::ProtoString* keysString = proto::ProtoString::fromUTF8String(ctx, "__keys__");
                         const proto::ProtoObject* keysObj = mapObj->getAttribute(ctx, keysString);
                         const proto::ProtoList* keys = (keysObj && keysObj->asList(ctx)) ? keysObj->asList(ctx) : ctx->newList();
                         keys = keys->appendLast(ctx, key);
                         mapObj->setAttribute(ctx, keysString, keys->asObject(ctx));
                    }
                }
            }
        } else if (op == OP_SET_ADD) {
            // i++;
            if (stack.size() >= static_cast<size_t>(arg)) {
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                proto::ProtoObject* setObj = const_cast<proto::ProtoObject*>(stack[stack.size() - arg]);
                const proto::ProtoString* dataString = env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__");
                const proto::ProtoObject* data = setObj->getAttribute(ctx, dataString);
                const proto::ProtoSet* s = (data && data->asSet(ctx)) ? data->asSet(ctx) : ctx->newSet();
                s = s->add(ctx, val);
                setObj->setAttribute(ctx, dataString, s->asObject(ctx));
            }
        } else if (op == OP_BUILD_SET) {
            // i++;
            proto::ProtoObject* setObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            if (env) setObj->addParent(ctx, env->getSetPrototype());
            const proto::ProtoSet* data = ctx->newSet();
            for (int j = 0; j < arg; ++j) {
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                data = data->add(ctx, val);
            }
            setObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), data->asObject(ctx));
            stack.push_back(setObj);
        } else if (op == OP_JUMP_ABSOLUTE) {
            // i++;
            if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 1;
        } else if (op == OP_LOAD_ATTR) {
            // i++;
            if (names && stack.size() >= 1 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    const proto::ProtoObject* val = env ? env->getAttribute(ctx, obj, nameObj->asString(ctx)) : obj->getAttribute(ctx, nameObj->asString(ctx));
                    if (val) {
                        stack.push_back(val);
                    } else {
                        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                        std::string attr;
                        nameObj->asString(ctx)->toUTF8String(ctx, attr);
                        if (env) env->raiseAttributeError(ctx, obj, attr);
                        continue;
                    }
                }
            }
        } else if (op == OP_STORE_ATTR) {
            // i++;
            if (names && stack.size() >= 2 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    if (env) {
                        obj = const_cast<proto::ProtoObject*>(env->setAttribute(ctx, obj, nameObj->asString(ctx), val));
                    } else {
                        proto::ProtoObject* mutableObj = const_cast<proto::ProtoObject*>(obj);
                        obj = const_cast<proto::ProtoObject*>(mutableObj->setAttribute(ctx, nameObj->asString(ctx), val));
                    }
                }
            }
        } else if (op == OP_BUILD_LIST) {
            // i++;
            if (stack.size() >= static_cast<size_t>(arg)) {
                std::vector<const proto::ProtoObject*> elems(arg);
                for (int j = arg - 1; j >= 0; --j) {
                    elems[j] = stack.back();
                    stack.pop_back();
                }
                const proto::ProtoList* lst = ctx->newList();
                for (int j = 0; j < arg; ++j)
                    lst = lst->appendLast(ctx, elems[j]);
                proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), lst->asObject(ctx)));
                
                if (env && env->getListPrototype()) {
                    if (get_env_diag()) std::cerr << "[proto-engine] OP_BUILD_LIST linking listPrototype=" << env->getListPrototype() << " to list=" << listObj << "\n" << std::flush;
                    listObj = const_cast<proto::ProtoObject*>(listObj->addParent(ctx, env->getListPrototype()));
                    listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx, env->getClassString(), env->getListPrototype()));
                } else if (get_env_diag()) {
                    std::cerr << "[proto-engine] OP_BUILD_LIST FAILED: env=" << env << " proto=" << (env ? env->getListPrototype() : nullptr) << "\n" << std::flush;
                }
                
                stack.push_back(listObj);
            }
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
            // i++;
            if (stack.size() >= static_cast<size_t>(arg * 2)) {
                proto::ProtoObject* mapObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                const proto::ProtoSparseList* data = ctx->newSparseList();
                const proto::ProtoList* keys = ctx->newList();
                for (int j = 0; j < arg; ++j) {
                    const proto::ProtoObject* value = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* key = stack.back();
                    stack.pop_back();
                    unsigned long h = key->getHash(ctx);
                    if (get_env_diag()) {
                        std::string kS = "other";
                        if (key->isString(ctx)) {
                            std::string ksRaw;
                            key->asString(ctx)->toUTF8String(ctx, ksRaw);
                            kS = "str(" + ksRaw + ")";
                        } else if (key->isInteger(ctx)) kS = "int(" + std::to_string(key->asLong(ctx)) + ")";
                        std::cerr << "[proto-engine] BUILD_MAP arg=" << arg << " j=" << j << " key=" << kS << " hash=" << h << " val=" << value << "\n" << std::flush;
                    }
                    data = data->setAt(ctx, h, value);
                    keys = keys->appendLast(ctx, key);
                }
                mapObj = const_cast<proto::ProtoObject*>(mapObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), data->asObject(ctx)));
                mapObj = const_cast<proto::ProtoObject*>(mapObj->setAttribute(ctx, env ? env->getKeysString() : proto::ProtoString::fromUTF8String(ctx, "__keys__"), keys->asObject(ctx)));
                
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                if (env && env->getDictPrototype()) {
                    mapObj = const_cast<proto::ProtoObject*>(mapObj->addParent(ctx, env->getDictPrototype()));
                    mapObj = const_cast<proto::ProtoObject*>(mapObj->setAttribute(ctx, env->getClassString(), env->getDictPrototype()));
                }
                
                stack.push_back(mapObj);
            }
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
            // i++;
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

            const proto::ProtoObject* result = invokeCallable(ctx, callable, plArgs, kwMap);
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (result) {
                stack.push_back(result);
            } else if (env && env->hasPendingException()) {
                return PROTO_NONE;
            } else {
                stack.push_back(PROTO_NONE);
            }
        } else if (op == OP_CALL_FUNCTION) {
            // i++;
            if (stack.size() < static_cast<size_t>(arg) + 1) continue;
            std::vector<const proto::ProtoObject*> argVec(arg);
            for (int j = arg - 1; j >= 0; --j) {
                argVec[j] = stack.back();
                stack.pop_back();
            }
            const proto::ProtoObject* callable = stack.back();
            stack.pop_back();
            const proto::ProtoList* args = ctx->newList();
            for (int j = 0; j < arg; ++j)
                args = args->appendLast(ctx, argVec[j]);
            const proto::ProtoObject* result = invokeCallable(ctx, callable, args);
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (result) {
                stack.push_back(result);
            } else if (env && env->hasPendingException()) {
                // If it returns nullptr and has exception, it's an error.
                return PROTO_NONE;
            } else {
                // Return value was None (nullptr) but no exception.
                stack.push_back(PROTO_NONE);
            }
        } else if (op == OP_BUILD_TUPLE) {
            // i++;
            if (stack.size() >= static_cast<size_t>(arg)) {
                std::vector<const proto::ProtoObject*> elems(arg);
                for (int j = arg - 1; j >= 0; --j) {
                    elems[j] = stack.back();
                    stack.pop_back();
                }
                const proto::ProtoList* lst = ctx->newList();
                for (int j = 0; j < arg; ++j)
                    lst = lst->appendLast(ctx, elems[j]);
                const proto::ProtoTuple* tup = ctx->newTupleFromList(lst);
                if (tup) {
                    proto::ProtoObject* tupObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
                    tupObj = const_cast<proto::ProtoObject*>(tupObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), tup->asObject(ctx)));
                    if (env && env->getTuplePrototype()) {
                        tupObj = const_cast<proto::ProtoObject*>(tupObj->addParent(ctx, env->getTuplePrototype()));
                        tupObj->setAttribute(ctx, env->getClassString(), env->getTuplePrototype());
                    }
                    stack.push_back(tupObj);
                }
            }
        } else if (op == OP_BUILD_FUNCTION) {
            // i++;
            if (!stack.empty() && frame) {
                const proto::ProtoObject* codeObj = stack.back();
                stack.pop_back();
                proto::ProtoObject* fn = createUserFunction(ctx, codeObj, const_cast<proto::ProtoObject*>(PythonEnvironment::getCurrentGlobals()), frame);
                if (fn) stack.push_back(fn);
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
                if (env && env->getTypePrototype()) {
                    targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, env->getClassString(), env->getTypePrototype()));
                }
                
                if (bases && bases->asTuple(ctx)) {
                    auto bt = bases->asTuple(ctx);
                    for (size_t j = 0; j < bt->getSize(ctx); ++j) {
                        targetClass = const_cast<proto::ProtoObject*>(targetClass->addParent(ctx, bt->getAt(ctx, j)));
                    }
                }
                
                // Copy ns attributes to targetClass
                const proto::ProtoSparseList* keys = ns->getAttributes(ctx);
                if (keys) {
                    auto it = keys->getIterator(ctx);
                    while (it && it->hasNext(ctx)) {
                        unsigned long key = it->nextKey(ctx);
                        const proto::ProtoObject* keyObj = reinterpret_cast<const proto::ProtoObject*>(key);
                        if (keyObj && keyObj->isString(ctx)) {
                            const proto::ProtoString* k = keyObj->asString(ctx);
                            if (env && (k == env->getFBackString() || k == env->getFLocalsString() || k == env->getFGlobalsString() || k == env->getFCodeString())) {
                                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                                continue;
                            }
                            const proto::ProtoObject* attrVal = ns->getAttribute(ctx, k);
                            targetClass = const_cast<proto::ProtoObject*>(targetClass->setAttribute(ctx, k, attrVal));
                        }
                        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                    }
                }
                
                // Set __call__ to support instantiation
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
                if (get_env_diag()) std::cerr << "[proto-exec-diag] OP_GET_ITER NO __iter__ for " << iterable << ", pushing self as fallback\n";
                stack.push_back(iterable);
                continue;
            }
            const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, emptyL, nullptr);
            if (it) {
                if (get_env_diag()) std::cerr << "[proto-exec-diag] OP_GET_ITER created iterator=" << it << " for iterable=" << iterable << "\n";
                stack.push_back(it);
            } else {
                if (get_env_diag()) std::cerr << "[proto-exec-diag] OP_GET_ITER FAILED (returned null) for iterable=" << iterable << ", pushing self\n";
                stack.push_back(iterable);
            }
        } else if (op == OP_FOR_ITER) {
            // i++;
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
                        i = static_cast<unsigned long>(arg) - 1;
                 }
                 continue;
            }

            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(ctx, "__next__");
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
            
            const proto::ProtoObject* nextM = env ? env->getAttribute(ctx, iterator, nextS) : iterator->getAttribute(ctx, nextS);
            if (get_env_diag()) {
                std::string ns; nextS->toUTF8String(ctx, ns);
                if (get_env_diag()) {
                    std::cerr << "[proto-exec-diag] OP_FOR_ITER iterator=" << iterator << " lookup " << ns << " (" << nextS << ") found nextM=" << nextM << "\n";
                }
            }
            if (!nextM || !nextM->asMethod(ctx)) {
                if (get_env_diag()) std::cerr << "[proto-exec-diag] OP_FOR_ITER NO NEXT METHOD, jumping to " << arg << "\n";
                stack.pop_back();
                if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                    i = static_cast<unsigned long>(arg) - 1;
                continue;
            }
            
            const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, iterator, nullptr, emptyL, nullptr);
            if (val && val != (env ? env->getNonePrototype() : nullptr)) {
                if (get_env_diag()) std::cerr << "[proto-exec-diag] OP_FOR_ITER got val=" << val << "\n";
                stack.push_back(val);
            } else {
                if (get_env_diag()) std::cerr << "[proto-exec-diag] OP_FOR_ITER exhausted\n";
                // nullptr or None returned by native __next__ signals exhaustion in protoPython protocol
                stack.pop_back();
                if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                    i = static_cast<unsigned long>(arg) - 1;
            }
        } else if (op == OP_UNPACK_SEQUENCE) {
            // i++;
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
                if (static_cast<int>(list->getSize(ctx)) < arg) continue;
                for (int j = arg - 1; j >= 0; --j)
                    stack.push_back(list->getAt(ctx, j));
            } else if (tup) {
                if (static_cast<int>(tup->getSize(ctx)) < arg) continue;
                for (int j = arg - 1; j >= 0; --j)
                    stack.push_back(tup->getAt(ctx, j));
            }
        } else if (op == OP_LOAD_GLOBAL) {
            // i++;
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
                if (nameObj->isString(ctx)) {
                    std::string name; nameObj->asString(ctx)->toUTF8String(ctx, name);
                    if (name == "range") {
                         std::cerr << "DEBUG: OP_LOAD_GLOBAL range lookup frame=" << frame << "\n";
                    }
                    const proto::ProtoObject* val = frame->getAttribute(ctx, nameObj->asString(ctx));
                    if (val) {
                        if (name == "range") { /* found */ }
                        stack.push_back(val);
                    } else {
                        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                        if (env) {
                            std::string name;
                            nameObj->asString(ctx)->toUTF8String(ctx, name);
                            val = env->resolve(name, ctx);
                            if (val) {
                                if (name == "range") { /* found */ }
                                stack.push_back(val);
                            } else {
                                env->raiseNameError(ctx, name);
                                return PROTO_NONE;
                            }
                        } else {
                            return PROTO_NONE;
                        }
                    }
                }
            }
        } else if (op == OP_STORE_GLOBAL) {
            // i++;
            if (names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
                if (stack.empty()) continue;
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
                    frame->setAttribute(ctx, nameObj->asString(ctx), PROTO_NONE);
                    // Also check __data__ if frame is a dict
                    const proto::ProtoObject* data = frame->getAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"));
                    if (data && data->asSparseList(ctx)) {
                        data->asSparseList(ctx)->removeAt(ctx, nameObj->getHash(ctx));
                    }
                }
            }
            if (env) env->invalidateResolveCache();
        } else if (op == OP_DELETE_FAST) {
            // i++;
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
                    obj->setAttribute(ctx, nameObj->asString(ctx), PROTO_NONE);
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
                invokeDunder(ctx, container, delItemS, args);
            }
        } else if (op == OP_SETUP_FINALLY) {
            blockStack.push_back({static_cast<unsigned long>(arg), stack.size()});
        } else if (op == OP_POP_BLOCK) {
            if (!blockStack.empty()) blockStack.pop_back();
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
    if (!ctx || !constants || !bytecode) return PROTO_NONE;
    unsigned long n = bytecode->getSize(ctx);
    return executeBytecodeRange(ctx, constants, bytecode, names, frame, 0, n ? n - 1 : 0);
}

} // namespace protoPython
