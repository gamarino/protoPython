#include <protoPython/BuiltinsModule.h>
#include <protoPython/PythonEnvironment.h>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <vector>

namespace protoPython {
namespace builtins {

static const proto::ProtoObject* py_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    
    // 1. Try asList
    if (obj->asList(context)) {
        return context->fromInteger(obj->asList(context)->getSize(context));
    }
    // 2. Try asSparseList
    if (obj->asSparseList(context)) {
        return context->fromInteger(obj->asSparseList(context)->getSize(context));
    }
    // 3. Try asString
    if (obj->isString(context)) {
        return context->fromInteger(obj->asString(context)->getSize(context));
    }
    
    // 4. Fallback to __len__ dunder
    const proto::ProtoObject* lenMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    if (lenMethod && lenMethod->asMethod(context)) {
        return lenMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
    }
    
    return PROTO_NONE;
}

static const proto::ProtoObject* py_id(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    return context->fromInteger(reinterpret_cast<long long>(obj));
}

static const proto::ProtoObject* py_print(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)keywordParameters;
    std::string sep = " ";
    std::string end = "\n";

    unsigned long size = positionalParameters->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* obj = positionalParameters->getAt(context, static_cast<int>(i));

        const proto::ProtoObject* strMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__str__"));
        const proto::ProtoObject* strObj = PROTO_NONE;
        if (strMethod && strMethod->asMethod(context)) {
            strObj = strMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
        }

        if (strObj && strObj->isString(context)) {
            std::string out;
            strObj->asString(context)->toUTF8String(context, out);
            std::cout << out;
        } else {
            std::cout << "<unprintable>";
        }

        if (i < size - 1) std::cout << sep;
    }
    std::cout << end;
    return PROTO_NONE;
}

static const proto::ProtoObject* py_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);

    const proto::ProtoObject* iterMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (iterMethod && iterMethod->asMethod(context)) {
        return iterMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
    }

    return PROTO_NONE;
}

static const proto::ProtoObject* py_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* defaultVal = positionalParameters->getSize(context) >= 2
        ? positionalParameters->getAt(context, 1) : nullptr;

    if (obj->asStringIterator(context)) {
        proto::ProtoStringIterator* it = const_cast<proto::ProtoStringIterator*>(obj->asStringIterator(context));
        if (!it || !it->hasNext(context)) return defaultVal ? defaultVal : PROTO_NONE;
        return it->next(context);
    }

    const proto::ProtoObject* nextMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (nextMethod && nextMethod->asMethod(context)) {
        const proto::ProtoObject* result = nextMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
        if (result == PROTO_NONE && defaultVal) return defaultVal;
        return result;
    }

    return defaultVal ? defaultVal : PROTO_NONE;
}

static const proto::ProtoObject* py_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* container = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* item = positionalParameters->getAt(context, 0);

    if (container->isString(context)) {
        const proto::ProtoString* str = container->asString(context);
        unsigned long size = str->getSize(context);
        if (!item->isString(context)) return PROTO_FALSE;
        std::string sub;
        item->asString(context)->toUTF8String(context, sub);
        if (sub.empty()) return PROTO_TRUE;
        std::string haystack;
        str->toUTF8String(context, haystack);
        return haystack.find(sub) != std::string::npos ? PROTO_TRUE : PROTO_FALSE;
    }

    const proto::ProtoObject* containsMethod = container->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__contains__"));
    if (containsMethod && containsMethod->asMethod(context)) {
        const proto::ProtoList* args = context->newList()->appendLast(context, item);
        return containsMethod->asMethod(context)(context, container, nullptr, args, nullptr);
    }

    return PROTO_FALSE;
}

static const proto::ProtoObject* py_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);

    if (obj->isString(context)) {
        return obj->asString(context)->getSize(context) > 0 ? PROTO_TRUE : PROTO_FALSE;
    }
    if (obj->isInteger(context)) {
        return obj->asLong(context) != 0 ? PROTO_TRUE : PROTO_FALSE;
    }
    if (obj->isDouble(context)) {
        return obj->asDouble(context) != 0.0 ? PROTO_TRUE : PROTO_FALSE;
    }

    const proto::ProtoObject* boolMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__bool__"));
    if (boolMethod && boolMethod->asMethod(context)) {
        return boolMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
    }

    return PROTO_TRUE;
}

