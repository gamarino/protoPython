#include <protoPython/MathModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/Tokenizer.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <limits>
#include <vector>

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

// A math function called with too few arguments raises TypeError (it
// answered None).
static const proto::ProtoObject* missingArgument(proto::ProtoContext* ctx) {
    if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx))
        env->raiseTypeError(ctx, "math function missing required argument");
    return nullptr;
}

// A real argument as a double, as CPython's math functions take it
// (PyFloat_AsDouble): an int is converted exactly rounded and raises
// OverflowError past the float range, an object goes through __float__ /
// __index__, anything else raises TypeError. (A big int used to read as 0.0.)
static double toDouble(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    return PythonEnvironment::realToDouble(ctx, obj);
}

// An int-valued argument (int, bool, int subclass), or nullptr.
static const proto::ProtoObject* intArgument(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return nullptr;
    if (obj == PROTO_TRUE) return ctx->fromInteger(1);
    if (obj == PROTO_FALSE) return ctx->fromInteger(0);
    if (obj->isInteger(ctx)) return obj;
    if (obj->isDouble(ctx) || obj->isString(ctx)) return nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* data = env ? obj->getAttribute(ctx, env->getDataString()) : nullptr;
    return (data && data->isInteger(ctx)) ? data : nullptr;
}

// int(x) for a float result of floor/ceil/trunc: exact at any magnitude.
static const proto::ProtoObject* integralDoubleToInt(proto::ProtoContext* ctx, double x) {
    return PythonEnvironment::integralDoubleToInt(ctx, x);
}

// CPython's loghelper: func(x) for a positive real; an int too large for a
// float is split as m * 2**e (m in [0.5, 1), correctly rounded), giving
// func(m) + func(2.0) * e.
static bool logHelper(proto::ProtoContext* ctx, const proto::ProtoObject* arg, double (*func)(double), double& out) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (const proto::ProtoObject* iv = intArgument(ctx, arg)) {
        if (iv->integerSign(ctx) <= 0) {
            if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "expected a positive input"));
            return false;
        }
        if (proto::isSmallInt(iv)) { out = func(static_cast<double>(iv->asLong(ctx))); return true; }
        std::string hex;
        iv->asIntegerString(ctx, 16)->toUTF8String(ctx, hex);
        for (char& c : hex) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const double m16 = std::strtod(("0x0." + hex).c_str(), nullptr);   // |n| / 16**len, rounded once
        int e2 = 0;
        const double m = std::frexp(m16, &e2);
        const long long e = static_cast<long long>(hex.size()) * 4 + e2;
        if (e <= 1024) {
            const double x = std::ldexp(m, static_cast<int>(e));
            if (std::isfinite(x)) { out = func(x); return true; }
        }
        out = func(m) + func(2.0) * static_cast<double>(e);
        return true;
    }
    const double x = toDouble(ctx, arg);
    if (!(x > 0.0) && !std::isnan(x)) {
        if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx,
            "expected a positive input, got " + PythonEnvironment::reprObject(ctx, ctx->fromDouble(x))));
        return false;
    }
    out = func(x);
    return true;
}

static double m_log(double x) { return std::log(x); }
static double m_log2(double x) { return std::log2(x); }
static double m_log10(double x) { return std::log10(x); }

static const proto::ProtoObject* py_sqrt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x < 0.0) return raise_math_domain(ctx, "math domain error");
    return ctx->fromDouble(std::sqrt(x));
}

static const proto::ProtoObject* py_sin(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::sin(x));
}

static const proto::ProtoObject* py_cos(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::cos(x));
}

static const proto::ProtoObject* py_tan(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::tan(x));
}

static const proto::ProtoObject* py_asin(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x < -1.0 || x > 1.0) return raise_math_domain(ctx, "math domain error");
    return ctx->fromDouble(std::asin(x));
}

static const proto::ProtoObject* py_acos(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x < -1.0 || x > 1.0) return raise_math_domain(ctx, "math domain error");
    return ctx->fromDouble(std::acos(x));
}

