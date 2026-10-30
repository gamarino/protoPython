#include <protoPython/PythonEnvironment.h>
#include <protoPython/OperatorModule.h>
#include <protoPython/ExecutionEngine.h>
#include <cmath>
#include <string>

namespace protoPython {
namespace operator_ {

// operator.eq, ne, lt, le, gt and ge: `a <op> b`, with the comparison
// operators' semantics (rich comparison methods, NotImplemented, TypeError
// for unorderable operands). They used to convert both operands with
// asLong, which throws a C++ exception for anything but an int.
static const proto::ProtoObject* richCompare(proto::ProtoContext* ctx,
                                             const proto::ProtoArguments* posArgs, int op,
                                             const char* name) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!posArgs || posArgs->getSize(ctx) != 2) {
        if (env) env->raiseTypeError(ctx, std::string(name) + " expected 2 arguments, got "
            + std::to_string(posArgs ? posArgs->getSize(ctx) : 0));
        return nullptr;
    }
    if (!env) return PROTO_NONE;
    return env->compareObjects(ctx, posArgs->getAt(ctx, 0), posArgs->getAt(ctx, 1), op, /*richResult=*/true);
}

static const proto::ProtoObject* py_eq(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    return richCompare(ctx, posArgs, 0, "eq");
}

static const proto::ProtoObject* py_lt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    return richCompare(ctx, posArgs, 2, "lt");
}

static const proto::ProtoObject* py_le(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    return richCompare(ctx, posArgs, 3, "le");
}

static const proto::ProtoObject* py_gt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    return richCompare(ctx, posArgs, 4, "gt");
}

static const proto::ProtoObject* py_ge(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    return richCompare(ctx, posArgs, 5, "ge");
}

static const proto::ProtoObject* py_ne(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    return richCompare(ctx, posArgs, 1, "ne");
}

// operator.contains(a, b): `b in a`.
static const proto::ProtoObject* py_contains(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (!posArgs || posArgs->getSize(ctx) != 2) {
        if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx)) {
            env->raiseTypeError(ctx, "contains expected 2 arguments, got "
                + std::to_string(posArgs ? posArgs->getSize(ctx) : 0));
        }
        return nullptr;
    }
    return containsOperator(ctx, posArgs->getAt(ctx, 0), posArgs->getAt(ctx, 1));
}


// _compare_digest(a, b): a == b in time independent of where they differ
// (hmac.compare_digest).  Both ASCII str, or both bytes-like.
static bool digestOctets(proto::ProtoContext* ctx, PythonEnvironment* env,
                         const proto::ProtoObject* obj, std::string& out) {
    const proto::ProtoObject* asBytes = obj;
    const proto::ProtoObject* bytesType = env->getBytesPrototype();
    if (!bytesType || env->getType(ctx, obj) != bytesType) {
        asBytes = env->callObject(bytesType, {obj});
        if (!asBytes) return false;
    }
    const proto::ProtoObject* data = asBytes->getAttribute(ctx, env->getDataString());
    const proto::ProtoByteBuffer* bb = data ? data->asByteBuffer(ctx) : nullptr;
    if (!bb) return false;
    out.resize(bb->getSize(ctx));
    for (size_t i = 0; i < out.size(); ++i) out[i] = bb->getAt(ctx, static_cast<int>(i));
    return true;
}

static const proto::ProtoObject* py_compare_digest(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_FALSE;
    if (!args || args->getSize(ctx) != 2) {
        env->raiseTypeError(ctx, "_compare_digest expected 2 arguments");
        return nullptr;
    }
    const proto::ProtoObject* a = args->getAt(ctx, 0);
    const proto::ProtoObject* b = args->getAt(ctx, 1);
    std::string x, y;
    const bool aStr = a->isString(ctx), bStr = b->isString(ctx);
    if (aStr != bStr) {
        env->raiseTypeError(ctx, "unsupported operand types(s) or combination of types: '"
            + std::string(aStr ? "str" : "bytes") + "' and '" + std::string(bStr ? "str" : "bytes") + "'");
        return nullptr;
    }
    if (aStr) {
        a->asString(ctx)->toUTF8String(ctx, x);
        b->asString(ctx)->toUTF8String(ctx, y);
        for (const std::string* t : {&x, &y})
            for (unsigned char ch : *t)
                if (ch >= 0x80) {
                    env->raiseTypeError(ctx, "comparing strings with non-ASCII characters is not supported");
                    return nullptr;
                }
    } else if (!digestOctets(ctx, env, a, x) || !digestOctets(ctx, env, b, y)) {
        env->raiseTypeError(ctx, "unsupported operand types(s) or combination of types");
        return nullptr;
    }
    // Visit every byte of `y` whatever the lengths, as CPython does.
    unsigned char diff = x.size() == y.size() ? 0 : 1;
    const std::string& left = x.size() == y.size() ? x : y;
    for (size_t i = 0; i < y.size(); ++i)
        diff |= static_cast<unsigned char>(left[i] ^ y[i]);
    return diff == 0 ? PROTO_TRUE : PROTO_FALSE;
}

// The native _operator: the functions that match CPython's.  operator.py
// (CPython's own, loaded as `operator`) defines everything in Python with
// the language's operators and imports these over its definitions.  The
// native module used to be registered as `operator` too, shadowing
// operator.py with arithmetic that handled only ints, floats and strs
// (operator.add('', 0) returned None, operator.sub overflowed past 64 bits,
// lists, sets and user classes were not supported) and with itemgetter,
// attrgetter and methodcaller that had no repr, no pickling and ignored
// methodcaller's keyword arguments.
const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "eq"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_eq));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "lt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lt));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "le"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_le));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "gt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_gt));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ge"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_ge));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ne"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_ne));
    {
        const proto::ProtoObject* containsFn = ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_contains);
        mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "contains"), containsFn);
        mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__contains__"), containsFn);
    }
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_compare_digest"),
        ctx->fromMethod(nullptr, py_compare_digest));
    return mod;
}

} // namespace operator_
} // namespace protoPython
