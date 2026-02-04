#include <protoPython/MathModule.h>
#include <cmath>

namespace protoPython {
namespace math {

static double toDouble(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (obj->isDouble(ctx)) return obj->asDouble(ctx);
    if (obj->isInteger(ctx)) return static_cast<double>(obj->asLong(ctx));
    return 0.0;
}

static const proto::ProtoObject* py_sqrt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x < 0.0) return PROTO_NONE;
    return ctx->fromDouble(std::sqrt(x));
}

static const proto::ProtoObject* py_sin(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::sin(x));
}

static const proto::ProtoObject* py_cos(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::cos(x));
}

static const proto::ProtoObject* py_tan(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::tan(x));
}

static const proto::ProtoObject* py_asin(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::asin(x));
}

static const proto::ProtoObject* py_acos(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::acos(x));
}

static const proto::ProtoObject* py_atan(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::atan(x));
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

static const proto::ProtoObject* py_copysign(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::copysign(x, y));
}

static const proto::ProtoObject* py_isclose(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_FALSE;
    double a = toDouble(ctx, posArgs->getAt(ctx, 0));
    double b = toDouble(ctx, posArgs->getAt(ctx, 1));
    double rel_tol = 1e-09, abs_tol = 0.0;
    if (posArgs->getSize(ctx) >= 3) rel_tol = toDouble(ctx, posArgs->getAt(ctx, 2));
    if (posArgs->getSize(ctx) >= 4) abs_tol = toDouble(ctx, posArgs->getAt(ctx, 3));
    if (std::isnan(a) && std::isnan(b)) return PROTO_TRUE;
    if (std::isnan(a) || std::isnan(b)) return PROTO_FALSE;
    if (std::isinf(a) && std::isinf(b)) return (a > 0) == (b > 0) ? PROTO_TRUE : PROTO_FALSE;
    if (std::isinf(a) || std::isinf(b)) return PROTO_FALSE;
    double diff = std::abs(a - b);
    return (diff <= rel_tol * std::max(std::abs(a), std::abs(b)) || diff <= abs_tol) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_isinf(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return std::isinf(x) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_isfinite(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return std::isfinite(x) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_isnan(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return std::isnan(x) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_log(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= 0.0) return PROTO_NONE;
    if (posArgs->getSize(ctx) >= 2) {
        double base = toDouble(ctx, posArgs->getAt(ctx, 1));
        if (base <= 0.0 || base == 1.0) return PROTO_NONE;
        return ctx->fromDouble(std::log(x) / std::log(base));
    }
    return ctx->fromDouble(std::log(x));
}

static const proto::ProtoObject* py_log10(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= 0.0) return PROTO_NONE;
    return ctx->fromDouble(std::log10(x));
}

static const proto::ProtoObject* py_exp(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::exp(x));
}

static long long gcd_impl(long long a, long long b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b != 0) { long long t = b; b = a % b; a = t; }
    return a;
}

static const proto::ProtoObject* py_gcd(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx);
    return ctx->fromInteger(gcd_impl(a, b));
}

static const proto::ProtoObject* py_lcm(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx);
    long long g = gcd_impl(a, b);
    if (g == 0) return ctx->fromInteger(0);
    long long ax = a < 0 ? -a : a;
    long long bx = b < 0 ? -b : b;
    return ctx->fromInteger(ax / g * bx);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sqrt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sqrt));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sin"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sin));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "cos"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_cos));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "tan"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_tan));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "asin"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_asin));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "acos"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_acos));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "atan"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_atan));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "floor"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_floor));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ceil"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_ceil));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "fabs"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fabs));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "trunc"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_trunc));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "copysign"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_copysign));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isclose"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isclose));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isinf"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isinf));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isfinite"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isfinite));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isnan"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isnan));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "log"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "log10"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log10));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exp"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_exp));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "gcd"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_gcd));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "lcm"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lcm));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pi"),
        ctx->fromDouble(3.14159265358979323846));
    return mod;
}

} // namespace math
} // namespace protoPython