static const proto::ProtoObject* py_atan(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::atan(x));
}

static const proto::ProtoObject* py_atan2(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 2) return missingArgument(ctx);
    double y = toDouble(ctx, posArgs->getAt(ctx, 0));
    double x = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::atan2(y, x));
}

static const proto::ProtoObject* py_degrees(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(x * (180.0 / 3.14159265358979323846));
}

static const proto::ProtoObject* py_radians(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
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
            const_cast<proto::ProtoObject*>(arg), nullptr, argsView(proto::ProtoArguments(nullptr, 0)), nullptr);
    }
    return env->callObject(m, { arg });
}

static const proto::ProtoObject* py_floor(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    const proto::ProtoObject* arg = posArgs->getAt(ctx, 0);
    if (const proto::ProtoObject* iv = intArgument(ctx, arg)) return iv;
    const proto::ProtoObject* r = mathDunderViaType(ctx, arg, "__floor__");
    if (r) return r;
    return integralDoubleToInt(ctx, std::floor(toDouble(ctx, arg)));
}

static const proto::ProtoObject* py_ceil(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    const proto::ProtoObject* arg = posArgs->getAt(ctx, 0);
    if (const proto::ProtoObject* iv = intArgument(ctx, arg)) return iv;
    const proto::ProtoObject* r = mathDunderViaType(ctx, arg, "__ceil__");
    if (r) return r;
    return integralDoubleToInt(ctx, std::ceil(toDouble(ctx, arg)));
}

static const proto::ProtoObject* py_fabs(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::fabs(x));
}

static const proto::ProtoObject* py_trunc(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    const proto::ProtoObject* arg = posArgs->getAt(ctx, 0);
    if (const proto::ProtoObject* iv = intArgument(ctx, arg)) return iv;
    const proto::ProtoObject* r = mathDunderViaType(ctx, arg, "__trunc__");
    if (r) return r;
    return integralDoubleToInt(ctx, std::trunc(toDouble(ctx, arg)));
}

static const proto::ProtoObject* py_copysign(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 2) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::copysign(x, y));
}

static const proto::ProtoObject* py_isclose(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments* kwArgs) {
    // math.isclose(a, b, *, rel_tol=1e-09, abs_tol=0.0), following CPython's
    // math_isclose_impl. The tolerances are keyword-only; they used to be
    // read from positions 3 and 4 and passing them by keyword had no effect.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::proto_ulong nargs = posArgs ? posArgs->getSize(ctx) : 0;
    if (nargs != 2) {
        if (env) env->raiseTypeError(ctx,
            "isclose() takes exactly 2 positional arguments (" + std::to_string(nargs) + " given)");
        return nullptr;
    }
    const double a = toDouble(ctx, posArgs->getAt(ctx, 0));
    const double b = toDouble(ctx, posArgs->getAt(ctx, 1));
    double rel_tol = 1e-09;
    double abs_tol = 0.0;
    if (kwArgs) {
        const proto::proto_ulong relH = PythonEnvironment::kwKey(ctx, PythonEnvironment::getInternedString(ctx, "rel_tol"));
        const proto::proto_ulong absH = PythonEnvironment::kwKey(ctx, PythonEnvironment::getInternedString(ctx, "abs_tol"));
        if (kwArgs->has(ctx, relH)) rel_tol = toDouble(ctx, kwArgs->getAt(ctx, relH));
        if (kwArgs->has(ctx, absH)) abs_tol = toDouble(ctx, kwArgs->getAt(ctx, absH));
    }
    if (rel_tol < 0.0 || abs_tol < 0.0) return raise_math_domain(ctx, "tolerances must be non-negative");
    // Exactly equal, which also covers two infinities of the same sign.
    if (a == b) return PROTO_TRUE;
    // An infinity is close only to itself. NaN is close to nothing, itself
    // included: every comparison below is false when diff is NaN.
    if (std::isinf(a) || std::isinf(b)) return PROTO_FALSE;
    const double diff = std::fabs(b - a);
    return (diff <= std::fabs(rel_tol * b) || diff <= std::fabs(rel_tol * a) || diff <= abs_tol)
        ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_isinf(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return std::isinf(x) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_isfinite(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return std::isfinite(x) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_isnan(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return std::isnan(x) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_log(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double num = 0.0;
    if (!logHelper(ctx, posArgs->getAt(ctx, 0), m_log, num)) return nullptr;
    if (posArgs->getSize(ctx) >= 2) {
        double den = 0.0;
        if (!logHelper(ctx, posArgs->getAt(ctx, 1), m_log, den)) return nullptr;
        if (den == 0.0) {
            if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx))
                env->raiseZeroDivisionError(ctx, "division by zero");
            return nullptr;
        }
        return ctx->fromDouble(num / den);
    }
    return ctx->fromDouble(num);
}

static const proto::ProtoObject* py_log10(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double r = 0.0;
    if (!logHelper(ctx, posArgs->getAt(ctx, 0), m_log10, r)) return nullptr;
    return ctx->fromDouble(r);
}

static const proto::ProtoObject* py_log2(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double r = 0.0;
    if (!logHelper(ctx, posArgs->getAt(ctx, 0), m_log2, r)) return nullptr;
    return ctx->fromDouble(r);
}

static const proto::ProtoObject* py_log1p(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= -1.0) return raise_math_domain(ctx, "math domain error");
    return ctx->fromDouble(std::log1p(x));
}

static const proto::ProtoObject* py_hypot(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 2) return missingArgument(ctx);
    double sum = 0.0;
    for (size_t k = 0; k < posArgs->getSize(ctx); k++)
        sum += toDouble(ctx, posArgs->getAt(ctx, static_cast<int>(k))) * toDouble(ctx, posArgs->getAt(ctx, static_cast<int>(k)));
    return ctx->fromDouble(std::sqrt(sum));
}

