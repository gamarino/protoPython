#include <protoPython/PythonEnvironment.h>
#include <protoPython/PythonModuleProvider.h>
#include <protoPython/NativeModuleProvider.h>
#include <protoPython/SysModule.h>
#include <protoPython/BuiltinsModule.h>
#include <protoPython/IOModule.h>
#include <protoPython/CollectionsModule.h>
#include <protoPython/ExceptionsModule.h>
#include <protoPython/LoggingModule.h>
#include <protoPython/MathModule.h>
#include <protoPython/OperatorModule.h>
#include <protoPython/FunctoolsModule.h>
#include <protoPython/ItertoolsModule.h>
#include <protoPython/JsonModule.h>
#include <protoPython/ReModule.h>
#include <protoPython/OsPathModule.h>
#include <protoPython/PathlibModule.h>
#include <protoPython/CollectionsAbcModule.h>
#include <protoCore.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <climits>
#include <vector>

namespace protoPython {

// --- Dunder Methods Implementation ---

static const proto::ProtoObject* py_object_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return PROTO_NONE;
}

/** object(): return new bare object instance (used when calling object()). */
static const proto::ProtoObject* py_object_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return self->newChild(context, true);
}

static const proto::ProtoObject* py_object_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    // Basic <object at 0x...> repr
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "<object at %p>", (void*)self);
    return context->fromUTF8String(buffer);
}

static const proto::ProtoObject* py_float_call(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) == 0) return ctx->fromDouble(0.0);
    const proto::ProtoObject* x = posArgs->getAt(ctx, 0);
    if (x->isInteger(ctx)) return ctx->fromDouble(static_cast<double>(x->asLong(ctx)));
    if (x->isDouble(ctx)) return x;
    return PROTO_NONE;
}

static const proto::ProtoObject* py_float_is_integer(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    if (!self->isDouble(context)) return PROTO_FALSE;
    double d = self->asDouble(context);
    return (d == std::floor(d) && d >= -9007199254740992.0 && d <= 9007199254740992.0) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_float_as_integer_ratio(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    if (!self->isDouble(context)) return PROTO_NONE;
    double d = self->asDouble(context);
    if (d == 0.0) {
        const proto::ProtoList* pair = context->newList()->appendLast(context, context->fromInteger(0))->appendLast(context, context->fromInteger(1));
        const proto::ProtoTuple* tup = context->newTupleFromList(pair);
        return tup ? tup->asObject(context) : PROTO_NONE;
    }
    int exp;
    double m = std::frexp(d, &exp);
    long long num = static_cast<long long>(m * (1LL << 53));
    long long den = 1LL << (53 - exp);
    if (d < 0) num = -num;
    const proto::ProtoList* pair = context->newList()->appendLast(context, context->fromInteger(num))->appendLast(context, context->fromInteger(den));
    const proto::ProtoTuple* tup = context->newTupleFromList(pair);
    return tup ? tup->asObject(context) : PROTO_NONE;
}

static const proto::ProtoObject* py_bool_call(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* obj = posArgs->getAt(ctx, 0);
    if (obj->isString(ctx)) return obj->asString(ctx)->getSize(ctx) > 0 ? PROTO_TRUE : PROTO_FALSE;
    if (obj->isInteger(ctx)) return obj->asLong(ctx) != 0 ? PROTO_TRUE : PROTO_FALSE;
    if (obj->isDouble(ctx)) return obj->asDouble(ctx) != 0.0 ? PROTO_TRUE : PROTO_FALSE;
    const proto::ProtoObject* boolMethod = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__bool__"));
    if (boolMethod && boolMethod->asMethod(ctx))
        return boolMethod->asMethod(ctx)(ctx, obj, nullptr, ctx->newList(), nullptr);
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_object_format(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* strM = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__str__"));
    if (strM && strM->asMethod(context)) {
        return strM->asMethod(context)(context, self, nullptr, posArgs && posArgs->getSize(context) > 0 ? posArgs : context->newList(), nullptr);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_object_str(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    // Default str(obj) calls repr(obj)
    return self->call(context, nullptr, proto::ProtoString::fromUTF8String(context, "__repr__"), self, nullptr);
}

// --- List Methods ---

static const proto::ProtoObject* py_list_append(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);

    if (positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* item = positionalParameters->getAt(context, 0);
        const proto::ProtoList* newList = list->appendLast(context, item);
        self->setAttribute(context, dataName, newList->asObject(context));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return context->fromInteger(0);
    return context->fromInteger(data->asList(context)->getSize(context));
}

struct SliceBounds { bool isSlice; long long start, stop, step; };

static SliceBounds get_slice_bounds(proto::ProtoContext* context, const proto::ProtoObject* indexObj, long long size) {
    SliceBounds sb{false, 0, 0, 1};
    const proto::ProtoString* startName = proto::ProtoString::fromUTF8String(context, "start");
    const proto::ProtoString* stopName = proto::ProtoString::fromUTF8String(context, "stop");
    const proto::ProtoString* stepName = proto::ProtoString::fromUTF8String(context, "step");
    const proto::ProtoObject* startObj = indexObj->getAttribute(context, startName);
    const proto::ProtoObject* stopObj = indexObj->getAttribute(context, stopName);
    if (!stopObj || stopObj == PROTO_NONE) return sb;
    sb.isSlice = true;
    sb.start = (startObj && startObj != PROTO_NONE && startObj->isInteger(context)) ? startObj->asLong(context) : 0;
    sb.stop = (stopObj && stopObj != PROTO_NONE && stopObj->isInteger(context)) ? stopObj->asLong(context) : size;
    const proto::ProtoObject* stepObj = indexObj->getAttribute(context, stepName);
    sb.step = (stepObj && stepObj != PROTO_NONE && stepObj->isInteger(context)) ? stepObj->asLong(context) : 1;
    if (sb.start < 0) sb.start += size;
    if (sb.stop < 0) sb.stop += size;
    if (sb.start < 0) sb.start = 0;
    if (sb.stop > size) sb.stop = size;
    if (sb.start > sb.stop) sb.start = sb.stop;
    return sb;
}

static const proto::ProtoObject* py_list_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* indexObj = positionalParameters->getAt(context, 0);
    long long size = static_cast<long long>(list->getSize(context));

    if (indexObj->isInteger(context)) {
        int index = static_cast<int>(indexObj->asLong(context));
        unsigned long ulen = list->getSize(context);
        if (index < 0) index += static_cast<int>(ulen);
        if (index < 0 || static_cast<unsigned long>(index) >= ulen) return PROTO_NONE;
        return list->getAt(context, index);
    }

    const proto::ProtoList* sliceList = indexObj->asList(context);
    if (sliceList) {
        unsigned long sliceSize = sliceList->getSize(context);
        if (sliceSize >= 2) {
            long long start = sliceList->getAt(context, 0)->asLong(context);
            long long stop = sliceList->getAt(context, 1)->asLong(context);
            long long step = sliceSize >= 3 ? sliceList->getAt(context, 2)->asLong(context) : 1;
            if (step != 1) return PROTO_NONE;
            if (start < 0) start += size;
            if (stop < 0) stop += size;
            if (start < 0) start = 0;
            if (stop > size) stop = size;
            if (start > stop) start = stop;
            const proto::ProtoList* result = context->newList();
            for (long long i = start; i < stop; i += step) {
                result = result->appendLast(context, list->getAt(context, static_cast<int>(i)));
            }
            return result->asObject(context);
        }
        return PROTO_NONE;
    }

    SliceBounds sb = get_slice_bounds(context, indexObj, size);
    if (sb.isSlice && sb.step == 1) {
        const proto::ProtoList* result = context->newList();
        for (long long i = sb.start; i < sb.stop; i += sb.step) {
            result = result->appendLast(context, list->getAt(context, static_cast<int>(i)));
        }
        return result->asObject(context);
    }

    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_setitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    int index = static_cast<int>(positionalParameters->getAt(context, 0)->asLong(context));
    const proto::ProtoObject* value = positionalParameters->getAt(context, 1);
    unsigned long size = list->getSize(context);
    if (index < 0) index += static_cast<int>(size);
    if (index < 0 || static_cast<unsigned long>(index) >= size) return PROTO_NONE;
    const proto::ProtoList* newList = list->setAt(context, index, value);
    self->setAttribute(context, dataName, newList->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;

    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;

    const proto::ProtoList* list = data->asList(context);
    const proto::ProtoListIterator* it = list->getIterator(context);

    const proto::ProtoObject* iterObj = iterProto->newChild(context, true);
    const proto::ProtoString* iterListName = proto::ProtoString::fromUTF8String(context, "__iter_list__");
    const proto::ProtoString* iterItName = proto::ProtoString::fromUTF8String(context, "__iter_it__");
    iterObj->setAttribute(context, iterListName, data);
    iterObj->setAttribute(context, iterItName, it->asObject(context));
    return iterObj;
}

static const proto::ProtoObject* py_list_iter_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterItName = proto::ProtoString::fromUTF8String(context, "__iter_it__");
    const proto::ProtoObject* itObj = self->getAttribute(context, iterItName);
    if (!itObj || !itObj->asListIterator(context)) return PROTO_NONE;
    const proto::ProtoListIterator* it = itObj->asListIterator(context);
    if (!it->hasNext(context)) return PROTO_NONE;
    const proto::ProtoObject* value = it->next(context);
    const proto::ProtoListIterator* nextIt = it->advance(context);
    self->setAttribute(context, iterItName, nextIt->asObject(context));
    return value;
}

static const proto::ProtoObject* py_list_reversed(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* revProtoName = proto::ProtoString::fromUTF8String(context, "__reversed_prototype__");
    const proto::ProtoObject* revProto = self->getAttribute(context, revProtoName);
    if (!revProto) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);
    long long n = static_cast<long long>(list->getSize(context));
    const proto::ProtoObject* revObj = revProto->newChild(context, true);
    revObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_list__"), data);
    revObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_idx__"), context->fromInteger(n - 1));
    return revObj;
}

static const proto::ProtoObject* py_list_reversed_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* data = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_list__"));
    const proto::ProtoObject* idxObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_idx__"));
    if (!data || !data->asList(context) || !idxObj || !idxObj->isInteger(context)) return PROTO_NONE;
    long long idx = idxObj->asLong(context);
    if (idx < 0) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);
    const proto::ProtoObject* value = list->getAt(context, static_cast<int>(idx));
    self->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_idx__"), context->fromInteger(idx - 1));
    return value;
}

static const proto::ProtoObject* py_list_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_FALSE;
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* value = positionalParameters->getAt(context, 0);
    return data->asList(context)->has(context, value) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_list_eq(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* other = positionalParameters->getAt(context, 0);
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoObject* otherData = other->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : self->asList(context);
    const proto::ProtoList* otherList = otherData && otherData->asList(context) ? otherData->asList(context) : other->asList(context);
    if (!list || !otherList) return PROTO_FALSE;
    if (list == otherList) return PROTO_TRUE;
    unsigned long size = list->getSize(context);
    if (size != otherList->getSize(context)) return PROTO_FALSE;
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* a = list->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* b = otherList->getAt(context, static_cast<int>(i));
        if (a == b) continue;
        if (a->compare(context, b) != 0) {
            auto className = [context](const proto::ProtoObject* obj) -> std::string {
                const proto::ProtoObject* cls = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
                if (!cls) return "";
                const proto::ProtoObject* nameObj = cls->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"));
                if (!nameObj || !nameObj->isString(context)) return "";
                std::string out;
                nameObj->asString(context)->toUTF8String(context, out);
                return out;
            };

            std::string aName = className(a);
            std::string bName = className(b);
            if (aName == "int" && bName == "int") {
                if (a->asLong(context) != b->asLong(context)) return PROTO_FALSE;
            } else if (aName == "str" && bName == "str") {
                std::string sa;
                std::string sb;
                a->asString(context)->toUTF8String(context, sa);
                b->asString(context)->toUTF8String(context, sb);
                if (sa != sb) return PROTO_FALSE;
            } else if (a->getHash(context) != b->getHash(context)) {
                return PROTO_FALSE;
            }
        }
    }
    return PROTO_TRUE;
}

static int compare_values(proto::ProtoContext* context, const proto::ProtoObject* a, const proto::ProtoObject* b) {
    if (a == b) return 0;
    if (a->isInteger(context) && b->isInteger(context)) {
        long long av = a->asLong(context);
        long long bv = b->asLong(context);
        if (av == bv) return 0;
        return av < bv ? -1 : 1;
    }
    if (a->isString(context) && b->isString(context)) {
        std::string sa;
        std::string sb;
        a->asString(context)->toUTF8String(context, sa);
        b->asString(context)->toUTF8String(context, sb);
        if (sa == sb) return 0;
        return sa < sb ? -1 : 1;
    }
    int cmp = a->compare(context, b);
    if (cmp != 0) return cmp;
    unsigned long ha = a->getHash(context);
    unsigned long hb = b->getHash(context);
    if (ha == hb) return 0;
    return ha < hb ? -1 : 1;
}

static int compare_lists(proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ProtoObject* other, bool* ok) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoObject* otherData = other->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : self->asList(context);
    const proto::ProtoList* otherList = otherData && otherData->asList(context) ? otherData->asList(context) : other->asList(context);
    if (!list || !otherList) {
        if (ok) *ok = false;
        return 0;
    }
    unsigned long size = list->getSize(context);
    unsigned long otherSize = otherList->getSize(context);
    unsigned long minSize = size < otherSize ? size : otherSize;
    for (unsigned long i = 0; i < minSize; ++i) {
        const proto::ProtoObject* a = list->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* b = otherList->getAt(context, static_cast<int>(i));
        int cmp = compare_values(context, a, b);
        if (cmp != 0) {
            if (ok) *ok = true;
            return cmp;
        }
    }
    if (size == otherSize) {
        if (ok) *ok = true;
        return 0;
    }
    if (ok) *ok = true;
    return size < otherSize ? -1 : 1;
}

static const proto::ProtoObject* py_list_lt(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    bool ok = false;
    int cmp = compare_lists(context, self, positionalParameters->getAt(context, 0), &ok);
    return ok && cmp < 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_list_le(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    bool ok = false;
    int cmp = compare_lists(context, self, positionalParameters->getAt(context, 0), &ok);
    return ok && cmp <= 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_list_gt(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    bool ok = false;
    int cmp = compare_lists(context, self, positionalParameters->getAt(context, 0), &ok);
    return ok && cmp > 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_list_ge(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    bool ok = false;
    int cmp = compare_lists(context, self, positionalParameters->getAt(context, 0), &ok);
    return ok && cmp >= 0 ? PROTO_TRUE : PROTO_FALSE;
}

// --- Dict Methods ---

static const proto::ProtoObject* py_dict_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context)) return PROTO_NONE;

    if (positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
        unsigned long hash = key->getHash(context);
        const proto::ProtoObject* value = data->asSparseList(context)->getAt(context, hash);
        if (value) return value;
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseKeyError(context, key);
        return PROTO_NONE;
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_setitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context)) return PROTO_NONE;
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* value = positionalParameters->getAt(context, 1);
    unsigned long hash = key->getHash(context);
    bool hadKey = data->asSparseList(context)->has(context, hash);
    const proto::ProtoSparseList* newSparse = data->asSparseList(context)->setAt(context, hash, value);
    self->setAttribute(context, dataName, newSparse->asObject(context));

    if (!hadKey) {
        const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
        const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
        const proto::ProtoList* keysList = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();
        keysList = keysList->appendLast(context, key);
        self->setAttribute(context, keysName, keysList->asObject(context));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context)) return context->fromInteger(0);
    return context->fromInteger(data->asSparseList(context)->getSize(context));
}

static const proto::ProtoObject* py_dict_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;

    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keysList = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();

    const proto::ProtoListIterator* it = keysList->getIterator(context);
    const proto::ProtoObject* iterObj = iterProto->newChild(context, true);
    const proto::ProtoString* iterItName = proto::ProtoString::fromUTF8String(context, "__iter_it__");
    iterObj->setAttribute(context, iterItName, it->asObject(context));
    return iterObj;
}

