#include <protoPython/MathModule.h>
#include <protoPython/PythonEnvironment.h>
#include <cmath>
#include <limits>

namespace protoPython {
namespace math {

// Raise math.ValueError(msg). The audit's #1 MathModule finding is
// that domain-error guards (sqrt(-1), log(0), acos(2), …) silently
// returned PROTO_NONE — the canonical "where did this NoneType come
// from" pattern. This helper packages the standard math-domain
// signal so each guard becomes a one-liner.
static const proto::ProtoObject* raise_math_domain(proto::ProtoContext* ctx, const char* msg) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) env->raiseValueError(ctx,
        PythonEnvironment::getInternedString(ctx, msg)->asObject(ctx));
    return nullptr;
}

static long long getLongSafe(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

static double toDouble(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return 0.0;
    if (obj->isDouble(ctx)) return obj->asDouble(ctx);
    if (obj->isInteger(ctx)) {
        try {
            return static_cast<double>(obj->asLong(ctx));
        } catch (...) {
            return 0.0;
        }
    }
    /* Handle Python-style __data__ wrapper (e.g. float/double stored in __data__) */
    const proto::ProtoObject* data = obj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"));
    if (data && data != PROTO_NONE) {
        if (data->isDouble(ctx)) return data->asDouble(ctx);
        if (data->isInteger(ctx)) {
            try {
                return static_cast<double>(data->asLong(ctx));
            } catch (...) {
                return 0.0;
            }
        }
    }
    return 0.0;
}

static const proto::ProtoObject* py_sqrt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x < 0.0) return raise_math_domain(ctx, "math domain error");
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
    if (x < -1.0 || x > 1.0) return raise_math_domain(ctx, "math domain error");
    return ctx->fromDouble(std::asin(x));
}

static const proto::ProtoObject* py_acos(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x < -1.0 || x > 1.0) return raise_math_domain(ctx, "math domain error");
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

// STRUCT-158/159/160: math.floor/ceil/trunc must first consult
// `type(arg).__floor__`/__ceil__/__trunc__ (Python special-method
// protocol — type-only lookup, instance overrides ignored).  Helper
// dispatches the dunder via env->getAttribute on the type; returns
// the dunder's result when honoured, else nullptr to let the caller
// fall back to its built-in numeric path.
static const proto::ProtoObject* mathDunderViaType(
    proto::ProtoContext* ctx, const proto::ProtoObject* arg, const char* dunderName) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
    if (!env || !arg) return nullptr;
    const proto::ProtoObject* argT = env->getType(ctx, arg);
    if (!argT) return nullptr;
    const proto::ProtoString* dS = protoPython::PythonEnvironment::getInternedString(ctx, dunderName);
    const proto::ProtoObject* m = env->getAttribute(ctx, argT, dS, /*raiseError=*/false);
    if (!m || m == PROTO_NONE) return nullptr;
    if (m->asMethod(ctx)) {
        return m->asMethod(ctx)(ctx,
            const_cast<proto::ProtoObject*>(arg), nullptr, ctx->newList(), nullptr);
    }
    return env->callObject(m, { arg });
}

static const proto::ProtoObject* py_floor(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* r = mathDunderViaType(ctx, arg, "__floor__");
    if (r) return r;
    double x = toDouble(ctx, arg);
    return ctx->fromInteger(static_cast<long long>(std::floor(x)));
}

static const proto::ProtoObject* py_ceil(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* r = mathDunderViaType(ctx, arg, "__ceil__");
    if (r) return r;
    double x = toDouble(ctx, arg);
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
    const proto::ProtoObject* arg = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* r = mathDunderViaType(ctx, arg, "__trunc__");
    if (r) return r;
    double x = toDouble(ctx, arg);
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
    if (x <= 0.0) return raise_math_domain(ctx, "math domain error");
    if (posArgs->getSize(ctx) >= 2) {
        double base = toDouble(ctx, posArgs->getAt(ctx, 1));
        if (base <= 0.0 || base == 1.0) return raise_math_domain(ctx, "math domain error");
        return ctx->fromDouble(std::log(x) / std::log(base));
    }
    return ctx->fromDouble(std::log(x));
}

