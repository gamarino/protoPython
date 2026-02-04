#include <protoPython/MathModule.h>
#include <cmath>

namespace protoPython {
namespace math {

static const proto::ProtoObject* py_sqrt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = static_cast<double>(posArgs->getAt(ctx, 0)->asLong(ctx));
    return ctx->fromDouble(std::sqrt(x));
}

static double toDouble(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (obj->isDouble(ctx)) return obj->asDouble(ctx);
    if (obj->isInteger(ctx)) return static_cast<double>(obj->asLong(ctx));
    return 0.0;
}

static const proto::ProtoObject* py_floor(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromInteger(static_cast<long long>(std::floor(x)));
}

static const proto::ProtoObject* py_ceil(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromInteger(static_cast<long long>(std::ceil(x)));
}

static const proto::ProtoObject* py_fabs(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::fabs(x));
}

static const proto::ProtoObject* py_trunc(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromInteger(static_cast<long long>(std::trunc(x)));
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sqrt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sqrt));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "floor"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_floor));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ceil"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_ceil));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "fabs"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fabs));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "trunc"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_trunc));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pi"),
        ctx->fromDouble(3.14159265358979323846));
    return mod;
}

} // namespace math
} // namespace protoPython
