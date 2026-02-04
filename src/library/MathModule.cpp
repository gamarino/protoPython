#include <protoPython/MathModule.h>
#include <cmath>
#include <limits>

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

static const proto::ProtoObject* py_atan2(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    double y = toDouble(ctx, posArgs->getAt(ctx, 0));
    double x = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::atan2(y, x));
}

static const proto::ProtoObject* py_degrees(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(x * (180.0 / 3.14159265358979323846));
}

static const proto::ProtoObject* py_radians(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(x * (3.14159265358979323846 / 180.0));
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

static const proto::ProtoObject* py_log2(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= 0.0) return PROTO_NONE;
    return ctx->fromDouble(std::log2(x));
}

static const proto::ProtoObject* py_log1p(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= -1.0) return PROTO_NONE;
    return ctx->fromDouble(std::log1p(x));
}

static const proto::ProtoObject* py_hypot(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    double sum = 0.0;
    for (size_t k = 0; k < posArgs->getSize(ctx); k++)
        sum += toDouble(ctx, posArgs->getAt(ctx, k)) * toDouble(ctx, posArgs->getAt(ctx, k));
    return ctx->fromDouble(std::sqrt(sum));
}

static const proto::ProtoObject* py_fmod(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::fmod(x, y));
}

static const proto::ProtoObject* py_remainder(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::remainder(x, y));
}

static const proto::ProtoObject* py_erf(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::erf(x));
}

static const proto::ProtoObject* py_erfc(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::erfc(x));
}

static const proto::ProtoObject* py_gamma(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= 0.0 && std::floor(x) == x) return PROTO_NONE; /* non-positive integer: domain error */
    double r = std::tgamma(x);
    if (std::isnan(r) || std::isinf(r)) return PROTO_NONE;
    return ctx->fromDouble(r);
}

static const proto::ProtoObject* py_lgamma(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= 0.0 && std::floor(x) == x) return PROTO_NONE; /* non-positive integer: domain error */
    double r = std::lgamma(x);
    if (std::isnan(r) || std::isinf(r)) return PROTO_NONE;
    return ctx->fromDouble(r);
}

static const proto::ProtoObject* py_exp(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::exp(x));
}

static const proto::ProtoObject* py_dist(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* pa = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* pb = posArgs->getAt(ctx, 1);
    const proto::ProtoObject* da = pa->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
    const proto::ProtoObject* db = pb->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
    if (!da || !db || !da->asList(ctx) || !db->asList(ctx)) return PROTO_NONE;
    const proto::ProtoList* la = da->asList(ctx);
    const proto::ProtoList* lb = db->asList(ctx);
    size_t na = static_cast<size_t>(la->getSize(ctx));
    size_t nb = static_cast<size_t>(lb->getSize(ctx));
    size_t n = (na < nb) ? na : nb;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double a = toDouble(ctx, la->getAt(ctx, static_cast<int>(i)));
        double b = toDouble(ctx, lb->getAt(ctx, static_cast<int>(i)));
        double d = a - b;
        sum += d * d;
    }
    return ctx->fromDouble(std::sqrt(sum));
}

static const proto::ProtoObject* py_perm(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long n = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long k = posArgs->getAt(ctx, 1)->asLong(ctx);
    if (k < 0 || n < 0 || k > n) return ctx->fromInteger(0);
    long long r = 1;
    for (long long i = n - k + 1; i <= n; ++i) r *= i;
    return ctx->fromInteger(r);
}

static const proto::ProtoObject* py_comb(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long n = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long k = posArgs->getAt(ctx, 1)->asLong(ctx);
    if (k < 0 || n < 0 || k > n) return ctx->fromInteger(0);
    if (k > n - k) k = n - k;
    long long r = 1;
    for (long long i = 1; i <= k; ++i) r = r * (n - k + i) / i;
    return ctx->fromInteger(r);
}

static const proto::ProtoObject* py_factorial(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    long long n = posArgs->getAt(ctx, 0)->asLong(ctx);
    if (n < 0) return PROTO_NONE;
    if (n == 0) return ctx->fromInteger(1);
    long long r = 1;
    for (long long i = 2; i <= n; ++i) r *= i;
    return ctx->fromInteger(r);
}

static const proto::ProtoObject* py_prod(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
    double result = 1.0;
    const proto::ProtoObject* da = iterable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
    if (!da || !da->asList(ctx)) return PROTO_NONE;
    const proto::ProtoList* list = da->asList(ctx);
    for (int i = 0, sz = list->getSize(ctx); i < sz; ++i)
        result *= toDouble(ctx, list->getAt(ctx, i));
    return ctx->fromDouble(result);
}

