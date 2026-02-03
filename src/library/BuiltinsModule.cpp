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
    
    const proto::ProtoObject* nextMethod = obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (nextMethod && nextMethod->asMethod(context)) {
        return nextMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
    }
    
    return PROTO_NONE;
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
                                   const proto::ProtoObject* dictProto, const proto::ProtoObject* tupleProto) {
    const proto::ProtoObject* builtins = ctx->newObject(true);
    
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "object"), objectProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "type"), typeProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "int"), intProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "str"), strProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "list"), listProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dict"), dictProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "tuple"), tupleProto);
    
    // Add functions
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "len"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_len));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "id"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_id));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "print"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_print));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "iter"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_iter));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "next"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_next));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isinstance"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_isinstance));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "issubclass"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_issubclass));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "range"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_range));

    return builtins;
}

} // namespace builtins
} // namespace protoPython