static const proto::ProtoObject* py_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    if (obj->isDouble(context)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", obj->asDouble(context));
        return context->fromUTF8String(buf);
    }
    const proto::ProtoObject* reprMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__repr__"));
    if (reprMethod && reprMethod->asMethod(context)) {
        return reprMethod->asMethod(context)(context, obj, nullptr, context->newList(), nullptr);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_format(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    if (obj->isDouble(context)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", obj->asDouble(context));
        return context->fromUTF8String(buf);
    }
    const proto::ProtoObject* formatMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__format__"));
    if (!formatMethod || !formatMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* args = context->newList();
    if (positionalParameters->getSize(context) >= 2) {
        args = args->appendLast(context, positionalParameters->getAt(context, 1));
    } else {
        args = args->appendLast(context, context->fromUTF8String(""));
    }
    return formatMethod->asMethod(context)(context, obj, nullptr, args, nullptr);
}

static const proto::ProtoObject* py_open(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* ioMod = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__io_module__"));
    if (!ioMod || ioMod == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* openFunc = ioMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "open"));
    if (!openFunc || !openFunc->asMethod(context)) return PROTO_NONE;
    return openFunc->asMethod(context)(context, ioMod, nullptr, positionalParameters, keywordParameters);
}

static const proto::ProtoObject* py_iter_self(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
}

static const proto::ProtoObject* py_enumerate_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);

static const proto::ProtoObject* py_enumerate(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    long long start = 0;
    if (positionalParameters->getSize(context) >= 2 && positionalParameters->getAt(context, 1)->isInteger(context))
        start = positionalParameters->getAt(context, 1)->asLong(context);

    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it || it == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoObject* enumProto = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__enumerate_proto__"));
    if (!enumProto) return PROTO_NONE;
    const proto::ProtoObject* enumObj = enumProto->newChild(context, true);
    enumObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__enumerate_it__"), it);
    enumObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__enumerate_idx__"), context->fromInteger(start));
    return enumObj;
}

static const proto::ProtoObject* py_enumerate_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* it = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__enumerate_it__"));
    const proto::ProtoObject* idxObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__enumerate_idx__"));
    if (!it || !idxObj) return PROTO_NONE;

    const proto::ProtoObject* nextMethod = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* value = nextMethod->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
    if (!value || value == PROTO_NONE) return PROTO_NONE;

    long long idx = idxObj->asLong(context);
    const proto::ProtoList* pair = context->newList()->appendLast(context, context->fromInteger(idx))->appendLast(context, value);
    self->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__enumerate_idx__"), context->fromInteger(idx + 1));
    return context->newTupleFromList(pair)->asObject(context);
}

static const proto::ProtoObject* py_reversed(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);

    const proto::ProtoObject* revMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed__"));
    if (revMethod && revMethod->asMethod(context)) {
        return revMethod->asMethod(context)(context, obj, nullptr, context->newList(), nullptr);
    }

    const proto::ProtoObject* lenMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    const proto::ProtoObject* getitemMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__getitem__"));
    if (!lenMethod || !lenMethod->asMethod(context) || !getitemMethod || !getitemMethod->asMethod(context))
        return PROTO_NONE;
    const proto::ProtoObject* lenObj = lenMethod->asMethod(context)(context, obj, nullptr, context->newList(), nullptr);
    if (!lenObj || !lenObj->isInteger(context)) return PROTO_NONE;
    long long n = lenObj->asLong(context);

    const proto::ProtoObject* revProto = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_proto__"));
    if (!revProto) return PROTO_NONE;
    const proto::ProtoObject* revObj = revProto->newChild(context, true);
    revObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_obj__"), obj);
    revObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_idx__"), context->fromInteger(n - 1));
    return revObj;
}

static const proto::ProtoObject* py_reversed_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* obj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_obj__"));
    const proto::ProtoObject* idxObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_idx__"));
    if (!obj || !idxObj) return PROTO_NONE;
    long long idx = idxObj->asLong(context);
    if (idx < 0) return PROTO_NONE;

    const proto::ProtoObject* getitemMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__getitem__"));
    if (!getitemMethod || !getitemMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(idx));
    const proto::ProtoObject* value = getitemMethod->asMethod(context)(context, obj, nullptr, args, nullptr);
    self->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__reversed_idx__"), context->fromInteger(idx - 1));
    return value;
}