static const proto::ProtoObject* py_log10(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    /* Fallback: first arg might be module (self), actual value at index 1. */
    if (x <= 0.0 && posArgs->getSize(ctx) >= 2) x = toDouble(ctx, posArgs->getAt(ctx, 1));
    /* Also try index 0 with getLongSafe for int-like values toDouble might miss. */
    if (x <= 0.0) {
        const proto::ProtoObject* a0 = posArgs->getAt(ctx, 0);
        if (a0 && a0 != PROTO_NONE) {
            long long n = getLongSafe(ctx, a0);
            if (n > 0) x = static_cast<double>(n);
        }
    }
    if (x <= 0.0) return raise_math_domain(ctx, "math domain error");
    return ctx->fromDouble(std::log10(x));
}

static const proto::ProtoObject* py_log2(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= 0.0) return raise_math_domain(ctx, "math domain error");
    return ctx->fromDouble(std::log2(x));
}

static const proto::ProtoObject* py_log1p(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= -1.0) return raise_math_domain(ctx, "math domain error");
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
    try {
        const proto::ProtoObject* pa = posArgs->getAt(ctx, 0);
        const proto::ProtoObject* pb = posArgs->getAt(ctx, 1);
        const proto::ProtoList* la = nullptr;
        const proto::ProtoList* lb = nullptr;
        const proto::ProtoObject* da = pa->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"));
        const proto::ProtoObject* db = pb->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"));
        if (da && da->asList(ctx)) la = da->asList(ctx);
        else if (pa->asList(ctx)) la = pa->asList(ctx);
        if (db && db->asList(ctx)) lb = db->asList(ctx);
        else if (pb->asList(ctx)) lb = pb->asList(ctx);
        if (!la || !lb) return PROTO_NONE;
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
    } catch (...) {
        return PROTO_NONE;
    }
}

static long long getLongSafe(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return 0;
    if (obj->isInteger(ctx)) {
        try { return obj->asLong(ctx); } catch (...) { return 0; }
    }
    const proto::ProtoObject* data = obj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"));
    if (data && data != PROTO_NONE && data->isInteger(ctx)) {
        try { return data->asLong(ctx); } catch (...) { return 0; }
    }
    return static_cast<long long>(toDouble(ctx, obj));
}

static const proto::ProtoObject* py_perm(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long n = getLongSafe(ctx, posArgs->getAt(ctx, 0));
    long long k = getLongSafe(ctx, posArgs->getAt(ctx, 1));
    if (k < 0 || n < 0 || k > n) return ctx->fromInteger(0);
    long long r = 1;
    for (long long i = n - k + 1; i <= n; ++i) r *= i;
    return ctx->fromInteger(r);
}

static const proto::ProtoObject* py_comb(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long n = getLongSafe(ctx, posArgs->getAt(ctx, 0));
    long long k = getLongSafe(ctx, posArgs->getAt(ctx, 1));
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
    const proto::ProtoObject* arg = posArgs->getAt(ctx, 0);
    // CPython rejects non-integer arguments with TypeError
    // ("factorial() only accepts integral values").  Without this
    // guard, asLong() on a float / str / etc. threw the protoCore
    // runtime_error and surfaced as `RuntimeError: internal C++
    // exception: Object is not an integer type.`
    if (!arg || (!arg->isInteger(ctx) && !arg->isBoolean(ctx))) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "factorial() only accepts integral values");
        return nullptr;
    }
    long long n = arg->asLong(ctx);
    if (n < 0) {
        return raise_math_domain(ctx,
            "factorial() not defined for negative values");
    }
    if (n == 0) return ctx->fromInteger(1);
    long long r = 1;
    for (long long i = 2; i <= n; ++i) {
        // Detect overflow: 64-bit factorial overflows at n=21. CPython
        // promotes to arbitrary-precision int; protoPython's primitive
        // long long carrier wraps silently, so raise OverflowError as
        // the explicit signal for now.
        if (r > std::numeric_limits<long long>::max() / i) {
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (env) env->raiseValueError(ctx,
                PythonEnvironment::getInternedString(ctx,
                    "factorial result exceeds 64-bit range")->asObject(ctx));
            return nullptr;
        }
        r *= i;
    }
    return ctx->fromInteger(r);
}

