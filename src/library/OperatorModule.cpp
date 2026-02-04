#include <protoPython/OperatorModule.h>
#include <cmath>
#include <string>

namespace protoPython {
namespace operator_ {

static double toDouble(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (obj->isDouble(ctx)) return obj->asDouble(ctx);
    if (obj->isInteger(ctx)) return static_cast<double>(obj->asLong(ctx));
    return 0.0;
}

static const proto::ProtoObject* py_add(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* b = posArgs->getAt(ctx, 1);
    if (a->isString(ctx) && b->isString(ctx)) {
        std::string sa, sb;
        a->asString(ctx)->toUTF8String(ctx, sa);
        b->asString(ctx)->toUTF8String(ctx, sb);
        return ctx->fromUTF8String((sa + sb).c_str());
    }
    if (a->isDouble(ctx) || b->isDouble(ctx))
        return ctx->fromDouble(toDouble(ctx, a) + toDouble(ctx, b));
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return ctx->fromInteger(a->asLong(ctx) + b->asLong(ctx));
    return PROTO_NONE;
}

#define BINARY_OP(name, op) \
static const proto::ProtoObject* py_##name( \
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*, \
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) { \
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE; \
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx); \
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx); \
    return ctx->fromInteger(static_cast<long long>(a op b)); \
}

BINARY_OP(sub, -)
BINARY_OP(mul, *)

static const proto::ProtoObject* py_truediv(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    double a = toDouble(ctx, posArgs->getAt(ctx, 0));
    double b = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(a / b);
}

static const proto::ProtoObject* py_eq(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx);
    return (a == b) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_lt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx);
    return (a < b) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_pow(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* b = posArgs->getAt(ctx, 1);
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
    double aa = toDouble(ctx, a);
    double bb = toDouble(ctx, b);
    return ctx->fromDouble(std::pow(aa, bb));
}

static const proto::ProtoObject* py_floordiv(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* b = posArgs->getAt(ctx, 1);
    if (b->isInteger(ctx) && b->asLong(ctx) == 0) return PROTO_NONE;
    if (b->isDouble(ctx) && b->asDouble(ctx) == 0.0) return PROTO_NONE;
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return ctx->fromInteger(a->asLong(ctx) / b->asLong(ctx));
    double aa = toDouble(ctx, a);
    double bb = toDouble(ctx, b);
    return ctx->fromInteger(static_cast<long long>(std::floor(aa / bb)));
}

static const proto::ProtoObject* py_mod(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* b = posArgs->getAt(ctx, 1);
    if (a->isInteger(ctx) && b->isInteger(ctx)) {
        long long bb = b->asLong(ctx);
        if (bb == 0) return PROTO_NONE;
        return ctx->fromInteger(a->asLong(ctx) % bb);
    }
    double aa = toDouble(ctx, a);
    double bb = toDouble(ctx, b);
    if (bb == 0.0) return PROTO_NONE;
    return ctx->fromDouble(std::fmod(aa, bb));
}

static const proto::ProtoObject* py_neg(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    if (a->isInteger(ctx)) return ctx->fromInteger(-a->asLong(ctx));
    if (a->isDouble(ctx)) return ctx->fromDouble(-a->asDouble(ctx));
    return PROTO_NONE;
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

static const proto::ProtoObject* py_not_(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    return isTruthy(ctx, posArgs->getAt(ctx, 0)) ? PROTO_FALSE : PROTO_TRUE;
}

static const proto::ProtoObject* py_invert(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    if (a->isInteger(ctx)) {
        long long n = a->asLong(ctx);
        return ctx->fromInteger(static_cast<long long>(~static_cast<unsigned long long>(n)));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_lshift(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx);
    if (b < 0 || b >= 64) return ctx->fromInteger(0);
    return ctx->fromInteger(static_cast<long long>(static_cast<unsigned long long>(a) << b));
}

static const proto::ProtoObject* py_rshift(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx);
    if (b < 0 || b >= 64) return ctx->fromInteger(0);
    return ctx->fromInteger(static_cast<long long>(a >> b));
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "add"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_add));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sub"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sub));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "mul"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_mul));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "eq"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_eq));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "lt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lt));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "truediv"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_truediv));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pow"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_pow));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "floordiv"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_floordiv));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "mod"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_mod));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "neg"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_neg));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "not_"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_not_));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "invert"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_invert));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "lshift"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lshift));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "rshift"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_rshift));
    return mod;
}

} // namespace operator_
} // namespace protoPython