static const proto::ProtoObject* py_sum(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* start = positionalParameters->getSize(context) >= 2 ? positionalParameters->getAt(context, 1) : context->fromInteger(0);

    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it) return start;

    const proto::ProtoObject* nextMethod = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextMethod || !nextMethod->asMethod(context)) return start;

    long long acc = start->isInteger(context) ? start->asLong(context) : 0;
    for (;;) {
        const proto::ProtoObject* val = nextMethod->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        if (val->isInteger(context)) acc += val->asLong(context);
    }
    return context->fromInteger(acc);
}

static const proto::ProtoObject* py_all(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_TRUE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_TRUE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it) return PROTO_TRUE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_TRUE;

    const proto::ProtoObject* boolType = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bool"));
    const proto::ProtoObject* boolCall = boolType ? boolType->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__")) : nullptr;
    if (!boolCall || !boolCall->asMethod(context)) return PROTO_TRUE;

    for (;;) {
        const proto::ProtoObject* val = nextMethod->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        const proto::ProtoList* oneArg = context->newList()->appendLast(context, val);
        const proto::ProtoObject* b = boolCall->asMethod(context)(context, boolType, nullptr, oneArg, nullptr);
        if (!b || b == PROTO_FALSE) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_any(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_FALSE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it) return PROTO_FALSE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_FALSE;

    const proto::ProtoObject* boolType = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bool"));
    const proto::ProtoObject* boolCall = boolType ? boolType->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__")) : nullptr;
    if (!boolCall || !boolCall->asMethod(context)) return PROTO_FALSE;

    for (;;) {
        const proto::ProtoObject* val = nextMethod->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        const proto::ProtoList* oneArg = context->newList()->appendLast(context, val);
        const proto::ProtoObject* b = boolCall->asMethod(context)(context, boolType, nullptr, oneArg, nullptr);
        if (b == PROTO_TRUE) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_callable(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* call = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__"));
    return (call && call != PROTO_NONE && call->asMethod(context)) ? PROTO_TRUE : PROTO_FALSE;
}

/** getattr(obj, name[, default]): return obj.name, or default if given and attribute missing. */
static const proto::ProtoObject* py_getattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    if (!nameObj->isString(context)) return PROTO_NONE;
    std::string name;
    nameObj->asString(context)->toUTF8String(context, name);
    const proto::ProtoObject* attr = obj->getAttribute(context, nameObj->asString(context));
    if (attr && attr != PROTO_NONE) return attr;
    if (positionalParameters->getSize(context) >= 3) return positionalParameters->getAt(context, 2);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_setattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 3) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* value = positionalParameters->getAt(context, 2);
    if (!nameObj->isString(context)) return PROTO_NONE;
    obj->setAttribute(context, nameObj->asString(context), value);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dir(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    (void)self;
    (void)positionalParameters;
    return context->newList()->asObject(context);
}

/** input([prompt]): stub returning empty string; full impl requires stdin. */
static const proto::ProtoObject* py_input(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return context->fromUTF8String("");
}

/** eval(expr): stub returning None; full impl requires expression parser. */
static const proto::ProtoObject* py_eval(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return PROTO_NONE;
}

/** help(obj): stub returning None. */
static const proto::ProtoObject* py_help(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return PROTO_NONE;
}

/** memoryview(obj): stub returning None. */
static const proto::ProtoObject* py_memoryview(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return PROTO_NONE;
}

/** super(): stub returning None; full impl requires MRO and type. */
static const proto::ProtoObject* py_super(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return PROTO_NONE;
}

/** exec(code): stub returning None; full impl requires statement execution. */
static const proto::ProtoObject* py_exec(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return PROTO_NONE;
}

/** breakpoint(): no-op stub; real breakpoint requires debugger integration. */
static const proto::ProtoObject* py_breakpoint(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)context;
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    return PROTO_NONE;
}

/** globals(): stub returning empty dict; full impl needs frame access. */
static const proto::ProtoObject* py_globals(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    proto::ProtoObject* d = const_cast<proto::ProtoObject*>(env->getDictPrototype()->newChild(context, true));
    d->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__keys__"), context->newList()->asObject(context));
    d->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    return d;
}