static const proto::ProtoObject* py_dict_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context)) return PROTO_FALSE;
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
    return data->asSparseList(context)->has(context, key->getHash(context)) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_dict_eq(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* other = positionalParameters->getAt(context, 0);
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoObject* otherData = other->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context) || !otherData || !otherData->asSparseList(context)) return PROTO_FALSE;
    const proto::ProtoSparseList* dictA = data->asSparseList(context);
    const proto::ProtoSparseList* dictB = otherData->asSparseList(context);

    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoObject* keysObjA = self->getAttribute(context, keysName);
    const proto::ProtoObject* keysObjB = other->getAttribute(context, keysName);
    const proto::ProtoList* keysA = keysObjA && keysObjA->asList(context) ? keysObjA->asList(context) : context->newList();
    const proto::ProtoList* keysB = keysObjB && keysObjB->asList(context) ? keysObjB->asList(context) : context->newList();
    if (keysA->getSize(context) != keysB->getSize(context)) return PROTO_FALSE;

    unsigned long size = keysA->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* key = keysA->getAt(context, static_cast<int>(i));
        unsigned long hash = key->getHash(context);
        if (!dictB->has(context, hash)) return PROTO_FALSE;
        const proto::ProtoObject* vA = dictA->getAt(context, hash);
        const proto::ProtoObject* vB = dictB->getAt(context, hash);
        if (vA->compare(context, vB) != 0) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_dict_lt(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_le(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_gt(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_ge(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return PROTO_NONE;
}

static std::string repr_object(proto::ProtoContext* context, const proto::ProtoObject* obj) {
    if (obj->isInteger(context)) {
        return std::to_string(obj->asLong(context));
    }
    if (obj->isBoolean(context)) {
        return obj->asBoolean(context) ? "True" : "False";
    }
    if (obj->isString(context)) {
        std::string s;
        obj->asString(context)->toUTF8String(context, s);
        return s;
    }
    if (obj->isNone(context)) {
        return "None";
    }
    if (!obj->isCell(context)) {
        return "<value>";
    }
    const proto::ProtoObject* reprMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__repr__"));
    if (reprMethod && reprMethod->asMethod(context)) {
        const proto::ProtoObject* out = reprMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
        if (out && out->isString(context)) {
            std::string s;
            out->asString(context)->toUTF8String(context, s);
            return s;
        }
    }
    return "<object>";
}

static const proto::ProtoObject* py_list_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return context->fromUTF8String("[]");

    unsigned long size = list->getSize(context);
    unsigned long limit = 20;
    std::string out = "[";
    for (unsigned long i = 0; i < size && i < limit; ++i) {
        if (i > 0) out += ", ";
        out += repr_object(context, list->getAt(context, static_cast<int>(i)));
    }
    if (size > limit) out += ", ...";
    out += "]";
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_list_str(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return py_list_repr(context, self, parentLink, positionalParameters, keywordParameters);
}

static const proto::ProtoObject* py_list_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_FALSE;
    return list->getSize(context) > 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_list_pop(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list || list->getSize(context) == 0) return PROTO_NONE;
    const proto::ProtoObject* last = list->getAt(context, static_cast<int>(list->getSize(context) - 1));
    const proto::ProtoList* newList = list->removeLast(context);
    self->setAttribute(context, dataName, newList->asObject(context));
    return last;
}

/**
 * list.extend(iterable): appends all items from iterable to the list.
 * Limitation: only list-like objects are supported (object.asList() or object.__data__
 * as list). Arbitrary iterables (e.g. range(), map(), filter()) are not supported;
 * use a loop with append() instead. An iterator-based fallback was removed to avoid
 * non-termination (infinite or very long iterators).
 */
static const proto::ProtoObject* py_list_extend(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* otherObj = positionalParameters->getAt(context, 0);
    if (!otherObj) return PROTO_NONE;

    const proto::ProtoList* otherList = nullptr;
    otherList = otherObj->asList(context);
    if (!otherList) {
        const proto::ProtoObject* otherData = otherObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
        if (otherData) {
            otherList = otherData->asList(context);
        }
    }
    
    // Get the current list from self - use the same pattern as py_list_append
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);
    
    // If no otherList, return early
    if (!otherList) return PROTO_NONE;
    
    // Perform extend by appending each element from otherList
    const proto::ProtoList* newList = list;
    unsigned long otherSize = otherList->getSize(context);
    for (unsigned long i = 0; i < otherSize; ++i) {
        newList = newList->appendLast(context, otherList->getAt(context, static_cast<int>(i)));
    }
    self->setAttribute(context, dataName, newList->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_reverse(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    unsigned long size = list->getSize(context);
    const proto::ProtoList* newList = context->newList();
    for (unsigned long i = size; i > 0; --i)
        newList = newList->appendLast(context, list->getAt(context, static_cast<int>(i - 1)));
    self->setAttribute(context, dataName, newList->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_sort(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    unsigned long size = list->getSize(context);
    std::vector<const proto::ProtoObject*> elems(size);
    for (unsigned long i = 0; i < size; ++i)
        elems[i] = list->getAt(context, static_cast<int>(i));
    std::sort(elems.begin(), elems.end(), [context](const proto::ProtoObject* a, const proto::ProtoObject* b) {
        return a->compare(context, b) < 0;
    });
    const proto::ProtoList* newList = context->newList();
    for (const proto::ProtoObject* obj : elems)
        newList = newList->appendLast(context, obj);
    self->setAttribute(context, dataName, newList->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_insert(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    int index = static_cast<int>(positionalParameters->getAt(context, 0)->asLong(context));
    const proto::ProtoObject* value = positionalParameters->getAt(context, 1);
    unsigned long size = list->getSize(context);
    if (index < 0) index += static_cast<int>(size);
    if (index < 0) index = 0;
    if (static_cast<unsigned long>(index) > size) index = static_cast<int>(size);
    const proto::ProtoList* newList = list->insertAt(context, index, value);
    self->setAttribute(context, dataName, newList->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_remove(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    const proto::ProtoObject* value = positionalParameters->getAt(context, 0);
    unsigned long size = list->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* elem = list->getAt(context, static_cast<int>(i));
        if (elem == value) {
            const proto::ProtoList* newList = list->removeAt(context, static_cast<int>(i));
            self->setAttribute(context, dataName, newList->asObject(context));
            return PROTO_NONE;
        }
        if (elem->isInteger(context) && value->isInteger(context) && elem->compare(context, value) == 0) {
            const proto::ProtoList* newList = list->removeAt(context, static_cast<int>(i));
            self->setAttribute(context, dataName, newList->asObject(context));
            return PROTO_NONE;
        }
        if (elem->isString(context) && value->isString(context)) {
            std::string es, vs;
            elem->asString(context)->toUTF8String(context, es);
            value->asString(context)->toUTF8String(context, vs);
            if (es == vs) {
                const proto::ProtoList* newList = list->removeAt(context, static_cast<int>(i));
                self->setAttribute(context, dataName, newList->asObject(context));
                return PROTO_NONE;
            }
        }
        if (elem->isDouble(context) && value->isDouble(context) && elem->asDouble(context) == value->asDouble(context)) {
            const proto::ProtoList* newList = list->removeAt(context, static_cast<int>(i));
            self->setAttribute(context, dataName, newList->asObject(context));
            return PROTO_NONE;
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) env->raiseValueError(context, context->fromUTF8String("list.remove(x): x not in list"));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_clear(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    self->setAttribute(context, dataName, context->newList()->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_copy(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    proto::ProtoObject* copyObj = const_cast<proto::ProtoObject*>(env->getListPrototype()->newChild(context, true));
    copyObj->setAttribute(context, dataName, list->asObject(context));
    return copyObj;
}

static const proto::ProtoObject* py_list_mul(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* other = posArgs->getAt(context, 0);
    if (!other->isInteger(context)) return PROTO_NONE;
    long long n = other->asLong(context);
    if (n < 0) n = 0;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoList* result = context->newList();
    unsigned long size = list->getSize(context);
    for (long long rep = 0; rep < n; ++rep)
        for (unsigned long i = 0; i < size; ++i)
            result = result->appendLast(context, list->getAt(context, static_cast<int>(i)));
    proto::ProtoObject* out = const_cast<proto::ProtoObject*>(env->getListPrototype()->newChild(context, true));
    out->setAttribute(context, dataName, result->asObject(context));
    return out;
}

static bool list_elem_equal(proto::ProtoContext* context, const proto::ProtoObject* elem, const proto::ProtoObject* value) {
    if (elem == value) return true;
    if (elem->isInteger(context) && value->isInteger(context) && elem->compare(context, value) == 0) return true;
    if (elem->isString(context) && value->isString(context)) {
        std::string es, vs;
        elem->asString(context)->toUTF8String(context, es);
        value->asString(context)->toUTF8String(context, vs);
        return es == vs;
    }
    if (elem->isDouble(context) && value->isDouble(context) && elem->asDouble(context) == value->asDouble(context)) return true;
    return false;
}

static const proto::ProtoObject* py_list_index(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    const proto::ProtoObject* value = positionalParameters->getAt(context, 0);
    long long start = 0;
    long long stop = static_cast<long long>(list->getSize(context));
    if (positionalParameters->getSize(context) >= 2 && positionalParameters->getAt(context, 1)->isInteger(context))
        start = positionalParameters->getAt(context, 1)->asLong(context);
    if (positionalParameters->getSize(context) >= 3 && positionalParameters->getAt(context, 2)->isInteger(context))
        stop = positionalParameters->getAt(context, 2)->asLong(context);
    if (start < 0) start = 0;
    for (long long i = start; i < stop && static_cast<unsigned long>(i) < list->getSize(context); ++i) {
        const proto::ProtoObject* elem = list->getAt(context, static_cast<int>(i));
        if (list_elem_equal(context, elem, value))
            return context->fromInteger(i);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) env->raiseValueError(context, context->fromUTF8String("list.index(x): x not in list"));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_count(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return context->fromInteger(0);
    const proto::ProtoObject* value = positionalParameters->getAt(context, 0);
    long count = 0;
    unsigned long size = list->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* elem = list->getAt(context, static_cast<int>(i));
        if (list_elem_equal(context, elem, value)) count++;
    }
    return context->fromInteger(count);
}

static const proto::ProtoString* bytes_data(proto::ProtoContext* context, const proto::ProtoObject* self) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    return data && data->isString(context) ? data->asString(context) : nullptr;
}

static const proto::ProtoObject* py_bytes_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* s = bytes_data(context, self);
    return s ? context->fromInteger(s->getSize(context)) : context->fromInteger(0);
}

static const proto::ProtoObject* py_bytes_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    int idx = static_cast<int>(positionalParameters->getAt(context, 0)->asLong(context));
    unsigned long size = s->getSize(context);
    if (idx < 0) idx += static_cast<int>(size);
    if (idx < 0 || static_cast<unsigned long>(idx) >= size) return PROTO_NONE;
    std::string c;
    s->toUTF8String(context, c);
    return context->fromInteger(static_cast<unsigned char>(c[static_cast<size_t>(idx)]));
}

static const proto::ProtoObject* py_bytes_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return PROTO_NONE;
    const proto::ProtoObject* iterObj = iterProto->newChild(context, true);
    iterObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__bytes_data__"), s->asObject(context));
    iterObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__bytes_index__"), context->fromInteger(0));
    return iterObj;
}

static const proto::ProtoObject* py_bytes_iter_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__bytes_data__");
    const proto::ProtoString* indexName = proto::ProtoString::fromUTF8String(context, "__bytes_index__");
    const proto::ProtoObject* dataObj = self->getAttribute(context, dataName);
    const proto::ProtoObject* indexObj = self->getAttribute(context, indexName);
    if (!dataObj || !dataObj->isString(context) || !indexObj || !indexObj->isInteger(context)) return PROTO_NONE;
    const proto::ProtoString* s = dataObj->asString(context);
    int idx = static_cast<int>(indexObj->asLong(context));
    unsigned long size = s->getSize(context);
    if (static_cast<unsigned long>(idx) >= size) return PROTO_NONE;
    std::string c;
    s->toUTF8String(context, c);
    const proto::ProtoObject* result = context->fromInteger(static_cast<unsigned char>(c[static_cast<size_t>(idx)]));
    self->setAttribute(context, indexName, context->fromInteger(idx + 1));
    return result;
}

static const proto::ProtoObject* py_bytes_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) == 0) {
        const proto::ProtoObject* empty = self->newChild(context, true);
        empty->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(""));
        return empty;
    }
    const proto::ProtoObject* itObj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterAttr = itObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterAttr || !iterAttr->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* empty = context->newList();
    const proto::ProtoObject* iterResult = iterAttr->asMethod(context)(context, itObj, nullptr, empty, nullptr);
    if (!iterResult) return PROTO_NONE;
    const proto::ProtoObject* nextAttr = iterResult->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextAttr || !nextAttr->asMethod(context)) return PROTO_NONE;

    std::string out;
    const proto::ProtoList* nextArgs = context->newList();
    for (;;) {
        const proto::ProtoObject* item = nextAttr->asMethod(context)(context, iterResult, nullptr, nextArgs, nullptr);
        if (!item || item == PROTO_NONE) break;
        long long v = item->asLong(context);
        if (v < 0 || v > 255) continue;
        out += static_cast<char>(static_cast<unsigned char>(v));
    }
    const proto::ProtoObject* b = self->newChild(context, true);
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(out.c_str()));
    return b;
}

static const proto::ProtoSet* set_data(proto::ProtoContext* context, const proto::ProtoObject* self) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    return data && data->asSet(context) ? data->asSet(context) : nullptr;
}

static const proto::ProtoObject* py_set_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return context->fromInteger(0);
    return context->fromInteger(s->getSize(context));
}

static const proto::ProtoObject* py_set_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s || positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    return s->has(context, positionalParameters->getAt(context, 0));
}

static const proto::ProtoObject* py_set_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_FALSE;
    return s->getSize(context) > 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_set_add(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSet* s = data && data->asSet(context) ? data->asSet(context) : context->newSet();
    const proto::ProtoSet* newSet = s->add(context, positionalParameters->getAt(context, 0));
    self->setAttribute(context, dataName, newSet->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_set_remove(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSet* s = data && data->asSet(context) ? data->asSet(context) : context->newSet();
    const proto::ProtoSet* newSet = s->remove(context, positionalParameters->getAt(context, 0));
    self->setAttribute(context, dataName, newSet->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_set_discard(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_NONE;
    const proto::ProtoObject* elem = positionalParameters->getAt(context, 0);
    if (!s->has(context, elem)) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoSet* newSet = s->remove(context, elem);
    self->setAttribute(context, dataName, newSet->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_set_copy(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoList* parents = self->getParents(context);
    const proto::ProtoObject* parent = parents && parents->getSize(context) > 0 ? parents->getAt(context, 0) : nullptr;
    const proto::ProtoObject* copyObj = context->newObject(true);
    if (parent) copyObj = copyObj->addParent(context, parent);
    copyObj = copyObj->setAttribute(context, dataName, s->asObject(context));
    return copyObj;
}

static const proto::ProtoObject* py_set_clear(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    self->setAttribute(context, dataName, context->newSet()->asObject(context));
    return PROTO_NONE;
}

static void add_iterable_to_set(proto::ProtoContext* context, const proto::ProtoObject* iterable, proto::ProtoSet*& acc) {
    const proto::ProtoObject* iterM = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it) return;
    const proto::ProtoObject* nextM = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextM || !nextM->asMethod(context)) return;
    for (;;) {
        const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        acc = const_cast<proto::ProtoSet*>(acc->add(context, val));
    }
}

static const proto::ProtoObject* py_set_union(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_NONE;
    proto::ProtoSet* acc = const_cast<proto::ProtoSet*>(context->newSet());
    const proto::ProtoSetIterator* it = s->getIterator(context);
    while (it->hasNext(context)) {
        acc = const_cast<proto::ProtoSet*>(acc->add(context, it->next(context)));
        it = it->advance(context);
    }
    for (unsigned long i = 0; i < posArgs->getSize(context); ++i)
        add_iterable_to_set(context, posArgs->getAt(context, static_cast<int>(i)), acc);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* parent = env->getSetPrototype();
    if (!parent) return PROTO_NONE;
    proto::ProtoObject* result = const_cast<proto::ProtoObject*>(parent->newChild(context, true));
    result->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), acc->asObject(context));
    return result;
}

static const proto::ProtoObject* py_set_intersection(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_NONE;
    proto::ProtoSet* acc = const_cast<proto::ProtoSet*>(context->newSet());
    if (posArgs->getSize(context) == 0) {
        const proto::ProtoSetIterator* it = s->getIterator(context);
        while (it->hasNext(context)) {
            acc = const_cast<proto::ProtoSet*>(acc->add(context, it->next(context)));
            it = it->advance(context);
        }
    } else {
        const proto::ProtoSetIterator* it = s->getIterator(context);
        while (it->hasNext(context)) {
            const proto::ProtoObject* val = it->next(context);
            bool in_all = true;
            for (unsigned long i = 0; i < posArgs->getSize(context) && in_all; ++i) {
                const proto::ProtoObject* other = posArgs->getAt(context, static_cast<int>(i));
                const proto::ProtoSet* os = set_data(context, other);
                if (os && os->has(context, val)) continue;
                const proto::ProtoObject* containsM = other->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__contains__"));
                if (!containsM || !containsM->asMethod(context)) { in_all = false; break; }
                const proto::ProtoList* arg = context->newList()->appendLast(context, val);
                const proto::ProtoObject* has = containsM->asMethod(context)(context, other, nullptr, arg, nullptr);
                in_all = (has && has == PROTO_TRUE);
            }
            if (in_all) acc = const_cast<proto::ProtoSet*>(acc->add(context, val));
            it = it->advance(context);
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* parent = env->getSetPrototype();
    if (!parent) return PROTO_NONE;
    proto::ProtoObject* result = const_cast<proto::ProtoObject*>(parent->newChild(context, true));
    result->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), acc->asObject(context));
    return result;
}

static const proto::ProtoObject* py_set_difference(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_NONE;
    proto::ProtoSet* acc = const_cast<proto::ProtoSet*>(context->newSet());
    const proto::ProtoSetIterator* it = s->getIterator(context);
    while (it->hasNext(context)) {
        acc = const_cast<proto::ProtoSet*>(acc->add(context, it->next(context)));
        it = it->advance(context);
    }
    for (unsigned long i = 0; i < posArgs->getSize(context); ++i) {
        const proto::ProtoObject* other = posArgs->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* iterM = other->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
        if (!iterM || !iterM->asMethod(context)) continue;
        const proto::ProtoObject* it2 = iterM->asMethod(context)(context, other, nullptr, context->newList(), nullptr);
        if (!it2) continue;
        const proto::ProtoObject* nextM = it2->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
        if (!nextM || !nextM->asMethod(context)) continue;
        for (;;) {
            const proto::ProtoObject* val = nextM->asMethod(context)(context, it2, nullptr, context->newList(), nullptr);
            if (!val || val == PROTO_NONE) break;
            if (acc->has(context, val)) acc = const_cast<proto::ProtoSet*>(acc->remove(context, val));
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* parent = env->getSetPrototype();
    if (!parent) return PROTO_NONE;
    proto::ProtoObject* result = const_cast<proto::ProtoObject*>(parent->newChild(context, true));
    result->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), acc->asObject(context));
    return result;
}

static const proto::ProtoObject* py_set_symmetric_difference(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_NONE;
    proto::ProtoSet* acc = const_cast<proto::ProtoSet*>(context->newSet());
    const proto::ProtoSetIterator* it = s->getIterator(context);
    while (it->hasNext(context)) {
        const proto::ProtoObject* val = it->next(context);
        acc = const_cast<proto::ProtoSet*>(acc->add(context, val));
        it = it->advance(context);
    }
    for (unsigned long i = 0; i < posArgs->getSize(context); ++i) {
        const proto::ProtoObject* other = posArgs->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* iterM = other->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
        if (!iterM || !iterM->asMethod(context)) continue;
        const proto::ProtoObject* it2 = iterM->asMethod(context)(context, other, nullptr, context->newList(), nullptr);
        if (!it2) continue;
        const proto::ProtoObject* nextM = it2->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
        if (!nextM || !nextM->asMethod(context)) continue;
        for (;;) {
            const proto::ProtoObject* val = nextM->asMethod(context)(context, it2, nullptr, context->newList(), nullptr);
            if (!val || val == PROTO_NONE) break;
            if (acc->has(context, val)) acc = const_cast<proto::ProtoSet*>(acc->remove(context, val));
            else acc = const_cast<proto::ProtoSet*>(acc->add(context, val));
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* parent = env->getSetPrototype();
    if (!parent) return PROTO_NONE;
    proto::ProtoObject* result = const_cast<proto::ProtoObject*>(parent->newChild(context, true));
    result->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), acc->asObject(context));
    return result;
}

static bool set_contains_all(proto::ProtoContext* context, const proto::ProtoObject* container, const proto::ProtoSet* elements) {
    const proto::ProtoSetIterator* it = elements->getIterator(context);
    while (it->hasNext(context)) {
        const proto::ProtoObject* val = it->next(context);
        const proto::ProtoSet* cs = set_data(context, container);
        if (cs && cs->has(context, val)) { it = it->advance(context); continue; }
        const proto::ProtoObject* containsM = container->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__contains__"));
        if (!containsM || !containsM->asMethod(context)) return false;
        const proto::ProtoList* arg = context->newList()->appendLast(context, val);
        const proto::ProtoObject* has = containsM->asMethod(context)(context, container, nullptr, arg, nullptr);
        if (!has || has != PROTO_TRUE) return false;
        it = it->advance(context);
    }
    return true;
}

static const proto::ProtoObject* py_set_issubset(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s || posArgs->getSize(context) < 1) return PROTO_NONE;
    return set_contains_all(context, posArgs->getAt(context, 0), s) ? PROTO_TRUE : PROTO_FALSE;
}

static bool iterable_contained_in(proto::ProtoContext* context, const proto::ProtoObject* container, const proto::ProtoObject* iterable) {
    const proto::ProtoObject* iterM = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return false;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it) return false;
    const proto::ProtoObject* nextM = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextM || !nextM->asMethod(context)) return false;
    for (;;) {
        const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        const proto::ProtoSet* cs = set_data(context, container);
        if (cs && cs->has(context, val)) continue;
        const proto::ProtoObject* containsM = container->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__contains__"));
        if (!containsM || !containsM->asMethod(context)) return false;
        const proto::ProtoList* arg = context->newList()->appendLast(context, val);
        const proto::ProtoObject* has = containsM->asMethod(context)(context, container, nullptr, arg, nullptr);
        if (!has || has != PROTO_TRUE) return false;
    }
    return true;
}

