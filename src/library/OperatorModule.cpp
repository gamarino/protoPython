#include <protoPython/OperatorModule.h>
#include <cmath>
#include <string>

namespace protoPython {
namespace operator_ {

static double toDouble(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return 0.0;
    if (obj->isDouble(ctx)) return obj->asDouble(ctx);
    if (obj->isInteger(ctx)) return static_cast<double>(obj->asLong(ctx));
    /* Handle Python-style __data__ wrapper (e.g. int/float in __data__). */
    const proto::ProtoObject* data = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
    if (data && data != PROTO_NONE) {
        if (data->isDouble(ctx)) return data->asDouble(ctx);
        if (data->isInteger(ctx)) return static_cast<double>(data->asLong(ctx));
    }
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
    if (!a || a == PROTO_NONE) return PROTO_NONE;
    long long n = 0;
    if (a->isInteger(ctx)) {
        try { n = a->asLong(ctx); } catch (...) { return PROTO_NONE; }
    } else {
        const proto::ProtoObject* data = a->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
        if (data && data != PROTO_NONE && data->isInteger(ctx)) {
            try { n = data->asLong(ctx); } catch (...) { return PROTO_NONE; }
        } else {
            /* Fallback: toDouble handles Double and some wrappers (e.g. int-like). */
            double d = toDouble(ctx, a);
            n = static_cast<long long>(d);
            if (static_cast<double>(n) != d) return PROTO_NONE;  /* Not an integer value */
        }
    }
    return ctx->fromInteger(static_cast<long long>(~static_cast<unsigned long long>(n)));
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

static const proto::ProtoObject* py_and_(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx);
    return ctx->fromInteger(static_cast<long long>(static_cast<unsigned long long>(a) & static_cast<unsigned long long>(b)));
}

static const proto::ProtoObject* py_or_(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx);
    return ctx->fromInteger(static_cast<long long>(static_cast<unsigned long long>(a) | static_cast<unsigned long long>(b)));
}

static const proto::ProtoObject* py_xor(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    long long a = posArgs->getAt(ctx, 0)->asLong(ctx);
    long long b = posArgs->getAt(ctx, 1)->asLong(ctx);
    return ctx->fromInteger(static_cast<long long>(static_cast<unsigned long long>(a) ^ static_cast<unsigned long long>(b)));
}

static const proto::ProtoObject* py_index(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* indexM = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__index__"));
    if (indexM && indexM->asMethod(ctx)) {
        const proto::ProtoList* noArgs = ctx->newList();
        const proto::ProtoObject* result = indexM->asMethod(ctx)(ctx, obj, nullptr, noArgs, nullptr);
        if (result && result->isInteger(ctx)) return result;
        return PROTO_NONE;
    }
    if (obj->isInteger(ctx)) return obj;
    return PROTO_NONE;
}

static const proto::ProtoObject* py_itemgetter_call(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* itemsObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__items__"));
    if (!itemsObj || !itemsObj->asList(ctx)) return PROTO_NONE;
    const proto::ProtoList* items = itemsObj->asList(ctx);
    
    auto getItem = [&](const proto::ProtoObject* key) -> const proto::ProtoObject* {
        const proto::ProtoString* getItemS = proto::ProtoString::fromUTF8String(ctx, "__getitem__");
        const proto::ProtoObject* method = obj->getAttribute(ctx, getItemS);
        if (method && method->asMethod(ctx)) {
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
            return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(obj), nullptr, args, nullptr);
        }
        return PROTO_NONE;
    };

    if (items->getSize(ctx) == 1) {
        return getItem(items->getAt(ctx, 0));
    } else {
        const proto::ProtoList* results = ctx->newList();
        for (unsigned long i = 0; i < items->getSize(ctx); ++i) {
            results = results->appendLast(ctx, getItem(items->getAt(ctx, i)));
        }
        const proto::ProtoTuple* tup = ctx->newTupleFromList(results);
        return tup ? tup->asObject(ctx) : PROTO_NONE;
    }
}

static const proto::ProtoObject* py_itemgetter(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    
    proto::ProtoObject* getter = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    getter->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__items__"), posArgs->asObject(ctx));
    getter->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(getter, py_itemgetter_call));
    return getter;
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
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "and_"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_and_));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "or_"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_or_));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "xor"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_xor));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "index"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_index));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "itemgetter"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_itemgetter));
    return mod;
}

} // namespace operator_
} // namespace protoPython