/** locals(): stub returning empty dict; full impl needs frame access. */
static const proto::ProtoObject* py_locals(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    proto::ProtoObject* d = const_cast<proto::ProtoObject*>(env->getDictPrototype()->newChild(context, true));
    d->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__keys__"), context->newList()->asObject(context));
    d->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    return d;
}

/** vars([object]): return object.__dict__ if given; stub for no-arg (locals). */
static const proto::ProtoObject* py_vars(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    (void)self;
    if (!positionalParameters || positionalParameters->getSize(context) == 0)
        return PROTO_NONE;  // vars() without args: stub (would be caller locals)
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* d = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__dict__"));
    if (d && d != PROTO_NONE)
        return d;
    return context->newSparseList()->asObject(context);
}

/** Compare two objects for sorting: int, string, else compare(). */
static int sorted_compare(proto::ProtoContext* context, const proto::ProtoObject* a, const proto::ProtoObject* b) {
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

static const proto::ProtoObject* py_sorted(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    (void)self;
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_NONE;

    std::vector<const proto::ProtoObject*> elems;
    for (;;) {
        const proto::ProtoObject* val = nextMethod->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        elems.push_back(val);
    }

    std::sort(elems.begin(), elems.end(), [context](const proto::ProtoObject* a, const proto::ProtoObject* b) {
        return sorted_compare(context, a, b) < 0;
    });

    const proto::ProtoList* resultList = context->newList();
    for (const proto::ProtoObject* obj : elems)
        resultList = resultList->appendLast(context, obj);

    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* listObj = env->getListPrototype()->newChild(context, true);
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), resultList->asObject(context));
    return listObj;
}

static const proto::ProtoObject* py_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* hashMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__hash__"));
    if (!hashMethod || !hashMethod->asMethod(context)) return PROTO_NONE;
    return hashMethod->asMethod(context)(context, obj, nullptr, context->newList(), nullptr);
}

static const proto::ProtoObject* py_hasattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    if (!nameObj->isString(context)) return PROTO_FALSE;
    const proto::ProtoObject* attr = obj->getAttribute(context, nameObj->asString(context));
    return (attr && attr != PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_raise(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    if (positionalParameters->getSize(context) > 0) {
        env->setPendingException(positionalParameters->getAt(context, 0));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_delattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    if (!nameObj->isString(context)) return PROTO_NONE;
    obj->setAttribute(context, nameObj->asString(context), PROTO_NONE);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_type(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    if (obj->isDouble(context)) {
        const proto::ProtoObject* fp = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "float"));
        return fp ? fp : PROTO_NONE;
    }
    if (obj == PROTO_TRUE || obj == PROTO_FALSE) {
        const proto::ProtoObject* bp = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bool"));
        return bp ? bp : PROTO_NONE;
    }
    const proto::ProtoObject* cls = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
    return cls ? cls : PROTO_NONE;
}

static const proto::ProtoObject* py_isinstance(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 1);
    if (obj == PROTO_TRUE || obj == PROTO_FALSE) {
        const proto::ProtoObject* boolType = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bool"));
        const proto::ProtoObject* intType = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "int"));
        if (cls == boolType || cls == intType) return PROTO_TRUE;
        if (intType && cls->isInstanceOf(context, intType)) return PROTO_TRUE;
    }
    return obj->isInstanceOf(context, cls);
}

static const proto::ProtoObject* py_issubclass(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* base = positionalParameters->getAt(context, 1);
    
    return cls->isInstanceOf(context, base);
}

static const proto::ProtoObject* py_abs(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    long long v = positionalParameters->getAt(context, 0)->asLong(context);
    return context->fromInteger(v < 0 ? -v : v);
}

static const proto::ProtoObject* py_min(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    long long m = positionalParameters->getAt(context, 0)->asLong(context);
    for (unsigned long i = 1; i < positionalParameters->getSize(context); ++i) {
        long long v = positionalParameters->getAt(context, static_cast<int>(i))->asLong(context);
        if (v < m) m = v;
    }
    return context->fromInteger(m);
}