static const proto::ProtoObject* py_set_issuperset(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return PROTO_NONE;
    return iterable_contained_in(context, self, posArgs->getAt(context, 0)) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_set_pop(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s || s->getSize(context) == 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseKeyError(context, context->fromUTF8String("pop from empty set"));
        return PROTO_NONE;
    }
    const proto::ProtoSetIterator* it = s->getIterator(context);
    if (!it || !it->hasNext(context)) return PROTO_NONE;
    const proto::ProtoObject* value = it->next(context);
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSet* current = data && data->asSet(context) ? data->asSet(context) : context->newSet();
    const proto::ProtoSet* newSet = current->remove(context, value);
    self->setAttribute(context, dataName, newSet->asObject(context));
    return value;
}

static const proto::ProtoObject* py_set_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_NONE;
    const proto::ProtoSetIterator* it = s->getIterator(context);
    const proto::ProtoObject* iterObj = iterProto->newChild(context, true);
    const proto::ProtoString* iterItName = proto::ProtoString::fromUTF8String(context, "__iter_it__");
    iterObj->setAttribute(context, iterItName, it->asObject(context));
    return iterObj;
}

static const proto::ProtoObject* py_set_iter_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterItName = proto::ProtoString::fromUTF8String(context, "__iter_it__");
    const proto::ProtoObject* itObj = self->getAttribute(context, iterItName);
    if (!itObj || !itObj->asSetIterator(context)) return PROTO_NONE;
    const proto::ProtoSetIterator* it = itObj->asSetIterator(context);
    if (!it->hasNext(context)) return PROTO_NONE;
    const proto::ProtoObject* value = it->next(context);
    const proto::ProtoSetIterator* nextIt = it->advance(context);
    self->setAttribute(context, iterItName, nextIt->asObject(context));
    return value;
}

static const proto::ProtoObject* py_frozenset_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return context->fromInteger(0);
    return context->fromInteger(s->getSize(context));
}

static const proto::ProtoObject* py_frozenset_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s || positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    return s->has(context, positionalParameters->getAt(context, 0));
}

static const proto::ProtoObject* py_frozenset_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_FALSE;
    return s->getSize(context) > 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_frozenset_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return PROTO_NONE;
    const proto::ProtoSetIterator* it = s->getIterator(context);
    const proto::ProtoObject* iterObj = iterProto->newChild(context, true);
    const proto::ProtoString* iterItName = proto::ProtoString::fromUTF8String(context, "__iter_it__");
    iterObj->setAttribute(context, iterItName, it->asObject(context));
    return iterObj;
}

static const proto::ProtoObject* py_frozenset_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = set_data(context, self);
    if (!s) return context->fromInteger(0);
    unsigned long h = 0x345678UL;
    const proto::ProtoSetIterator* it = s->getIterator(context);
    while (it->hasNext(context)) {
        const proto::ProtoObject* val = it->next(context);
        h ^= (val->getHash(context) + (h << 6) + (h >> 2));
        it = it->advance(context);
    }
    return context->fromInteger(static_cast<long long>(h));
}

static const proto::ProtoObject* py_frozenset_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = self->getAttribute(context, iterProtoName);
    if (!iterProto || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* itObj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterAttr = itObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterAttr || !iterAttr->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* empty = context->newList();
    const proto::ProtoObject* iterResult = iterAttr->asMethod(context)(context, itObj, nullptr, empty, nullptr);
    if (!iterResult) return PROTO_NONE;
    const proto::ProtoObject* nextAttr = iterResult->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextAttr || !nextAttr->asMethod(context)) return PROTO_NONE;

    const proto::ProtoSet* acc = context->newSet();
    const proto::ProtoList* nextArgs = context->newList();
    for (;;) {
        const proto::ProtoObject* item = nextAttr->asMethod(context)(context, iterResult, nullptr, nextArgs, nullptr);
        if (!item || item == PROTO_NONE) break;
        acc = acc->add(context, item);
    }
    const proto::ProtoObject* fs = self->newChild(context, true);
    fs->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), acc->asObject(context));
    return fs;
}

static const proto::ProtoObject* py_int_format(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    long long v = self->asLong(context);
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", v);
    return context->fromUTF8String(buf);
}

static const proto::ProtoString* str_from_self(proto::ProtoContext* context, const proto::ProtoObject* self);

static const proto::ProtoObject* py_str_format_dunder(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = str_from_self(context, self);
    if (!s) return PROTO_NONE;
    return s->asObject(context);
}

static const proto::ProtoObject* py_int_bit_length(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    long long v = self->asLong(context);
    if (v == 0) return context->fromInteger(0);
    unsigned long long u;
    if (v == LLONG_MIN)
        u = static_cast<unsigned long long>(LLONG_MAX) + 1;
    else if (v < 0)
        u = static_cast<unsigned long long>(-v);
    else
        u = static_cast<unsigned long long>(v);
    int bits = 0;
    while (u) { bits++; u >>= 1; }
    return context->fromInteger(bits);
}

static const proto::ProtoObject* py_int_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return context->fromInteger(self->asLong(context));
}

static const proto::ProtoString* bytes_from_object(proto::ProtoContext* context, const proto::ProtoObject* obj) {
    if (obj->isString(context)) return obj->asString(context);
    const proto::ProtoObject* data = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
    return data && data->isString(context) ? data->asString(context) : nullptr;
}

static const proto::ProtoObject* py_int_from_bytes(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)self;
    if (posArgs->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoString* b = bytes_from_object(context, posArgs->getAt(context, 0));
    if (!b) return PROTO_NONE;
    std::string bytesStr;
    b->toUTF8String(context, bytesStr);
    std::string byteorderStr;
    posArgs->getAt(context, 1)->asString(context)->toUTF8String(context, byteorderStr);
    bool little = (byteorderStr == "little");
    long long result = 0;
    if (little) {
        for (size_t i = bytesStr.size(); i > 0; --i)
            result = (result << 8) | (static_cast<unsigned char>(bytesStr[i - 1]) & 0xff);
    } else {
        for (unsigned char c : bytesStr)
            result = (result << 8) | (c & 0xff);
    }
    return context->fromInteger(result);
}

static const proto::ProtoObject* py_int_to_bytes(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 2) return PROTO_NONE;
    long long v = self->asLong(context);
    int length = static_cast<int>(posArgs->getAt(context, 0)->asLong(context));
    std::string byteorderStr;
    posArgs->getAt(context, 1)->asString(context)->toUTF8String(context, byteorderStr);
    bool little = (byteorderStr == "little");
    std::string out;
    unsigned long long u = (v < 0) ? static_cast<unsigned long long>(-v) : static_cast<unsigned long long>(v);
    for (int i = 0; i < length; ++i) {
        out += static_cast<char>(u & 0xff);
        u >>= 8;
    }
    if (!little) std::reverse(out.begin(), out.end());
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(out.c_str()));
    return b;
}

static const proto::ProtoObject* py_str_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = str_from_self(context, self);
    if (!s) return context->fromInteger(0);
    return context->fromInteger(static_cast<long long>(s->getHash(context)));
}

static const proto::ProtoObject* py_tuple_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asTuple(context)) return context->fromInteger(0);
    const proto::ProtoTuple* t = data->asTuple(context);
    unsigned long h = 0x345678UL;
    long n = static_cast<long>(t->getSize(context));
    for (long i = 0; i < n; ++i) {
        const proto::ProtoObject* el = t->getAt(context, static_cast<int>(i));
        h ^= (el ? el->getHash(context) : 0) + (h << 6) + (h >> 2);
    }
    return context->fromInteger(static_cast<long long>(h));
}

static const proto::ProtoObject* py_tuple_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asTuple(context)) return context->fromInteger(0);
    return context->fromInteger(data->asTuple(context)->getSize(context));
}

static const proto::ProtoObject* py_tuple_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asTuple(context)) return PROTO_NONE;
    const proto::ProtoTuple* tuple = data->asTuple(context);
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    int index = static_cast<int>(positionalParameters->getAt(context, 0)->asLong(context));
    unsigned long size = tuple->getSize(context);
    if (index < 0) index += static_cast<int>(size);
    if (index < 0 || static_cast<unsigned long>(index) >= size) return PROTO_NONE;
    return tuple->getAt(context, index);
}

static const proto::ProtoObject* py_tuple_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;

    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asTuple(context)) return PROTO_NONE;

    const proto::ProtoObject* iterObj = iterProto->newChild(context, true);
    const proto::ProtoString* iterTupleName = proto::ProtoString::fromUTF8String(context, "__iter_tuple__");
    const proto::ProtoString* iterIndexName = proto::ProtoString::fromUTF8String(context, "__iter_index__");
    iterObj->setAttribute(context, iterTupleName, data);
    iterObj->setAttribute(context, iterIndexName, context->fromInteger(0));
    return iterObj;
}

static const proto::ProtoObject* py_tuple_iter_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* iterTupleName = proto::ProtoString::fromUTF8String(context, "__iter_tuple__");
    const proto::ProtoString* iterIndexName = proto::ProtoString::fromUTF8String(context, "__iter_index__");
    const proto::ProtoObject* tupleObj = self->getAttribute(context, iterTupleName);
    const proto::ProtoObject* indexObj = self->getAttribute(context, iterIndexName);
    if (!tupleObj || !tupleObj->asTuple(context) || !indexObj) return PROTO_NONE;

    const proto::ProtoTuple* tuple = tupleObj->asTuple(context);
    int index = static_cast<int>(indexObj->asLong(context));
    unsigned long size = tuple->getSize(context);
    if (static_cast<unsigned long>(index) >= size) return PROTO_NONE;

    const proto::ProtoObject* value = tuple->getAt(context, index);
    self->setAttribute(context, iterIndexName, context->fromInteger(index + 1));
    return value;
}

static const proto::ProtoObject* py_tuple_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asTuple(context)) return PROTO_FALSE;
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* value = positionalParameters->getAt(context, 0);
    return data->asTuple(context)->has(context, value) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_tuple_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asTuple(context)) return PROTO_FALSE;
    return data->asTuple(context)->getSize(context) > 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_tuple_index(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asTuple(context)) return PROTO_NONE;
    const proto::ProtoTuple* tuple = data->asTuple(context);
    const proto::ProtoObject* value = positionalParameters->getAt(context, 0);
    long long start = 0;
    long long stop = static_cast<long long>(tuple->getSize(context));
    if (positionalParameters->getSize(context) >= 2 && positionalParameters->getAt(context, 1)->isInteger(context))
        start = positionalParameters->getAt(context, 1)->asLong(context);
    if (positionalParameters->getSize(context) >= 3 && positionalParameters->getAt(context, 2)->isInteger(context))
        stop = positionalParameters->getAt(context, 2)->asLong(context);
    if (start < 0) start = 0;
    for (long long i = start; i < stop && static_cast<unsigned long>(i) < tuple->getSize(context); ++i) {
        const proto::ProtoObject* elem = tuple->getAt(context, static_cast<int>(i));
        if (list_elem_equal(context, elem, value))
            return context->fromInteger(i);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) env->raiseValueError(context, context->fromUTF8String("tuple.index(x): x not in tuple"));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_tuple_count(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return context->fromInteger(0);
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asTuple(context)) return context->fromInteger(0);
    const proto::ProtoTuple* tuple = data->asTuple(context);
    const proto::ProtoObject* value = positionalParameters->getAt(context, 0);
    long count = 0;
    unsigned long size = tuple->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* elem = tuple->getAt(context, static_cast<int>(i));
        if (list_elem_equal(context, elem, value)) count++;
    }
    return context->fromInteger(count);
}

static const proto::ProtoObject* py_str_encode(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    const proto::ProtoString* s = str_from_self(context, self);
    if (!s) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    std::string raw;
    s->toUTF8String(context, raw);
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    const proto::ProtoObject* b = bytesProto->newChild(context, true);
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.c_str()));
    return b;
}

static const proto::ProtoObject* py_bytes_decode(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    std::string raw;
    s->toUTF8String(context, raw);
    const proto::ProtoObject* strProto = env->getStrPrototype();
    if (!strProto) return PROTO_NONE;
    const proto::ProtoObject* st = strProto->newChild(context, true);
    st->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.c_str()));
    return st;
}

static const proto::ProtoObject* py_bytes_hex(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return context->fromUTF8String("");
    std::string raw;
    s->toUTF8String(context, raw);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        out += hex[c >> 4];
        out += hex[c & 0x0f];
    }
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_bytes_fromhex(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* strObj = posArgs->getAt(context, 0);
    if (!strObj->isString(context)) return PROTO_NONE;
    std::string hexStr;
    strObj->asString(context)->toUTF8String(context, hexStr);
    std::string raw;
    for (size_t i = 0; i + 1 < hexStr.size(); i += 2) {
        int hi = 0, lo = 0;
        char c1 = hexStr[i], c2 = hexStr[i + 1];
        if (c1 >= '0' && c1 <= '9') hi = c1 - '0';
        else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
        else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
        else continue;
        if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
        else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
        else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
        else continue;
        raw += static_cast<char>(hi * 16 + lo);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.c_str()));
    return b;
}

static const proto::ProtoObject* py_bytes_find(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s || posArgs->getSize(context) < 1) return context->fromInteger(-1);
    std::string haystack;
    s->toUTF8String(context, haystack);
    const proto::ProtoObject* sub = posArgs->getAt(context, 0);
    long long start = 0, end = static_cast<long long>(haystack.size());
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isInteger(context))
        start = posArgs->getAt(context, 1)->asLong(context);
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context))
        end = posArgs->getAt(context, 2)->asLong(context);
    std::string needle;
    if (sub->isInteger(context)) {
        long long v = sub->asLong(context);
        if (v < 0 || v > 255) return context->fromInteger(-1);
        needle = static_cast<char>(static_cast<unsigned char>(v));
    } else if (sub->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))) {
        const proto::ProtoString* subStr = bytes_data(context, sub);
        if (!subStr) return context->fromInteger(-1);
        subStr->toUTF8String(context, needle);
    } else
        return context->fromInteger(-1);
    size_t pos = haystack.find(needle, static_cast<size_t>(start));
    if (pos == std::string::npos || static_cast<long long>(pos) >= end)
        return context->fromInteger(-1);
    return context->fromInteger(static_cast<long long>(pos));
}

