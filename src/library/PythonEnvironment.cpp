#include <protoPython/PythonEnvironment.h>
#include <protoPython/PythonModuleProvider.h>
#include <protoPython/NativeModuleProvider.h>
#include <protoPython/SysModule.h>
#include <protoPython/BuiltinsModule.h>
#include <protoPython/IOModule.h>
#include <protoPython/CollectionsModule.h>
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
    int index = static_cast<int>(positionalParameters->getAt(context, 0)->asLong(context));
    unsigned long size = list->getSize(context);
    if (index < 0) index += static_cast<int>(size);
    if (index < 0 || static_cast<unsigned long>(index) >= size) return PROTO_NONE;
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
        return data->asSparseList(context)->getAt(context, hash);
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

// --- PythonEnvironment Implementation ---

PythonEnvironment::PythonEnvironment(const std::string& stdLibPath, const std::vector<std::string>& searchPaths) : space() {
    context = new proto::ProtoContext(&space);
    initializeRootObjects(stdLibPath, searchPaths);
}

PythonEnvironment::~PythonEnvironment() {
    delete context;
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

    // 1. Create 'object' base
    objectPrototype = context->newObject(true);
    objectPrototype = objectPrototype->setAttribute(context, py_init, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_init));
    objectPrototype = objectPrototype->setAttribute(context, py_repr, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_repr));
    objectPrototype = objectPrototype->setAttribute(context, py_str, context->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_str));

    // 2. Create 'type'
    typePrototype = context->newObject(true);
    typePrototype = typePrototype->addParent(context, objectPrototype);
    typePrototype = typePrototype->setAttribute(context, py_class, typePrototype);

    // 3. Circularity: object's class is type
    objectPrototype = objectPrototype->setAttribute(context, py_class, typePrototype);

    // 4. Basic types
    intPrototype = context->newObject(true);
    intPrototype = intPrototype->addParent(context, objectPrototype);
    intPrototype = intPrototype->setAttribute(context, py_class, typePrototype);
    intPrototype = intPrototype->setAttribute(context, py_name, context->fromUTF8String("int"));

    strPrototype = context->newObject(true);
    strPrototype = strPrototype->addParent(context, objectPrototype);
    strPrototype = strPrototype->setAttribute(context, py_class, typePrototype);
    strPrototype = strPrototype->setAttribute(context, py_name, context->fromUTF8String("str"));

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

    tuplePrototype = context->newObject(true);
    tuplePrototype = tuplePrototype->addParent(context, objectPrototype);
    tuplePrototype = tuplePrototype->setAttribute(context, py_class, typePrototype);
    tuplePrototype = tuplePrototype->setAttribute(context, py_name, context->fromUTF8String("tuple"));
    tuplePrototype = tuplePrototype->setAttribute(context, py_len, context->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_len));

    // 5. Initialize Native Module Provider
    auto& registry = proto::ProviderRegistry::instance();
    auto nativeProvider = std::make_unique<NativeModuleProvider>();

    // sys module
    sysModule = sys::initialize(context, this);
    nativeProvider->registerModule("sys", [this](proto::ProtoContext* ctx) { return sysModule; });

    // builtins module
    builtinsModule = builtins::initialize(context, objectPrototype, typePrototype, intPrototype, strPrototype, listPrototype, dictPrototype, tuplePrototype);
    nativeProvider->registerModule("builtins", [this](proto::ProtoContext* ctx) { return builtinsModule; });

    // _io module
    nativeProvider->registerModule("_io", [](proto::ProtoContext* ctx) { return io::initialize(ctx); });

    // _collections module
    nativeProvider->registerModule("_collections", [](proto::ProtoContext* ctx) { return collections::initialize(ctx); });

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

const proto::ProtoObject* PythonEnvironment::resolve(const std::string& name) {
    // 1. Check builtins
    if (builtinsModule) {
        const proto::ProtoObject* attr = builtinsModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, name.c_str()));
        if (attr && attr != PROTO_NONE) return attr;
    }

    // 2. Direct resolution fallback for hardcoded prototypes (already in builtins, but keep for robustness)
    if (name == "object") return objectPrototype;
    if (name == "type") return typePrototype;
    if (name == "int") return intPrototype;
    if (name == "str") return strPrototype;
    if (name == "list") return listPrototype;
    if (name == "dict") return dictPrototype;
    
    // Fallback: try to import as a module
    const proto::ProtoObject* modWrapper = context->space->getImportModule(name.c_str(), "val");
    if (modWrapper) {
        return modWrapper->getAttribute(context, proto::ProtoString::fromUTF8String(context, "val"));
    }

    return PROTO_NONE;
}

} // namespace protoPython