static const proto::ProtoObject* py_prod(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* da = iterable->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"));
    if (!da || !da->asList(ctx)) return PROTO_NONE;
    const proto::ProtoList* list = da->asList(ctx);
    int sz = static_cast<int>(list->getSize(ctx));

    // Integer fast path: as long as every element is an int (no
    // floats), accumulate as long long and return an integer. This
    // matches CPython's `math.prod` which promotes naturally — the
    // previous implementation accumulated as double, so
    // `math.prod([10**18, 10**18])` lost precision via 1e36 → double.
    long long iacc = 1;
    bool allInt = true;
    for (int i = 0; i < sz; ++i) {
        const proto::ProtoObject* el = list->getAt(ctx, i);
        if (!el || !el->isInteger(ctx)) { allInt = false; break; }
        long long v = el->asLong(ctx);
        // Overflow guard: detect before the multiply wraps. CPython
        // promotes to PyLong; we still raise so the caller sees an
        // explicit error rather than a silently wrong answer.
        if (v != 0 && std::abs(iacc) > std::numeric_limits<long long>::max() / std::abs(v)) {
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (env) env->raiseValueError(ctx,
                PythonEnvironment::getInternedString(ctx,
                    "prod() integer result exceeds 64-bit range")->asObject(ctx));
            return nullptr;
        }
        iacc *= v;
    }
    if (allInt) {
        // Honour optional `start` kwarg via second positional (rare in
        // user code; CPython reads the kwarg too — left for a follow-up).
        return ctx->fromInteger(iacc);
    }

    // Fallback: float accumulation when any element isn't an int.
    double dacc = 1.0;
    for (int i = 0; i < sz; ++i) {
        dacc *= toDouble(ctx, list->getAt(ctx, i));
    }
    return ctx->fromDouble(dacc);
}

static const proto::ProtoObject* py_sumprod(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* b = posArgs->getAt(ctx, 1);
    const proto::ProtoObject* da = a->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"));
    const proto::ProtoObject* db = b->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"));
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
    if (n < 0) {
        return raise_math_domain(ctx,
            "isqrt() argument must be nonnegative");
    }
    // Newton iteration: more accurate than sqrt(double) round-down for
    // n > 2^53 where double mantissa precision runs out. Same algorithm
    // CPython uses (PyLong_isqrt path).
    if (n < 2) return ctx->fromInteger(n);
    long long r = static_cast<long long>(std::sqrt(static_cast<double>(n)));
    while (r > 0 && r > n / r) {
        r = (r + n / r) / 2;
    }
    while ((r + 1) <= n / (r + 1)) ++r;
    if (r * r > n) --r;
    return ctx->fromInteger(r);
}

static const proto::ProtoObject* py_acosh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x < 1.0) return raise_math_domain(ctx, "math domain error");
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
    if (x <= -1.0 || x >= 1.0) return raise_math_domain(ctx, "math domain error");
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

