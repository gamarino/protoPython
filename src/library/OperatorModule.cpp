#include <protoPython/OperatorModule.h>

namespace protoPython {
namespace operator_ {

#define BINARY_OP(name, op) \
static const proto::ProtoObject* py_##name( \
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*, \
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) { \
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE; \
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx); \
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx); \
    return ctx->fromInteger(static_cast<long long>(a op b)); \
}

BINARY_OP(add, +)
BINARY_OP(sub, -)
BINARY_OP(mul, *)

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
    return mod;
}

} // namespace operator_
} // namespace protoPython
