#include <protoPython/BuiltinsModule.h>
#include <protoPython/Compiler.h>
#include <protoPython/Parser.h>
#include <protoPython/Tokenizer.h>
#include <protoPython/PythonEnvironment.h>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

namespace protoPython {
namespace builtins {
using protoPython::PythonEnvironment;

static const proto::ProtoObject* py_import(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) == 0) return PROTO_NONE;
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 0);
    if (!nameObj->isString(context)) return PROTO_NONE;
    std::string moduleName;
    nameObj->asString(context)->toUTF8String(context, moduleName);
    
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    return env ? env->resolve(moduleName) : PROTO_NONE;
}

static const proto::ProtoObject* py_dir(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    // Stub: returns a minimal list of names or empty list
    return context->newList()->asObject(context);
}

static const proto::ProtoObject* py_id(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)keywordParameters;
    if (positionalParameters->getSize(context) == 0) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    // id in Python is often the memory address
    return context->fromInteger(reinterpret_cast<long long>(obj));
}

static const proto::ProtoObject* py_getattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)keywordParameters;
    if (positionalParameters->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    if (!nameObj->isString(context)) return PROTO_NONE;
    const proto::ProtoObject* attr = obj->getAttribute(context, nameObj->asString(context));
    if (attr) return attr;
    if (positionalParameters->getSize(context) >= 3) return positionalParameters->getAt(context, 2);
    return PROTO_NONE; // Should raise AttributeError
}