static const proto::ProtoObject* py_bytes_count(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s || posArgs->getSize(context) < 1) return context->fromInteger(0);
    std::string haystack;
    s->toUTF8String(context, haystack);
    const proto::ProtoObject* sub = posArgs->getAt(context, 0);
    long long start = 0, end = static_cast<long long>(haystack.size());
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isInteger(context))
        start = posArgs->getAt(context, 1)->asLong(context);
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context))
        end = posArgs->getAt(context, 2)->asLong(context);
    std::string needle;
    if (sub->isInteger(context)) {
        long long v = sub->asLong(context);
        if (v < 0 || v > 255) return context->fromInteger(0);
        needle = static_cast<char>(static_cast<unsigned char>(v));
    } else if (sub->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))) {
        const proto::ProtoString* subStr = bytes_data(context, sub);
        if (!subStr) return context->fromInteger(0);
        subStr->toUTF8String(context, needle);
    } else
        return context->fromInteger(0);
    size_t count = 0;
    size_t pos = static_cast<size_t>(start);
    while (pos < haystack.size() && static_cast<long long>(pos) < end) {
        size_t found = haystack.find(needle, pos);
        if (found == std::string::npos || static_cast<long long>(found) >= end) break;
        count++;
        pos = found + (needle.empty() ? 1 : needle.size());
    }
    return context->fromInteger(static_cast<long long>(count));
}

static void bytes_needle_from_arg(proto::ProtoContext* context, const proto::ProtoObject* arg, std::string& out) {
    if (arg->isInteger(context)) {
        long long v = arg->asLong(context);
        if (v >= 0 && v <= 255) out = static_cast<char>(static_cast<unsigned char>(v));
    } else if (arg->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))) {
        const proto::ProtoString* subStr = bytes_data(context, arg);
        if (subStr) subStr->toUTF8String(context, out);
    }
}

static const proto::ProtoObject* py_bytes_startswith(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s || posArgs->getSize(context) < 1) return PROTO_FALSE;
    std::string haystack;
    s->toUTF8String(context, haystack);
    std::string prefix;
    bytes_needle_from_arg(context, posArgs->getAt(context, 0), prefix);
    long long start = 0, end = static_cast<long long>(haystack.size());
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isInteger(context))
        start = posArgs->getAt(context, 1)->asLong(context);
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context))
        end = posArgs->getAt(context, 2)->asLong(context);
    if (start < 0) start = 0;
    if (end > static_cast<long long>(haystack.size())) end = static_cast<long long>(haystack.size());
    if (prefix.size() > static_cast<size_t>(end - start) || start > end) return PROTO_FALSE;
    return haystack.compare(static_cast<size_t>(start), prefix.size(), prefix) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_bytes_endswith(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s || posArgs->getSize(context) < 1) return PROTO_FALSE;
    std::string haystack;
    s->toUTF8String(context, haystack);
    std::string suffix;
    bytes_needle_from_arg(context, posArgs->getAt(context, 0), suffix);
    long long start = 0, end = static_cast<long long>(haystack.size());
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isInteger(context))
        start = posArgs->getAt(context, 1)->asLong(context);
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context))
        end = posArgs->getAt(context, 2)->asLong(context);
    if (start < 0) start = 0;
    if (end > static_cast<long long>(haystack.size())) end = static_cast<long long>(haystack.size());
    if (suffix.size() > static_cast<size_t>(end - start) || start > end) return PROTO_FALSE;
    size_t pos = static_cast<size_t>(end) - suffix.size();
    if (pos < static_cast<size_t>(start)) return PROTO_FALSE;
    return haystack.compare(pos, suffix.size(), suffix) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_bytes_index(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* r = py_bytes_find(context, self, nullptr, posArgs, nullptr);
    if (!r || !r->isInteger(context) || r->asLong(context) < 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseValueError(context, context->fromUTF8String("subsection not found"));
        return PROTO_NONE;
    }
    return r;
}

static const proto::ProtoObject* py_bytes_rfind(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s || posArgs->getSize(context) < 1) return context->fromInteger(-1);
    std::string haystack;
    s->toUTF8String(context, haystack);
    const proto::ProtoObject* sub = posArgs->getAt(context, 0);
    long long start = 0, end = static_cast<long long>(haystack.size());
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isInteger(context))
        start = posArgs->getAt(context, 1)->asLong(context);
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context))
        end = posArgs->getAt(context, 2)->asLong(context);
    std::string needle;
    if (sub->isInteger(context)) {
        long long v = sub->asLong(context);
        if (v < 0 || v > 255) return context->fromInteger(-1);
        needle = static_cast<char>(static_cast<unsigned char>(v));
    } else if (sub->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))) {
        const proto::ProtoString* subStr = bytes_data(context, sub);
        if (!subStr) return context->fromInteger(-1);
        subStr->toUTF8String(context, needle);
    } else
        return context->fromInteger(-1);
    if (start >= end || static_cast<size_t>(start) >= haystack.size())
        return context->fromInteger(-1);
    size_t len = static_cast<size_t>(std::min(end, static_cast<long long>(haystack.size())) - start);
    std::string slice = haystack.substr(static_cast<size_t>(start), len);
    size_t found = slice.rfind(needle);
    if (found == std::string::npos)
        return context->fromInteger(-1);
    return context->fromInteger(static_cast<long long>(start) + static_cast<long long>(found));
}

static const proto::ProtoObject* py_bytes_rindex(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* r = py_bytes_rfind(context, self, nullptr, posArgs, nullptr);
    if (!r || !r->isInteger(context) || r->asLong(context) < 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseValueError(context, context->fromUTF8String("subsection not found"));
        return PROTO_NONE;
    }
    return r;
}

static const proto::ProtoObject* py_bytes_replace(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s || posArgs->getSize(context) < 2) return PROTO_NONE;
    std::string raw;
    s->toUTF8String(context, raw);
    std::string old_str, new_str;
    bytes_needle_from_arg(context, posArgs->getAt(context, 0), old_str);
    bytes_needle_from_arg(context, posArgs->getAt(context, 1), new_str);
    long long count = -1;
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context))
        count = posArgs->getAt(context, 2)->asLong(context);
    std::string out;
    size_t start = 0;
    long long n = 0;
    while (count < 0 || n < count) {
        size_t pos = raw.find(old_str, start);
        if (pos == std::string::npos) break;
        out += raw.substr(start, pos - start);
        out += new_str;
        start = pos + old_str.size();
        n++;
    }
    out += raw.substr(start);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(out.c_str()));
    return b;
}

static const proto::ProtoObject* py_bytes_isdigit(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return PROTO_FALSE;
    std::string raw;
    s->toUTF8String(context, raw);
    if (raw.empty()) return PROTO_FALSE;
    for (unsigned char c : raw)
        if (!std::isdigit(c)) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_bytes_isalpha(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return PROTO_FALSE;
    std::string raw;
    s->toUTF8String(context, raw);
    if (raw.empty()) return PROTO_FALSE;
    for (unsigned char c : raw)
        if (!std::isalpha(c)) return PROTO_FALSE;
    return PROTO_TRUE;
}

static bool bytes_byte_in_chars(unsigned char c, const std::string& ch) {
    if (ch.empty()) return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
    return ch.find(c) != std::string::npos;
}

static const proto::ProtoObject* py_bytes_lstrip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return PROTO_NONE;
    std::string raw;
    s->toUTF8String(context, raw);
    std::string chars;
    if (posArgs && posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))) {
        const proto::ProtoString* chStr = bytes_data(context, posArgs->getAt(context, 0));
        if (chStr) chStr->toUTF8String(context, chars);
    }
    size_t start = 0;
    while (start < raw.size() && bytes_byte_in_chars(static_cast<unsigned char>(raw[start]), chars)) start++;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.substr(start).c_str()));
    return b;
}

static const proto::ProtoObject* py_bytes_rstrip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return PROTO_NONE;
    std::string raw;
    s->toUTF8String(context, raw);
    std::string chars;
    if (posArgs && posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))) {
        const proto::ProtoString* chStr = bytes_data(context, posArgs->getAt(context, 0));
        if (chStr) chStr->toUTF8String(context, chars);
    }
    size_t end = raw.size();
    while (end > 0 && bytes_byte_in_chars(static_cast<unsigned char>(raw[end - 1]), chars)) end--;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.substr(0, end).c_str()));
    return b;
}

static const proto::ProtoObject* py_bytes_strip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return PROTO_NONE;
    std::string raw;
    s->toUTF8String(context, raw);
    std::string chars;
    if (posArgs && posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))) {
        const proto::ProtoString* chStr = bytes_data(context, posArgs->getAt(context, 0));
        if (chStr) chStr->toUTF8String(context, chars);
    }
    size_t start = 0;
    while (start < raw.size() && bytes_byte_in_chars(static_cast<unsigned char>(raw[start]), chars)) start++;
    size_t end = raw.size();
    while (end > start && bytes_byte_in_chars(static_cast<unsigned char>(raw[end - 1]), chars)) end--;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.substr(start, end - start).c_str()));
    return b;
}

static std::string bytes_sep_from_arg(proto::ProtoContext* context, const proto::ProtoObject* arg) {
    if (!arg || arg->isInteger(context)) {
        long long v = arg && arg->isInteger(context) ? arg->asLong(context) : 32;
        if (v < 0 || v > 255) return " ";
        return std::string(1, static_cast<char>(static_cast<unsigned char>(v)));
    }
    if (arg->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))) {
        const proto::ProtoString* s = bytes_data(context, arg);
        if (s) { std::string r; s->toUTF8String(context, r); return r; }
    }
    return " ";
}

static const proto::ProtoObject* py_bytes_split(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return PROTO_NONE;
    std::string raw;
    s->toUTF8String(context, raw);
    std::string sep = (posArgs && posArgs->getSize(context) >= 1) ? bytes_sep_from_arg(context, posArgs->getAt(context, 0)) : " ";
    if (sep.empty()) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    const proto::ProtoList* result = context->newList();
    size_t start = 0;
    for (;;) {
        size_t pos = raw.find(sep, start);
        if (pos == std::string::npos) {
            proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
            b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.substr(start).c_str()));
            result = result->appendLast(context, b);
            break;
        }
        proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
        b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.substr(start, pos - start).c_str()));
        result = result->appendLast(context, b);
        start = pos + sep.size();
    }
    return result->asObject(context);
}

static const proto::ProtoObject* py_bytes_join(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* sep = bytes_data(context, self);
    if (!sep || !posArgs || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string sepStr;
    sep->toUTF8String(context, sepStr);
    const proto::ProtoObject* iterable = posArgs->getAt(context, 0);
    const proto::ProtoObject* iterM = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* nextM = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextM || !nextM->asMethod(context)) return PROTO_NONE;
    std::string out;
    bool first = true;
    for (;;) {
        const proto::ProtoObject* item = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!item || item == PROTO_NONE) break;
        if (!first) out += sepStr;
        first = false;
        if (item->isInteger(context)) {
            long long v = item->asLong(context);
            if (v >= 0 && v <= 255) out += static_cast<char>(static_cast<unsigned char>(v));
        } else if (item->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))) {
            const proto::ProtoString* bs = bytes_data(context, item);
            if (bs) { std::string p; bs->toUTF8String(context, p); out += p; }
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(out.c_str()));
    return b;
}

static const proto::ProtoString* str_from_self(proto::ProtoContext* context, const proto::ProtoObject* self) {
    if (self->isString(context)) return self->asString(context);
    const proto::ProtoObject* data = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
    return data && data->isString(context) ? data->asString(context) : nullptr;
}

static const proto::ProtoObject* py_str_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    const proto::ProtoStringIterator* it = str->getIterator(context);
    return it ? it->asObject(context) : PROTO_NONE;
}

static const proto::ProtoObject* py_str_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* item = positionalParameters->getAt(context, 0);
    if (!item->isString(context)) return PROTO_FALSE;
    std::string haystack;
    str->toUTF8String(context, haystack);
    std::string needle;
    item->asString(context)->toUTF8String(context, needle);
    return haystack.find(needle) != std::string::npos ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_str_find(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || positionalParameters->getSize(context) < 1) return context->fromInteger(-1);
    std::string haystack;
    str->toUTF8String(context, haystack);
    const proto::ProtoObject* subObj = positionalParameters->getAt(context, 0);
    if (!subObj->isString(context)) return context->fromInteger(-1);
    std::string needle;
    subObj->asString(context)->toUTF8String(context, needle);
    size_t pos = haystack.find(needle);
    return context->fromInteger(pos == std::string::npos ? -1 : static_cast<long long>(pos));
}

static const proto::ProtoObject* py_str_index(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* result = py_str_find(context, self, parentLink, positionalParameters, keywordParameters);
    if (!result || !result->isInteger(context)) return PROTO_NONE;
    long long pos = result->asLong(context);
    if (pos < 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseValueError(context, context->fromUTF8String("substring not found"));
        return PROTO_NONE;
    }
    return result;
}

static const proto::ProtoObject* py_str_rfind(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 1) return context->fromInteger(-1);
    std::string haystack;
    str->toUTF8String(context, haystack);
    const proto::ProtoObject* subObj = posArgs->getAt(context, 0);
    if (!subObj->isString(context)) return context->fromInteger(-1);
    std::string needle;
    subObj->asString(context)->toUTF8String(context, needle);
    long long start = 0, end = static_cast<long long>(haystack.size());
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isInteger(context))
        start = posArgs->getAt(context, 1)->asLong(context);
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context))
        end = posArgs->getAt(context, 2)->asLong(context);
    if (start < 0) start = 0;
    if (end > static_cast<long long>(haystack.size())) end = static_cast<long long>(haystack.size());
    if (start >= end) return context->fromInteger(-1);
    std::string slice = haystack.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
    size_t found = slice.rfind(needle);
    if (found == std::string::npos) return context->fromInteger(-1);
    return context->fromInteger(static_cast<long long>(start) + static_cast<long long>(found));
}

static const proto::ProtoObject* py_str_rindex(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* r = py_str_rfind(context, self, nullptr, posArgs, nullptr);
    if (!r || !r->isInteger(context) || r->asLong(context) < 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseValueError(context, context->fromUTF8String("substring not found"));
        return PROTO_NONE;
    }
    return r;
}

static const proto::ProtoObject* py_str_count(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || positionalParameters->getSize(context) < 1) return context->fromInteger(0);
    std::string haystack;
    str->toUTF8String(context, haystack);
    const proto::ProtoObject* subObj = positionalParameters->getAt(context, 0);
    if (!subObj->isString(context)) return context->fromInteger(0);
    std::string needle;
    subObj->asString(context)->toUTF8String(context, needle);
    if (needle.empty()) return context->fromInteger(static_cast<long long>(haystack.size()) + 1);
    long long start = 0;
    long long end = static_cast<long long>(haystack.size());
    if (positionalParameters->getSize(context) >= 2 && positionalParameters->getAt(context, 1) != PROTO_NONE)
        start = positionalParameters->getAt(context, 1)->asLong(context);
    if (positionalParameters->getSize(context) >= 3 && positionalParameters->getAt(context, 2) != PROTO_NONE)
        end = positionalParameters->getAt(context, 2)->asLong(context);
    if (start < 0) start += static_cast<long long>(haystack.size());
    if (end < 0) end += static_cast<long long>(haystack.size());
    if (start < 0) start = 0;
    if (end > static_cast<long long>(haystack.size())) end = static_cast<long long>(haystack.size());
    if (start >= end) return context->fromInteger(0);
    size_t count = 0;
    size_t pos = static_cast<size_t>(start);
    const size_t endPos = static_cast<size_t>(end);
    while (pos < endPos) {
        size_t found = haystack.find(needle, pos);
        if (found == std::string::npos || found >= endPos) break;
        count++;
        pos = found + needle.size();
    }
    return context->fromInteger(static_cast<long long>(count));
}

static const proto::ProtoObject* py_str_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    return str->getSize(context) > 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_str_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    long long size = static_cast<long long>(s.size());
    const proto::ProtoObject* indexObj = positionalParameters->getAt(context, 0);

    SliceBounds sb = get_slice_bounds(context, indexObj, size);
    if (sb.isSlice && sb.step == 1) {
        std::string sub = s.substr(static_cast<size_t>(sb.start), static_cast<size_t>(sb.stop - sb.start));
        return context->fromUTF8String(sub.c_str());
    }

    const proto::ProtoList* sliceList = indexObj->asList(context);
    if (sliceList && sliceList->getSize(context) >= 2) {
        long long start = sliceList->getAt(context, 0)->asLong(context);
        long long stop = sliceList->getAt(context, 1)->asLong(context);
        long long step = sliceList->getSize(context) >= 3 ? sliceList->getAt(context, 2)->asLong(context) : 1;
        if (step != 1) return PROTO_NONE;
        if (start < 0) start += size;
        if (stop < 0) stop += size;
        if (start < 0) start = 0;
        if (stop > size) stop = size;
        if (start > stop) start = stop;
        std::string sub = s.substr(static_cast<size_t>(start), static_cast<size_t>(stop - start));
        return context->fromUTF8String(sub.c_str());
    }

    int idx = static_cast<int>(indexObj->asLong(context));
    if (idx < 0) idx += static_cast<int>(size);
    if (idx < 0 || static_cast<unsigned long>(idx) >= s.size()) return PROTO_NONE;
    char c[2] = { s[static_cast<size_t>(idx)], '\0' };
    return context->fromUTF8String(c);
}

