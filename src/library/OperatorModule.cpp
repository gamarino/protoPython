#include <protoPython/OperatorModule.h>
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
    return mod;
}

} // namespace operator_
} // namespace protoPython