static const proto::ProtoObject* py_fmod(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 2) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::fmod(x, y));
}

// math.pow(x, y): C's pow on two floats; ValueError for a domain error
// (a negative base with a non-integral exponent, zero to a negative power),
// OverflowError when finite arguments overflow. (math.pow was missing.)
static const proto::ProtoObject* py_math_pow(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (posArgs->getSize(ctx) != 2) {
        if (env) env->raiseTypeError(ctx, "pow expected 2 arguments, got " + std::to_string(posArgs->getSize(ctx)));
        return nullptr;
    }
    const double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    const double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    const double r = std::pow(x, y);
    if (std::isfinite(x) && std::isfinite(y)) {
        if (std::isnan(r) || (std::isinf(r) && x == 0.0)) return raise_math_domain(ctx, "math domain error");
        if (std::isinf(r)) {
            if (env) env->raiseOverflowError(ctx, "math range error");
            return nullptr;
        }
    }
    return ctx->fromDouble(r);
}

static const proto::ProtoObject* py_remainder(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 2) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::remainder(x, y));
}

static const proto::ProtoObject* py_erf(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::erf(x));
}

static const proto::ProtoObject* py_erfc(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::erfc(x));
}

static const proto::ProtoObject* py_gamma(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    // ValueError at the poles, OverflowError when a finite argument
    // overflows (both answered None).
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (posArgs->getSize(ctx) != 1) {
        if (env) env->raiseTypeError(ctx, "gamma() takes exactly one argument (" + std::to_string(posArgs->getSize(ctx)) + " given)");
        return nullptr;
    }
    const double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (std::isnan(x)) return ctx->fromDouble(x);
    if (std::isinf(x)) {
        if (x > 0.0) return ctx->fromDouble(x);
        if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "expected a noninteger or positive integer, got " + PythonEnvironment::reprObject(ctx, ctx->fromDouble(x)))); return nullptr;
    }
    if (x <= 0.0 && std::floor(x) == x) {
        if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx,
            "expected a noninteger or positive integer, got " + PythonEnvironment::reprObject(ctx, ctx->fromDouble(x))));
        return nullptr;
    }
    const double r = std::tgamma(x);
    if (std::isinf(r)) {
        if (env) env->raiseOverflowError(ctx, "math range error");
        return nullptr;
    }
    return ctx->fromDouble(r);
}