static const proto::ProtoObject* py_slice_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* startName = proto::ProtoString::fromUTF8String(context, "start");
    const proto::ProtoString* stopName = proto::ProtoString::fromUTF8String(context, "stop");
    const proto::ProtoString* stepName = proto::ProtoString::fromUTF8String(context, "step");
    unsigned long n = positionalParameters->getSize(context);
    const proto::ProtoObject* start = PROTO_NONE;
    const proto::ProtoObject* stop = PROTO_NONE;
    const proto::ProtoObject* step = PROTO_NONE;
    if (n == 1) {
        stop = positionalParameters->getAt(context, 0);
    } else if (n >= 2) {
        start = positionalParameters->getAt(context, 0);
        stop = positionalParameters->getAt(context, 1);
        if (n >= 3) step = positionalParameters->getAt(context, 2);
    }
    const proto::ProtoObject* sliceObj = context->newObject(true);
    sliceObj = sliceObj->addParent(context, self);
    sliceObj = sliceObj->setAttribute(context, startName, start ? start : context->fromInteger(0));
    sliceObj = sliceObj->setAttribute(context, stopName, stop ? stop : context->fromInteger(0));
    sliceObj = sliceObj->setAttribute(context, stepName, step ? step : context->fromInteger(1));
    return sliceObj;
}

static const proto::ProtoObject* py_slice_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* start = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "start"));
    const proto::ProtoObject* stop = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "stop"));
    const proto::ProtoObject* step = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "step"));
    std::string s_start = (!start || start == PROTO_NONE) ? "None" : (start->isInteger(context) ? std::to_string(start->asLong(context)) : "None");
    std::string s_stop = (!stop || stop == PROTO_NONE) ? "None" : (stop->isInteger(context) ? std::to_string(stop->asLong(context)) : "None");
    std::string out = "slice(" + s_start + ", " + s_stop;
    if (step && step != PROTO_NONE && (!step->isInteger(context) || step->asLong(context) != 1))
        out += ", " + (step->isInteger(context) ? std::to_string(step->asLong(context)) : "None");
    out += ")";
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_str_upper(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c -= 32;
    }
    return context->fromUTF8String(s.c_str());
}

static const proto::ProtoObject* py_str_format(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string tpl;
    str->toUTF8String(context, tpl);
    std::string out;
    unsigned long idx = 0;
    for (size_t i = 0; i < tpl.size(); ++i) {
        if (tpl[i] == '{' && i + 1 < tpl.size() && tpl[i + 1] == '}') {
            if (idx < positionalParameters->getSize(context)) {
                const proto::ProtoObject* obj = positionalParameters->getAt(context, static_cast<int>(idx));
                const proto::ProtoObject* strM = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__str__"));
                if (strM && strM->asMethod(context)) {
                    std::string s;
                    const proto::ProtoObject* so = strM->asMethod(context)(context, obj, nullptr, context->newList(), nullptr);
                    if (so && so->isString(context)) so->asString(context)->toUTF8String(context, s);
                    out += s;
                }
            }
            idx++;
            i++;
        } else {
            out += tpl[i];
        }
    }
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_str_lower(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    return context->fromUTF8String(s.c_str());
}

static const proto::ProtoObject* py_str_split(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string sep = " ";
    if (posArgs->getSize(context) >= 1) {
        const proto::ProtoObject* sepObj = posArgs->getAt(context, 0);
        if (sepObj->isString(context)) sepObj->asString(context)->toUTF8String(context, sep);
    }
    const proto::ProtoList* result = context->newList();
    if (sep.empty()) return PROTO_NONE;
    size_t start = 0;
    for (;;) {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            result = result->appendLast(context, context->fromUTF8String(s.substr(start).c_str()));
            break;
        }
        result = result->appendLast(context, context->fromUTF8String(s.substr(start, pos - start).c_str()));
        start = pos + sep.size();
    }
    return result->asObject(context);
}

static const proto::ProtoObject* py_str_rsplit(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string sep = " ";
    if (posArgs->getSize(context) >= 1) {
        const proto::ProtoObject* sepObj = posArgs->getAt(context, 0);
        if (sepObj->isString(context)) sepObj->asString(context)->toUTF8String(context, sep);
    }
    long long maxsplit = -1;
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isInteger(context))
        maxsplit = posArgs->getAt(context, 1)->asLong(context);
    const proto::ProtoList* result = context->newList();
    if (sep.empty()) return PROTO_NONE;
    std::vector<std::string> parts;
    size_t end = s.size();
    while (maxsplit != 0) {
        size_t pos = std::string::npos;
        if (end >= sep.size()) {
            for (size_t i = end - sep.size(); i != static_cast<size_t>(-1); --i) {
                if (s.compare(i, sep.size(), sep) == 0) { pos = i; break; }
            }
        }
        if (pos == std::string::npos) {
            parts.insert(parts.begin(), s.substr(0, end));
            break;
        }
        parts.insert(parts.begin(), s.substr(pos + sep.size(), end - (pos + sep.size())));
        end = pos;
        if (maxsplit > 0) maxsplit--;
    }
    if (end > 0) parts.insert(parts.begin(), s.substr(0, end));
    for (const auto& p : parts)
        result = result->appendLast(context, context->fromUTF8String(p.c_str()));
    return result->asObject(context);
}

static const proto::ProtoObject* py_str_splitlines(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    bool keepends = false;
    if (posArgs && posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->isInteger(context))
        keepends = posArgs->getAt(context, 0)->asLong(context) != 0;
    const proto::ProtoList* result = context->newList();
    size_t start = 0;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\n') {
            std::string line = s.substr(start, i - start);
            if (keepends) line += '\n';
            result = result->appendLast(context, context->fromUTF8String(line.c_str()));
            start = i + 1;
            i++;
        } else if (s[i] == '\r') {
            std::string line = s.substr(start, i - start);
            if (keepends) line += (i + 1 < s.size() && s[i + 1] == '\n') ? "\r\n" : "\r";
            result = result->appendLast(context, context->fromUTF8String(line.c_str()));
            i = (i + 1 < s.size() && s[i + 1] == '\n') ? i + 2 : i + 1;
            start = i;
        } else {
            i++;
        }
    }
    result = result->appendLast(context, context->fromUTF8String(s.substr(start).c_str()));
    return result->asObject(context);
}

static bool is_ascii_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static const proto::ProtoObject* py_str_strip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    size_t start = 0;
    while (start < s.size() && is_ascii_whitespace(s[start])) start++;
    size_t end = s.size();
    while (end > start && is_ascii_whitespace(s[end - 1])) end--;
    return context->fromUTF8String(s.substr(start, end - start).c_str());
}

static bool char_in_chars(unsigned char c, const std::string& ch) {
    if (ch.empty()) return is_ascii_whitespace(c);
    return ch.find(c) != std::string::npos;
}

static const proto::ProtoObject* py_str_lstrip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string chars;
    if (posArgs && posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->isString(context))
        posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, chars);
    size_t start = 0;
    while (start < s.size() && char_in_chars(static_cast<unsigned char>(s[start]), chars)) start++;
    return context->fromUTF8String(s.substr(start).c_str());
}

static const proto::ProtoObject* py_str_rstrip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string chars;
    if (posArgs && posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->isString(context))
        posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, chars);
    size_t end = s.size();
    while (end > 0 && char_in_chars(static_cast<unsigned char>(s[end - 1]), chars)) end--;
    return context->fromUTF8String(s.substr(0, end).c_str());
}

static const proto::ProtoObject* py_str_removeprefix(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || !posArgs || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string prefix;
    if (posArgs->getAt(context, 0)->isString(context))
        posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, prefix);
    if (prefix.size() <= s.size() && s.compare(0, prefix.size(), prefix) == 0)
        return context->fromUTF8String(s.substr(prefix.size()).c_str());
    return context->fromUTF8String(s.c_str());
}

static const proto::ProtoObject* py_str_removesuffix(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || !posArgs || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string suffix;
    if (posArgs->getAt(context, 0)->isString(context))
        posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, suffix);
    if (suffix.size() <= s.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
        return context->fromUTF8String(s.substr(0, s.size() - suffix.size()).c_str());
    return context->fromUTF8String(s.c_str());
}

static const proto::ProtoObject* py_str_startswith(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string prefix;
    posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, prefix);
    if (prefix.size() > s.size()) return PROTO_FALSE;
    return s.compare(0, prefix.size(), prefix) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_str_endswith(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string suffix;
    posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, suffix);
    if (suffix.size() > s.size()) return PROTO_FALSE;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_str_replace(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 2) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string oldStr, newStr;
    posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, oldStr);
    posArgs->getAt(context, 1)->asString(context)->toUTF8String(context, newStr);
    int count = -1;
    if (posArgs->getSize(context) >= 3) {
        count = static_cast<int>(posArgs->getAt(context, 2)->asLong(context));
    }
    std::string result;
    size_t pos = 0;
    int replaced = 0;
    while (pos < s.size() && (count < 0 || replaced < count)) {
        size_t found = s.find(oldStr, pos);
        if (found == std::string::npos) {
            result += s.substr(pos);
            break;
        }
        result += s.substr(pos, found - pos);
        result += newStr;
        pos = found + oldStr.size();
        replaced++;
    }
    if (pos < s.size())
        result += s.substr(pos);
    return context->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* py_str_capitalize(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return context->fromUTF8String("");
    std::string r;
    r += static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    for (size_t i = 1; i < s.size(); ++i)
        r += static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return context->fromUTF8String(r.c_str());
}

static const proto::ProtoObject* py_str_title(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string r;
    bool after_boundary = true;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_') {
            r += after_boundary ? static_cast<char>(std::toupper(c)) : static_cast<char>(std::tolower(c));
            after_boundary = false;
        } else {
            r += static_cast<char>(c);
            after_boundary = true;
        }
    }
    return context->fromUTF8String(r.c_str());
}

static const proto::ProtoObject* py_str_swapcase(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string r;
    for (unsigned char c : s) {
        if (std::isupper(c)) r += static_cast<char>(std::tolower(c));
        else if (std::islower(c)) r += static_cast<char>(std::toupper(c));
        else r += static_cast<char>(c);
    }
    return context->fromUTF8String(r.c_str());
}

static const proto::ProtoObject* py_str_casefold(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string r;
    for (unsigned char c : s)
        r += static_cast<char>(std::tolower(c));
    return context->fromUTF8String(r.c_str());
}

