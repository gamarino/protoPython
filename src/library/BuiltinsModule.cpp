#include <protoPython/BuiltinsModule.h>
#include <iostream>

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
    unsigned long size = positionalParameters->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* obj = positionalParameters->getAt(context, static_cast<int>(i));
        
        // Call str(obj)
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
        
        if (i < size - 1) std::cout << " ";
    }
    std::cout << std::endl;
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
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);

    if (obj->asStringIterator(context)) {
        proto::ProtoStringIterator* it = const_cast<proto::ProtoStringIterator*>(obj->asStringIterator(context));
        if (!it || !it->hasNext(context)) return PROTO_NONE;
        return it->next(context);
    }

    const proto::ProtoObject* nextMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (nextMethod && nextMethod->asMethod(context)) {
        return nextMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
    }

    return PROTO_NONE;
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

    const proto::ProtoObject* boolMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__bool__"));
    if (boolMethod && boolMethod->asMethod(context)) {
        return boolMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
    }

    return PROTO_TRUE;
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
    
    const proto::ProtoList* list = context->newList();
    if (step > 0) {
        for (long long i = start; i < stop; i += step) {
            list = list->appendLast(context, context->fromInteger(i));
        }
    } else {
        for (long long i = start; i > stop; i += step) {
            list = list->appendLast(context, context->fromInteger(i));
        }
    }
    
    return list->asObject(context);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                   const proto::ProtoObject* typeProto, const proto::ProtoObject* intProto,
                                   const proto::ProtoObject* strProto, const proto::ProtoObject* listProto,
                                   const proto::ProtoObject* dictProto, const proto::ProtoObject* tupleProto,
                                   const proto::ProtoObject* setProto, const proto::ProtoObject* bytesProto,
                                   const proto::ProtoObject* sliceType) {
    const proto::ProtoObject* builtins = ctx->newObject(true);

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
    
    // Add functions
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "len"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_len));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "id"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_id));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "print"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_print));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "iter"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_iter));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "next"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "contains"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_contains));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "bool"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_bool));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isinstance"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_isinstance));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "issubclass"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_issubclass));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "range"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_range));

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "abs"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_abs));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "min"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_min));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "max"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_max));

    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "callable"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_callable));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_getattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_setattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hasattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hasattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "delattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_delattr));

    return builtins;
}

} // namespace builtins
} // namespace protoPython
