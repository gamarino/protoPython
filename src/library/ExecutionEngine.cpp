#include <protoPython/ExecutionEngine.h>
#include <protoPython/Compiler.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/MemoryManager.hpp>
#include <protoCore.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace protoPython {

namespace {
/** __call__ for user-defined functions: push context (RAII), build frame, run __code__, promote return value.
 * Reads co_varnames, co_nparams, co_automatic_count from code object to size automatic slots and bind args. */
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

    const proto::ProtoList* parameterNames = nullptr;
    const proto::ProtoList* localNames = nullptr;
    if (co_varnames && automatic_count > 0 && static_cast<unsigned long>(automatic_count) <= co_varnames->getSize(ctx)) {
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

    if (calleeCtx->getAutomaticLocalsCount() > 0 && args) {
        unsigned long argCount = args->getSize(calleeCtx);
        unsigned int nSlots = calleeCtx->getAutomaticLocalsCount();
        unsigned long toCopy = (argCount < static_cast<unsigned long>(nparams)) ? argCount : static_cast<unsigned long>(nparams);
        if (toCopy > nSlots) toCopy = nSlots;
        proto::ProtoObject** slots = const_cast<proto::ProtoObject**>(calleeCtx->getAutomaticLocals());
        for (unsigned long i = 0; i < toCopy; ++i)
            slots[i] = const_cast<proto::ProtoObject*>(args->getAt(calleeCtx, static_cast<int>(i)));
    }

    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(calleeCtx->newObject(true));
    if (!frame) return PROTO_NONE;
    frame->addParent(ctx, globalsObj);
    const proto::ProtoObject* result = runCodeObject(calleeCtx, codeObj, frame);
    promote(calleeCtx, result);
    return result;
}

/** Create a callable object with __code__, __globals__, and __call__. */
static proto::ProtoObject* createUserFunction(proto::ProtoContext* ctx, const proto::ProtoObject* codeObj, proto::ProtoObject* globalsFrame) {
    if (!ctx || !codeObj || !globalsFrame) return nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* fn = ctx->newObject(true);
    fn = fn->setAttribute(ctx, env ? env->getCodeString() : proto::ProtoString::fromUTF8String(ctx, "__code__"), codeObj);
    fn = fn->setAttribute(ctx, env ? env->getGlobalsString() : proto::ProtoString::fromUTF8String(ctx, "__globals__"), globalsFrame);
    fn = fn->setAttribute(ctx, env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(fn), runUserFunctionCall));
    return const_cast<proto::ProtoObject*>(fn);
}
} // anonymous namespace

/** Return true if obj is an embedded value (e.g. small int, bool); do not call getAttribute on it. */
static bool isEmbeddedValue(const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    constexpr unsigned long POINTER_TAG_EMBEDDED_VALUE = 1;
    return (reinterpret_cast<uintptr_t>(obj) & 0x3FUL) == POINTER_TAG_EMBEDDED_VALUE;
}