static const proto::ProtoObject* py_str_isalpha(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_FALSE;
    for (unsigned char c : s)
        if (!std::isalpha(c)) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_str_isdigit(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_FALSE;
    for (unsigned char c : s)
        if (!std::isdigit(c)) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_str_isdecimal(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_FALSE;
    for (unsigned char c : s)
        if (!std::isdigit(c)) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_str_isnumeric(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_FALSE;
    for (unsigned char c : s)
        if (!std::isdigit(c)) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_str_isspace(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_FALSE;
    for (unsigned char c : s)
        if (!std::isspace(c)) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_str_isalnum(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_FALSE;
    for (unsigned char c : s)
        if (!std::isalnum(c)) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_str_isupper(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_FALSE;
    for (unsigned char c : s)
        if (std::isalpha(c) && !std::isupper(c)) return PROTO_FALSE;
    return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isalpha(c); }) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_str_islower(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_FALSE;
    for (unsigned char c : s)
        if (std::isalpha(c) && !std::islower(c)) return PROTO_FALSE;
    return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isalpha(c); }) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_str_isprintable(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_TRUE;
    for (unsigned char c : s)
        if (!std::isprint(c)) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_str_isascii(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    for (unsigned char c : s)
        if (c > 127) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_str_isidentifier(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_FALSE;
    std::string s;
    str->toUTF8String(context, s);
    if (s.empty()) return PROTO_FALSE;
    unsigned char c0 = static_cast<unsigned char>(s[0]);
    if (!std::isalpha(c0) && c0 != '_') return PROTO_FALSE;
    for (size_t i = 1; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (!std::isalnum(c) && c != '_') return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_str_center(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    int width = static_cast<int>(posArgs->getAt(context, 0)->asLong(context));
    char fillchar = ' ';
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isString(context)) {
        std::string fc;
        posArgs->getAt(context, 1)->asString(context)->toUTF8String(context, fc);
        if (!fc.empty()) fillchar = fc[0];
    }
    if (width <= static_cast<int>(s.size())) return context->fromUTF8String(s.c_str());
    int pad = width - static_cast<int>(s.size());
    int left = pad / 2, right = pad - left;
    std::string result(left, fillchar);
    result += s;
    result.append(right, fillchar);
    return context->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* py_str_ljust(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    int width = static_cast<int>(posArgs->getAt(context, 0)->asLong(context));
    char fillchar = ' ';
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isString(context)) {
        std::string fc;
        posArgs->getAt(context, 1)->asString(context)->toUTF8String(context, fc);
        if (!fc.empty()) fillchar = fc[0];
    }
    if (width <= static_cast<int>(s.size())) return context->fromUTF8String(s.c_str());
    s.append(width - static_cast<int>(s.size()), fillchar);
    return context->fromUTF8String(s.c_str());
}

static const proto::ProtoObject* py_str_rjust(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    int width = static_cast<int>(posArgs->getAt(context, 0)->asLong(context));
    char fillchar = ' ';
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isString(context)) {
        std::string fc;
        posArgs->getAt(context, 1)->asString(context)->toUTF8String(context, fc);
        if (!fc.empty()) fillchar = fc[0];
    }
    if (width <= static_cast<int>(s.size())) return context->fromUTF8String(s.c_str());
    std::string result(width - static_cast<int>(s.size()), fillchar);
    result += s;
    return context->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* py_str_expandtabs(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    int tabsize = 8;
    if (posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->isInteger(context))
        tabsize = static_cast<int>(posArgs->getAt(context, 0)->asLong(context));
    if (tabsize <= 0) tabsize = 1;
    std::string result;
    int col = 0;
    for (unsigned char c : s) {
        if (c == '\t') {
            int spaces = tabsize - (col % tabsize);
            result.append(spaces, ' ');
            col += spaces;
        } else {
            result += c;
            if (c == '\n') col = 0;
            else col++;
        }
    }
    return context->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* py_str_zfill(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    int width = static_cast<int>(posArgs->getAt(context, 0)->asLong(context));
    if (width <= static_cast<int>(s.size())) return context->fromUTF8String(s.c_str());
    size_t sign = 0;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) sign = 1;
    std::string result(sign, s[0]);
    result.append(width - static_cast<int>(s.size()), '0');
    result += s.substr(sign);
    return context->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* py_str_partition(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string sep;
    posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, sep);
    if (sep.empty()) return PROTO_NONE;
    size_t pos = s.find(sep);
    if (pos == std::string::npos) {
        const proto::ProtoList* lst = context->newList()
            ->appendLast(context, context->fromUTF8String(s.c_str()))
            ->appendLast(context, context->fromUTF8String(""))
            ->appendLast(context, context->fromUTF8String(""));
        const proto::ProtoTuple* tup = context->newTupleFromList(lst);
        return tup ? tup->asObject(context) : PROTO_NONE;
    }
    std::string before = s.substr(0, pos);
    std::string after = s.substr(pos + sep.size());
    const proto::ProtoList* lst = context->newList()
        ->appendLast(context, context->fromUTF8String(before.c_str()))
        ->appendLast(context, context->fromUTF8String(sep.c_str()))
        ->appendLast(context, context->fromUTF8String(after.c_str()));
    const proto::ProtoTuple* tup = context->newTupleFromList(lst);
    return tup ? tup->asObject(context) : PROTO_NONE;
}

static const proto::ProtoObject* py_str_rpartition(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string sep;
    posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, sep);
    if (sep.empty()) return PROTO_NONE;
    size_t pos = s.rfind(sep);
    if (pos == std::string::npos) {
        const proto::ProtoList* lst = context->newList()
            ->appendLast(context, context->fromUTF8String(""))
            ->appendLast(context, context->fromUTF8String(""))
            ->appendLast(context, context->fromUTF8String(s.c_str()));
        const proto::ProtoTuple* tup = context->newTupleFromList(lst);
        return tup ? tup->asObject(context) : PROTO_NONE;
    }
    std::string before = s.substr(0, pos);
    std::string after = s.substr(pos + sep.size());
    const proto::ProtoList* lst = context->newList()
        ->appendLast(context, context->fromUTF8String(before.c_str()))
        ->appendLast(context, context->fromUTF8String(sep.c_str()))
        ->appendLast(context, context->fromUTF8String(after.c_str()));
    const proto::ProtoTuple* tup = context->newTupleFromList(lst);
    return tup ? tup->asObject(context) : PROTO_NONE;
}

static const proto::ProtoObject* py_str_join(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* sep = str_from_self(context, self);
    if (!sep || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string sepStr;
    sep->toUTF8String(context, sepStr);
    const proto::ProtoObject* iterable = posArgs->getAt(context, 0);
    const proto::ProtoObject* iterM = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it || it == PROTO_NONE) return context->fromUTF8String("");
    const proto::ProtoObject* nextM = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextM || !nextM->asMethod(context)) return context->fromUTF8String("");
    std::string out;
    bool first = true;
    for (;;) {
        const proto::ProtoObject* item = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!item || item == PROTO_NONE) break;
        if (!first) out += sepStr;
        first = false;
        if (item->isString(context)) {
            std::string part;
            item->asString(context)->toUTF8String(context, part);
            out += part;
        }
    }
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_dict_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();

    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = data && data->asSparseList(context) ? data->asSparseList(context) : nullptr;
    if (!dict) return context->fromUTF8String("{}");

    unsigned long size = keys->getSize(context);
    unsigned long limit = 20;
    std::string out = "{";
    for (unsigned long i = 0; i < size && i < limit; ++i) {
        if (i > 0) out += ", ";
        const proto::ProtoObject* key = keys->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* value = dict->getAt(context, key->getHash(context));
        out += repr_object(context, key);
        out += ": ";
        out += repr_object(context, value);
    }
    if (size > limit) out += ", ...";
    out += "}";
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_dict_str(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    return py_dict_repr(context, self, parentLink, positionalParameters, keywordParameters);
}

static const proto::ProtoObject* py_dict_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = data && data->asSparseList(context) ? data->asSparseList(context) : nullptr;
    if (!dict) return PROTO_FALSE;
    return dict->getSize(context) > 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_dict_keys(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();
    return keys->asObject(context);
}

static const proto::ProtoObject* py_dict_values(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();

    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = data && data->asSparseList(context) ? data->asSparseList(context) : nullptr;
    if (!dict) return context->newList()->asObject(context);

    const proto::ProtoList* values = context->newList();
    unsigned long size = keys->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* key = keys->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* val = dict->getAt(context, key->getHash(context));
        values = values->appendLast(context, val ? val : PROTO_NONE);
    }
    return values->asObject(context);
}

static const proto::ProtoObject* py_dict_items(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();

    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = data && data->asSparseList(context) ? data->asSparseList(context) : nullptr;
    if (!dict) return context->newList()->asObject(context);

    const proto::ProtoList* items = context->newList();
    unsigned long size = keys->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* key = keys->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* val = dict->getAt(context, key->getHash(context));
        const proto::ProtoList* pairList = context->newList()->appendLast(context, key)->appendLast(context, val ? val : PROTO_NONE);
        const proto::ProtoTuple* pairTuple = context->newTupleFromList(pairList);
        items = items->appendLast(context, pairTuple->asObject(context));
    }
    return items->asObject(context);
}

static const proto::ProtoObject* py_dict_get(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* defaultVal = positionalParameters->getSize(context) > 1 ? positionalParameters->getAt(context, 1) : PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = data && data->asSparseList(context) ? data->asSparseList(context) : nullptr;
    if (!dict) return defaultVal;
    const proto::ProtoObject* val = dict->getAt(context, key->getHash(context));
    return val ? val : defaultVal;
}

static const proto::ProtoObject* py_dict_update(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* other = positionalParameters->getAt(context, 0);
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");

    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();
    const proto::ProtoObject* dataObj = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = dataObj && dataObj->asSparseList(context) ? dataObj->asSparseList(context) : context->newSparseList();

    const proto::ProtoObject* otherKeysObj = other->getAttribute(context, keysName);
    const proto::ProtoList* otherKeys = otherKeysObj && otherKeysObj->asList(context) ? otherKeysObj->asList(context) : context->newList();
    const proto::ProtoObject* otherDataObj = other->getAttribute(context, dataName);
    const proto::ProtoSparseList* otherDict = otherDataObj && otherDataObj->asSparseList(context) ? otherDataObj->asSparseList(context) : nullptr;

    if (otherDict) {
        for (unsigned long i = 0; i < otherKeys->getSize(context); ++i) {
            const proto::ProtoObject* key = otherKeys->getAt(context, static_cast<int>(i));
            const proto::ProtoObject* value = otherDict->getAt(context, key->getHash(context));
            if (!value) continue;
            unsigned long hash = key->getHash(context);
            dict = dict->setAt(context, hash, value);
            bool found = false;
            for (unsigned long j = 0; j < keys->getSize(context); ++j) {
                if (keys->getAt(context, static_cast<int>(j))->getHash(context) == hash) { found = true; break; }
            }
            if (!found) keys = keys->appendLast(context, key);
        }
    }
    self->setAttribute(context, keysName, keys->asObject(context));
    self->setAttribute(context, dataName, dict->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_clear(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    self->setAttribute(context, keysName, context->newList()->asObject(context));
    self->setAttribute(context, dataName, context->newSparseList()->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_fromkeys(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(context, 0);
    const proto::ProtoObject* value = posArgs->getSize(context) >= 2 ? posArgs->getAt(context, 1) : PROTO_NONE;

    const proto::ProtoObject* iterM = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* itObj = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!itObj) return PROTO_NONE;

    const proto::ProtoObject* nextM = itObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextM || !nextM->asMethod(context)) return PROTO_NONE;

    const proto::ProtoList* keysList = context->newList();
    const proto::ProtoSparseList* sparse = context->newSparseList();
    for (;;) {
        const proto::ProtoObject* key = nextM->asMethod(context)(context, itObj, nullptr, context->newList(), nullptr);
        if (!key || key == PROTO_NONE) break;
        unsigned long hash = key->getHash(context);
        bool found = false;
        for (unsigned long j = 0; j < keysList->getSize(context); ++j) {
            if (keysList->getAt(context, static_cast<int>(j))->getHash(context) == hash) { found = true; break; }
        }
        if (!found) keysList = keysList->appendLast(context, key);
        sparse = sparse->setAt(context, hash, value);
    }

    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* result = context->newObject(true);
    result = result->addParent(context, self);
    result = result->setAttribute(context, keysName, keysList->asObject(context));
    result = result->setAttribute(context, dataName, sparse->asObject(context));
    return result;
}

static const proto::ProtoObject* py_dict_copy(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoObject* dataObj = self->getAttribute(context, dataName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();
    const proto::ProtoSparseList* dict = dataObj && dataObj->asSparseList(context) ? dataObj->asSparseList(context) : context->newSparseList();

    const proto::ProtoList* parents = self->getParents(context);
    const proto::ProtoObject* parent = parents && parents->getSize(context) > 0 ? parents->getAt(context, 0) : nullptr;
    const proto::ProtoObject* copyObj = context->newObject(true);
    if (parent) copyObj = copyObj->addParent(context, parent);
    copyObj = copyObj->setAttribute(context, keysName, keys->asObject(context));
    copyObj = copyObj->setAttribute(context, dataName, dict->asObject(context));
    return copyObj;
}

static const proto::ProtoObject* py_dict_or(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* other = posArgs->getAt(context, 0);
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* selfKeysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* selfKeys = selfKeysObj && selfKeysObj->asList(context) ? selfKeysObj->asList(context) : context->newList();
    const proto::ProtoObject* selfDataObj = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* selfDict = selfDataObj && selfDataObj->asSparseList(context) ? selfDataObj->asSparseList(context) : context->newSparseList();
    const proto::ProtoList* keys = context->newList();
    const proto::ProtoSparseList* dict = context->newSparseList();
    for (unsigned long i = 0; i < selfKeys->getSize(context); ++i) {
        const proto::ProtoObject* key = selfKeys->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* value = selfDict->getAt(context, key->getHash(context));
        if (!value) continue;
        keys = keys->appendLast(context, key);
        dict = dict->setAt(context, key->getHash(context), value);
    }
    const proto::ProtoObject* otherKeysObj = other->getAttribute(context, keysName);
    const proto::ProtoList* otherKeys = otherKeysObj && otherKeysObj->asList(context) ? otherKeysObj->asList(context) : context->newList();
    const proto::ProtoObject* otherDataObj = other->getAttribute(context, dataName);
    const proto::ProtoSparseList* otherDict = otherDataObj && otherDataObj->asSparseList(context) ? otherDataObj->asSparseList(context) : nullptr;
    if (otherDict) {
        for (unsigned long i = 0; i < otherKeys->getSize(context); ++i) {
            const proto::ProtoObject* key = otherKeys->getAt(context, static_cast<int>(i));
            const proto::ProtoObject* value = otherDict->getAt(context, key->getHash(context));
            if (!value) continue;
            unsigned long hash = key->getHash(context);
            dict = dict->setAt(context, hash, value);
            bool found = false;
            for (unsigned long j = 0; j < keys->getSize(context); ++j) {
                if (keys->getAt(context, static_cast<int>(j))->getHash(context) == hash) { found = true; break; }
            }
            if (!found) keys = keys->appendLast(context, key);
        }
    }
    const proto::ProtoList* parents = self->getParents(context);
    const proto::ProtoObject* parent = parents && parents->getSize(context) > 0 ? parents->getAt(context, 0) : nullptr;
    const proto::ProtoObject* result = context->newObject(true);
    if (parent) result = result->addParent(context, parent);
    result = result->setAttribute(context, keysName, keys->asObject(context));
    result = result->setAttribute(context, dataName, dict->asObject(context));
    return result;
}

static const proto::ProtoObject* py_dict_ror(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* other = posArgs->getAt(context, 0);
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* otherKeysObj = other->getAttribute(context, keysName);
    const proto::ProtoList* otherKeys = otherKeysObj && otherKeysObj->asList(context) ? otherKeysObj->asList(context) : context->newList();
    const proto::ProtoObject* otherDataObj = other->getAttribute(context, dataName);
    const proto::ProtoSparseList* otherDict = otherDataObj && otherDataObj->asSparseList(context) ? otherDataObj->asSparseList(context) : context->newSparseList();
    const proto::ProtoList* keys = context->newList();
    const proto::ProtoSparseList* dict = context->newSparseList();
    for (unsigned long i = 0; i < otherKeys->getSize(context); ++i) {
        const proto::ProtoObject* key = otherKeys->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* value = otherDict->getAt(context, key->getHash(context));
        if (!value) continue;
        keys = keys->appendLast(context, key);
        dict = dict->setAt(context, key->getHash(context), value);
    }
    const proto::ProtoObject* selfKeysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* selfKeys = selfKeysObj && selfKeysObj->asList(context) ? selfKeysObj->asList(context) : context->newList();
    const proto::ProtoObject* selfDataObj = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* selfDict = selfDataObj && selfDataObj->asSparseList(context) ? selfDataObj->asSparseList(context) : nullptr;
    if (selfDict) {
        for (unsigned long i = 0; i < selfKeys->getSize(context); ++i) {
            const proto::ProtoObject* key = selfKeys->getAt(context, static_cast<int>(i));
            const proto::ProtoObject* value = selfDict->getAt(context, key->getHash(context));
            if (!value) continue;
            unsigned long hash = key->getHash(context);
            dict = dict->setAt(context, hash, value);
            bool found = false;
            for (unsigned long j = 0; j < keys->getSize(context); ++j) {
                if (keys->getAt(context, static_cast<int>(j))->getHash(context) == hash) { found = true; break; }
            }
            if (!found) keys = keys->appendLast(context, key);
        }
    }
    const proto::ProtoList* parents = other->getParents(context);
    const proto::ProtoObject* parent = parents && parents->getSize(context) > 0 ? parents->getAt(context, 0) : nullptr;
    const proto::ProtoObject* result = context->newObject(true);
    if (parent) result = result->addParent(context, parent);
    result = result->setAttribute(context, keysName, keys->asObject(context));
    result = result->setAttribute(context, dataName, dict->asObject(context));
    return result;
}

static const proto::ProtoObject* py_dict_setdefault(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* defaultVal = positionalParameters->getSize(context) > 1 ? positionalParameters->getAt(context, 1) : PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = data && data->asSparseList(context) ? data->asSparseList(context) : nullptr;
    if (!dict) return PROTO_NONE;
    unsigned long hash = key->getHash(context);
    const proto::ProtoObject* existing = dict->getAt(context, hash);
    if (existing) return existing;
    const proto::ProtoSparseList* newDict = dict->setAt(context, hash, defaultVal);
    self->setAttribute(context, dataName, newDict->asObject(context));

    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keysList = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();
    keysList = keysList->appendLast(context, key);
    self->setAttribute(context, keysName, keysList->asObject(context));
    return defaultVal;
}

static const proto::ProtoObject* py_dict_pop(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseValueError(context, context->fromUTF8String("pop expected at least 1 argument, got 0"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* defaultVal = positionalParameters->getSize(context) > 1 ? positionalParameters->getAt(context, 1) : nullptr;
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoObject* dataObj = self->getAttribute(context, dataName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();
    const proto::ProtoSparseList* dict = dataObj && dataObj->asSparseList(context) ? dataObj->asSparseList(context) : nullptr;
    if (!dict) {
        if (defaultVal) return defaultVal;
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseValueError(context, context->fromUTF8String("KeyError"));
        return PROTO_NONE;
    }
    unsigned long hash = key->getHash(context);
    if (!dict->has(context, hash)) {
        if (defaultVal) return defaultVal;
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseValueError(context, context->fromUTF8String("KeyError"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* value = dict->getAt(context, hash);
    const proto::ProtoSparseList* newDict = dict->removeAt(context, hash);
    const proto::ProtoList* newKeys = context->newList();
    unsigned long size = keys->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* k = keys->getAt(context, static_cast<int>(i));
        if (k->getHash(context) != hash)
            newKeys = newKeys->appendLast(context, k);
    }
    self->setAttribute(context, dataName, newDict->asObject(context));
    self->setAttribute(context, keysName, newKeys->asObject(context));
    return value;
}

static const proto::ProtoObject* py_dict_popitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoObject* dataObj = self->getAttribute(context, dataName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();
    const proto::ProtoSparseList* dict = dataObj && dataObj->asSparseList(context) ? dataObj->asSparseList(context) : nullptr;
    if (!dict || keys->getSize(context) == 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseKeyError(context, context->fromUTF8String("popitem(): dictionary is empty"));
        return PROTO_NONE;
    }
    unsigned long lastIdx = keys->getSize(context) - 1;
    const proto::ProtoObject* key = keys->getAt(context, static_cast<int>(lastIdx));
    unsigned long hash = key->getHash(context);
    const proto::ProtoObject* value = dict->getAt(context, hash);
    const proto::ProtoSparseList* newDict = dict->removeAt(context, hash);
    const proto::ProtoList* newKeys = context->newList();
    for (unsigned long i = 0; i < lastIdx; ++i)
        newKeys = newKeys->appendLast(context, keys->getAt(context, static_cast<int>(i)));
    self->setAttribute(context, dataName, newDict->asObject(context));
    self->setAttribute(context, keysName, newKeys->asObject(context));
    const proto::ProtoList* pair = context->newList()->appendLast(context, key)->appendLast(context, value);
    const proto::ProtoTuple* tup = context->newTupleFromList(pair);
    return tup ? tup->asObject(context) : PROTO_NONE;
}

// --- PythonEnvironment Implementation ---

static std::unordered_map<proto::ProtoContext*, PythonEnvironment*> s_contextToEnv;

void PythonEnvironment::registerContext(proto::ProtoContext* ctx, PythonEnvironment* env) {
    s_contextToEnv[ctx] = env;
}

void PythonEnvironment::unregisterContext(proto::ProtoContext* ctx) {
    s_contextToEnv.erase(ctx);
}

PythonEnvironment* PythonEnvironment::fromContext(proto::ProtoContext* ctx) {
    auto it = s_contextToEnv.find(ctx);
    return it != s_contextToEnv.end() ? it->second : nullptr;
}

PythonEnvironment::PythonEnvironment(const std::string& stdLibPath, const std::vector<std::string>& searchPaths,
                                     const std::vector<std::string>& argv) : space(), argv_(argv) {
    context = new proto::ProtoContext(&space);
    registerContext(context, this);
    initializeRootObjects(stdLibPath, searchPaths);
}

PythonEnvironment::~PythonEnvironment() {
    unregisterContext(context);
    delete context;
}

void PythonEnvironment::raiseKeyError(proto::ProtoContext* ctx, const proto::ProtoObject* key) {
    if (!keyErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
    const proto::ProtoObject* exc = keyErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), keyErrorType, args, nullptr);
    if (exc) setPendingException(exc);
}

void PythonEnvironment::raiseValueError(proto::ProtoContext* ctx, const proto::ProtoObject* msg) {
    if (!valueErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, msg);
    const proto::ProtoObject* exc = valueErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), valueErrorType, args, nullptr);
    if (exc) setPendingException(exc);
}

void PythonEnvironment::initializeRootObjects(const std::string& stdLibPath, const std::vector<std::string>& searchPaths) {
    const proto::ProtoString* py_init = proto::ProtoString::fromUTF8String(context, "__init__");
    const proto::ProtoString* py_repr = proto::ProtoString::fromUTF8String(context, "__repr__");
    const proto::ProtoString* py_str = proto::ProtoString::fromUTF8String(context, "__str__");
    const proto::ProtoString* py_class = proto::ProtoString::fromUTF8String(context, "__class__");
    const proto::ProtoString* py_name = proto::ProtoString::fromUTF8String(context, "__name__");
    const proto::ProtoString* py_append = proto::ProtoString::fromUTF8String(context, "append");
    const proto::ProtoString* py_getitem = proto::ProtoString::fromUTF8String(context, "__getitem__");
    const proto::ProtoString* py_setitem = proto::ProtoString::fromUTF8String(context, "__setitem__");
    const proto::ProtoString* py_len = proto::ProtoString::fromUTF8String(context, "__len__");
    const proto::ProtoString* py_iter = proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* py_next = proto::ProtoString::fromUTF8String(context, "__next__");
    const proto::ProtoString* py_iter_proto = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoString* py_contains = proto::ProtoString::fromUTF8String(context, "__contains__");
    const proto::ProtoString* py_eq = proto::ProtoString::fromUTF8String(context, "__eq__");
    const proto::ProtoString* py_lt = proto::ProtoString::fromUTF8String(context, "__lt__");
    const proto::ProtoString* py_le = proto::ProtoString::fromUTF8String(context, "__le__");
    const proto::ProtoString* py_gt = proto::ProtoString::fromUTF8String(context, "__gt__");
    const proto::ProtoString* py_ge = proto::ProtoString::fromUTF8String(context, "__ge__");
    const proto::ProtoString* py_bool = proto::ProtoString::fromUTF8String(context, "__bool__");
    const proto::ProtoString* py_pop = proto::ProtoString::fromUTF8String(context, "pop");
    const proto::ProtoString* py_extend = proto::ProtoString::fromUTF8String(context, "extend");
    const proto::ProtoString* py_insert = proto::ProtoString::fromUTF8String(context, "insert");
    const proto::ProtoString* py_remove = proto::ProtoString::fromUTF8String(context, "remove");
    const proto::ProtoString* py_keys = proto::ProtoString::fromUTF8String(context, "keys");
    const proto::ProtoString* py_values = proto::ProtoString::fromUTF8String(context, "values");
    const proto::ProtoString* py_items = proto::ProtoString::fromUTF8String(context, "items");
    const proto::ProtoString* py_get = proto::ProtoString::fromUTF8String(context, "get");
    const proto::ProtoString* py_setdefault = proto::ProtoString::fromUTF8String(context, "setdefault");
    const proto::ProtoString* py_update = proto::ProtoString::fromUTF8String(context, "update");
    const proto::ProtoString* py_clear = proto::ProtoString::fromUTF8String(context, "clear");
    const proto::ProtoString* py_reverse = proto::ProtoString::fromUTF8String(context, "reverse");
    const proto::ProtoString* py_sort = proto::ProtoString::fromUTF8String(context, "sort");
    const proto::ProtoString* py_copy = proto::ProtoString::fromUTF8String(context, "copy");
    const proto::ProtoString* py_upper = proto::ProtoString::fromUTF8String(context, "upper");
    const proto::ProtoString* py_lower = proto::ProtoString::fromUTF8String(context, "lower");
    const proto::ProtoString* py_format = proto::ProtoString::fromUTF8String(context, "format");
    const proto::ProtoString* py_split = proto::ProtoString::fromUTF8String(context, "split");
    const proto::ProtoString* py_join = proto::ProtoString::fromUTF8String(context, "join");
    const proto::ProtoString* py_strip = proto::ProtoString::fromUTF8String(context, "strip");
    const proto::ProtoString* py_lstrip = proto::ProtoString::fromUTF8String(context, "lstrip");
    const proto::ProtoString* py_rstrip = proto::ProtoString::fromUTF8String(context, "rstrip");
    const proto::ProtoString* py_replace = proto::ProtoString::fromUTF8String(context, "replace");
    const proto::ProtoString* py_startswith = proto::ProtoString::fromUTF8String(context, "startswith");
    const proto::ProtoString* py_endswith = proto::ProtoString::fromUTF8String(context, "endswith");
    const proto::ProtoString* py_add = proto::ProtoString::fromUTF8String(context, "add");

    // 1. Create 'object' base
    objectPrototype = context->newObject(true);
    const proto::ProtoString* py_format_dunder = proto::ProtoString::fromUTF8String(context, "__format__");
    objectPrototype = objectPrototype->setAttribute(context, py_init, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_init));
    objectPrototype = objectPrototype->setAttribute(context, py_repr, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_repr));
    objectPrototype = objectPrototype->setAttribute(context, py_str, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_str));
    objectPrototype = objectPrototype->setAttribute(context, py_format_dunder, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_format));
    objectPrototype = objectPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__"), context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_call));

    // 2. Create 'type'
    typePrototype = context->newObject(true);
    typePrototype = typePrototype->addParent(context, objectPrototype);
    typePrototype = typePrototype->setAttribute(context, py_class, typePrototype);

    // 3. Circularity: object's class is type
    objectPrototype = objectPrototype->setAttribute(context, py_class, typePrototype);

    // 4. Basic types
    intPrototype = context->newObject(true);
    intPrototype = intPrototype->addParent(context, objectPrototype);
    const proto::ProtoString* py_hash = proto::ProtoString::fromUTF8String(context, "__hash__");
    intPrototype = intPrototype->setAttribute(context, py_class, typePrototype);
    intPrototype = intPrototype->setAttribute(context, py_name, context->fromUTF8String("int"));
    intPrototype = intPrototype->setAttribute(context, py_hash, context->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_hash));
    intPrototype = intPrototype->setAttribute(context, py_format_dunder, context->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_format));
    const proto::ProtoString* py_bit_length = proto::ProtoString::fromUTF8String(context, "bit_length");
    intPrototype = intPrototype->setAttribute(context, py_bit_length, context->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_bit_length));
    const proto::ProtoString* py_from_bytes = proto::ProtoString::fromUTF8String(context, "from_bytes");
    const proto::ProtoString* py_to_bytes = proto::ProtoString::fromUTF8String(context, "to_bytes");
    intPrototype = intPrototype->setAttribute(context, py_from_bytes, context->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_from_bytes));
    intPrototype = intPrototype->setAttribute(context, py_to_bytes, context->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_to_bytes));

    strPrototype = context->newObject(true);
    strPrototype = strPrototype->addParent(context, objectPrototype);
    strPrototype = strPrototype->setAttribute(context, py_class, typePrototype);
    strPrototype = strPrototype->setAttribute(context, py_name, context->fromUTF8String("str"));
    strPrototype = strPrototype->setAttribute(context, py_iter, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_iter));
    strPrototype = strPrototype->setAttribute(context, py_getitem, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_getitem));
    strPrototype = strPrototype->setAttribute(context, py_contains, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_contains));
    strPrototype = strPrototype->setAttribute(context, py_bool, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_bool));
    strPrototype = strPrototype->setAttribute(context, py_upper, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_upper));
    strPrototype = strPrototype->setAttribute(context, py_lower, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_lower));
    strPrototype = strPrototype->setAttribute(context, py_format, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_format));
    strPrototype = strPrototype->setAttribute(context, py_format_dunder, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_format_dunder));
    strPrototype = strPrototype->setAttribute(context, py_hash, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_hash));
    strPrototype = strPrototype->setAttribute(context, py_split, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_split));
    strPrototype = strPrototype->setAttribute(context, py_join, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_join));
    strPrototype = strPrototype->setAttribute(context, py_strip, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_strip));
    strPrototype = strPrototype->setAttribute(context, py_lstrip, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_lstrip));
    strPrototype = strPrototype->setAttribute(context, py_rstrip, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rstrip));
    strPrototype = strPrototype->setAttribute(context, py_replace, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_replace));
    strPrototype = strPrototype->setAttribute(context, py_startswith, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_startswith));
    strPrototype = strPrototype->setAttribute(context, py_endswith, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_endswith));
    const proto::ProtoString* py_find = proto::ProtoString::fromUTF8String(context, "find");
    const proto::ProtoString* py_index = proto::ProtoString::fromUTF8String(context, "index");
    strPrototype = strPrototype->setAttribute(context, py_find, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_find));
    strPrototype = strPrototype->setAttribute(context, py_index, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_index));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "rfind"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rfind));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "rindex"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rindex));
    const proto::ProtoString* py_count = proto::ProtoString::fromUTF8String(context, "count");
    strPrototype = strPrototype->setAttribute(context, py_count, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_count));
    const proto::ProtoString* py_rsplit = proto::ProtoString::fromUTF8String(context, "rsplit");
    strPrototype = strPrototype->setAttribute(context, py_rsplit, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rsplit));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "splitlines"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_splitlines));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "removeprefix"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_removeprefix));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "removesuffix"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_removesuffix));
    const proto::ProtoString* py_center = proto::ProtoString::fromUTF8String(context, "center");
    const proto::ProtoString* py_ljust = proto::ProtoString::fromUTF8String(context, "ljust");
    const proto::ProtoString* py_rjust = proto::ProtoString::fromUTF8String(context, "rjust");
    const proto::ProtoString* py_zfill = proto::ProtoString::fromUTF8String(context, "zfill");
    const proto::ProtoString* py_partition = proto::ProtoString::fromUTF8String(context, "partition");
    const proto::ProtoString* py_rpartition = proto::ProtoString::fromUTF8String(context, "rpartition");
    strPrototype = strPrototype->setAttribute(context, py_center, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_center));
    strPrototype = strPrototype->setAttribute(context, py_ljust, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_ljust));
    strPrototype = strPrototype->setAttribute(context, py_rjust, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rjust));
    strPrototype = strPrototype->setAttribute(context, py_zfill, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_zfill));
    strPrototype = strPrototype->setAttribute(context, py_partition, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_partition));
    strPrototype = strPrototype->setAttribute(context, py_rpartition, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rpartition));
    const proto::ProtoString* py_expandtabs = proto::ProtoString::fromUTF8String(context, "expandtabs");
    strPrototype = strPrototype->setAttribute(context, py_expandtabs, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_expandtabs));
    const proto::ProtoString* py_capitalize = proto::ProtoString::fromUTF8String(context, "capitalize");
    const proto::ProtoString* py_title = proto::ProtoString::fromUTF8String(context, "title");
    const proto::ProtoString* py_swapcase = proto::ProtoString::fromUTF8String(context, "swapcase");
    strPrototype = strPrototype->setAttribute(context, py_capitalize, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_capitalize));
    strPrototype = strPrototype->setAttribute(context, py_title, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_title));
    strPrototype = strPrototype->setAttribute(context, py_swapcase, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_swapcase));
    const proto::ProtoString* py_casefold = proto::ProtoString::fromUTF8String(context, "casefold");
    const proto::ProtoString* py_isalpha = proto::ProtoString::fromUTF8String(context, "isalpha");
    const proto::ProtoString* py_isdigit = proto::ProtoString::fromUTF8String(context, "isdigit");
    const proto::ProtoString* py_isspace = proto::ProtoString::fromUTF8String(context, "isspace");
    const proto::ProtoString* py_isalnum = proto::ProtoString::fromUTF8String(context, "isalnum");
    strPrototype = strPrototype->setAttribute(context, py_casefold, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_casefold));
    strPrototype = strPrototype->setAttribute(context, py_isalpha, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isalpha));
    strPrototype = strPrototype->setAttribute(context, py_isdigit, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isdigit));
    strPrototype = strPrototype->setAttribute(context, py_isspace, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isspace));
    strPrototype = strPrototype->setAttribute(context, py_isalnum, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isalnum));
    const proto::ProtoString* py_isupper = proto::ProtoString::fromUTF8String(context, "isupper");
    const proto::ProtoString* py_islower = proto::ProtoString::fromUTF8String(context, "islower");
    strPrototype = strPrototype->setAttribute(context, py_isupper, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isupper));
    strPrototype = strPrototype->setAttribute(context, py_islower, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_islower));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "isidentifier"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isidentifier));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "isprintable"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isprintable));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "isascii"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isascii));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "isdecimal"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isdecimal));
    strPrototype = strPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "isnumeric"), context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isnumeric));

    listPrototype = context->newObject(true);
    listPrototype = listPrototype->addParent(context, objectPrototype);
    listPrototype = listPrototype->setAttribute(context, py_class, typePrototype);
    listPrototype = listPrototype->setAttribute(context, py_name, context->fromUTF8String("list"));
    listPrototype = listPrototype->setAttribute(context, py_append, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_append));
    listPrototype = listPrototype->setAttribute(context, py_len, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_len));
    listPrototype = listPrototype->setAttribute(context, py_getitem, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_getitem));
    listPrototype = listPrototype->setAttribute(context, py_setitem, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_setitem));
    listPrototype = listPrototype->setAttribute(context, py_iter, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_iter));
    listPrototype = listPrototype->setAttribute(context, py_contains, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_contains));
    listPrototype = listPrototype->setAttribute(context, py_eq, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_eq));
    listPrototype = listPrototype->setAttribute(context, py_lt, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_lt));
    listPrototype = listPrototype->setAttribute(context, py_le, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_le));
    listPrototype = listPrototype->setAttribute(context, py_gt, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_gt));
    listPrototype = listPrototype->setAttribute(context, py_ge, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_ge));
    listPrototype = listPrototype->setAttribute(context, py_repr, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_repr));
    listPrototype = listPrototype->setAttribute(context, py_str, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_str));
    listPrototype = listPrototype->setAttribute(context, py_bool, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_bool));
    listPrototype = listPrototype->setAttribute(context, py_pop, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_pop));
    listPrototype = listPrototype->setAttribute(context, py_extend, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_extend));
    listPrototype = listPrototype->setAttribute(context, py_insert, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_insert));
    listPrototype = listPrototype->setAttribute(context, py_remove, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_remove));
    listPrototype = listPrototype->setAttribute(context, py_clear, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_clear));
    listPrototype = listPrototype->setAttribute(context, py_reverse, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_reverse));
    listPrototype = listPrototype->setAttribute(context, py_sort, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_sort));
    listPrototype = listPrototype->setAttribute(context, py_copy, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_copy));
    const proto::ProtoString* py_list_index_name = proto::ProtoString::fromUTF8String(context, "index");
    const proto::ProtoString* py_list_count_name = proto::ProtoString::fromUTF8String(context, "count");
    listPrototype = listPrototype->setAttribute(context, py_list_index_name, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_index));
    listPrototype = listPrototype->setAttribute(context, py_list_count_name, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_count));
    const proto::ProtoString* py_mul = proto::ProtoString::fromUTF8String(context, "__mul__");
    const proto::ProtoString* py_rmul = proto::ProtoString::fromUTF8String(context, "__rmul__");
    listPrototype = listPrototype->setAttribute(context, py_mul, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_mul));
    listPrototype = listPrototype->setAttribute(context, py_rmul, context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_mul));

    const proto::ProtoObject* listIterProto = context->newObject(true);
    listIterProto = listIterProto->addParent(context, objectPrototype);
    listIterProto = listIterProto->setAttribute(context, py_next, context->fromMethod(const_cast<proto::ProtoObject*>(listIterProto), py_list_iter_next));
    listPrototype = listPrototype->setAttribute(context, py_iter_proto, listIterProto);
    const proto::ProtoObject* listReverseIterProto = context->newObject(true);
    listReverseIterProto = listReverseIterProto->addParent(context, objectPrototype);
    listReverseIterProto = listReverseIterProto->setAttribute(context, py_next, context->fromMethod(const_cast<proto::ProtoObject*>(listReverseIterProto), py_list_reversed_next));
    listPrototype = listPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_prototype__"), listReverseIterProto);
    listPrototype = listPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed__"), context->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_reversed));

    dictPrototype = context->newObject(true);
    dictPrototype = dictPrototype->addParent(context, objectPrototype);
    dictPrototype = dictPrototype->setAttribute(context, py_class, typePrototype);
    dictPrototype = dictPrototype->setAttribute(context, py_name, context->fromUTF8String("dict"));
    dictPrototype = dictPrototype->setAttribute(context, py_getitem, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_getitem));
    dictPrototype = dictPrototype->setAttribute(context, py_setitem, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_setitem));
    dictPrototype = dictPrototype->setAttribute(context, py_len, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_len));
    dictPrototype = dictPrototype->setAttribute(context, py_iter, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_iter));
    dictPrototype = dictPrototype->setAttribute(context, py_iter_proto, listIterProto);
    dictPrototype = dictPrototype->setAttribute(context, py_contains, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_contains));
    dictPrototype = dictPrototype->setAttribute(context, py_eq, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_eq));
    dictPrototype = dictPrototype->setAttribute(context, py_lt, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_lt));
    dictPrototype = dictPrototype->setAttribute(context, py_le, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_le));
    dictPrototype = dictPrototype->setAttribute(context, py_gt, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_gt));
    dictPrototype = dictPrototype->setAttribute(context, py_ge, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_ge));
    dictPrototype = dictPrototype->setAttribute(context, py_repr, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_repr));
    dictPrototype = dictPrototype->setAttribute(context, py_str, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_str));
    dictPrototype = dictPrototype->setAttribute(context, py_bool, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_bool));
    dictPrototype = dictPrototype->setAttribute(context, py_keys, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_keys));
    dictPrototype = dictPrototype->setAttribute(context, py_values, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_values));
    dictPrototype = dictPrototype->setAttribute(context, py_items, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_items));
    dictPrototype = dictPrototype->setAttribute(context, py_get, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_get));
    dictPrototype = dictPrototype->setAttribute(context, py_setdefault, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_setdefault));
    dictPrototype = dictPrototype->setAttribute(context, py_pop, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_pop));
    const proto::ProtoString* py_popitem = proto::ProtoString::fromUTF8String(context, "popitem");
    dictPrototype = dictPrototype->setAttribute(context, py_popitem, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_popitem));
    dictPrototype = dictPrototype->setAttribute(context, py_update, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_update));
    dictPrototype = dictPrototype->setAttribute(context, py_clear, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_clear));
    dictPrototype = dictPrototype->setAttribute(context, py_copy, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_copy));
    dictPrototype = dictPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "fromkeys"), context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_fromkeys));
    dictPrototype = dictPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__or__"), context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_or));
    dictPrototype = dictPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__ror__"), context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_ror));

    tuplePrototype = context->newObject(true);
    tuplePrototype = tuplePrototype->addParent(context, objectPrototype);
    tuplePrototype = tuplePrototype->setAttribute(context, py_class, typePrototype);
    tuplePrototype = tuplePrototype->setAttribute(context, py_name, context->fromUTF8String("tuple"));
    tuplePrototype = tuplePrototype->setAttribute(context, py_len, context->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_len));
    tuplePrototype = tuplePrototype->setAttribute(context, py_getitem, context->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_getitem));
    tuplePrototype = tuplePrototype->setAttribute(context, py_iter, context->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_iter));
    tuplePrototype = tuplePrototype->setAttribute(context, py_contains, context->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_contains));
    tuplePrototype = tuplePrototype->setAttribute(context, py_bool, context->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_bool));
    tuplePrototype = tuplePrototype->setAttribute(context, py_hash, context->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_hash));
    const proto::ProtoString* py_tuple_index_name = proto::ProtoString::fromUTF8String(context, "index");
    const proto::ProtoString* py_tuple_count_name = proto::ProtoString::fromUTF8String(context, "count");
    tuplePrototype = tuplePrototype->setAttribute(context, py_tuple_index_name, context->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_index));
    tuplePrototype = tuplePrototype->setAttribute(context, py_tuple_count_name, context->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_count));

    const proto::ProtoObject* tupleIterProto = context->newObject(true);
    tupleIterProto = tupleIterProto->addParent(context, objectPrototype);
    tupleIterProto = tupleIterProto->setAttribute(context, py_next, context->fromMethod(const_cast<proto::ProtoObject*>(tupleIterProto), py_tuple_iter_next));
    tuplePrototype = tuplePrototype->setAttribute(context, py_iter_proto, tupleIterProto);

    setPrototype = context->newObject(true);
    setPrototype = setPrototype->addParent(context, objectPrototype);
    setPrototype = setPrototype->setAttribute(context, py_class, typePrototype);
    setPrototype = setPrototype->setAttribute(context, py_name, context->fromUTF8String("set"));
    setPrototype = setPrototype->setAttribute(context, py_len, context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_len));
    setPrototype = setPrototype->setAttribute(context, py_contains, context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_contains));
    setPrototype = setPrototype->setAttribute(context, py_bool, context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_bool));
    setPrototype = setPrototype->setAttribute(context, py_add, context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_add));
    setPrototype = setPrototype->setAttribute(context, py_remove, context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_remove));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "pop"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_pop));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "discard"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_discard));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "copy"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_copy));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "clear"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_clear));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "union"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_union));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "intersection"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_intersection));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "difference"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_difference));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "symmetric_difference"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_symmetric_difference));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "issubset"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_issubset));
    setPrototype = setPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "issuperset"), context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_issuperset));
    setPrototype = setPrototype->setAttribute(context, py_iter, context->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_iter));

    const proto::ProtoObject* setIterProto = context->newObject(true);
    setIterProto = setIterProto->addParent(context, objectPrototype);
    setIterProto = setIterProto->setAttribute(context, py_next, context->fromMethod(const_cast<proto::ProtoObject*>(setIterProto), py_set_iter_next));
    setPrototype = setPrototype->setAttribute(context, py_iter_proto, setIterProto);

    const proto::ProtoString* py_call = proto::ProtoString::fromUTF8String(context, "__call__");
    frozensetPrototype = context->newObject(true);
    frozensetPrototype = frozensetPrototype->addParent(context, objectPrototype);
    frozensetPrototype = frozensetPrototype->setAttribute(context, py_class, typePrototype);
    frozensetPrototype = frozensetPrototype->setAttribute(context, py_name, context->fromUTF8String("frozenset"));
    frozensetPrototype = frozensetPrototype->setAttribute(context, py_call, context->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_call));
    frozensetPrototype = frozensetPrototype->setAttribute(context, py_len, context->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_len));
    frozensetPrototype = frozensetPrototype->setAttribute(context, py_contains, context->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_contains));
    frozensetPrototype = frozensetPrototype->setAttribute(context, py_bool, context->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_bool));
    frozensetPrototype = frozensetPrototype->setAttribute(context, py_iter, context->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_iter));
    frozensetPrototype = frozensetPrototype->setAttribute(context, py_hash, context->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_hash));
    frozensetPrototype = frozensetPrototype->setAttribute(context, py_iter_proto, setIterProto);

    bytesPrototype = context->newObject(true);
    bytesPrototype = bytesPrototype->addParent(context, objectPrototype);
    bytesPrototype = bytesPrototype->setAttribute(context, py_class, typePrototype);
    bytesPrototype = bytesPrototype->setAttribute(context, py_name, context->fromUTF8String("bytes"));
    bytesPrototype = bytesPrototype->setAttribute(context, py_len, context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_len));
    bytesPrototype = bytesPrototype->setAttribute(context, py_getitem, context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_getitem));
    bytesPrototype = bytesPrototype->setAttribute(context, py_iter, context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_iter));
    bytesPrototype = bytesPrototype->setAttribute(context, py_call, context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_call));

    const proto::ProtoObject* bytesIterProto = context->newObject(true);
    bytesIterProto = bytesIterProto->addParent(context, objectPrototype);
    bytesIterProto = bytesIterProto->setAttribute(context, py_next, context->fromMethod(const_cast<proto::ProtoObject*>(bytesIterProto), py_bytes_iter_next));
    bytesPrototype = bytesPrototype->setAttribute(context, py_iter_proto, bytesIterProto);
    const proto::ProtoString* py_encode = proto::ProtoString::fromUTF8String(context, "encode");
    const proto::ProtoString* py_decode = proto::ProtoString::fromUTF8String(context, "decode");
    strPrototype = strPrototype->setAttribute(context, py_encode, context->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_encode));
    bytesPrototype = bytesPrototype->setAttribute(context, py_decode, context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_decode));
    const proto::ProtoString* py_hex = proto::ProtoString::fromUTF8String(context, "hex");
    const proto::ProtoString* py_fromhex = proto::ProtoString::fromUTF8String(context, "fromhex");
    bytesPrototype = bytesPrototype->setAttribute(context, py_hex, context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_hex));
    bytesPrototype = bytesPrototype->setAttribute(context, py_fromhex, context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_fromhex));
    const proto::ProtoString* py_bytes_find_name = proto::ProtoString::fromUTF8String(context, "find");
    const proto::ProtoString* py_bytes_count_name = proto::ProtoString::fromUTF8String(context, "count");
    bytesPrototype = bytesPrototype->setAttribute(context, py_bytes_find_name, context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_find));
    bytesPrototype = bytesPrototype->setAttribute(context, py_bytes_count_name, context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_count));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "index"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_index));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "rfind"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_rfind));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "rindex"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_rindex));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "startswith"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_startswith));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "endswith"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_endswith));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "strip"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_strip));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "lstrip"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_lstrip));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "rstrip"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_rstrip));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "split"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_split));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "join"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_join));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "replace"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_replace));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "isdigit"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_isdigit));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "isalpha"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_isalpha));
    bytesPrototype = bytesPrototype->setAttribute(context, proto::ProtoString::fromUTF8String(context, "isascii"), context->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_isascii));

    sliceType = context->newObject(true);
    sliceType = sliceType->addParent(context, objectPrototype);
    sliceType = sliceType->setAttribute(context, py_class, typePrototype);
    sliceType = sliceType->setAttribute(context, py_name, context->fromUTF8String("slice"));
    sliceType = sliceType->setAttribute(context, py_call, context->fromMethod(const_cast<proto::ProtoObject*>(sliceType), py_slice_call));
    sliceType = sliceType->setAttribute(context, py_repr, context->fromMethod(const_cast<proto::ProtoObject*>(sliceType), py_slice_repr));

    floatPrototype = context->newObject(true);
    floatPrototype = floatPrototype->addParent(context, objectPrototype);
    floatPrototype = floatPrototype->setAttribute(context, py_class, typePrototype);
    floatPrototype = floatPrototype->setAttribute(context, py_name, context->fromUTF8String("float"));
    floatPrototype = floatPrototype->setAttribute(context, py_call, context->fromMethod(const_cast<proto::ProtoObject*>(floatPrototype), py_float_call));
    const proto::ProtoString* py_is_integer = proto::ProtoString::fromUTF8String(context, "is_integer");
    floatPrototype = floatPrototype->setAttribute(context, py_is_integer, context->fromMethod(const_cast<proto::ProtoObject*>(floatPrototype), py_float_is_integer));
    const proto::ProtoString* py_as_integer_ratio = proto::ProtoString::fromUTF8String(context, "as_integer_ratio");
    floatPrototype = floatPrototype->setAttribute(context, py_as_integer_ratio, context->fromMethod(const_cast<proto::ProtoObject*>(floatPrototype), py_float_as_integer_ratio));

    boolPrototype = context->newObject(true);
    boolPrototype = boolPrototype->addParent(context, intPrototype);
    boolPrototype = boolPrototype->setAttribute(context, py_class, typePrototype);
    boolPrototype = boolPrototype->setAttribute(context, py_name, context->fromUTF8String("bool"));
    boolPrototype = boolPrototype->setAttribute(context, py_call, context->fromMethod(const_cast<proto::ProtoObject*>(boolPrototype), py_bool_call));

    // 5. Initialize Native Module Provider
    auto& registry = proto::ProviderRegistry::instance();
    auto nativeProvider = std::make_unique<NativeModuleProvider>();

    // sys module (argv set later via setArgv before executeModule)
    sysModule = sys::initialize(context, this, &argv_);
    nativeProvider->registerModule("sys", [this](proto::ProtoContext* ctx) { return sysModule; });

    // _io module (created before builtins so open() can delegate)
    const proto::ProtoObject* ioModule = io::initialize(context);
    nativeProvider->registerModule("_io", [ioModule](proto::ProtoContext*) { return ioModule; });

    // builtins module
    builtinsModule = builtins::initialize(context, objectPrototype, typePrototype, intPrototype, strPrototype, listPrototype, dictPrototype, tuplePrototype, setPrototype, bytesPrototype, sliceType, frozensetPrototype, floatPrototype, boolPrototype, ioModule);
    nativeProvider->registerModule("builtins", [this](proto::ProtoContext* ctx) { return builtinsModule; });

    // _collections module
    nativeProvider->registerModule("_collections", [this](proto::ProtoContext* ctx) { return collections::initialize(ctx, dictPrototype); });
    nativeProvider->registerModule("logging", [](proto::ProtoContext* ctx) { return logging::initialize(ctx); });
    nativeProvider->registerModule("operator", [](proto::ProtoContext* ctx) { return operator_::initialize(ctx); });
    nativeProvider->registerModule("math", [](proto::ProtoContext* ctx) { return math::initialize(ctx); });
    nativeProvider->registerModule("functools", [](proto::ProtoContext* ctx) { return functools::initialize(ctx); });
    nativeProvider->registerModule("itertools", [](proto::ProtoContext* ctx) { return itertools::initialize(ctx); });
    nativeProvider->registerModule("re", [](proto::ProtoContext* ctx) { return re::initialize(ctx); });
    nativeProvider->registerModule("json", [](proto::ProtoContext* ctx) { return json::initialize(ctx); });
    nativeProvider->registerModule("os.path", [](proto::ProtoContext* ctx) { return os_path::initialize(ctx); });
    nativeProvider->registerModule("pathlib", [](proto::ProtoContext* ctx) { return pathlib::initialize(ctx); });
    nativeProvider->registerModule("collections.abc", [](proto::ProtoContext* ctx) { return collections_abc::initialize(ctx); });

    const proto::ProtoObject* exceptionsMod = exceptions::initialize(context, objectPrototype, typePrototype);
    keyErrorType = exceptionsMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "KeyError"));
    valueErrorType = exceptionsMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "ValueError"));
    nativeProvider->registerModule("exceptions", [exceptionsMod](proto::ProtoContext*) { return exceptionsMod; });

    registry.registerProvider(std::move(nativeProvider));

    // 6. Initialize StdLib Module Provider
    std::vector<std::string> allPaths;
    if (!stdLibPath.empty()) allPaths.push_back(stdLibPath);
    else allPaths.push_back("./lib/python3.14");
    for (const auto& p : searchPaths) allPaths.push_back(p);
    
    registry.registerProvider(std::make_unique<PythonModuleProvider>(allPaths));
    
    // 7. Populate sys.path and sys.modules
    const proto::ProtoString* py_path = proto::ProtoString::fromUTF8String(context, "path");
    const proto::ProtoString* py_modules = proto::ProtoString::fromUTF8String(context, "modules");
    
    // a. Update sys.path
    const proto::ProtoObject* pathListObj = sysModule->getAttribute(context, py_path);
    if (pathListObj && pathListObj->asList(context)) {
        const proto::ProtoList* pList = pathListObj->asList(context);
        for (const auto& p : allPaths) {
            pList = pList->appendLast(context, context->fromUTF8String(p.c_str()));
        }
        sysModule = sysModule->setAttribute(context, py_path, pList->asObject(context));
    }
    
    // b. Create sys.modules and add sys/builtins
    const proto::ProtoObject* modulesDictObj = sysModule->getAttribute(context, py_modules);
    if (modulesDictObj) {
        modulesDictObj = modulesDictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "sys"), sysModule);
        modulesDictObj = modulesDictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "builtins"), builtinsModule);
        sysModule = sysModule->setAttribute(context, py_modules, modulesDictObj);
    }

    // 8. Prepend to resolution chain
    const proto::ProtoObject* chainObj = context->space->getResolutionChain();
    if (chainObj) {
        const proto::ProtoList* chain = chainObj->asList(context);
        if (chain) {
            chain = chain->insertAt(context, 0, context->fromUTF8String("provider:python_stdlib"));
            chain = chain->insertAt(context, 0, context->fromUTF8String("provider:native"));
            context->space->setResolutionChain(chain->asObject(context));
        }
    }
}