static const proto::ProtoObject* py_setattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)keywordParameters;
    if (positionalParameters->getSize(context) < 3) return PROTO_NONE;
    proto::ProtoObject* obj = const_cast<proto::ProtoObject*>(positionalParameters->getAt(context, 0));
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    const proto::ProtoObject* val = positionalParameters->getAt(context, 2);
    if (!nameObj->isString(context)) return PROTO_NONE;
    obj->setAttribute(context, nameObj->asString(context), val);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_hasattr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self; (void)parentLink; (void)keywordParameters;
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* nameObj = positionalParameters->getAt(context, 1);
    if (!nameObj->isString(context)) return PROTO_FALSE;
    return obj->getAttribute(context, nameObj->asString(context)) ? PROTO_TRUE : PROTO_FALSE;
}

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
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* lenS = env ? env->getLenString() : proto::ProtoString::fromUTF8String(context, "__len__");
    const proto::ProtoObject* lenMethod = obj->getAttribute(context, lenS);
    if (lenMethod && lenMethod->asMethod(context)) {
        return lenMethod->asMethod(context)(context, obj, nullptr, env ? env->getEmptyList() : nullptr, nullptr);
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

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* strS = env ? env->getStrString() : proto::ProtoString::fromUTF8String(context, "__str__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    unsigned long size = positionalParameters->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* obj = positionalParameters->getAt(context, static_cast<int>(i));

        const proto::ProtoObject* strMethod = obj->getAttribute(context, strS);
        const proto::ProtoObject* strObj = PROTO_NONE;
        if (strMethod && strMethod->asMethod(context)) {
            strObj = strMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
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
    std::cout << end << std::flush;
    return env ? env->getNonePrototype() : PROTO_NONE;
}

static const proto::ProtoObject* py_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");

    const proto::ProtoObject* iterMethod = obj->getAttribute(context, iterS);
    if (iterMethod && iterMethod->asMethod(context)) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
        return iterMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
    }

    if (env) {
        std::string typeName = "object";
        const proto::ProtoString* classS = env->getClassString();
        const proto::ProtoString* nameS = env->getNameString();
        const proto::ProtoObject* cls = obj->getAttribute(context, classS);
        if (cls) {
            const proto::ProtoObject* nameAttr = cls->getAttribute(context, nameS);
            if (nameAttr && nameAttr->isString(context)) nameAttr->asString(context)->toUTF8String(context, typeName);
        }
        env->raiseTypeError(context, "'" + typeName + "' object is not iterable");
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

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoObject* nextMethod = obj->getAttribute(context, nextS);
    if (nextMethod && nextMethod->asMethod(context)) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
        const proto::ProtoObject* result = nextMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
        if (!result || result == (env ? env->getNonePrototype() : nullptr)) {
            if (defaultVal) return defaultVal;
            if (env) env->raiseStopIteration(context);
            return PROTO_NONE;
        }
        return result;
    }

    if (env) {
        std::string typeName = "object";
        const proto::ProtoString* classS = env->getClassString();
        const proto::ProtoString* nameS = env->getNameString();
        const proto::ProtoObject* cls = obj->getAttribute(context, classS);
        if (cls) {
            const proto::ProtoObject* nameAttr = cls->getAttribute(context, nameS);
            if (nameAttr && nameAttr->isString(context)) nameAttr->asString(context)->toUTF8String(context, typeName);
        }
        env->raiseTypeError(context, "'" + typeName + "' object is not an iterator");
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
    const proto::ProtoObject* item = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* container = positionalParameters->getAt(context, 1);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);

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

    const proto::ProtoString* containsS = env ? env->getContainsString() : proto::ProtoString::fromUTF8String(context, "__contains__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* containsMethod = container->getAttribute(context, containsS);
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

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* boolS = env ? env->getBoolString() : proto::ProtoString::fromUTF8String(context, "__bool__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* boolMethod = obj->getAttribute(context, boolS);
    if (boolMethod && boolMethod->asMethod(context)) {
        return boolMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
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
    if (obj->isInteger(context)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)obj->asLong(context));
        return context->fromUTF8String(buf);
    }
    if (obj->isDouble(context)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", obj->asDouble(context));
        return context->fromUTF8String(buf);
    }
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* reprS = env ? env->getReprString() : proto::ProtoString::fromUTF8String(context, "__repr__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* reprMethod = obj->getAttribute(context, reprS);
    if (reprMethod && reprMethod->asMethod(context)) {
        return reprMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
    }
    return context->fromUTF8String("<object>");
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* formatS = env ? env->getFormatString() : proto::ProtoString::fromUTF8String(context, "__format__");

    const proto::ProtoObject* formatMethod = obj->getAttribute(context, formatS);
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* ioMod = self->getAttribute(context, env ? env->getIOModuleString() : proto::ProtoString::fromUTF8String(context, "__io_module__"));
    if (!ioMod || ioMod == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* openFunc = ioMod->getAttribute(context, env ? env->getOpenString() : proto::ProtoString::fromUTF8String(context, "open"));
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

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* enumProtoS = env ? env->getEnumProtoString() : proto::ProtoString::fromUTF8String(context, "__enumerate_proto__");
    const proto::ProtoString* itS = env ? env->getEnumIterString() : proto::ProtoString::fromUTF8String(context, "__enumerate_it__");
    const proto::ProtoString* idxS = env ? env->getEnumIdxString() : proto::ProtoString::fromUTF8String(context, "__enumerate_idx__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, iterS);
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it || it == (env ? env->getNonePrototype() : nullptr)) return PROTO_NONE;

    const proto::ProtoObject* enumProto = self->getAttribute(context, enumProtoS);
    if (!enumProto) return PROTO_NONE;
    const proto::ProtoObject* enumObj = enumProto->newChild(context, true);
    enumObj->setAttribute(context, itS, it);
    enumObj->setAttribute(context, idxS, context->fromInteger(start));
    return enumObj;
}

static const proto::ProtoObject* py_enumerate_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* itS = env ? env->getEnumIterString() : proto::ProtoString::fromUTF8String(context, "__enumerate_it__");
    const proto::ProtoString* idxS = env ? env->getEnumIdxString() : proto::ProtoString::fromUTF8String(context, "__enumerate_idx__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoObject* it = self->getAttribute(context, itS);
    const proto::ProtoObject* idxObj = self->getAttribute(context, idxS);
    if (!it || !idxObj) return PROTO_NONE;

    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_NONE;
    
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* value = nextMethod->asMethod(context)(context, it, nullptr, emptyL, nullptr);
    if (!value || value == (env ? env->getNonePrototype() : nullptr)) return PROTO_NONE;

    long long idx = idxObj->asLong(context);
    self->setAttribute(context, idxS, context->fromInteger(idx + 1));
    
    const proto::ProtoList* l = context->newList();
    l = l->appendLast(context, idxObj);
    l = l->appendLast(context, value);
    return context->newTupleFromList(l)->asObject(context);
}

static const proto::ProtoObject* py_reversed(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* reversedS = env ? env->getReversedString() : proto::ProtoString::fromUTF8String(context, "__reversed__");
    const proto::ProtoString* lenS = env ? env->getLenString() : proto::ProtoString::fromUTF8String(context, "__len__");
    const proto::ProtoString* getitemS = env ? env->getGetItemString() : proto::ProtoString::fromUTF8String(context, "__getitem__");
    const proto::ProtoString* revProtoS = env ? env->getRevProtoString() : proto::ProtoString::fromUTF8String(context, "__reversed_proto__");
    const proto::ProtoString* revObjS = env ? env->getRevObjString() : proto::ProtoString::fromUTF8String(context, "__reversed_obj__");
    const proto::ProtoString* revIdxS = env ? env->getRevIdxString() : proto::ProtoString::fromUTF8String(context, "__reversed_idx__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* revMethod = obj->getAttribute(context, reversedS);
    if (revMethod && revMethod->asMethod(context)) {
        return revMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
    }

    const proto::ProtoObject* lenMethod = obj->getAttribute(context, lenS);
    const proto::ProtoObject* getitemMethod = obj->getAttribute(context, getitemS);
    if (!lenMethod || !lenMethod->asMethod(context) || !getitemMethod || !getitemMethod->asMethod(context))
        return PROTO_NONE;
    const proto::ProtoObject* lenObj = lenMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
    if (!lenObj || !lenObj->isInteger(context)) return PROTO_NONE;
    long long n = lenObj->asLong(context);

    const proto::ProtoObject* revProto = self->getAttribute(context, revProtoS);
    if (!revProto) return PROTO_NONE;
    const proto::ProtoObject* revObj = revProto->newChild(context, true);
    revObj->setAttribute(context, revObjS, obj);
    revObj->setAttribute(context, revIdxS, context->fromInteger(n - 1));
    return revObj;
}

static const proto::ProtoObject* py_reversed_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* objS = env ? env->getRevObjString() : proto::ProtoString::fromUTF8String(context, "__reversed_obj__");
    const proto::ProtoString* idxS = env ? env->getRevIdxString() : proto::ProtoString::fromUTF8String(context, "__reversed_idx__");
    const proto::ProtoString* getitemS = env ? env->getGetItemString() : proto::ProtoString::fromUTF8String(context, "__getitem__");

    const proto::ProtoObject* obj = self->getAttribute(context, objS);
    const proto::ProtoObject* idxObj = self->getAttribute(context, idxS);
    if (!obj || !idxObj) return PROTO_NONE;
    long long idx = idxObj->asLong(context);
    if (idx < 0) return PROTO_NONE;

    const proto::ProtoObject* getitemMethod = obj->getAttribute(context, getitemS);
    if (!getitemMethod || !getitemMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(idx));
    const proto::ProtoObject* value = getitemMethod->asMethod(context)(context, obj, nullptr, args, nullptr);
    self->setAttribute(context, idxS, context->fromInteger(idx - 1));
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

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, iterS);
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it) return start;

    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");
    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) return start;

    long long acc = start->isInteger(context) ? start->asLong(context) : 0;
    auto nextFn = nextMethod->asMethod(context);
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;
    
    for (;;) {
        const proto::ProtoObject* val = nextFn(context, it, nullptr, emptyL, nullptr);
        if (!val || val == noneObj) break;
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
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, iterS);
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_TRUE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it) return PROTO_TRUE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_TRUE;

    auto nextFn = nextMethod->asMethod(context);
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    for (;;) {
        const proto::ProtoObject* val = nextFn(context, it, nullptr, emptyL, nullptr);
        if (!val || val == noneObj) break;
        if (!val->asBoolean(context)) return PROTO_FALSE;
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
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, iterS);
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_FALSE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it) return PROTO_FALSE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, nextS);
    if (!nextMethod || !nextMethod->asMethod(context)) return PROTO_FALSE;

    auto nextFn = nextMethod->asMethod(context);
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    for (;;) {
        const proto::ProtoObject* val = nextFn(context, it, nullptr, emptyL, nullptr);
        if (!val || val == noneObj) break;
        if (val->asBoolean(context)) return PROTO_TRUE;
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* call = obj->getAttribute(context, env ? env->getCallString() : proto::ProtoString::fromUTF8String(context, "__call__"));
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
    /* Use canonical ProtoString from content so getAttribute matches setAttribute storage. */
    std::string nameStr;
    nameObj->asString(context)->toUTF8String(context, nameStr);
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(context, nameStr.c_str());
    const proto::ProtoObject* attr = obj->getAttribute(context, key);
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
    /* Use canonical ProtoString from content so getAttribute matches setAttribute storage. */
    std::string nameStr;
    nameObj->asString(context)->toUTF8String(context, nameStr);
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(context, nameStr.c_str());
    obj->setAttribute(context, key, value);
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
    
    const proto::ProtoObject* target = nullptr;
    if (positionalParameters->getSize(context) >= 1) {
        target = positionalParameters->getAt(context, 0);
    } else {
        // Without args, dir() should show locals. In our environment, 
        // we might not have easy access to the exact local frame here.
        // Return an empty list for now or try to get it from PythonEnvironment.
        return context->newList()->asObject(context);
    }

    if (!target) return context->newList()->asObject(context);

    std::vector<std::string> names;
    auto collect = [&](const proto::ProtoObject* obj) {
        if (!obj) return;
        const proto::ProtoSparseList* attrs = obj->getAttributes(context);
        if (attrs) {
            auto* it = const_cast<proto::ProtoSparseListIterator*>(attrs->getIterator(context));
            while (it && it->hasNext(context)) {
                unsigned long key = it->nextKey(context);
                const proto::ProtoString* s = reinterpret_cast<const proto::ProtoString*>(key);
                if (s) {
                    std::string name;
                    s->toUTF8String(context, name);
                    if (!name.empty()) names.push_back(name);
                }
                it = const_cast<proto::ProtoSparseListIterator*>(it->advance(context));
            }
        }
    };

    collect(target);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    const proto::ProtoList* result = context->newList();
    for (const auto& name : names) {
        result = result->appendLast(context, context->fromUTF8String(name.c_str()));
    }
    
    // Wrap in a Python list object (Step 1347 fix)
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* listType = self->getAttribute(context, env ? env->getListTypeString() : proto::ProtoString::fromUTF8String(context, "list"));
    if (listType && listType != PROTO_NONE) {
        const proto::ProtoObject* listObj = listType->newChild(context, true);
        listObj->setAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"), result->asObject(context));
        return listObj;
    }

    return result->asObject(context);
}