static const proto::ProtoObject* py_max(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    long long m = positionalParameters->getAt(context, 0)->asLong(context);
    for (unsigned long i = 1; i < positionalParameters->getSize(context); ++i) {
        long long v = positionalParameters->getAt(context, static_cast<int>(i))->asLong(context);
        if (v > m) m = v;
    }
    return context->fromInteger(m);
}

static const proto::ProtoObject* py_pow(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    unsigned long n = positionalParameters->getSize(context);
    if (n < 2) return PROTO_NONE;
    long long base = positionalParameters->getAt(context, 0)->asLong(context);
    long long exp = positionalParameters->getAt(context, 1)->asLong(context);
    bool hasMod = n >= 3;
    long long mod = hasMod ? positionalParameters->getAt(context, 2)->asLong(context) : 0;
    if (hasMod && mod == 0) return PROTO_NONE;
    if (exp < 0) return PROTO_NONE;
    long long result = 1;
    long long b = base;
    long long e = exp;
    if (hasMod) {
        b = ((b % mod) + mod) % mod;
        while (e > 0) {
            if (e & 1) result = (result * b) % mod;
            b = (b * b) % mod;
            e >>= 1;
        }
        return context->fromInteger(result);
    }
    while (e > 0) {
        if (e & 1) result *= b;
        b *= b;
        e >>= 1;
    }
    return context->fromInteger(result);
}

static const proto::ProtoObject* py_divmod(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    long long a = positionalParameters->getAt(context, 0)->asLong(context);
    long long b = positionalParameters->getAt(context, 1)->asLong(context);
    if (b == 0) return PROTO_NONE;
    long long quot = a / b;
    long long rem = a % b;
    if (rem != 0) {
        if (b > 0 && rem < 0) { quot--; rem += b; }
        else if (b < 0 && rem > 0) { quot++; rem -= b; }
    }
    const proto::ProtoList* pair = context->newList()
        ->appendLast(context, context->fromInteger(quot))
        ->appendLast(context, context->fromInteger(rem));
    return context->newTupleFromList(pair)->asObject(context);
}