static const proto::ProtoObject* py_sumprod(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* b = posArgs->getAt(ctx, 1);
    const proto::ProtoObject* da = a->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
    const proto::ProtoObject* db = b->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
    if (!da || !db || !da->asList(ctx) || !db->asList(ctx)) return PROTO_NONE;
    const proto::ProtoList* la = da->asList(ctx);
    const proto::ProtoList* lb = db->asList(ctx);
    size_t na = static_cast<size_t>(la->getSize(ctx));
    size_t nb = static_cast<size_t>(lb->getSize(ctx));
    size_t n = (na < nb) ? na : nb;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double x = toDouble(ctx, la->getAt(ctx, static_cast<int>(i)));
        double y = toDouble(ctx, lb->getAt(ctx, static_cast<int>(i)));
        sum += x * y;
    }
    return ctx->fromDouble(sum);
}

static const proto::ProtoObject* py_isqrt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    long long n = posArgs->getAt(ctx, 0)->asLong(ctx);
    if (n < 0) return PROTO_NONE;
    long long r = static_cast<long long>(std::sqrt(static_cast<double>(n)));
    if (r * r > n) --r;
    return ctx->fromInteger(r);
}

static const proto::ProtoObject* py_acosh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x < 1.0) return PROTO_NONE;
    return ctx->fromDouble(std::acosh(x));
}

static const proto::ProtoObject* py_asinh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::asinh(x));
}

static const proto::ProtoObject* py_atanh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= -1.0 || x >= 1.0) return PROTO_NONE;
    return ctx->fromDouble(std::atanh(x));
}

static const proto::ProtoObject* py_cosh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::cosh(x));
}

static const proto::ProtoObject* py_sinh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::sinh(x));
}

static const proto::ProtoObject* py_tanh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::tanh(x));
}

static const proto::ProtoObject* py_ulp(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (std::isnan(x)) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    if (std::isinf(x)) return ctx->fromDouble(std::numeric_limits<double>::infinity());
    if (x == 0.0) return ctx->fromDouble(std::numeric_limits<double>::denorm_min());
    double next = std::nextafter(x, (x > 0 ? 1.0 : -1.0) * std::numeric_limits<double>::infinity());
    return ctx->fromDouble(std::abs(next - x));
}

static const proto::ProtoObject* py_nextafter(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::nextafter(x, y));
}

static const proto::ProtoObject* py_ldexp(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    int exp = static_cast<int>(posArgs->getAt(ctx, 1)->asLong(ctx));
    return ctx->fromDouble(std::ldexp(x, exp));
}

static const proto::ProtoObject* py_frexp(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    int exp = 0;
    double mantissa = std::frexp(x, &exp);
    const proto::ProtoList* lst = ctx->newList()
        ->appendLast(ctx, ctx->fromDouble(mantissa))
        ->appendLast(ctx, ctx->fromInteger(exp));
    const proto::ProtoTuple* tup = ctx->newTupleFromList(lst);
    return tup ? tup->asObject(ctx) : PROTO_NONE;
}

static const proto::ProtoObject* py_modf(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double intpart = 0.0;
    double frac = std::modf(x, &intpart);
    const proto::ProtoList* lst = ctx->newList()
        ->appendLast(ctx, ctx->fromDouble(frac))
        ->appendLast(ctx, ctx->fromDouble(intpart));
    const proto::ProtoTuple* tup = ctx->newTupleFromList(lst);
    return tup ? tup->asObject(ctx) : PROTO_NONE;
}

static const proto::ProtoObject* py_cbrt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::cbrt(x));
}

static const proto::ProtoObject* py_exp2(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::exp2(x));
}

static const proto::ProtoObject* py_expm1(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::expm1(x));
}

static const proto::ProtoObject* py_fma(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 3) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    double z = toDouble(ctx, posArgs->getAt(ctx, 2));
    return ctx->fromDouble(std::fma(x, y, z));
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
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "atan2"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_atan2));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "degrees"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_degrees));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "radians"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_radians));
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
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "log2"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log2));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "log1p"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log1p));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hypot"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_hypot));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "fmod"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fmod));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "remainder"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_remainder));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "erf"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_erf));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "erfc"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_erfc));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "gamma"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_gamma));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "lgamma"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lgamma));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exp"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_exp));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dist"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_dist));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "perm"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_perm));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "comb"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_comb));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "factorial"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_factorial));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "prod"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_prod));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sumprod"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sumprod));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isqrt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isqrt));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "acosh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_acosh));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "asinh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_asinh));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "atanh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_atanh));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "cosh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_cosh));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sinh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sinh));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "tanh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_tanh));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ulp"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_ulp));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "nextafter"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_nextafter));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ldexp"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_ldexp));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "frexp"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_frexp));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modf"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_modf));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "cbrt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_cbrt));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exp2"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_exp2));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "expm1"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_expm1));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "fma"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fma));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "gcd"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_gcd));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "lcm"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lcm));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pi"),
        ctx->fromDouble(3.14159265358979323846));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "e"),
        ctx->fromDouble(2.71828182845904523536));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "nan"),
        ctx->fromDouble(std::numeric_limits<double>::quiet_NaN()));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "inf"),
        ctx->fromDouble(std::numeric_limits<double>::infinity()));
    return mod;
}

} // namespace math
} // namespace protoPython