int PythonEnvironment::runModuleMain(const std::string& moduleName) {
    const proto::ProtoObject* mod = resolve(moduleName);
    if (mod == nullptr || mod == PROTO_NONE)
        return -1;
    const proto::ProtoString* mainName = proto::ProtoString::fromUTF8String(context, "main");
    const proto::ProtoObject* mainAttr = mod->getAttribute(context, mainName);
    if (mainAttr == nullptr || mainAttr == PROTO_NONE || !mainAttr->isMethod(context))
        return 0;
    const proto::ProtoList* emptyArgs = context->newList();
    mainAttr->asMethod(context)(context, const_cast<proto::ProtoObject*>(mod), nullptr, emptyArgs, nullptr);
    return 0;
}

int PythonEnvironment::executeModule(const std::string& moduleName) {
    const proto::ProtoObject* mod = resolve(moduleName);
    if (mod == nullptr || mod == PROTO_NONE)
        return -1;

    if (executionHook) executionHook(moduleName, 0);
    if (traceFunction) {
        const proto::ProtoObject* callAttr = traceFunction->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__"));
        if (callAttr && callAttr->asMethod(context)) {
            const proto::ProtoList* args = context->newList()
                ->appendLast(context, PROTO_NONE)
                ->appendLast(context, context->fromUTF8String("call"))
                ->appendLast(context, PROTO_NONE);
            callAttr->asMethod(context)(context, traceFunction, nullptr, args, nullptr);
        }
    }

    int ret = runModuleMain(moduleName);
    if (ret != 0) {
        if (executionHook) executionHook(moduleName, 1);
        return ret == -1 ? -1 : -2;
    }

    const proto::ProtoObject* exc = takePendingException();
    if (exc) {
        if (executionHook) executionHook(moduleName, 1);
        return -2;
    }

    if (traceFunction) {
        const proto::ProtoObject* callAttr = traceFunction->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__"));
        if (callAttr && callAttr->asMethod(context)) {
            const proto::ProtoList* args = context->newList()
                ->appendLast(context, PROTO_NONE)
                ->appendLast(context, context->fromUTF8String("return"))
                ->appendLast(context, PROTO_NONE);
            callAttr->asMethod(context)(context, traceFunction, nullptr, args, nullptr);
        }
    }
    if (executionHook) executionHook(moduleName, 1);
    return exitRequested_ != 0 ? -3 : 0;
}