/** input([prompt]): read line from stdin, return as string. */
static const proto::ProtoObject* py_input(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) >= 1) {
        const proto::ProtoObject* promptObj = positionalParameters->getAt(context, 0);
        if (promptObj && promptObj->isString(context)) {
            std::string prompt;
            promptObj->asString(context)->toUTF8String(context, prompt);
            std::cout << prompt << std::flush;
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    std::istream* in = env ? env->getStdin() : &std::cin;
    std::string line;
    if (in && std::getline(*in, line))
        return context->fromUTF8String(line.c_str());
    return context->fromUTF8String("");
}

/** _tokenize_source(source): return list of (toktype_int, value_str) for tokenizer. Internal use by tokenize module. */
static const proto::ProtoObject* py__tokenize_source(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* srcObj = positionalParameters->getAt(context, 0);
    if (!srcObj->isString(context)) return PROTO_NONE;
    std::string source;
    srcObj->asString(context)->toUTF8String(context, source);
    Tokenizer tok(source);
    const proto::ProtoList* result = context->newList();
    while (true) {
        Token t = tok.next();
        if (t.type == TokenType::EndOfFile) break;
        const proto::ProtoList* pair = context->newList();
        pair = pair->appendLast(context, context->fromInteger(static_cast<int>(t.type)));
        pair = pair->appendLast(context, context->fromUTF8String(t.value.c_str()));
        result = result->appendLast(context, reinterpret_cast<const proto::ProtoObject*>(pair));
    }
    return reinterpret_cast<const proto::ProtoObject*>(result);
}

/** compile(source, filename='<string>', mode='eval'): return code object. */
static const proto::ProtoObject* py_compile(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* sourceObj = positionalParameters->getAt(context, 0);
    if (!sourceObj->isString(context)) return PROTO_NONE;
    std::string source;
    sourceObj->asString(context)->toUTF8String(context, source);
    std::string filename = "<string>";
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* fn = positionalParameters->getAt(context, 1);
        if (fn->isString(context)) fn->asString(context)->toUTF8String(context, filename);
    }
    std::string mode = "eval";
    if (positionalParameters->getSize(context) >= 3) {
        const proto::ProtoObject* m = positionalParameters->getAt(context, 2);
        if (m->isString(context)) m->asString(context)->toUTF8String(context, mode);
    }
    Compiler compiler(context, filename);
    if (mode == "eval") {
        Parser parser(source);
        std::unique_ptr<ASTNode> expr = parser.parseExpression();
        if (!expr || !compiler.compileExpression(expr.get())) return PROTO_NONE;
    } else {
        Parser parser(source);
        std::unique_ptr<ModuleNode> mod = parser.parseModule();
        if (!mod || mod->body.empty() || !compiler.compileModule(mod.get())) return PROTO_NONE;
    }
    return makeCodeObject(context, compiler.getConstants(), compiler.getNames(), compiler.getBytecode());
}