static const proto::ProtoObject* py_lgamma(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    // ValueError at the poles, OverflowError when a finite argument
    // overflows (both answered None).
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (posArgs->getSize(ctx) != 1) {
        if (env) env->raiseTypeError(ctx, "lgamma() takes exactly one argument (" + std::to_string(posArgs->getSize(ctx)) + " given)");
        return nullptr;
    }
    const double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (std::isnan(x)) return ctx->fromDouble(x);
    if (std::isinf(x)) {
        if (x > 0.0) return ctx->fromDouble(x);
        return ctx->fromDouble(HUGE_VAL);
    }
    if (x <= 0.0 && std::floor(x) == x) {
        if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx,
            "expected a noninteger or positive integer, got " + PythonEnvironment::reprObject(ctx, ctx->fromDouble(x))));
        return nullptr;
    }
    const double r = std::lgamma(x);
    if (std::isinf(r)) {
        if (env) env->raiseOverflowError(ctx, "math range error");
        return nullptr;
    }
    return ctx->fromDouble(r);
}

static const proto::ProtoObject* py_exp(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::exp(x));
}

static const proto::ProtoObject* py_dist(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    // dist(p, q): Euclidean distance of two points given as sequences of
    // real numbers (any iterable; it answered None for a tuple and hid
    // every conversion error).
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return nullptr;
    if (posArgs->getSize(ctx) != 2) {
        env->raiseTypeError(ctx, "dist expected 2 arguments, got " + std::to_string(posArgs->getSize(ctx)));
        return nullptr;
    }
    auto coords = [&](const proto::ProtoObject* seq, std::vector<double>& out) -> bool {
        const proto::ProtoObject* it = env->iter(seq);
        if (!it) return false;
        PythonEnvironment::TransientPin pinIt(env, it);
        while (const proto::ProtoObject* item = env->next(it)) out.push_back(toDouble(ctx, item));
        return true;
    };
    std::vector<double> pa, pb;
    if (!coords(posArgs->getAt(ctx, 0), pa) || !coords(posArgs->getAt(ctx, 1), pb)) return nullptr;
    if (pa.size() != pb.size()) {
        env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "both points must have the same number of dimensions"));
        return nullptr;
    }
    double r = 0.0;
    for (size_t i = 0; i < pa.size(); ++i) r = std::hypot(r, pa[i] - pb[i]);
    return ctx->fromDouble(r);
}

// An integer argument (int, bool, int subclass, __index__) as an int object;
// TypeError for anything else, as CPython's math functions raise.
static const proto::ProtoObject* indexArgument(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (const proto::ProtoObject* iv = intArgument(ctx, obj)) return iv;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return nullptr;
    const proto::ProtoObject* cls = obj ? env->getType(ctx, obj) : nullptr;
    const proto::ProtoObject* m = (cls && obj && !obj->isDouble(ctx)) ? env->getAttribute(ctx, cls,
        PythonEnvironment::getInternedString(ctx, "__index__"), false) : nullptr;
    if (m && m != PROTO_NONE) {
        const proto::ProtoObject* r = env->callMethod(obj, "__index__", {});
        if (const proto::ProtoObject* iv = intArgument(ctx, r)) return iv;
        if (r) env->raiseTypeError(ctx, "__index__ returned non-int (type " + PythonEnvironment::typeNameOf(ctx, r) + ")");
        return nullptr;
    }
    env->raiseTypeError(ctx, "'" + PythonEnvironment::typeNameOf(ctx, obj) + "' object cannot be interpreted as an integer");
    return nullptr;
}

// A machine integer argument: an int beyond long long raises OverflowError
// (it read as 0, silently).
static long long getLongSafe(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    const proto::ProtoObject* iv = indexArgument(ctx, obj);
    if (!iv) return 0;
    return iv->asLong(ctx);
}