void PythonEnvironment::incrementSysStats(const char* key) {
    if (!sysModule) return;
    const proto::ProtoObject* stats = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "stats"));
    if (!stats) return;
    const proto::ProtoObject* val = stats->getAttribute(context, proto::ProtoString::fromUTF8String(context, key));
    long long n = (val && val->isInteger(context)) ? val->asLong(context) : 0;
    const proto::ProtoObject* newStats = stats->setAttribute(context, proto::ProtoString::fromUTF8String(context, key), context->fromInteger(n + 1));
    sysModule = sysModule->setAttribute(context, proto::ProtoString::fromUTF8String(context, "stats"), newStats);
}

void PythonEnvironment::enableDefaultTrace() {
    if (!sysModule) return;
    const proto::ProtoObject* def = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "_trace_default"));
    if (def && def != PROTO_NONE) setTraceFunction(def);
}

void PythonEnvironment::invalidateResolveCache() {
    resolveCache_.clear();
}

const proto::ProtoObject* PythonEnvironment::resolve(const std::string& name) {
    auto it = resolveCache_.find(name);
    if (it != resolveCache_.end() && it->second != nullptr)
        return it->second;

    const proto::ProtoObject* result = PROTO_NONE;

    // 1. Type shortcuts (prototypes) so they are never shadowed by builtins or modules
    if (name == "object") result = objectPrototype;
    else if (name == "type") result = typePrototype;
    else if (name == "int") result = intPrototype;
    else if (name == "float") result = floatPrototype;
    else if (name == "bool") result = boolPrototype;
    else if (name == "str") result = strPrototype;
    else if (name == "list") result = listPrototype;
    else if (name == "dict") result = dictPrototype;

    // 2. Try module import so that names like "sys" resolve to the real module object,
    //    not a builtins attribute (e.g. a method) that would cause type mismatch in getAttribute
    if (!result || result == PROTO_NONE) {
        const proto::ProtoObject* modWrapper = context->space->getImportModule(name.c_str(), "val");
        if (modWrapper) {
            result = modWrapper->getAttribute(context, proto::ProtoString::fromUTF8String(context, "val"));
            if (result && result != PROTO_NONE && sysModule) {
                const proto::ProtoObject* mods = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "modules"));
                if (mods) {
                    const proto::ProtoObject* updated = mods->setAttribute(context, proto::ProtoString::fromUTF8String(context, name.c_str()), result);
                    sysModule = sysModule->setAttribute(context, proto::ProtoString::fromUTF8String(context, "modules"), updated);
                }
            }
        }
    }

    // 3. Fall back to builtins (e.g. len, print, type as callable)
    if (!result || result == PROTO_NONE) {
        if (builtinsModule) {
            const proto::ProtoObject* attr = builtinsModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, name.c_str()));
            if (attr && attr != PROTO_NONE) result = attr;
        }
    }

    resolveCache_[name] = result;
    return result;
}

} // namespace protoPython