static const proto::ProtoObject* py_ascii(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* reprMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__repr__"));
    if (!reprMethod || !reprMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* reprObj = reprMethod->asMethod(context)(context, obj, nullptr, context->newList(), nullptr);
    if (!reprObj || !reprObj->isString(context)) return PROTO_NONE;
    std::string s;
    reprObj->asString(context)->toUTF8String(context, s);
    std::string out;
    for (unsigned char c : s) {
        if (c >= 32 && c < 127 && c != '\\' && c != '\'') out += c;
        else if (c == '\\') out += "\\\\";
        else if (c == '\'') out += "\\'";
        else { char buf[8]; snprintf(buf, sizeof(buf), "\\x%02x", c); out += buf; }
    }
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_ord(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isString(context)) return PROTO_NONE;
    std::string s;
    arg->asString(context)->toUTF8String(context, s);
    if (s.empty()) return PROTO_NONE;
    unsigned char first = static_cast<unsigned char>(s[0]);
    if ((first & 0x80) == 0)
        return context->fromInteger(static_cast<long long>(first));
    if ((first & 0xE0) == 0xC0 && s.size() >= 2) {
        long long cp = (first & 0x1F) << 6 | (static_cast<unsigned char>(s[1]) & 0x3F);
        return context->fromInteger(cp);
    }
    if ((first & 0xF0) == 0xE0 && s.size() >= 3) {
        long long cp = (first & 0x0F) << 12 | (static_cast<unsigned char>(s[1]) & 0x3F) << 6 | (static_cast<unsigned char>(s[2]) & 0x3F);
        return context->fromInteger(cp);
    }
    if ((first & 0xF8) == 0xF0 && s.size() >= 4) {
        long long cp = (first & 0x07) << 18 | (static_cast<unsigned char>(s[1]) & 0x3F) << 12
            | (static_cast<unsigned char>(s[2]) & 0x3F) << 6 | (static_cast<unsigned char>(s[3]) & 0x3F);
        return context->fromInteger(cp);
    }
    return context->fromInteger(static_cast<long long>(first));
}

static const proto::ProtoObject* py_chr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    long long i = arg->asLong(context);
    if (i < 0 || i > 0x10FFFF) return PROTO_NONE;
    char buf[8];
    int n = 0;
    if (i <= 0x7F) {
        buf[n++] = static_cast<char>(i);
    } else if (i <= 0x7FF) {
        buf[n++] = static_cast<char>(0xC0 | (i >> 6));
        buf[n++] = static_cast<char>(0x80 | (i & 0x3F));
    } else if (i <= 0xFFFF) {
        buf[n++] = static_cast<char>(0xE0 | (i >> 12));
        buf[n++] = static_cast<char>(0x80 | ((i >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (i & 0x3F));
    } else {
        buf[n++] = static_cast<char>(0xF0 | (i >> 18));
        buf[n++] = static_cast<char>(0x80 | ((i >> 12) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | ((i >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (i & 0x3F));
    }
    buf[n] = '\0';
    return context->fromUTF8String(buf);
}

static const proto::ProtoObject* py_bin(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    long long i = arg->asLong(context);
    if (i == 0) return context->fromUTF8String("0b0");
    std::string s = "0b";
    unsigned long long u;
    if (i < 0) {
        s += "-";
        u = static_cast<unsigned long long>(-i);
    } else {
        u = static_cast<unsigned long long>(i);
    }
    std::string bits;
    while (u) { bits += (u & 1) ? '1' : '0'; u >>= 1; }
    for (auto it = bits.rbegin(); it != bits.rend(); ++it) s += *it;
    return context->fromUTF8String(s.c_str());
}

static const proto::ProtoObject* py_oct(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    long long i = arg->asLong(context);
    if (i == 0) return context->fromUTF8String("0o0");
    char buf[32];
    if (i < 0)
        snprintf(buf, sizeof(buf), "-0o%llo", static_cast<unsigned long long>(-i));
    else
        snprintf(buf, sizeof(buf), "0o%llo", static_cast<unsigned long long>(i));
    return context->fromUTF8String(buf);
}

static const proto::ProtoObject* py_hex(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = positionalParameters->getAt(context, 0);
    if (!arg->isInteger(context)) return PROTO_NONE;
    long long i = arg->asLong(context);
    if (i == 0) return context->fromUTF8String("0x0");
    char buf[24];
    if (i < 0)
        snprintf(buf, sizeof(buf), "-0x%llx", static_cast<unsigned long long>(-i));
    else
        snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(i));
    return context->fromUTF8String(buf);
}

static const proto::ProtoObject* py_round(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* n = positionalParameters->getAt(context, 0);
    int ndigits = 0;
    if (positionalParameters->getSize(context) >= 2) {
        ndigits = static_cast<int>(positionalParameters->getAt(context, 1)->asLong(context));
    }
    if (n->isInteger(context)) {
        if (ndigits == 0) return n;
        double d = static_cast<double>(n->asLong(context));
        for (int i = 0; i < ndigits; ++i) d *= 10.0;
        d = std::round(d);
        for (int i = 0; i < ndigits; ++i) d /= 10.0;
        return context->fromDouble(d);
    }
    if (n->isDouble(context)) {
        double d = n->asDouble(context);
        if (ndigits == 0) return context->fromDouble(std::round(d));
        for (int i = 0; i < ndigits; ++i) d *= 10.0;
        d = std::round(d);
        for (int i = 0; i < ndigits; ++i) d /= 10.0;
        return context->fromDouble(d);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_range_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);

static const proto::ProtoObject* py_range(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    long long start = 0;
    long long stop = 0;
    long long step = 1;

    unsigned long argsSize = positionalParameters->getSize(context);
    if (argsSize == 1) {
        stop = positionalParameters->getAt(context, 0)->asLong(context);
    } else if (argsSize >= 2) {
        start = positionalParameters->getAt(context, 0)->asLong(context);
        stop = positionalParameters->getAt(context, 1)->asLong(context);
        if (argsSize >= 3) {
            step = positionalParameters->getAt(context, 2)->asLong(context);
        }
    }

    if (step == 0) return PROTO_NONE;

    const proto::ProtoObject* rangeProto = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__range_proto__"));
    if (!rangeProto) return PROTO_NONE;
    const proto::ProtoObject* rangeObj = rangeProto->newChild(context, true);
    rangeObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__range_cur__"), context->fromInteger(start));
    rangeObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__range_stop__"), context->fromInteger(stop));
    rangeObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__range_step__"), context->fromInteger(step));
    return rangeObj;
}

static const proto::ProtoObject* py_zip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    unsigned long n = positionalParameters->getSize(context);
    const proto::ProtoList* itersList = context->newList();
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* iterable = positionalParameters->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* iterM = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
        if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
        const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
        if (!it || it == PROTO_NONE) return PROTO_NONE;
        itersList = itersList->appendLast(context, it);
    }
    const proto::ProtoObject* zipProto = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__zip_proto__"));
    if (!zipProto) return PROTO_NONE;
    const proto::ProtoObject* zipObj = zipProto->newChild(context, true);
    zipObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__zip_iters__"), itersList->asObject(context));
    return zipObj;
}

static const proto::ProtoObject* py_zip_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* itersObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__zip_iters__"));
    if (!itersObj || !itersObj->asList(context)) return PROTO_NONE;
    const proto::ProtoList* iters = itersObj->asList(context);
    if (iters->getSize(context) == 0) return PROTO_NONE;
    const proto::ProtoList* tupleParts = context->newList();
    for (unsigned long i = 0; i < iters->getSize(context); ++i) {
        const proto::ProtoObject* it = iters->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* nextM = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
        if (!nextM || !nextM->asMethod(context)) return PROTO_NONE;
        const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) return PROTO_NONE;
        tupleParts = tupleParts->appendLast(context, val);
    }
    return context->newTupleFromList(tupleParts)->asObject(context);
}

static const proto::ProtoObject* py_filter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* func = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* call = func->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__"));
    if (!call || !call->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* iterM = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it || it == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* filterProto = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__filter_proto__"));
    if (!filterProto) return PROTO_NONE;
    const proto::ProtoObject* boolType = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bool"));
    const proto::ProtoObject* filterObj = filterProto->newChild(context, true);
    filterObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__filter_func__"), func);
    filterObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__filter_iter__"), it);
    if (boolType) filterObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__filter_bool__"), boolType);
    return filterObj;
}

static const proto::ProtoObject* py_filter_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* func = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__filter_func__"));
    const proto::ProtoObject* it = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__filter_iter__"));
    if (!func || !it) return PROTO_NONE;
    const proto::ProtoObject* call = func->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__"));
    const proto::ProtoObject* nextM = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!call || !call->asMethod(context) || !nextM || !nextM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* boolType = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__filter_bool__"));
    const proto::ProtoObject* boolCall = boolType ? boolType->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__")) : nullptr;
    if (!boolCall || !boolCall->asMethod(context)) return PROTO_NONE;
    for (;;) {
        const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) return PROTO_NONE;
        const proto::ProtoList* oneArg = context->newList()->appendLast(context, val);
        const proto::ProtoObject* result = call->asMethod(context)(context, func, nullptr, oneArg, nullptr);
        const proto::ProtoList* boolArg = context->newList()->appendLast(context, result);
        const proto::ProtoObject* b = boolCall->asMethod(context)(context, boolType, nullptr, boolArg, nullptr);
        if (b == PROTO_TRUE) return val;
    }
}

