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
#include <protoCore.h>

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

    SliceBounds sb = get_slice_bounds(context, indexObj, size);
    if (sb.isSlice && sb.step == 1) {
        const proto::ProtoList* result = context->newList();
        for (long long i = sb.start; i < sb.stop; i += sb.step) {
            result = result->appendLast(context, list->getAt(context, static_cast<int>(i)));
        }
        return result->asObject(context);
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
    }

    int index = static_cast<int>(indexObj->asLong(context));
    unsigned long ulen = list->getSize(context);
    if (index < 0) index += static_cast<int>(ulen);
    if (index < 0 || static_cast<unsigned long>(index) >= ulen) return PROTO_NONE;
    return list->getAt(context, index);
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

static const proto::ProtoObject* py_list_extend(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* otherObj = positionalParameters->getAt(context, 0);
    const proto::ProtoList* otherList = otherObj->asList(context);
    if (!otherList) return PROTO_NONE;
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    const proto::ProtoList* newList = list;
    unsigned long otherSize = otherList->getSize(context);
    for (unsigned long i = 0; i < otherSize; ++i) {
        newList = newList->appendLast(context, otherList->getAt(context, static_cast<int>(i)));
    }
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
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
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
        if (elem->compare(context, value) == 0) {
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

static const proto::ProtoObject* py_int_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return context->fromInteger(self->asLong(context));
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
    const proto::ProtoString* py_copy = proto::ProtoString::fromUTF8String(context, "copy");
    const proto::ProtoString* py_upper = proto::ProtoString::fromUTF8String(context, "upper");
    const proto::ProtoString* py_lower = proto::ProtoString::fromUTF8String(context, "lower");
    const proto::ProtoString* py_format = proto::ProtoString::fromUTF8String(context, "format");
    const proto::ProtoString* py_split = proto::ProtoString::fromUTF8String(context, "split");
    const proto::ProtoString* py_join = proto::ProtoString::fromUTF8String(context, "join");
    const proto::ProtoString* py_add = proto::ProtoString::fromUTF8String(context, "add");

    // 1. Create 'object' base
    objectPrototype = context->newObject(true);
    const proto::ProtoString* py_format_dunder = proto::ProtoString::fromUTF8String(context, "__format__");
    objectPrototype = objectPrototype->setAttribute(context, py_init, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_init));
    objectPrototype = objectPrototype->setAttribute(context, py_repr, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_repr));
    objectPrototype = objectPrototype->setAttribute(context, py_str, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_str));
    objectPrototype = objectPrototype->setAttribute(context, py_format_dunder, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_format));

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

    const proto::ProtoObject* listIterProto = context->newObject(true);
    listIterProto = listIterProto->addParent(context, objectPrototype);
    listIterProto = listIterProto->setAttribute(context, py_next, context->fromMethod(const_cast<proto::ProtoObject*>(listIterProto), py_list_iter_next));
    listPrototype = listPrototype->setAttribute(context, py_iter_proto, listIterProto);

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
    dictPrototype = dictPrototype->setAttribute(context, py_update, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_update));
    dictPrototype = dictPrototype->setAttribute(context, py_clear, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_clear));
    dictPrototype = dictPrototype->setAttribute(context, py_copy, context->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_copy));

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

    sliceType = context->newObject(true);
    sliceType = sliceType->addParent(context, objectPrototype);
    sliceType = sliceType->setAttribute(context, py_class, typePrototype);
    sliceType = sliceType->setAttribute(context, py_name, context->fromUTF8String("slice"));
    sliceType = sliceType->setAttribute(context, py_call, context->fromMethod(const_cast<proto::ProtoObject*>(sliceType), py_slice_call));

    floatPrototype = context->newObject(true);
    floatPrototype = floatPrototype->addParent(context, objectPrototype);
    floatPrototype = floatPrototype->setAttribute(context, py_class, typePrototype);
    floatPrototype = floatPrototype->setAttribute(context, py_name, context->fromUTF8String("float"));
    floatPrototype = floatPrototype->setAttribute(context, py_call, context->fromMethod(const_cast<proto::ProtoObject*>(floatPrototype), py_float_call));

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

    // 1. Check builtins
    if (builtinsModule) {
        const proto::ProtoObject* attr = builtinsModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, name.c_str()));
        if (attr && attr != PROTO_NONE) result = attr;
    }

    if (!result || result == PROTO_NONE) {
        if (name == "object") result = objectPrototype;
        else if (name == "type") result = typePrototype;
        else if (name == "int") result = intPrototype;
        else if (name == "float") result = floatPrototype;
        else if (name == "bool") result = boolPrototype;
        else if (name == "str") result = strPrototype;
        else if (name == "list") result = listPrototype;
        else if (name == "dict") result = dictPrototype;
    }

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

    resolveCache_[name] = result;
    return result;
}

} // namespace protoPython