// Python 3.9+: math.gcd / math.lcm accept any number of integer
// arguments (zero, one, or many), returning gcd(0, ...) = 0 and
// lcm(0, ...) = 0 / lcm() = 1. The previous implementation hard-
// coded `< 2`, so user code relying on the variadic form (e.g.
// gcd(a, b, c) or `gcd()`) silently returned PROTO_NONE → downstream
// "'NoneType' has no attribute X" — a pattern this audit specifically
// targets.
static const proto::ProtoObject* py_gcd(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    unsigned long n = posArgs ? posArgs->getSize(ctx) : 0;
    if (n == 0) return ctx->fromInteger(0);
    long long acc = posArgs->getAt(ctx, 0)->asLong(ctx);
    if (acc < 0) acc = -acc;
    for (unsigned long i = 1; i < n; ++i) {
        long long x = posArgs->getAt(ctx, static_cast<int>(i))->asLong(ctx);
        if (x < 0) x = -x;
        acc = gcd_impl(acc, x);
        if (acc == 1) break;  // terminate early — gcd can only stay 1
    }
    return ctx->fromInteger(acc);
}

static const proto::ProtoObject* py_lcm(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    unsigned long n = posArgs ? posArgs->getSize(ctx) : 0;
    if (n == 0) return ctx->fromInteger(1);
    long long acc = posArgs->getAt(ctx, 0)->asLong(ctx);
    if (acc < 0) acc = -acc;
    for (unsigned long i = 1; i < n; ++i) {
        long long x = posArgs->getAt(ctx, static_cast<int>(i))->asLong(ctx);
        if (x < 0) x = -x;
        if (acc == 0 || x == 0) { acc = 0; break; }
        long long g = gcd_impl(acc, x);
        // Overflow guard: acc/g * x can overflow long long for large
        // operands. Detect and report rather than wrap silently.
        if (acc / g > std::numeric_limits<long long>::max() / x) {
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (env) env->raiseValueError(ctx,
                PythonEnvironment::getInternedString(ctx,
                    "lcm result exceeds 64-bit range")->asObject(ctx));
            return nullptr;
        }
        acc = (acc / g) * x;
    }
    return ctx->fromInteger(acc);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "sqrt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sqrt));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "sin"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sin));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "cos"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_cos));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tan"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_tan));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "asin"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_asin));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "acos"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_acos));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "atan"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_atan));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "atan2"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_atan2));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "degrees"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_degrees));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "radians"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_radians));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "floor"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_floor));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ceil"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_ceil));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "fabs"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fabs));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "trunc"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_trunc));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "copysign"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_copysign));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "isclose"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isclose));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "isinf"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isinf));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "isfinite"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isfinite));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "isnan"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isnan));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "log"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "log10"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log10));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "log2"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log2));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "log1p"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log1p));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "hypot"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_hypot));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "fmod"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fmod));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "remainder"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_remainder));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "erf"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_erf));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "erfc"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_erfc));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "gamma"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_gamma));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "lgamma"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lgamma));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "exp"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_exp));
    /* Register dist after perm to avoid hash collision overwrite (if dist/perm collide, dist wins). */
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "perm"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_perm));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "dist"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_dist));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "comb"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_comb));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "factorial"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_factorial));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "prod"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_prod));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "sumprod"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sumprod));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "isqrt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isqrt));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "acosh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_acosh));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "asinh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_asinh));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "atanh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_atanh));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "cosh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_cosh));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "sinh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sinh));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tanh"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_tanh));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ulp"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_ulp));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "nextafter"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_nextafter));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ldexp"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_ldexp));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "frexp"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_frexp));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "modf"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_modf));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "cbrt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_cbrt));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "exp2"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_exp2));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "expm1"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_expm1));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "fma"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fma));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "gcd"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_gcd));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "lcm"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lcm));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pi"),
        ctx->fromDouble(3.14159265358979323846));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "e"),
        ctx->fromDouble(2.71828182845904523536));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tau"),
        ctx->fromDouble(6.28318530717958647692));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "nan"),
        ctx->fromDouble(std::numeric_limits<double>::quiet_NaN()));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "inf"),
        ctx->fromDouble(std::numeric_limits<double>::infinity()));
    return mod;
}

} // namespace math
} // namespace protoPython