static const proto::ProtoObject* binaryAdd(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return ctx->fromInteger(a->asLong(ctx) + b->asLong(ctx));
    if (a->isString(ctx) && b->isString(ctx)) {
        std::string sa, sb;
        a->asString(ctx)->toUTF8String(ctx, sa);
        b->asString(ctx)->toUTF8String(ctx, sb);
        return ctx->fromUTF8String((sa + sb).c_str());
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* binarySubtract(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return ctx->fromInteger(a->asLong(ctx) - b->asLong(ctx));
    return PROTO_NONE;
}

static const proto::ProtoObject* binaryMultiply(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* r = a->multiply(ctx, b);
    return r ? r : PROTO_NONE;
}

static const proto::ProtoObject* binaryTrueDivide(proto::ProtoContext* ctx,
    const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if ((b->isInteger(ctx) && b->asLong(ctx) == 0) || (b->isDouble(ctx) && b->asDouble(ctx) == 0.0)) {
        PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
        return PROTO_NONE;
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
    if ((b->isInteger(ctx) && b->asLong(ctx) == 0) || (b->isDouble(ctx) && b->asDouble(ctx) == 0.0)) {
        PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
        return PROTO_NONE;
    }
    if (a->isInteger(ctx) && b->isInteger(ctx)) {
        return ctx->fromInteger(a->asLong(ctx) % b->asLong(ctx));
    }
    if (a->isDouble(ctx) || b->isDouble(ctx)) {
        double aa = a->isDouble(ctx) ? a->asDouble(ctx) : static_cast<double>(a->asLong(ctx));
        double bb = b->isDouble(ctx) ? b->asDouble(ctx) : static_cast<double>(b->asLong(ctx));
        return ctx->fromDouble(std::fmod(aa, bb));
    }
    return PROTO_NONE;
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
    if ((b->isInteger(ctx) && b->asLong(ctx) == 0) || (b->isDouble(ctx) && b->asDouble(ctx) == 0.0)) {
        PythonEnvironment::fromContext(ctx)->raiseZeroDivisionError(ctx);
        return PROTO_NONE;
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
    int c = a->compare(ctx, b);
    if (op == 0) result = (c == 0);
    else if (op == 1) result = (c != 0);
    else if (op == 2) result = (c < 0);
    else if (op == 3) result = (c <= 0);
    else if (op == 4) result = (c > 0);
    else if (op == 5) result = (c >= 0);
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
    if (callable->asMethod(ctx)) {
        return callable->asMethod(ctx)(ctx, callable, nullptr, args, kwargs);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* callAttr = callable->getAttribute(ctx, env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__"));
    if (!callAttr || !callAttr->asMethod(ctx)) return PROTO_NONE;
    return callAttr->asMethod(ctx)(ctx, callable, nullptr, args, kwargs);
}

const proto::ProtoObject* invokePythonCallable(proto::ProtoContext* ctx,
    const proto::ProtoObject* callable, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return invokeCallable(ctx, callable, args, kwargs);
}

const proto::ProtoObject* executeBytecodeRange(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode,
    const proto::ProtoList* names,
    proto::ProtoObject* frame,
    unsigned long pcStart,
    unsigned long pcEnd) {
    if (!ctx || !constants || !bytecode) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    unsigned long n = bytecode->getSize(ctx);
    if (n == 0) return PROTO_NONE;
    if (pcEnd >= n) pcEnd = n - 1;
    /* 64-byte aligned execution state to avoid false sharing when multiple threads run tasks. */
    alignas(64) std::vector<const proto::ProtoObject*> stack;
    stack.reserve(64);
    for (unsigned long i = pcStart; i < n; ++i) {
        if (i > pcEnd) break;
        const proto::ProtoObject* instr = bytecode->getAt(ctx, static_cast<int>(i));
        if (!instr->isInteger(ctx)) continue;
        int op = static_cast<int>(instr->asLong(ctx));
        int arg = (i + 1 < n && bytecode->getAt(ctx, static_cast<int>(i + 1))->isInteger(ctx))
            ? static_cast<int>(bytecode->getAt(ctx, static_cast<int>(i + 1))->asLong(ctx)) : 0;

        if (op == OP_LOAD_CONST) {
            i++;
            if (static_cast<unsigned long>(arg) < constants->getSize(ctx))
                stack.push_back(constants->getAt(ctx, arg));
        } else if (op == OP_RETURN_VALUE) {
            if (stack.empty()) return PROTO_NONE;
            const proto::ProtoObject* ret = stack.back();
            ctx->returnValue = ret;
            return ret;  /* exit block immediately; destructor will promote */
        } else if (op == OP_LOAD_NAME && names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            if (nameObj->isString(ctx)) {
                const proto::ProtoObject* val = frame->getAttribute(ctx, nameObj->asString(ctx));
                if (val && val != PROTO_NONE) {
                    stack.push_back(val);
                } else {
                    if (env) {
                        // Check if it's explicitly None in builtins
                        std::string name;
                        nameObj->asString(ctx)->toUTF8String(ctx, name);
                        val = env->resolve(name);
                        if (val) { // Now resolve returns the real None object for "None"
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
        } else if (op == OP_STORE_NAME && names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            const proto::ProtoObject* val = stack.back();
            stack.pop_back();
            if (nameObj->isString(ctx))
                frame->setAttribute(ctx, nameObj->asString(ctx), val);
        } else if (op == OP_LOAD_FAST) {
            i++;
            const unsigned int nSlots = ctx->getAutomaticLocalsCount();
            if (arg >= 0 && static_cast<unsigned long>(arg) < nSlots) {
                const proto::ProtoObject** slots = ctx->getAutomaticLocals();
                const proto::ProtoObject* val = slots[arg];
                if (val) stack.push_back(val);
            }
        } else if (op == OP_STORE_FAST) {
            i++;
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
            if (r) stack.push_back(r);
        } else if (op == OP_INPLACE_ADD) {
            i++;
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
                if (r) stack.push_back(r);
            }
        } else if (op == OP_BINARY_SUBTRACT) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binarySubtract(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_INPLACE_SUBTRACT) {
            i++;
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
                if (r) stack.push_back(r);
            }
        } else if (op == OP_BINARY_MULTIPLY) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryMultiply(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_INPLACE_MULTIPLY) {
            i++;
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
                if (r) stack.push_back(r);
            }
        } else if (op == OP_BINARY_TRUE_DIVIDE) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryTrueDivide(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_BINARY_MODULO) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryModulo(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_BINARY_POWER) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryPower(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_BINARY_FLOOR_DIVIDE) {
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = binaryFloorDivide(ctx, a, b);
            if (r) stack.push_back(r);
        } else if (op == OP_INPLACE_TRUE_DIVIDE) {
            i++;
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
                if (r) stack.push_back(r);
            }
        } else if (op == OP_INPLACE_FLOOR_DIVIDE) {
            i++;
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
                if (r) stack.push_back(r);
            }
        } else if (op == OP_INPLACE_MODULO) {
            i++;
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
                if (r) stack.push_back(r);
            }
        } else if (op == OP_INPLACE_POWER) {
            i++;
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
                if (r) stack.push_back(r);
            }
        } else if (op == OP_INPLACE_LSHIFT) {
            i++;
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
            i++;
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
            i++;
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
            i++;
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
            i++;
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
            i++;
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
            i++;
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
            i++;
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
            i++;
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
            i++;
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
            i++;
        } else if (op == OP_COMPARE_OP) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* b = stack.back();
            stack.pop_back();
            const proto::ProtoObject* a = stack.back();
            stack.pop_back();
            const proto::ProtoObject* r = compareOp(ctx, a, b, arg);
            if (r) stack.push_back(r);
        } else if (op == OP_POP_JUMP_IF_FALSE) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* top = stack.back();
            stack.pop_back();
            if (!isTruthy(ctx, top) && arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 1;
        } else if (op == OP_JUMP_ABSOLUTE) {
            i++;
            if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                i = static_cast<unsigned long>(arg) - 1;
        } else if (op == OP_LOAD_ATTR && names && stack.size() >= 1 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            const proto::ProtoObject* obj = stack.back();
            stack.pop_back();
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            if (nameObj->isString(ctx)) {
                const proto::ProtoObject* val = obj->getAttribute(ctx, nameObj->asString(ctx));
                if (val && val != PROTO_NONE) {
                    stack.push_back(val);
                } else {
                    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                    std::string attr;
                    nameObj->asString(ctx)->toUTF8String(ctx, attr);
                    if (env) env->raiseAttributeError(ctx, obj, attr);
                    return PROTO_NONE;
                }
            }
        } else if (op == OP_STORE_ATTR && names && stack.size() >= 2 && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            const proto::ProtoObject* val = stack.back();
            stack.pop_back();
            const proto::ProtoObject* obj = stack.back();
            stack.pop_back();
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            if (nameObj->isString(ctx)) {
                proto::ProtoObject* mutableObj = const_cast<proto::ProtoObject*>(obj);
                mutableObj->setAttribute(ctx, nameObj->asString(ctx), val);
            }
        } else if (op == OP_BUILD_LIST) {
            i++;
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
                listObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), lst->asObject(ctx));
                
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                if (env && env->getListPrototype()) {
                    listObj->addParent(ctx, env->getListPrototype());
                    listObj->setAttribute(ctx, env ? env->getClassString() : proto::ProtoString::fromUTF8String(ctx, "__class__"), env->getListPrototype());
                }
                
                stack.push_back(listObj);
            }
        } else if (op == OP_BINARY_SUBSCR) {
            i++;
            if (stack.size() < 2) continue;
            const proto::ProtoObject* key = stack.back();
            stack.pop_back();
            const proto::ProtoObject* container = stack.back();
            stack.pop_back();
            const proto::ProtoObject* getitem = container->getAttribute(ctx, env ? env->getGetItemString() : proto::ProtoString::fromUTF8String(ctx, "__getitem__"));
            if (getitem && getitem->asMethod(ctx)) {
                const proto::ProtoList* oneArg = ctx->newList()->appendLast(ctx, key);
                const proto::ProtoObject* result = getitem->asMethod(ctx)(ctx, container, nullptr, oneArg, nullptr);
                if (result) stack.push_back(result);
            } else {
                const proto::ProtoObject* data = container->getAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"));
                if (data && data->asList(ctx) && key->isInteger(ctx)) {
                    long long idx = key->asLong(ctx);
                    const proto::ProtoList* list = data->asList(ctx);
                    if (idx >= 0 && static_cast<unsigned long>(idx) < list->getSize(ctx)) {
                        stack.push_back(list->getAt(ctx, static_cast<int>(idx)));
                    }
                } else if (data && data->asSparseList(ctx)) {
                    unsigned long h = key->getHash(ctx);
                    const proto::ProtoObject* val = data->asSparseList(ctx)->getAt(ctx, h);
                    if (val) stack.push_back(val);
                }
            }
        } else if (op == OP_BUILD_MAP) {
            i++;
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
                    data = data->setAt(ctx, h, value);
                    keys = keys->appendLast(ctx, key);
                }
                mapObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), data->asObject(ctx));
                mapObj->setAttribute(ctx, env ? env->getKeysString() : proto::ProtoString::fromUTF8String(ctx, "__keys__"), keys->asObject(ctx));
                
                PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                if (env && env->getDictPrototype()) {
                    mapObj->addParent(ctx, env->getDictPrototype());
                    mapObj->setAttribute(ctx, env ? env->getClassString() : proto::ProtoString::fromUTF8String(ctx, "__class__"), env->getDictPrototype());
                }
                
                stack.push_back(mapObj);
            }
        } else if (op == OP_STORE_SUBSCR) {
            i++;
            if (stack.size() < 3) continue;
            const proto::ProtoObject* value = stack.back();
            stack.pop_back();
            const proto::ProtoObject* key = stack.back();
            stack.pop_back();
            proto::ProtoObject* container = const_cast<proto::ProtoObject*>(stack.back());
            stack.pop_back();
            const proto::ProtoObject* setitem = container->getAttribute(ctx, env ? env->getSetItemString() : proto::ProtoString::fromUTF8String(ctx, "__setitem__"));
            if (setitem && setitem->asMethod(ctx)) {
                const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, value);
                setitem->asMethod(ctx)(ctx, container, nullptr, args, nullptr);
            }
            /* When container has no __setitem__, subscript assignment is a no-op (e.g. minimal bytecode with BUILD_MAP/BUILD_LIST objects). */
        } else if (op == OP_CALL_FUNCTION_KW) {
            i++;
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
            if (result) stack.push_back(result);
            else return PROTO_NONE;
        } else if (op == OP_CALL_FUNCTION) {
            i++;
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
            if (result) {
                stack.push_back(result);
            } else {
                // If it returns nullptr, it must be an exception.
                // Standardized None is now a real object.
                return PROTO_NONE;
            }
        } else if (op == OP_BUILD_TUPLE) {
            i++;
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
                    proto::ProtoObject* tupObj = const_cast<proto::ProtoObject*>(tup->asObject(ctx));
                    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                    if (env && env->getTuplePrototype()) {
                        tupObj->addParent(ctx, env->getTuplePrototype());
                        tupObj->setAttribute(ctx, env ? env->getClassString() : proto::ProtoString::fromUTF8String(ctx, "__class__"), env->getTuplePrototype());
                    }
                    stack.push_back(tupObj);
                }
            }
        } else if (op == OP_BUILD_FUNCTION) {
            i++;
            if (!stack.empty() && frame) {
                const proto::ProtoObject* codeObj = stack.back();
                stack.pop_back();
                proto::ProtoObject* fn = createUserFunction(ctx, codeObj, frame);
                if (fn) stack.push_back(fn);
            }
        } else if (op == OP_GET_ITER) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* iterable = stack.back();
            stack.pop_back();
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(ctx, "__iter__");
            
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
            
            const proto::ProtoObject* iterM = iterable->getAttribute(ctx, iterS);
            if (!iterM || !iterM->asMethod(ctx)) continue;
            const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, emptyL, nullptr);
            if (it) stack.push_back(it);
        } else if (op == OP_FOR_ITER) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* iterator = stack.back();
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(ctx, "__next__");
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
            
            const proto::ProtoObject* nextM = iterator->getAttribute(ctx, nextS);
            if (!nextM || !nextM->asMethod(ctx)) {
                stack.pop_back();
                if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                    i = static_cast<unsigned long>(arg) - 1;
                continue;
            }
            
            const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, iterator, nullptr, emptyL, nullptr);
            if (val && val != (env ? env->getNonePrototype() : nullptr)) {
                stack.push_back(val);
            } else {
                // nullptr or None returned by native __next__ signals exhaustion in protoPython protocol
                stack.pop_back();
                if (arg >= 0 && static_cast<unsigned long>(arg) < n)
                    i = static_cast<unsigned long>(arg) - 1;
            }
        } else if (op == OP_UNPACK_SEQUENCE) {
            i++;
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
        } else if (op == OP_LOAD_GLOBAL && names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            if (nameObj->isString(ctx)) {
                const proto::ProtoObject* val = frame->getAttribute(ctx, nameObj->asString(ctx));
                if (val && val != PROTO_NONE) {
                    stack.push_back(val);
                } else {
                    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
                    if (env) {
                        std::string name;
                        nameObj->asString(ctx)->toUTF8String(ctx, name);
                        val = env->resolve(name);
                        if (val) {
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
        } else if (op == OP_STORE_GLOBAL && names && frame && static_cast<unsigned long>(arg) < names->getSize(ctx)) {
            i++;
            if (stack.empty()) continue;
            const proto::ProtoObject* nameObj = names->getAt(ctx, arg);
            const proto::ProtoObject* val = stack.back();
            stack.pop_back();
            if (nameObj->isString(ctx))
                frame->setAttribute(ctx, nameObj->asString(ctx), val);
        } else if (op == OP_BUILD_SLICE) {
            i++;
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
            sliceObj->setAttribute(ctx, env ? env->getStartString() : proto::ProtoString::fromUTF8String(ctx, "start"), startObj);
            sliceObj->setAttribute(ctx, env ? env->getStopString() : proto::ProtoString::fromUTF8String(ctx, "stop"), stopObj);
            sliceObj->setAttribute(ctx, env ? env->getStepString() : proto::ProtoString::fromUTF8String(ctx, "step"), stepObj);
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
        }
    }
    return stack.empty() ? PROTO_NONE : stack.back();
}

const proto::ProtoObject* executeMinimalBytecode(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode,
    const proto::ProtoList* names,
    proto::ProtoObject* frame) {
    if (!ctx || !bytecode) return PROTO_NONE;
    unsigned long n = bytecode->getSize(ctx);
    return executeBytecodeRange(ctx, constants, bytecode, names, frame, 0, n ? n - 1 : 0);
}

} // namespace protoPython