static const proto::ProtoObject* py_map(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* func = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* call = func->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__"));
    if (!call || !call->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* iterM = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it || it == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* mapProto = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__map_proto__"));
    if (!mapProto) return PROTO_NONE;
    const proto::ProtoObject* mapObj = mapProto->newChild(context, true);
    mapObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__map_func__"), func);
    mapObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__map_iter__"), it);
    return mapObj;
}

static const proto::ProtoObject* py_map_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* func = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__map_func__"));
    const proto::ProtoObject* it = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__map_iter__"));
    if (!func || !it) return PROTO_NONE;
    const proto::ProtoObject* call = func->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__"));
    const proto::ProtoObject* nextM = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!call || !call->asMethod(context) || !nextM || !nextM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
    if (!val || val == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoList* oneArg = context->newList()->appendLast(context, val);
    return call->asMethod(context)(context, func, nullptr, oneArg, nullptr);
}

static const proto::ProtoObject* py_range_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* curObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__range_cur__"));
    const proto::ProtoObject* stopObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__range_stop__"));
    const proto::ProtoObject* stepObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__range_step__"));
    if (!curObj || !stopObj || !stepObj) return PROTO_NONE;
    long long cur = curObj->asLong(context);
    long long stop = stopObj->asLong(context);
    long long step = stepObj->asLong(context);

    bool done = (step > 0 && cur >= stop) || (step < 0 && cur <= stop);
    if (done) return PROTO_NONE;

    self->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__range_cur__"), context->fromInteger(cur + step));
    return context->fromInteger(cur);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                   const proto::ProtoObject* typeProto, const proto::ProtoObject* intProto,
                                   const proto::ProtoObject* strProto, const proto::ProtoObject* listProto,
                                   const proto::ProtoObject* dictProto, const proto::ProtoObject* tupleProto,
                                   const proto::ProtoObject* setProto, const proto::ProtoObject* bytesProto,
                                   const proto::ProtoObject* sliceType, const proto::ProtoObject* frozensetProto,
                                   const proto::ProtoObject* floatProto, const proto::ProtoObject* boolProto,
                                   const proto::ProtoObject* ioModule) {
    const proto::ProtoObject* builtins = ctx->newObject(true);
    if (ioModule && ioModule != PROTO_NONE) {
        builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__io_module__"), ioModule);
    }

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "True"), PROTO_TRUE);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "False"), PROTO_FALSE);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "object"), objectProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "type"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_type));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "int"), intProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "str"), strProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "list"), listProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dict"), dictProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "tuple"), tupleProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "set"), setProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "bytes"), bytesProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "slice"), sliceType);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "frozenset"), frozensetProto);
    if (floatProto) builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "float"), floatProto);
    if (boolProto) builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "bool"), boolProto);
    
    // Add functions
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "len"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_len));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "repr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_repr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "format"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_format));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "open"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_open));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "id"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_id));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "print"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_print));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "iter"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_iter));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "next"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "contains"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_contains));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "in"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_contains));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isinstance"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_isinstance));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "issubclass"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_issubclass));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "range"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_range));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "enumerate"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_enumerate));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "reversed"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_reversed));
    const proto::ProtoObject* zipProto = ctx->newObject(true);
    zipProto = zipProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(zipProto), py_iter_self));
    zipProto = zipProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(zipProto), py_zip_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__zip_proto__"), zipProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "zip"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_zip));
    const proto::ProtoObject* filterProto = ctx->newObject(true);
    filterProto = filterProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(filterProto), py_iter_self));
    filterProto = filterProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(filterProto), py_filter_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__filter_proto__"), filterProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "filter"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_filter));
    const proto::ProtoObject* mapProto = ctx->newObject(true);
    mapProto = mapProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mapProto), py_iter_self));
    mapProto = mapProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mapProto), py_map_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__map_proto__"), mapProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "map"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_map));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sum"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_sum));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "all"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_all));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "any"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_any));

    const proto::ProtoObject* enumProto = ctx->newObject(true);
    enumProto = enumProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(enumProto), py_iter_self));
    enumProto = enumProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(enumProto), py_enumerate_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__enumerate_proto__"), enumProto);

    const proto::ProtoObject* revProto = ctx->newObject(true);
    revProto = revProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(revProto), py_iter_self));
    revProto = revProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(revProto), py_reversed_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__reversed_proto__"), revProto);

    const proto::ProtoObject* rangeProto = ctx->newObject(true);
    rangeProto = rangeProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(rangeProto), py_iter_self));
    rangeProto = rangeProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(rangeProto), py_range_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__range_proto__"), rangeProto);

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "abs"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_abs));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "min"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_min));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "max"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_max));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pow"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_pow));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "round"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_round));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "divmod"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_divmod));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ascii"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_ascii));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ord"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_ord));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "chr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_chr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "bin"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_bin));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "oct"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_oct));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hex"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hex));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "sorted"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_sorted));

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "callable"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_callable));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_getattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_setattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hasattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hasattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "delattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_delattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "raise"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_raise));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dir"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_dir));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "vars"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_vars));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "input"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_input));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "breakpoint"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_breakpoint));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "globals"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_globals));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "locals"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_locals));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "eval"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_eval));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exec"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_exec));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hash"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hash));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "help"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_help));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "memoryview"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_memoryview));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "super"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_super));

    return builtins;
}

} // namespace builtins
} // namespace protoPython