// perm(n, k) / comb(n, k): exact at any size (they wrapped around long long).
static const proto::ProtoObject* permComb(proto::ProtoContext* ctx, const proto::ProtoArguments* posArgs, bool comb) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::proto_ulong argc = posArgs->getSize(ctx);
    if (argc < (comb ? 2u : 1u) || argc > 2) {
        if (env) env->raiseTypeError(ctx, std::string(comb ? "comb" : "perm") + " expected " +
            (comb ? "2 arguments" : "at most 2 arguments") + ", got " + std::to_string(argc));
        return nullptr;
    }
    const proto::ProtoObject* n = indexArgument(ctx, posArgs->getAt(ctx, 0));
    if (!n) return nullptr;
    const proto::ProtoObject* kArg = argc > 1 ? posArgs->getAt(ctx, 1) : PROTO_NONE;
    const proto::ProtoObject* k = (kArg == PROTO_NONE || (env && kArg == env->getNonePrototype()))
        ? (comb ? nullptr : n) : indexArgument(ctx, kArg);
    if (!k) return nullptr;
    if (n->integerSign(ctx) < 0) {
        if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "n must be a non-negative integer"));
        return nullptr;
    }
    if (k->integerSign(ctx) < 0) {
        if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "k must be a non-negative integer"));
        return nullptr;
    }
    if (k->compare(ctx, n) > 0) return ctx->fromInteger(0);
    if (comb) {
        const proto::ProtoObject* nk = n->subtract(ctx, k);
        if (nk->compare(ctx, k) < 0) k = nk;
    }
    long long kk = 0;
    if (!PythonEnvironment::tryAsLong(ctx, k, kk)) {
        if (env) env->raiseOverflowError(ctx, std::string("min(n - k, k) must not exceed ") + "9223372036854775807");
        return nullptr;
    }
    const proto::ProtoObject* r = ctx->fromInteger(1);
    const proto::ProtoObject* term = n->subtract(ctx, k);   // n - k
    const proto::ProtoObject* one = ctx->fromInteger(1);
    for (long long i = 1; i <= kk; ++i) {
        term = term->add(ctx, one);                           // n - k + i
        r = r->multiply(ctx, term);
        if (comb) r = r->divide(ctx, ctx->fromInteger(i));    // exact: r is C(n-k+i, i)
    }
    return r;
}

static const proto::ProtoObject* py_perm(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    return permComb(ctx, posArgs, false);
}

static const proto::ProtoObject* py_comb(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    return permComb(ctx, posArgs, true);
}

static const proto::ProtoObject* py_factorial(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
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

// math.fsum(iterable): the correctly rounded sum, by CPython's algorithm
// (Shewchuk's exact partial sums, then a round-half-even correction of the
// last partial). An infinity or NaN term makes the result inf / nan, and
// opposite infinities a ValueError, as CPython answers.
static const proto::ProtoObject* py_fsum(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* it = env->iter(posArgs->getAt(ctx, 0));
    if (!it) {
        env->raiseTypeError(ctx, "fsum() argument must be iterable");
        return nullptr;
    }
    PythonEnvironment::TransientPin pinIt(env, it);
    std::vector<double> partials;
    double specialSum = 0.0;   // sum of the inf / nan terms
    double infSum = 0.0;       // to tell +inf + -inf apart from nan
    while (const proto::ProtoObject* item = env->next(it)) {
        if (!item->isDouble(ctx) && !item->isInteger(ctx)) {
            env->raiseTypeError(ctx, "must be real number");
            return nullptr;
        }
        double x = toDouble(ctx, item);
        if (!std::isfinite(x)) {
            if (std::isinf(x)) infSum += x;
            specialSum += x;
            continue;
        }
        size_t i = 0;
        for (double y : partials) {
            if (std::fabs(x) < std::fabs(y)) std::swap(x, y);
            const double hi = x + y;
            const double lo = y - (hi - x);
            if (lo != 0.0) partials[i++] = lo;
            x = hi;
        }
        partials.resize(i);
        partials.push_back(x);
    }
    if (specialSum != 0.0 || std::isnan(specialSum)) {
        if (std::isnan(infSum)) {
            env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx, "-inf + inf in fsum")->asObject(ctx));
            return nullptr;
        }
        return ctx->fromDouble(specialSum);
    }
    double hi = 0.0;
    size_t n = partials.size();
    if (n > 0) {
        hi = partials[--n];
        double lo = 0.0;
        while (n > 0) {
            const double x = hi;
            const double y = partials[--n];
            hi = x + y;
            const double yr = hi - x;
            lo = y - yr;
            if (lo != 0.0) break;
        }
        // Round half to even when the remaining partials push it that way.
        if (n > 0 && ((lo < 0.0 && partials[n - 1] < 0.0) || (lo > 0.0 && partials[n - 1] > 0.0))) {
            const double y = lo * 2.0;
            const double x = hi + y;
            const double yr = x - hi;
            if (y == yr) hi = x;
        }
    }
    return ctx->fromDouble(hi);
}