/** eval(expr, globals=None, locals=None): compile and run expression. */
static const proto::ProtoObject* py_eval(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* exprObj = positionalParameters->getAt(context, 0);
    if (!exprObj->isString(context)) return PROTO_NONE;
    std::string source;
    exprObj->asString(context)->toUTF8String(context, source);
    Parser parser(source);
    std::unique_ptr<ASTNode> expr = parser.parseExpression();
    if (!expr) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env && parser.hasError()) {
            std::string lineText = source;
            // Extract the specific line
            int line = parser.getLastErrorLine();
            size_t start = 0;
            for (int i = 1; i < line; ++i) {
                size_t next = source.find('\n', start);
                if (next == std::string::npos) break;
                start = next + 1;
            }
            size_t end = source.find('\n', start);
            lineText = source.substr(start, end == std::string::npos ? std::string::npos : end - start);
            env->raiseSyntaxError(context, parser.getLastErrorMsg(), line, parser.getLastErrorColumn(), lineText);
        }
        return PROTO_NONE;
    }
    Compiler compiler(context, "<string>");
    if (!compiler.compileExpression(expr.get())) return PROTO_NONE;
    const proto::ProtoObject* codeObj = makeCodeObject(context, compiler.getConstants(), compiler.getNames(), compiler.getBytecode());
    if (!codeObj) return PROTO_NONE;
    proto::ProtoObject* frame = nullptr;
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* g = positionalParameters->getAt(context, 1);
        if (g && g != PROTO_NONE) frame = const_cast<proto::ProtoObject*>(g);
    }
    if (!frame) frame = const_cast<proto::ProtoObject*>(context->newObject(true));
    return runCodeObject(context, codeObj, frame);
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
    (void)keywordParameters;

    PythonEnvironment* env = PythonEnvironment::fromContext(context);

    if (positionalParameters->getSize(context) == 0) {
        std::cout << "Welcome to protoPython 0.1.0 help!\n"
                  << "If this is your first time using Python, you should definitely check out\n"
                  << "the tutorial on the Internet at https://docs.python.org/3/tutorial/.\n\n"
                  << "Enter the name of any module, keyword, or topic to get help on writing\n"
                  << "Python programs and using Python modules.  To quit this help utility and\n"
                  << "return to the interpreter, just type \"quit\".\n\n"
                  << "To get help on an object, type help(object).\n";
        return PROTO_NONE;
    }

    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* type = obj->getAttribute(context, env ? env->getClassString() : proto::ProtoString::fromUTF8String(context, "__class__"));
    std::string typeName = "object";
    if (type) {
        const proto::ProtoObject* nameAttr = type->getAttribute(context, env ? env->getNameString() : proto::ProtoString::fromUTF8String(context, "__name__"));
        if (nameAttr && nameAttr->isString(context)) nameAttr->asString(context)->toUTF8String(context, typeName);
    }

    // Attempt to call __repr__
    const proto::ProtoString* reprS = env ? env->getReprString() : proto::ProtoString::fromUTF8String(context, "__repr__");
    const proto::ProtoObject* reprMethod = obj->getAttribute(context, reprS);
    if (reprMethod && reprMethod->asMethod(context)) {
        const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
        const proto::ProtoObject* r = reprMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
        if (r && r->isString(context)) {
            std::string s;
            r->asString(context)->toUTF8String(context, s);
            std::cout << "  Value: " << s << "\n";
        }
    }

    const proto::ProtoObject* doc = obj->getAttribute(context, env ? env->getDocString() : proto::ProtoString::fromUTF8String(context, "__doc__"));
    if (doc && doc->isString(context)) {
        std::string s;
        doc->asString(context)->toUTF8String(context, s);
        std::cout << "\n  Docstring:\n    " << s << "\n";
    } else {
        std::cout << "\n  (No docstring found for this object)\n";
    }

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