static const proto::ProtoObject* py_prod(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments* kwArgs) {
    // prod(iterable, *, start=1): start * x1 * x2 ..., with Python's `*`
    // (exact ints at any size, floats, objects through __mul__). Any
    // iterable is accepted (a tuple or generator answered None).
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return nullptr;
    if (posArgs->getSize(ctx) != 1) {
        env->raiseTypeError(ctx, "prod() takes exactly 1 positional argument (" +
                                 std::to_string(posArgs->getSize(ctx)) + " given)");
        return nullptr;
    }
    const proto::ProtoObject* acc = ctx->fromInteger(1);
    if (kwArgs && kwArgs->getSize(ctx) > 0) {
        const proto::ProtoString* startS = PythonEnvironment::getInternedString(ctx, "start");
        if (kwArgs->has(ctx, PythonEnvironment::kwKey(ctx, startS)))
            acc = kwArgs->getAt(ctx, PythonEnvironment::kwKey(ctx, startS));
    }
    const proto::ProtoObject* it = env->iter(posArgs->getAt(ctx, 0));
    if (!it) return nullptr;
    PythonEnvironment::TransientPin pinIt(env, it);
    PythonEnvironment::TransientPin pinAcc(env, acc);
    while (const proto::ProtoObject* item = env->next(it)) {
        acc = env->binaryOp(acc, TokenType::Star, item);
        if (!acc) return nullptr;
        pinAcc.reset(acc);
    }
    return acc;
}

static const proto::ProtoObject* py_sumprod(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    // sumprod(p, q): sum of p[i] * q[i] with Python's `+` and `*`, so ints
    // stay exact ints (the result was always a float). Floats are summed
    // in order, without CPython's extended-precision accumulation.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return nullptr;
    if (posArgs->getSize(ctx) != 2) {
        env->raiseTypeError(ctx, "sumprod expected 2 arguments, got " + std::to_string(posArgs->getSize(ctx)));
        return nullptr;
    }
    const proto::ProtoObject* ia = env->iter(posArgs->getAt(ctx, 0));
    if (!ia) return nullptr;
    PythonEnvironment::TransientPin pinA(env, ia);
    const proto::ProtoObject* ib = env->iter(posArgs->getAt(ctx, 1));
    if (!ib) return nullptr;
    PythonEnvironment::TransientPin pinB(env, ib);
    const proto::ProtoObject* acc = ctx->fromInteger(0);
    PythonEnvironment::TransientPin pinAcc(env, acc);
    for (;;) {
        const proto::ProtoObject* x = env->next(ia);
        PythonEnvironment::TransientPin pinX(env, x);
        const proto::ProtoObject* y = env->next(ib);
        if (!x && !y) break;
        if (!x || !y) {
            env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "Inputs are not the same length"));
            return nullptr;
        }
        const proto::ProtoObject* p = env->binaryOp(x, TokenType::Star, y);
        if (!p) return nullptr;
        acc = env->binaryOp(acc, TokenType::Plus, p);
        if (!acc) return nullptr;
        pinAcc.reset(acc);
    }
    return acc;
}

static const proto::ProtoObject* py_isqrt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
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
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x < 1.0) return raise_math_domain(ctx, "math domain error");
    return ctx->fromDouble(std::acosh(x));
}

static const proto::ProtoObject* py_asinh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::asinh(x));
}

static const proto::ProtoObject* py_atanh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (x <= -1.0 || x >= 1.0) return raise_math_domain(ctx, "math domain error");
    return ctx->fromDouble(std::atanh(x));
}

static const proto::ProtoObject* py_cosh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::cosh(x));
}

static const proto::ProtoObject* py_sinh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::sinh(x));
}

static const proto::ProtoObject* py_tanh(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::tanh(x));
}

static const proto::ProtoObject* py_ulp(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (std::isnan(x)) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    if (std::isinf(x)) return ctx->fromDouble(std::numeric_limits<double>::infinity());
    if (x == 0.0) return ctx->fromDouble(std::numeric_limits<double>::denorm_min());
    double next = std::nextafter(x, (x > 0 ? 1.0 : -1.0) * std::numeric_limits<double>::infinity());
    return ctx->fromDouble(std::abs(next - x));
}

static const proto::ProtoObject* py_nextafter(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 2) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double y = toDouble(ctx, posArgs->getAt(ctx, 1));
    return ctx->fromDouble(std::nextafter(x, y));
}

static const proto::ProtoObject* py_ldexp(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 2) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    int exp = static_cast<int>(posArgs->getAt(ctx, 1)->asLong(ctx));
    return ctx->fromDouble(std::ldexp(x, exp));
}

static const proto::ProtoObject* py_frexp(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    int exp = 0;
    double mantissa = std::frexp(x, &exp);
    const proto::ProtoList* lst = ctx->newList()
        ->appendLast(ctx, ctx->fromDouble(mantissa))
        ->appendLast(ctx, ctx->fromInteger(exp));
    return ::protoPython::PythonEnvironment::tupleFromList(ctx, lst);
}

static const proto::ProtoObject* py_modf(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    double intpart = 0.0;
    double frac = std::modf(x, &intpart);
    const proto::ProtoList* lst = ctx->newList()
        ->appendLast(ctx, ctx->fromDouble(frac))
        ->appendLast(ctx, ctx->fromDouble(intpart));
    return ::protoPython::PythonEnvironment::tupleFromList(ctx, lst);
}

static const proto::ProtoObject* py_cbrt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::cbrt(x));
}

static const proto::ProtoObject* py_exp2(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::exp2(x));
}

static const proto::ProtoObject* py_expm1(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return missingArgument(ctx);
    double x = toDouble(ctx, posArgs->getAt(ctx, 0));
    return ctx->fromDouble(std::expm1(x));
}

static const proto::ProtoObject* py_fma(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 3) return missingArgument(ctx);
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
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    proto::proto_ulong n = posArgs ? posArgs->getSize(ctx) : 0;
    if (n == 0) return ctx->fromInteger(0);
    long long acc = posArgs->getAt(ctx, 0)->asLong(ctx);
    if (acc < 0) acc = -acc;
    for (proto::proto_ulong i = 1; i < n; ++i) {
        long long x = posArgs->getAt(ctx, static_cast<int>(i))->asLong(ctx);
        if (x < 0) x = -x;
        acc = gcd_impl(acc, x);
        if (acc == 1) break;  // terminate early — gcd can only stay 1
    }
    return ctx->fromInteger(acc);
}

static const proto::ProtoObject* py_lcm(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    proto::proto_ulong n = posArgs ? posArgs->getSize(ctx) : 0;
    if (n == 0) return ctx->fromInteger(1);
    long long acc = posArgs->getAt(ctx, 0)->asLong(ctx);
    if (acc < 0) acc = -acc;
    for (proto::proto_ulong i = 1; i < n; ++i) {
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
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pow"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_math_pow));
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
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "fsum"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fsum));
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