/** exec(source, globals=None, locals=None): compile and run source. */
static const proto::ProtoObject* py_exec(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)self;
    (void)parentLink;
    (void)keywordParameters;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* sourceObj = positionalParameters->getAt(context, 0);
    if (!sourceObj->isString(context)) return PROTO_NONE;
    std::string source;
    sourceObj->asString(context)->toUTF8String(context, source);
    Parser parser(source);
    std::unique_ptr<ModuleNode> mod = parser.parseModule();
    if (parser.hasError()) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) {
            std::string lineText = source;
            int line = parser.getLastErrorLine();
            size_t start = 0;
            for (int i = 1; i < line; ++i) {
                size_t next = source.find('\n', start);
                if (next == std::string::npos) break;
                start = next + 1;
            }
            size_t end = source.find('\n', start);
            lineText = source.substr(start, end == std::string::npos ? std::string::npos : end - start);
            env->raiseSyntaxError(context, parser.getLastErrorMsg(), line, parser.getLastErrorColumn(), lineText);
        }
        return PROTO_NONE;
    }
    if (!mod || mod->body.empty()) {
        return PROTO_NONE;
    }
    if (std::getenv("PROTO_THREAD_DIAG")) {
        size_t n = mod->body.size();
        std::cerr << "[proto-thread-diag] exec: parsed " << n << " statements\n" << std::flush;
    }
    Compiler compiler(context, "<string>");
    if (!compiler.compileModule(mod.get())) {
        if (std::getenv("PROTO_THREAD_DIAG"))
            std::cerr << "[proto-thread-diag] exec: compileModule failed\n" << std::flush;
        return PROTO_NONE;
    }
    const proto::ProtoObject* codeObj = makeCodeObject(context, compiler.getConstants(), compiler.getNames(), compiler.getBytecode());
    if (!codeObj) return PROTO_NONE;
    proto::ProtoObject* frame = nullptr;
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* g = positionalParameters->getAt(context, 1);
        if (g && g != PROTO_NONE) frame = const_cast<proto::ProtoObject*>(g);
    }
    if (!frame) frame = const_cast<proto::ProtoObject*>(context->newObject(true));
    const proto::ProtoObject* result = runCodeObject(context, codeObj, frame);
    return result;
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
    d->setAttribute(context, env ? env->getKeysString() : proto::ProtoString::fromUTF8String(context, "__keys__"), context->newList()->asObject(context));
    d->setAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
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
    d->setAttribute(context, env ? env->getKeysString() : proto::ProtoString::fromUTF8String(context, "__keys__"), context->newList()->asObject(context));
    d->setAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* d = obj->getAttribute(context, env ? env->getDictDunderString() : proto::ProtoString::fromUTF8String(context, "__dict__"));
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
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterMethod = iterable->getAttribute(context, env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__"));
    if (!iterMethod || !iterMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterMethod->asMethod(context)(context, iterable, nullptr, env ? env->getEmptyList() : context->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* nextMethod = it->getAttribute(context, env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__"));
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

    if (!env) return PROTO_NONE;
    const proto::ProtoObject* listObj = env->getListPrototype()->newChild(context, true);
    listObj->setAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"), resultList->asObject(context));
    return listObj;
}

static const proto::ProtoObject* py_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* hashMethod = obj->getAttribute(context, env ? env->getHashString() : proto::ProtoString::fromUTF8String(context, "__hash__"));
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
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    if (obj->isDouble(context)) {
        const proto::ProtoObject* fp = self->getAttribute(context, env ? env->getFloatTypeNameString() : proto::ProtoString::fromUTF8String(context, "float"));
        if (env && fp && fp != PROTO_NONE) env->getBuiltins()->setAttribute(context, env->getFloatTypeNameString(), fp);
        return fp ? fp : PROTO_NONE;
    }
    if (obj == PROTO_TRUE || obj == PROTO_FALSE) {
        const proto::ProtoObject* bp = self->getAttribute(context, env ? env->getBoolTypeNameString() : proto::ProtoString::fromUTF8String(context, "bool"));
        if (env && bp && bp != PROTO_NONE) env->getBuiltins()->setAttribute(context, env->getBoolTypeNameString(), bp);
        return bp ? bp : PROTO_NONE;
    }
    if (obj->isString(context)) {
        const proto::ProtoObject* sp = self->getAttribute(context, env ? env->getStrTypeNameString() : proto::ProtoString::fromUTF8String(context, "str"));
        if (env && sp && sp != PROTO_NONE) env->getBuiltins()->setAttribute(context, env->getStrTypeNameString(), sp);
        return sp ? sp : PROTO_NONE;
    }
    const proto::ProtoObject* cls = obj->getAttribute(context, env ? env->getClassString() : proto::ProtoString::fromUTF8String(context, "__class__"));
    return cls ? cls : PROTO_NONE;
}

static const proto::ProtoObject* py_isinstance(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (positionalParameters->getSize(context) < 2) return PROTO_FALSE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* cls = positionalParameters->getAt(context, 1);
    if (obj == PROTO_TRUE || obj == PROTO_FALSE) {
        const proto::ProtoObject* boolType = self->getAttribute(context, env ? env->getBoolTypeNameString() : proto::ProtoString::fromUTF8String(context, "bool"));
        const proto::ProtoObject* intType = self->getAttribute(context, env ? env->getIntTypeNameString() : proto::ProtoString::fromUTF8String(context, "int"));
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
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* reprMethod = obj->getAttribute(context, env ? env->getReprString() : proto::ProtoString::fromUTF8String(context, "__repr__"));
    if (!reprMethod || !reprMethod->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* reprObj = reprMethod->asMethod(context)(context, obj, nullptr, emptyL, nullptr);
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
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
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
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
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
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
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

    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* rangeProtoS = env ? env->getRangeProtoString() : proto::ProtoString::fromUTF8String(context, "__range_proto__");
    const proto::ProtoString* curS = env ? env->getRangeCurString() : proto::ProtoString::fromUTF8String(context, "__range_cur__");
    const proto::ProtoString* stopS = env ? env->getRangeStopString() : proto::ProtoString::fromUTF8String(context, "__range_stop__");
    const proto::ProtoString* stepS = env ? env->getRangeStepString() : proto::ProtoString::fromUTF8String(context, "__range_step__");

    const proto::ProtoObject* rangeProto = self->getAttribute(context, rangeProtoS);
    if (!rangeProto) return PROTO_NONE;
    const proto::ProtoObject* rangeObj = rangeProto->newChild(context, true);
    rangeObj->setAttribute(context, curS, context->fromInteger(start));
    rangeObj->setAttribute(context, stopS, context->fromInteger(stop));
    rangeObj->setAttribute(context, stepS, context->fromInteger(step));
    return rangeObj;
}

static const proto::ProtoObject* py_zip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    unsigned long n = positionalParameters->getSize(context);
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* zipProtoS = env ? env->getZipProtoString() : proto::ProtoString::fromUTF8String(context, "__zip_proto__");
    const proto::ProtoString* zipItersS = env ? env->getZipItersString() : proto::ProtoString::fromUTF8String(context, "__zip_iters__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    const proto::ProtoList* itersList = context->newList();
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* iterable = positionalParameters->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
        if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
        const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
        if (!it || it == noneObj) return PROTO_NONE;
        itersList = itersList->appendLast(context, it);
    }
    const proto::ProtoObject* zipProto = self->getAttribute(context, zipProtoS);
    if (!zipProto) return PROTO_NONE;
    const proto::ProtoObject* zipObj = zipProto->newChild(context, true);
    zipObj->setAttribute(context, zipItersS, itersList->asObject(context));
    return zipObj;
}

static const proto::ProtoObject* py_zip_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* itersS = env ? env->getZipItersString() : proto::ProtoString::fromUTF8String(context, "__zip_iters__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");

    const proto::ProtoObject* itersObj = self->getAttribute(context, itersS);
    if (!itersObj || !itersObj->asList(context)) return PROTO_NONE;
    const proto::ProtoList* iters = itersObj->asList(context);
    unsigned long n = iters->getSize(context);
    if (n == 0) return PROTO_NONE;

    const proto::ProtoList* resList = context->newList();
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();

    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* it = iters->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* nextM = it ? it->getAttribute(context, nextS) : nullptr;
        if (!nextM || !nextM->asMethod(context)) return PROTO_NONE;
        const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
        if (!val || val == (env ? env->getNonePrototype() : nullptr)) return PROTO_NONE;
        resList = resList->appendLast(context, val);
    }
    return context->newTupleFromList(resList)->asObject(context);
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
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(context, "__call__");
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* filterProtoS = env ? env->getFilterProtoString() : proto::ProtoString::fromUTF8String(context, "__filter_proto__");
    const proto::ProtoString* boolTypeS = env ? env->getBoolTypeNameString() : proto::ProtoString::fromUTF8String(context, "bool");
    const proto::ProtoString* filterFuncS = env ? env->getFilterFuncString() : proto::ProtoString::fromUTF8String(context, "__filter_func__");
    const proto::ProtoString* filterIterS = env ? env->getFilterIterString() : proto::ProtoString::fromUTF8String(context, "__filter_iter__");
    const proto::ProtoString* filterBoolS = env ? env->getFilterBoolString() : proto::ProtoString::fromUTF8String(context, "__filter_bool__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    const proto::ProtoObject* call = func->getAttribute(context, callS);
    if (!call || !call->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
    if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it || it == noneObj) return PROTO_NONE;
    const proto::ProtoObject* filterProto = self->getAttribute(context, filterProtoS);
    if (!filterProto) return PROTO_NONE;
    const proto::ProtoObject* boolType = self->getAttribute(context, boolTypeS);
    const proto::ProtoObject* filterObj = filterProto->newChild(context, true);
    filterObj->setAttribute(context, filterFuncS, func);
    filterObj->setAttribute(context, filterIterS, it);
    if (boolType) filterObj->setAttribute(context, filterBoolS, boolType);
    return filterObj;
}

static bool filter_is_truthy(proto::ProtoContext* context, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    if (obj == PROTO_FALSE) return false;
    if (obj == PROTO_TRUE) return true;
    if (obj->isInteger(context)) return obj->asLong(context) != 0;
    if (obj->isDouble(context)) return obj->asDouble(context) != 0.0;
    return true;
}

static const proto::ProtoObject* py_filter_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* funcS = env ? env->getFilterFuncString() : proto::ProtoString::fromUTF8String(context, "__filter_func__");
    const proto::ProtoString* iterS = env ? env->getFilterIterString() : proto::ProtoString::fromUTF8String(context, "__filter_iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");
    const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(context, "__call__");

    const proto::ProtoObject* func = self->getAttribute(context, funcS);
    const proto::ProtoObject* it = self->getAttribute(context, iterS);
    if (!func || !it) return PROTO_NONE;
    const proto::ProtoObject* call = func->getAttribute(context, callS);
    const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
    if (!call || !call->asMethod(context) || !nextM || !nextM->asMethod(context)) return PROTO_NONE;

    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    for (;;) {
        const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
        if (!val || val == noneObj) return PROTO_NONE;
        
        const proto::ProtoList* args = context->newList()->appendLast(context, val);
        const proto::ProtoObject* result = call->asMethod(context)(context, func, nullptr, args, nullptr);
        if (filter_is_truthy(context, result)) return val;
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
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(context, "__call__");
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* mapProtoS = env ? env->getMapProtoString() : proto::ProtoString::fromUTF8String(context, "__map_proto__");
    const proto::ProtoString* mapFuncS = env ? env->getMapFuncString() : proto::ProtoString::fromUTF8String(context, "__map_func__");
    const proto::ProtoString* mapIterS = env ? env->getMapIterString() : proto::ProtoString::fromUTF8String(context, "__map_iter__");
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* noneObj = env ? env->getNonePrototype() : nullptr;

    const proto::ProtoObject* call = func->getAttribute(context, callS);
    if (!call || !call->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
    if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
    if (!it || it == noneObj) return PROTO_NONE;
    const proto::ProtoObject* mapProto = self->getAttribute(context, mapProtoS);
    if (!mapProto) return PROTO_NONE;
    const proto::ProtoObject* mapObj = mapProto->newChild(context, true);
    mapObj->setAttribute(context, mapFuncS, func);
    mapObj->setAttribute(context, mapIterS, it);
    return mapObj;
}

static const proto::ProtoObject* py_map_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* funcS = env ? env->getMapFuncString() : proto::ProtoString::fromUTF8String(context, "__map_func__");
    const proto::ProtoString* iterS = env ? env->getMapIterString() : proto::ProtoString::fromUTF8String(context, "__map_iter__");
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");
    const proto::ProtoString* callS = env ? env->getCallString() : proto::ProtoString::fromUTF8String(context, "__call__");

    const proto::ProtoObject* func = self->getAttribute(context, funcS);
    const proto::ProtoObject* it = self->getAttribute(context, iterS);
    if (!func || !it) return PROTO_NONE;
    const proto::ProtoObject* call = func->getAttribute(context, callS);
    const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
    if (!call || !call->asMethod(context) || !nextM || !nextM->asMethod(context)) return PROTO_NONE;
    
    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
    const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
    if (!val || val == (env ? env->getNonePrototype() : nullptr)) return PROTO_NONE;
    const proto::ProtoList* oneArg = context->newList()->appendLast(context, val);
    return call->asMethod(context)(context, func, nullptr, oneArg, nullptr);
}
static const proto::ProtoObject* py_range_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::protoPython::PythonEnvironment* env = ::protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoString* curS = env ? env->getRangeCurString() : proto::ProtoString::fromUTF8String(context, "__range_cur__");
    const proto::ProtoString* stopS = env ? env->getRangeStopString() : proto::ProtoString::fromUTF8String(context, "__range_stop__");
    const proto::ProtoString* stepS = env ? env->getRangeStepString() : proto::ProtoString::fromUTF8String(context, "__range_step__");

    const proto::ProtoObject* curObj = self->getAttribute(context, curS);
    const proto::ProtoObject* stopObj = self->getAttribute(context, stopS);
    const proto::ProtoObject* stepObj = self->getAttribute(context, stepS);
    if (!curObj || !stopObj || !stepObj) return PROTO_NONE;
    long long cur = curObj->asLong(context);
    long long stop = stopObj->asLong(context);
    long long step = stepObj->asLong(context);

    bool done = (step > 0 && cur >= stop) || (step < 0 && cur <= stop);
    if (done) return PROTO_NONE;

    self->setAttribute(context, curS, context->fromInteger(cur + step));
    return context->fromInteger(cur);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                   const proto::ProtoObject* typeProto, const proto::ProtoObject* intProto,
                                   const proto::ProtoObject* strProto, const proto::ProtoObject* listProto,
                                   const proto::ProtoObject* dictProto, const proto::ProtoObject* tupleProto,
                                   const proto::ProtoObject* setProto, const proto::ProtoObject* bytesProto,
                                   const proto::ProtoObject* noneProto,
                                   const proto::ProtoObject* sliceType, const proto::ProtoObject* frozensetProto,
                                   const proto::ProtoObject* floatProto, const proto::ProtoObject* boolProto,
                                   const proto::ProtoObject* ioModule) {
    const proto::ProtoObject* builtins = ctx->newObject(true);
    if (noneProto) {
        builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "None"), noneProto);
    }
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
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dir"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_dir));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "id"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_id));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_getattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "setattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_setattr));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hasattr"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hasattr));
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
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_tokenize_source"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py__tokenize_source));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "compile"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_compile));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "eval"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_eval));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exec"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_exec));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "hash"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_hash));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "help"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_help));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "memoryview"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_memoryview));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "super"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_super));
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__import__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(builtins), py_import));

    return builtins;
}

} // namespace builtins
} // namespace protoPython
