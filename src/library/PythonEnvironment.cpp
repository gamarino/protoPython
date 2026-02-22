#include <protoPython/PythonEnvironment.h>
#include <protoPython/Tokenizer.h>
#include <protoPython/SignalModule.h>
#include <protoPython/PythonModuleProvider.h>
#include <protoPython/CompiledModuleProvider.h>
#include <protoPython/NativeModuleProvider.h>
#include <protoPython/SysModule.h>
#include <protoPython/HPyModuleProvider.h>
#include <protoPython/TimeModule.h>
#include <protoPython/ThreadModule.h>
#include <protoPython/BuiltinsModule.h>
#include <protoPython/AstModule.h>
#include <protoPython/ErrnoModule.h>
#include <protoPython/StatModule.h>
#include <protoPython/StructModule.h>
#include <protoPython/ContextvarsModule.h>
#include <protoPython/CodecsModule.h>
#include <protoPython/IOModule.h>
#include <protoPython/CollectionsModule.h>
#include <protoPython/ExceptionsModule.h>
#include <protoPython/LoggingModule.h>
#include <protoPython/MathModule.h>
#include <protoPython/OperatorModule.h>
#include <protoPython/FunctoolsModule.h>
#include <protoPython/ItertoolsModule.h>
#include <protoPython/JsonModule.h>
#include <protoPython/CodecsModule.h>
#include <protoPython/ReModule.h>
#include <protoPython/OsModule.h>
#include <protoPython/OsPathModule.h>
#include <protoPython/PathlibModule.h>
#include <protoPython/CollectionsAbcModule.h>
#include <protoPython/AtexitModule.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/Parser.h>
#include <protoPython/Compiler.h>
#include <protoCore.h>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <thread>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <cstring>
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

static bool get_thread_diag() {
    static bool diag = std::getenv("PROTO_THREAD_DIAG") != nullptr;
    return diag;
}

static bool get_env_diag() {
    static bool diag = std::getenv("PROTO_ENV_DIAG") != nullptr;
    return diag;
}

#include <cstring>

namespace protoPython {

static const proto::ProtoString* getInternalString(proto::ProtoContext* ctx, const char* name) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        if (std::strcmp(name, "__keys__") == 0) return env->getKeysString();
        if (std::strcmp(name, "__data__") == 0) return env->getDataString();
        if (std::strcmp(name, "__dict__") == 0) return env->getDictDunderString();
        if (std::strcmp(name, "__init__") == 0) return env->getInitString();
        if (std::strcmp(name, "__name__") == 0) return env->getNameString();
        if (std::strcmp(name, "__class__") == 0) return env->getClassString();
        if (std::strcmp(name, "__str__") == 0) return env->getStrString();
        if (std::strcmp(name, "__repr__") == 0) return env->getReprString();
        if (std::strcmp(name, "__iter__") == 0) return env->getIterString();
        if (std::strcmp(name, "__next__") == 0) return env->getNextString();
        if (std::strcmp(name, "__contains__") == 0) return env->getContainsString();
        if (std::strcmp(name, "__get__") == 0) return env->getGetDunderString();
        if (std::strcmp(name, "__set__") == 0) return env->getSetDunderString();
        if (std::strcmp(name, "__delete__") == 0) return env->getDelDunderString();
    }
    return proto::ProtoString::fromUTF8String(ctx, name);
}

static bool isEmbeddedValue(const proto::ProtoObject* obj) {
    if (!obj) return false;
    if (obj == PROTO_NONE || obj == PROTO_TRUE || obj == PROTO_FALSE) return true;
    // For integers, we'd ideally use isInteger(ctx), but without ctx, we check bits 
    // strictly according to known public constants if possible, or fallback.
    // Given the constraints, bitwise check is only safe if it matches public constants.
    return (reinterpret_cast<uintptr_t>(obj) & 0x01) != 0;
}

// --- Dunder Methods Implementation ---

namespace builtins {
    const proto::ProtoObject* py_object_new(
        proto::ProtoContext* context,
        const proto::ProtoObject* self,
        const proto::ParentLink* parentLink,
        const proto::ProtoList* positionalParameters,
        const proto::ProtoSparseList* keywordParameters);
}

namespace weakref { const proto::ProtoObject* initialize(proto::ProtoContext* ctx); }
namespace math { const proto::ProtoObject* initialize(proto::ProtoContext* ctx); }
static const proto::ProtoObject* py_str_call(proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink* parentLink, const proto::ProtoList* posArgs, const proto::ProtoSparseList*);
static const proto::ProtoObject* py_repr_call(proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink* parentLink, const proto::ProtoList* posArgs, const proto::ProtoSparseList*);

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
    const proto::ProtoSparseList* keywordParameters);

static const proto::ProtoObject* py_list_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

static const proto::ProtoObject* py_tuple_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

static const proto::ProtoObject* py_dict_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

static const proto::ProtoObject* py_str_maketrans(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters);

// --- SafeImportLock Implementation ---

PythonEnvironment::SafeImportLock::SafeImportLock(PythonEnvironment* env, proto::ProtoContext* ctx)
    : env_(env), ctx_(ctx) {
    if (ctx_ && ctx_->thread) {
        ctx_->thread->synchToGC();
    }
}

PythonEnvironment::SafeImportLock::~SafeImportLock() {
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
    // Step V74: Return new child with __class__ set to self
    const proto::ProtoObject* obj = self->newChild(context, true);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* classS = env ? env->getClassString() : getInternalString(context, "__class__");
    obj = obj->setAttribute(context, classS, self);
    
    // Step V75: Call __init__ if present
    const proto::ProtoString* initS = env ? env->getInitString() : getInternalString(context, "__init__");
    const proto::ProtoObject* initM = env ? env->getAttribute(context, obj, initS) : obj->getAttribute(context, initS);
    if (initM && initM != PROTO_NONE) {
        const proto::ProtoList* args = positionalParameters ? positionalParameters : (env ? env->getEmptyList() : context->newList());
        obj->call(context, nullptr, initS, obj, args, keywordParameters);
    }
    
    return obj;
}

static const proto::ProtoObject* py_frame_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* code = self->getAttribute(context, env->getFCodeString());
    std::string name = "<unknown>";
    std::string filename = "<unknown>";
    if (code) {
        const proto::ProtoObject* co_name = code->getAttribute(context, env->getNameString());
        if (co_name && co_name->isString(context)) co_name->asString(context)->toUTF8String(context, name);
        const proto::ProtoObject* co_filename = code->getAttribute(context, proto::ProtoString::fromUTF8String(context, "co_filename"));
        if (co_filename && co_filename->isString(context)) co_filename->asString(context)->toUTF8String(context, filename);
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "<frame at %p, file '%s', code %s>", 
             (void*)self, filename.c_str(), name.c_str());
    return context->fromUTF8String(buf);
}

static const proto::ProtoObject* py_type_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (get_env_diag()) {
        printf("DEBUG: py_type_call called self=%p\n", (void*)self);
    }
    return builtins::py_type(context, self, parentLink, positionalParameters, keywordParameters);
}

static const proto::ProtoObject* py_type_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* nameS = env ? env->getNameString() : getInternalString(context, "__name__");
    const proto::ProtoObject* name = self ? self->getAttribute(context, nameS) : nullptr;
    std::string nStr = "object";
    if (name) {
        if (name->isString(context)) {
            name->asString(context)->toUTF8String(context, nStr);
        }
    }
    std::string out = "<class '" + nStr + "'>";
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_mappingproxy_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* data = self->getAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"));
    std::string r = PythonEnvironment::reprObject(context, data ? data : PROTO_NONE);
    std::string out = "mappingproxy(" + r + ")";
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_mappingproxy_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!args || args->getSize(context) < 1) return nullptr;
    const proto::ProtoObject* key = args->getAt(context, 0);
    const proto::ProtoObject* data = self->getAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"));
    if (data) {
        // Fix for types: if the wrapped object is a Type, use attribute access (for __dict__).
        // Types in protoPython store members as attributes.
        const proto::ProtoObject* cls = data->getAttribute(context, env ? env->getClassString() : proto::ProtoString::fromUTF8String(context, "__class__"));
        bool isType = false;
        if (env && cls == env->getTypePrototype()) {
            isType = true;
        } else {
            const proto::ProtoObject* isPyCls = data->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__is_python_class__"));
            if (isPyCls && isPyCls != PROTO_NONE) isType = true;
            else {
                const proto::ProtoObject* mro = data->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__mro__"));
                if (mro && mro->asList(context)) isType = true;
            }
        }

        if (isType) {
             const proto::ProtoObject* res = data->getAttribute(context, key->asString(context));
             if (res) return res;
             // If not found, fall through to raise KeyError (or return nullptr if caller handles it?)
             // py_mappingproxy_getitem is called by OP_BINARY_SUBSCR which expects raised exception or valid return.
             // If we return nullptr, OP_BINARY_SUBSCR might raise TypeError (unsubscriptable) or KeyError?
             // Actually OP_BINARY_SUBSCR calling a dunder method usually expects the method to raise exception on failure.
             if (env) env->raiseKeyError(context, key);
             return nullptr;
        }

        return env ? env->getItem(data, key) : data->getAttribute(context, key->asString(context));
    }
    return nullptr;
}

static const proto::ProtoObject* py_type_get_dict(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!self || !env) return PROTO_NONE;
    
    if (get_env_diag()) printf("DEBUG: py_type_get_dict called for %p\n", (void*)self);

    // In ProtoCore, self IS already a dictionary-like object in many ways
    // But for CPython parity, we return a MappingProxy of 'self'
    proto::ProtoObject* proxy = const_cast<proto::ProtoObject*>(env->getMappingProxyPrototype()->newChild(context, true));
    proxy->setAttribute(context, env->getDataString(), self);
    return proxy;
}

static const proto::ProtoObject* py_object_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!self) return PROTO_NONE;
    uintptr_t ptrVal = reinterpret_cast<uintptr_t>(self);
    intptr_t hashVal = static_cast<intptr_t>(ptrVal >> 4);
    return context->fromLong(hashVal);
}


static const proto::ProtoObject* py_object_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (self->isInteger(context)) {
        return context->fromUTF8String(std::to_string(self->asLong(context)).c_str());
    }
    if (self->isDouble(context)) {
        return context->fromUTF8String(std::to_string(self->asDouble(context)).c_str());
    }
    if (self->isString(context)) {
        std::string s;
        self->asString(context)->toUTF8String(context, s);
        return context->fromUTF8String(("'" + s + "'").c_str());
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (self == PROTO_TRUE) return context->fromUTF8String("True");
    if (self == PROTO_FALSE) return context->fromUTF8String("False");
    if (self == PROTO_NONE || (env && self == env->getNonePrototype())) {
        return context->fromUTF8String("None");
    }

    // Basic <object at 0x...> repr
    const proto::ProtoObject* nameAttr = self->getAttribute(context, env ? env->getNameString() : getInternalString(context, "__name__"));
    std::string name = "object";
    if (nameAttr && nameAttr->isString(context)) {
        nameAttr->asString(context)->toUTF8String(context, name);
    } else {
        const proto::ProtoObject* cls = self->getAttribute(context, env ? env->getClassString() : getInternalString(context, "__class__"));
        if (cls) {
            const proto::ProtoObject* clsName = cls->getAttribute(context, env ? env->getNameString() : getInternalString(context, "__name__"));
            if (clsName && clsName->isString(context)) clsName->asString(context)->toUTF8String(context, name);
        }
    }

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "<%s object at %p>", name.c_str(), (void*)self);
    return context->fromUTF8String(buffer);
}

static const proto::ProtoObject* py_float_call(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) == 0) return ctx->fromDouble(0.0);
    const proto::ProtoObject* x = posArgs->getAt(ctx, 0);
    if (x->isInteger(ctx)) return ctx->fromDouble(static_cast<double>(x->asLong(ctx)));
    if (x->isDouble(ctx)) return x;
    if (x->isString(ctx)) {
        std::string s;
        x->asString(ctx)->toUTF8String(ctx, s);
        try {
            return ctx->fromDouble(std::stod(s));
        } catch (...) {
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (env) env->raiseValueError(ctx, ctx->fromUTF8String(("invalid literal for float(): " + s).c_str()));
            return PROTO_NONE;
        }
    }
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

static const proto::ProtoObject* py_float_hex(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (!self->isDouble(context)) return PROTO_NONE;
    double d = self->asDouble(context);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%a", d);
    return context->fromUTF8String(buf);
}

static const proto::ProtoObject* py_float_fromhex(
    proto::ProtoContext* context,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1 || !posArgs->getAt(context, 0)->isString(context)) return PROTO_NONE;
    std::string s;
    posArgs->getAt(context, 0)->asString(context)->toUTF8String(context, s);
    double d = 0.0;
    if (std::sscanf(s.c_str(), "%la", &d) != 1) return PROTO_NONE;
    return context->fromDouble(d);
}

static const proto::ProtoObject* py_int_call(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) == 0) return ctx->fromInteger(0);
    const proto::ProtoObject* x = posArgs->getAt(ctx, 0);
    if (x->isInteger(ctx)) return x;
    if (x->isDouble(ctx)) return ctx->fromInteger(static_cast<long long>(std::trunc(x->asDouble(ctx))));
    if (x->isString(ctx)) {
        std::string s;
        x->asString(ctx)->toUTF8String(ctx, s);
        try {
            return ctx->fromInteger(std::stoll(s, nullptr, 0));
        } catch (...) {
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (env) env->raiseValueError(ctx, ctx->fromUTF8String(("invalid literal for int() with base 0: " + s).c_str()));
            return nullptr;
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) env->raiseTypeError(ctx, "int() argument must be a string, a bytes-like object or a number");
    return nullptr;
}

static const proto::ProtoObject* py_none_type_call(
    proto::ProtoContext* context,
    const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    return env ? env->getNonePrototype() : PROTO_NONE;
}

static const proto::ProtoObject* py_ellipsis_type_call(
    proto::ProtoContext* context,
    const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    return env ? env->getEllipsisPrototype() : PROTO_NONE;
}

static const proto::ProtoObject* py_notimplemented_type_call(
    proto::ProtoContext* context,
    const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    return env ? env->getNotImplementedPrototype() : PROTO_NONE;
}

static const proto::ProtoObject* py_bool_call(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* obj = posArgs->getAt(ctx, 0);
    if (obj == PROTO_TRUE) return PROTO_TRUE;
    if (obj == PROTO_FALSE) return PROTO_FALSE;
    if (obj == PROTO_NONE) return PROTO_FALSE;
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
    const proto::ProtoObject* strM = self->getAttribute(context, getInternalString(context, "__str__"));
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
    if (self == PROTO_NONE) return context->fromUTF8String("None");
    PythonEnvironment* env = PythonEnvironment::fromContext(context);

    if (self == PROTO_NONE || (env && self == env->getNonePrototype())) {
        return context->fromUTF8String("None");
    }
    if (self->isString(context)) return self;
    if (self->isInteger(context)) return context->fromUTF8String(std::to_string(self->asLong(context)).c_str());
    if (self->isDouble(context)) return context->fromUTF8String(std::to_string(self->asDouble(context)).c_str());
    if (self == PROTO_TRUE) return context->fromUTF8String("True");
    if (self == PROTO_FALSE) return context->fromUTF8String("False");

    // Default str(obj) calls repr(obj)
    const proto::ProtoList* args = context->newList()->appendLast(context, self);
    return py_repr_call(context, nullptr, nullptr, args, nullptr);
}

static const proto::ProtoObject* py_str_call(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) == 0) return ctx->fromUTF8String("");
    const proto::ProtoObject* x = posArgs->getAt(ctx, 0);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* cls = env ? env->getType(ctx, x) : x->getAttribute(ctx, getInternalString(ctx, "__class__"));
    const proto::ProtoObject* strMethod = cls ? cls->getAttribute(ctx, env ? env->getStrString() : getInternalString(ctx, "__str__")) : nullptr;
    if (strMethod && strMethod->asMethod(ctx)) {
        return strMethod->asMethod(ctx)(ctx, x, nullptr, env ? env->getEmptyList() : ctx->newList(), nullptr);
    }
    return py_object_str(ctx, x, parentLink, nullptr, nullptr);
}

static const proto::ProtoObject* py_repr_call(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) == 0) return ctx->fromUTF8String("");
    const proto::ProtoObject* x = posArgs->getAt(ctx, 0);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* cls = env ? env->getType(ctx, x) : x->getAttribute(ctx, getInternalString(ctx, "__class__"));
    const proto::ProtoObject* reprMethod = cls ? cls->getAttribute(ctx, env ? env->getReprString() : getInternalString(ctx, "__repr__")) : nullptr;
    if (reprMethod && reprMethod->asMethod(ctx)) {
        return reprMethod->asMethod(ctx)(ctx, x, nullptr, env ? env->getEmptyList() : ctx->newList(), nullptr);
    }
    return py_object_repr(ctx, x, parentLink, nullptr, nullptr);
}

// --- List Methods ---

static const proto::ProtoObject* py_list_append(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return context->fromInteger(0);
    return context->fromInteger(data->asList(context)->getSize(context));
}

struct SliceBounds { bool isSlice; long long start, stop, step; };

static SliceBounds get_slice_bounds(proto::ProtoContext* context, const proto::ProtoObject* indexObj, long long size) {
    SliceBounds sb{false, 0, size, 1};
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* startName = env ? env->getStartString() : proto::ProtoString::fromUTF8String(context, "start");
    const proto::ProtoString* stopName = env ? env->getStopString() : proto::ProtoString::fromUTF8String(context, "stop");
    const proto::ProtoString* stepName = env ? env->getStepString() : proto::ProtoString::fromUTF8String(context, "step");
    
    // Check if it's a slice object by looking for these attributes if sliceType is not available or not used as parent
    bool isSliceInstance = env && env->getSliceType() && indexObj->isInstanceOf(context, env->getSliceType()) == PROTO_TRUE;
    
    const proto::ProtoObject* startObj = indexObj->getAttribute(context, startName);
    const proto::ProtoObject* stopObj = indexObj->getAttribute(context, stopName);
    const proto::ProtoObject* stepObj = indexObj->getAttribute(context, stepName);
    
    // An object is a slice if it's strictly a slice instance, or if it explicitly has one of the attributes natively.
    // getAttribute on primitive types like int returns PROTO_NONE, so we must be careful not to mistake it for "attribute exists and = None".
    bool hasSliceAttrs = indexObj->hasOwnAttribute(context, startName) == PROTO_TRUE || 
                         indexObj->hasOwnAttribute(context, stopName) == PROTO_TRUE || 
                         indexObj->hasOwnAttribute(context, stepName) == PROTO_TRUE;

    if (!isSliceInstance && !hasSliceAttrs) return sb;

    sb.isSlice = true;
    sb.start = (startObj && startObj != PROTO_NONE && startObj->isInteger(context)) ? startObj->asLong(context) : 0;
    sb.stop = (stopObj && stopObj != PROTO_NONE && stopObj->isInteger(context)) ? stopObj->asLong(context) : size;
    sb.step = (stepObj && stepObj != PROTO_NONE && stepObj->isInteger(context)) ? stepObj->asLong(context) : 1;
    
    if (sb.start < 0) sb.start += size;
    if (sb.stop < 0) sb.stop += size;
    if (sb.start < 0) sb.start = 0;
    if (sb.stop > size) sb.stop = size;
    
    // For positive step, start > stop means empty slice
    // For negative step, start < stop means empty slice
    if (sb.step > 0 && sb.start > sb.stop) sb.start = sb.stop;
    else if (sb.step < 0 && sb.start < sb.stop) sb.start = sb.stop;
    
    return sb;
}

static const proto::ProtoObject* py_list_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return nullptr; // Fallback to __class_getitem__ for types
    const proto::ProtoList* list = data->asList(context);
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* indexObj = positionalParameters->getAt(context, 0);
    long long size = static_cast<long long>(list->getSize(context));

    if (indexObj->isInteger(context)) {
        long long index = indexObj->asLong(context);
        if (index < 0) index += size;
        if (index < 0 || index >= size) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) env->raiseIndexError(context, "list index out of range");
            return PROTO_NONE;
        }
        return list->getAt(context, static_cast<int>(index));
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
    if (sb.isSlice) {
        proto::ProtoObject* newListObj = const_cast<proto::ProtoObject*>(context->newObject(true));
        const proto::ProtoList* newList = context->newList();
        PythonEnvironment* env = PythonEnvironment::fromContext(context);

        if (sb.step > 0) {
            for (long long i = sb.start; i < sb.stop; i += sb.step) {
                newList = newList->appendLast(context, list->getAt(context, static_cast<int>(i)));
            }
        } else if (sb.step < 0) {
            for (long long i = sb.start; i > sb.stop; i += sb.step) {
                newList = newList->appendLast(context, list->getAt(context, static_cast<int>(i)));
            }
        }

        newListObj->setAttribute(context, env ? env->getDataString() : getInternalString(context, "__data__"), newList->asObject(context));
        if (env && env->getListPrototype()) newListObj->addParent(context, env->getListPrototype());
        return newListObj;
    }

    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_setitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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

static const proto::ProtoObject* py_list_delitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = PythonEnvironment::fromContext(context)->getDataString();
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    int index = static_cast<int>(positionalParameters->getAt(context, 0)->asLong(context));
    unsigned long size = list->getSize(context);
    if (index < 0) index += static_cast<int>(size);
    if (index < 0 || static_cast<unsigned long>(index) >= size) return PROTO_NONE;
    const proto::ProtoList* newList = list->removeAt(context, index);
    self->setAttribute(context, dataName, newList->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = env ? env->getAttribute(context, self, iterProtoName) : self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;

    const proto::ProtoList* list = self->asList(context);
    const proto::ProtoObject* data = self;
    if (!list) {
        const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(context, "__data__");
        data = self->getAttribute(context, dataName);
        if (data) list = data->asList(context);
    }
    if (!list) return PROTO_NONE;

    const proto::ProtoListIterator* it = list->getIterator(context);

    const proto::ProtoObject* iterObj = iterProto->newChild(context, true);
    const proto::ProtoString* iterListName = proto::ProtoString::fromUTF8String(context, "__iter_list__");
    const proto::ProtoString* iterItName = proto::ProtoString::fromUTF8String(context, "__iter_it__");
    iterObj = iterObj->setAttribute(context, iterListName, data);
    iterObj = iterObj->setAttribute(context, iterItName, it->asObject(context));
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
    if (!it || !it->hasNext(context)) return nullptr;
    const proto::ProtoObject* value = it->next(context);
    const proto::ProtoListIterator* nextIt = it->advance(context);
    self = self->setAttribute(context, iterItName, nextIt->asObject(context));
    return value;
}

static const proto::ProtoObject* py_list_reversed(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* revProtoName = proto::ProtoString::fromUTF8String(context, "__reversed_prototype__");
    const proto::ProtoObject* revProto = self->getAttribute(context, revProtoName);
    if (!revProto) return PROTO_NONE;
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    if (idx < 0) return nullptr;
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoObject* otherData = other->getAttribute(context, dataName);
    const proto::ProtoList* list = (data && data != PROTO_NONE && data->asList(context)) ? data->asList(context) : self->asList(context);
    const proto::ProtoList* otherList = (otherData && otherData != PROTO_NONE && otherData->asList(context)) ? otherData->asList(context) : other->asList(context);
    if (!list || !otherList) return PROTO_FALSE;
    if (list == otherList) return PROTO_TRUE;
    unsigned long size = list->getSize(context);
    if (size != otherList->getSize(context)) return PROTO_FALSE;
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* a = list->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* b = otherList->getAt(context, static_cast<int>(i));
        if (a == b) continue;
        if (env && !env->objectsEqual(context, a, b)) return PROTO_FALSE;
        if (!env && a->compare(context, b) != 0) return PROTO_FALSE;
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context)) return nullptr; // Fallback for dict type

    if (positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
        unsigned long hash = key->getHash(context);
        const proto::ProtoSparseList* dict = data->asSparseList(context);
        if (dict->has(context, hash)) {
            const proto::ProtoObject* res = dict->getAt(context, hash);
            return res;
        }
        // PythonEnvironment* env = PythonEnvironment::fromContext(context); // This line was redundant
        if (std::getenv("PROTO_ENV_DIAG")) {
        }
        env->raiseKeyError(context, key);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_setitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(context, "__data__");
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
        const proto::ProtoString* keysName = env ? env->getKeysString() : getInternalString(context, "__keys__");
        const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
        const proto::ProtoList* keysList = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();
        keysList = keysList->appendLast(context, key);
        self->setAttribute(context, keysName, keysList->asObject(context));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_delitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env->getDataString();
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context)) return PROTO_NONE;
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
    unsigned long hash = key->getHash(context);
    
    if (!data->asSparseList(context)->has(context, hash)) {
        env->raiseKeyError(context, key);
        return PROTO_NONE;
    }

    const proto::ProtoSparseList* newSparse = data->asSparseList(context)->removeAt(context, hash);
    self->setAttribute(context, dataName, newSparse->asObject(context));

    const proto::ProtoString* keysName = env->getKeysString();
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    if (keysObj && keysObj->asList(context)) {
        const proto::ProtoList* list = keysObj->asList(context);
        for (int i = 0; i < list->getSize(context); ++i) {
            unsigned long kh = list->getAt(context, i)->getHash(context);
            if (kh == hash) {
                 list = list->removeAt(context, i);
                 break;
            }
        }
        self->setAttribute(context, keysName, list->asObject(context));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = env ? env->getAttribute(context, self, iterProtoName) : self->getAttribute(context, iterProtoName);
    if (!iterProto) {
        return PROTO_NONE;
    }

    const proto::ProtoList* keysList = nullptr;
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoObject* keysObj = env ? env->getAttribute(context, self, keysName) : self->getAttribute(context, keysName);

    if (keysObj && keysObj->asList(context)) {
        keysList = keysObj->asList(context);
    } else if (self->asSparseList(context)) {
        const proto::ProtoSparseList* sparse = self->asSparseList(context);
        const proto::ProtoList* keys = context->newList();
        const proto::ProtoSparseListIterator* it = sparse->getIterator(context);
        while (it && it->hasNext(context)) {
            size_t idx = it->nextKey(context);
            keys = keys->appendLast(context, context->fromInteger(static_cast<long long>(idx)));
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(context);
        }
        keysList = keys;
    } else {
        keysList = context->newList();
    }

    const proto::ProtoListIterator* it = keysList->getIterator(context);
    const proto::ProtoObject* iterObj = iterProto->newChild(context, true);
    const proto::ProtoString* iterItName = proto::ProtoString::fromUTF8String(context, "__iter_it__");
    iterObj = const_cast<proto::ProtoObject*>(iterObj->setAttribute(context, iterItName, it->asObject(context)));
    return iterObj;
}

static const proto::ProtoObject* py_dict_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = data ? data->asSparseList(context) : nullptr;
    
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
    
    if (dict) {
        unsigned long h = key->getHash(context);
        return dict->has(context, h) ? PROTO_TRUE : PROTO_FALSE;
    }
    
    // Fallback for objects used as dicts (like modules)
    if (key->isString(context)) {
        std::string k;
        key->asString(context)->toUTF8String(context, k);
        bool has = (self->hasAttribute(context, key->asString(context)) == PROTO_TRUE);
        if (has) return PROTO_TRUE;
    }
    
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_dict_eq(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* other = positionalParameters->getAt(context, 0);
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoObject* otherData = other->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context) || !otherData || !otherData->asSparseList(context)) return PROTO_FALSE;
    const proto::ProtoSparseList* dictA = data->asSparseList(context);
    const proto::ProtoSparseList* dictB = otherData->asSparseList(context);

    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
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
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env && !env->objectsEqual(context, vA, vB)) return PROTO_FALSE;
        if (!env && vA->compare(context, vB) != 0) return PROTO_FALSE;
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

std::string PythonEnvironment::reprObject(proto::ProtoContext* context, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) {
        return "None";
    }
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* reprS = env ? env->getReprString() : getInternalString(context, "__repr__");
    const proto::ProtoObject* reprMethod = env ? env->getAttribute(context, obj, reprS) : obj->getAttribute(context, reprS);
    if (std::getenv("PROTO_ENV_DIAG")) {
        const proto::ProtoObject* cls = env ? env->getAttribute(context, obj, env->getClassString()) : obj->getAttribute(context, getInternalString(context, "__class__"));
        std::string clsName = "<unknown>";
        if (cls) {
            const proto::ProtoObject* nameAttr = env ? env->getAttribute(context, cls, env->getNameString()) : cls->getAttribute(context, getInternalString(context, "__name__"));
            if (nameAttr && nameAttr->isString(context)) {
                nameAttr->asString(context)->toUTF8String(context, clsName);
            }
        }
        fprintf(stderr, "DEBUG: reprObject obj=%p class=%s\n", (void*)obj, clsName.c_str());
        fflush(stderr);
    }
    if (reprMethod && reprMethod->asMethod(context)) {
        const proto::ProtoObject* out = reprMethod->asMethod(context)(context, obj, nullptr, nullptr, nullptr);
        if (std::getenv("PROTO_ENV_DIAG")) {
            fprintf(stderr, "DEBUG: reprObject out=%p\n", (void*)out);
            fflush(stderr);
        }
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env && self == env->getListPrototype()) {
        return py_type_repr(context, self, parentLink, positionalParameters, keywordParameters);
    }
    const PythonEnvironment* env_local = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env_local ? env_local->getDataString() : getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data ? data->asList(context) : nullptr;
    if (!list && data) {
        if (data->asTuple(context)) list = data->asTuple(context)->asList(context);
    }
    if (!list) return context->fromUTF8String("[]");

    unsigned long size = list->getSize(context);
    unsigned long limit = 20;
    std::string out = "[";
    for (unsigned long i = 0; i < size && i < limit; ++i) {
        if (i > 0) out += ", ";
        const proto::ProtoObject* item = list->getAt(context, static_cast<int>(i));
        std::string r = PythonEnvironment::reprObject(context, item);
        if (std::getenv("PROTO_ENV_DIAG")) {
        }
        out += r;
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

static const proto::ProtoObject* py_tuple_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (get_env_diag()) {
        printf("DEBUG: py_tuple_repr called self=%p\n", (void*)self);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env && self == env->getTuplePrototype()) {
        if (get_env_diag()) printf("DEBUG: py_tuple_repr delegating to py_type_repr because self == tuplePrototype\n");
        return py_type_repr(context, self, parentLink, positionalParameters, keywordParameters);
    }
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoTuple* tup = (data && data->asTuple(context)) ? data->asTuple(context) : self->asTuple(context);
    const proto::ProtoList* list = tup ? tup->asList(context) : (data && data->asList(context) ? data->asList(context) : nullptr);
    if (!list) {
        if (get_env_diag()) printf("DEBUG: py_tuple_repr NO LIST FOUND -> returning ()\n");
        return context->fromUTF8String("()");
    }

    unsigned long size = list->getSize(context);
    unsigned long limit = 20;
    std::string out = "(";
    for (unsigned long i = 0; i < size && i < limit; ++i) {
        if (i > 0) out += ", ";
        out += PythonEnvironment::reprObject(context, list->getAt(context, static_cast<int>(i)));
    }
    if (size == 1) out += ",";
    if (size > limit) out += ", ...";
    out += ")";
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_tuple_eq(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* other = positionalParameters->getAt(context, 0);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoObject* otherData = other->getAttribute(context, dataName);
    const proto::ProtoTuple* tup = (data && data != PROTO_NONE && data->asTuple(context)) ? data->asTuple(context) : self->asTuple(context);
    const proto::ProtoTuple* otherTup = (otherData && otherData != PROTO_NONE && otherData->asTuple(context)) ? otherData->asTuple(context) : other->asTuple(context);
    const proto::ProtoList* list = tup ? tup->asList(context) : nullptr;
    const proto::ProtoList* otherList = otherTup ? otherTup->asList(context) : nullptr;
    if (!list || !otherList) return PROTO_FALSE;
    if (list == otherList) return PROTO_TRUE;
    unsigned long size = list->getSize(context);
    if (size != otherList->getSize(context)) return PROTO_FALSE;
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* a = list->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* b = otherList->getAt(context, static_cast<int>(i));
        if (a == b) continue;
        if (env && !env->objectsEqual(context, a, b)) return PROTO_FALSE;
        if (!env && a->compare(context, b) != 0) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_list_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_FALSE;
    return list->getSize(context) > 0 ? PROTO_TRUE : PROTO_FALSE;
}

static bool list_elem_equal(proto::ProtoContext* context, const proto::ProtoObject* elem, const proto::ProtoObject* value) {
    if (elem == value) return true;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) return env->objectsEqual(context, elem, value);
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

static const proto::ProtoObject* py_list_pop(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    
    unsigned long size = list->getSize(context);
    if (size == 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseIndexError(context, "pop from empty list");
        return PROTO_NONE;
    }

    int index = static_cast<int>(size - 1);
    if (positionalParameters && positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* idxObj = positionalParameters->getAt(context, 0);
        if (idxObj->isInteger(context)) {
            index = static_cast<int>(idxObj->asLong(context));
            if (index < 0) index += static_cast<int>(size);
        }
    }

    if (index < 0 || static_cast<unsigned long>(index) >= size) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseIndexError(context, "pop index out of range");
        return PROTO_NONE;
    }

    const proto::ProtoObject* item = list->getAt(context, index);
    const proto::ProtoList* newList = list->removeAt(context, index);
    self->setAttribute(context, dataName, newList->asObject(context));
    return item;
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

    const proto::ProtoList* otherList = otherObj->asList(context);
    const proto::ProtoTuple* otherTuple = nullptr;
    if (!otherList) {
        otherTuple = otherObj->asTuple(context);
        if (!otherTuple) {
            const proto::ProtoObject* otherData = otherObj->getAttribute(context, getInternalString(context, "__data__"));
            if (otherData) {
                otherList = otherData->asList(context);
                if (!otherList) otherTuple = otherData->asTuple(context);
            }
        }
    }
    
    // Get the current list from self
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);
    
    if (!otherList && !otherTuple) return PROTO_NONE;
    
    const proto::ProtoList* newList = list;
    if (otherList) {
        unsigned long otherSize = otherList->getSize(context);
        for (unsigned long i = 0; i < otherSize; ++i) {
            newList = newList->appendLast(context, otherList->getAt(context, static_cast<int>(i)));
        }
    } else if (otherTuple) {
        unsigned long otherSize = otherTuple->getSize(context);
        for (unsigned long i = 0; i < otherSize; ++i) {
            newList = newList->appendLast(context, otherTuple->getAt(context, static_cast<int>(i)));
        }
    }
    self->setAttribute(context, dataName, newList->asObject(context));
    return PROTO_NONE;
}

/** list.__iadd__(other): in-place extend with other, return self (for +=). */
static const proto::ProtoObject* py_list_iadd(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* otherObj = positionalParameters->getAt(context, 0);
    if (!otherObj) return PROTO_NONE;

    const proto::ProtoList* otherList = otherObj->asList(context);
    if (!otherList) {
        const proto::ProtoObject* otherData = otherObj->getAttribute(context, getInternalString(context, "__data__"));
        if (otherData) otherList = otherData->asList(context);
    }
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asList(context)) return PROTO_NONE;
    const proto::ProtoList* list = data->asList(context);
    if (!otherList) return PROTO_NONE;

    const proto::ProtoList* newList = list;
    unsigned long otherSize = otherList->getSize(context);
    for (unsigned long i = 0; i < otherSize; ++i) {
        newList = newList->appendLast(context, otherList->getAt(context, static_cast<int>(i)));
    }
    const_cast<proto::ProtoObject*>(self)->setAttribute(context, dataName, newList->asObject(context));
    return self;
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoList* list = data && data->asList(context) ? data->asList(context) : nullptr;
    if (!list) return PROTO_NONE;
    const proto::ProtoObject* value = positionalParameters->getAt(context, 0);
    unsigned long size = list->getSize(context);
    for (unsigned long i = 0; i < size; ++i) {
        const proto::ProtoObject* elem = list->getAt(context, static_cast<int>(i));
        if (list_elem_equal(context, elem, value)) {
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    self->setAttribute(context, dataName, context->newList()->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_list_copy(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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


static const proto::ProtoObject* py_list_index(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    if (!positionalParameters || positionalParameters->getSize(context) < 1) return context->fromInteger(0);
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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

static const proto::ProtoObject* py_none_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_type_class_getitem(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    // Return self (the class) to satisfy GenericAlias = type(list[int]) and Reader[int]
    return self;
}

static const proto::ProtoObject* py_union_type_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::get(context);
    const proto::ProtoObject* argsObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__args__"));
    const proto::ProtoList* args = argsObj ? argsObj->asList(context) : nullptr;
    if (!args || args->getSize(context) < 2) return context->fromUTF8String("UnionType");
    
    std::string s1 = PythonEnvironment::reprObject(context, args->getAt(context, 0));
    std::string s2 = PythonEnvironment::reprObject(context, args->getAt(context, 1));
    return context->fromUTF8String((s1 + " | " + s2).c_str());
}

static const proto::ProtoObject* py_type_or(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return self;
    const proto::ProtoObject* other = posArgs->getAt(context, 0);
    
    PythonEnvironment* env = PythonEnvironment::get(context);
    if (!env) return self;
    
    const proto::ProtoObject* unionProto = env->getUnionTypePrototype();
    if (!unionProto) return self;

    const proto::ProtoObject* unionObj = unionProto->newChild(context, true);
    // Minimal: store the types in a list attribute
    const proto::ProtoList* types = context->newList()->appendLast(context, self)->appendLast(context, other);
    unionObj = unionObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__args__"), types->asObject(context));
    
    return unionObj;
}

static const proto::ProtoObject* py_none_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return proto::ProtoString::fromUTF8String(context, "None")->asObject(context);
}

static const proto::ProtoObject* py_int_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self->asLong(context) != 0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_float_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self->asDouble(context) != 0.0 ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoString* bytes_data(proto::ProtoContext* context, const proto::ProtoObject* self);

static const proto::ProtoObject* py_bytes_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env && self == env->getBytesPrototype()) {
        return py_type_repr(context, self, parentLink, positionalParameters, keywordParameters);
    }
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return proto::ProtoString::fromUTF8String(context, "b''")->asObject(context);
    
    std::string str;
    s->toUTF8String(context, str);
    
    std::string res = "b'";
    for (unsigned char c : str) {
        if (c >= 32 && c < 127 && c != '\'' && c != '\\') {
            res += (char)c;
        } else if (c == '\'') {
            res += "\\'";
        } else if (c == '\\') {
            res += "\\\\";
        } else if (c == '\n') {
            res += "\\n";
        } else if (c == '\r') {
            res += "\\r";
        } else if (c == '\t') {
            res += "\\t";
        } else {
            char buf[5];
            snprintf(buf, sizeof(buf), "\\x%02x", c);
            res += buf;
        }
    }
    res += "'";
    return proto::ProtoString::fromUTF8String(context, res.c_str())->asObject(context);
}

static const proto::ProtoString* bytes_data(proto::ProtoContext* context, const proto::ProtoObject* self) {
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    if (!dataObj || !dataObj->isString(context) || !indexObj || !indexObj->isInteger(context)) return nullptr;
    const proto::ProtoString* s = dataObj->asString(context);
    int idx = static_cast<int>(indexObj->asLong(context));
    unsigned long size = s->getSize(context);
    if (static_cast<unsigned long>(idx) >= size) return nullptr;
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (positionalParameters->getSize(context) == 0) {
        const proto::ProtoObject* empty = self->newChild(context, true);
        empty->setAttribute(context, env ? env->getClassString() : getInternalString(context, "__class__"), self);
        empty->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(""));
        return empty;
    }
    const proto::ProtoObject* itObj = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* iterAttr = itObj->getAttribute(context, getInternalString(context, "__iter__"));
    if (!iterAttr || !iterAttr->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* empty = context->newList();
    const proto::ProtoObject* iterResult = iterAttr->asMethod(context)(context, itObj, nullptr, empty, nullptr);
    if (!iterResult) return PROTO_NONE;
    const proto::ProtoObject* nextAttr = iterResult->getAttribute(context, getInternalString(context, "__next__"));
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
    b->setAttribute(context, env ? env->getClassString() : getInternalString(context, "__class__"), self);
    b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(out.c_str()));
    return b;
}


static const proto::ProtoObject* py_set_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;

    proto::ProtoObject* instance = const_cast<proto::ProtoObject*>(self->newChild(context, true));
    instance->setAttribute(context, getInternalString(context, "__class__"), self);
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoSet* s = context->newSet();

    if (positionalParameters->getSize(context) >= 1) {
        const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoString* iterS = env ? env->getIterString() : getInternalString(context, "__iter__");
        const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
        if (iterM && iterM->asMethod(context)) {
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
            const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
            if (it && it != PROTO_NONE) {
                const proto::ProtoString* nextS = env ? env->getNextString() : getInternalString(context, "__next__");
                const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
                if (nextM && nextM->asMethod(context)) {
                    for (;;) {
                        const proto::ProtoObject* item = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
                        if (!item || item == PROTO_NONE || (env && item == env->getNonePrototype())) break;
                        s = s->add(context, item);
                    }
                }
            }
        }
    }

    instance->setAttribute(context, dataName, s->asObject(context));
    return instance;
}

static const proto::ProtoObject* py_list_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;

    proto::ProtoObject* instance = const_cast<proto::ProtoObject*>(self->newChild(context, true));
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(context, "__data__");
    proto::ProtoList* l = const_cast<proto::ProtoList*>(context->newList());

    if (positionalParameters && positionalParameters->getSize(context) >= 1) {
        const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
        const proto::ProtoList* otherL = iterable->asList(context);
        if (otherL) {
            unsigned long sz = otherL->getSize(context);
            for (unsigned long i = 0; i < sz; ++i) {
                l = const_cast<proto::ProtoList*>(l->appendLast(context, otherL->getAt(context, static_cast<int>(i))));
            }
        } else {
            const proto::ProtoTuple* otherT = iterable->asTuple(context);
            if (otherT) {
                unsigned long sz = otherT->getSize(context);
                for (unsigned long i = 0; i < sz; ++i) {
                    l = const_cast<proto::ProtoList*>(l->appendLast(context, otherT->getAt(context, static_cast<int>(i))));
                }
            } else {
                const proto::ProtoString* iterS = env ? env->getIterString() : getInternalString(context, "__iter__");
                const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
                if (get_env_diag()) {
                }
                if (iterM && iterM->asMethod(context)) {
                    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
                    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
                    if (it && it != PROTO_NONE) {
                        const proto::ProtoString* nextS = env ? env->getNextString() : getInternalString(context, "__next__");
                        const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
                        if (get_env_diag()) {
                        }
                        if (nextM && nextM->asMethod(context)) {
                            for (;;) {
                                const proto::ProtoObject* item = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
                                if (!item) {
                                    if (env && env->hasPendingException()) {
                                        if (env->isStopIteration(context, env->peekPendingException())) {
                                            env->clearPendingException();
                                        }
                                    }
                                    break;
                                }
                                l = const_cast<proto::ProtoList*>(l->appendLast(context, item));
                            }
                        }
                    }
                }
            }
        }
    }

    instance->setAttribute(context, env ? env->getClassString() : getInternalString(context, "__class__"), self);
    instance->setAttribute(context, dataName, l->asObject(context));
    return instance;
}

static const proto::ProtoObject* py_tuple_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (get_env_diag()) fprintf(stderr, "DEBUG: py_tuple_call entry self=%p repr=%s\n", (void*)self, PythonEnvironment::reprObject(context, self).c_str());
    proto::ProtoObject* instance = const_cast<proto::ProtoObject*>(self->newChild(context, true));
    if (get_env_diag()) fprintf(stderr, "DEBUG: py_tuple_call created instance=%p\n", (void*)instance);
    instance->setAttribute(context, env ? env->getClassString() : getInternalString(context, "__class__"), self);
    const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(context, "__data__");
    proto::ProtoList* l = const_cast<proto::ProtoList*>(context->newList());

    if (positionalParameters && positionalParameters->getSize(context) >= 1) {
        const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
        if (get_env_diag()) fprintf(stderr, "DEBUG: py_tuple_call iterable=%p repr=%s\n", (void*)iterable, PythonEnvironment::reprObject(context, iterable).c_str());
        const proto::ProtoList* otherL = iterable->asList(context);
        if (otherL) {
            unsigned long sz = otherL->getSize(context);
            if (get_env_diag()) fprintf(stderr, "DEBUG: py_tuple_call asList size=%lu\n", sz);
            for (unsigned long i = 0; i < sz; ++i) {
                l = const_cast<proto::ProtoList*>(l->appendLast(context, otherL->getAt(context, static_cast<int>(i))));
            }
        } else {
            const proto::ProtoTuple* otherT = iterable->asTuple(context);
            if (otherT) {
                unsigned long sz = otherT->getSize(context);
                if (get_env_diag()) fprintf(stderr, "DEBUG: py_tuple_call asTuple size=%lu\n", sz);
                for (unsigned long i = 0; i < sz; ++i) {
                    l = const_cast<proto::ProtoList*>(l->appendLast(context, otherT->getAt(context, static_cast<int>(i))));
                }
            } else {
                const proto::ProtoString* iterS = env ? env->getIterString() : getInternalString(context, "__iter__");
                const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
                if (iterM && iterM->asMethod(context)) {
                    const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
                    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
                    if (get_env_diag()) fprintf(stderr, "DEBUG: py_tuple_call iterator=%p repr=%s\n", (void*)it, it ? PythonEnvironment::reprObject(context, it).c_str() : "nullptr");
                    if (it && it != PROTO_NONE) {
                        const proto::ProtoString* nextS = env ? env->getNextString() : getInternalString(context, "__next__");
                        const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
                        if (nextM && nextM->asMethod(context)) {
                            for (;;) {
                                const proto::ProtoObject* item = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
                                if (!item) {
                                    if (env && env->hasPendingException()) {
                                        const proto::ProtoObject* exc = env->peekPendingException();
                                        if (env->isStopIteration(context, exc)) {
                                            env->clearPendingException();
                                        } else {
                                            return nullptr;
                                        }
                                    }
                                    break;
                                }
                                if (get_env_diag()) {
                                    std::string r = PythonEnvironment::reprObject(context, item);
                                    fprintf(stderr, "DEBUG: py_tuple_call item=%p repr=%s\n", (void*)item, r.c_str());
                                    fflush(stderr);
                                }
                                l = const_cast<proto::ProtoList*>(l->appendLast(context, item));
                            }
                        }
                    }
                }
            }
        }
    }

    const proto::ProtoTuple* t = context->newTupleFromList(l);
    instance->setAttribute(context, dataName, t->asObject(context));
    if (get_env_diag()) fprintf(stderr, "DEBUG: py_tuple_call returning instance=%p repr=%s\n", (void*)instance, PythonEnvironment::reprObject(context, instance).c_str());
    return instance;
}

static const proto::ProtoObject* py_dict_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;

    proto::ProtoObject* instance = const_cast<proto::ProtoObject*>(self->newChild(context, true));
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    instance->setAttribute(context, env ? env->getClassString() : getInternalString(context, "__class__"), self);
    const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(context, "__data__");
    const proto::ProtoString* keysName = env ? env->getKeysString() : getInternalString(context, "__keys__");
    proto::ProtoSparseList* d = const_cast<proto::ProtoSparseList*>(context->newSparseList());
    proto::ProtoList* keysList = const_cast<proto::ProtoList*>(context->newList());

    if (positionalParameters && positionalParameters->getSize(context) >= 1) {
        const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
        const proto::ProtoString* iterS = env ? env->getIterString() : getInternalString(context, "__iter__");
        const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
        if (iterM && iterM->asMethod(context)) {
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
            const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
            if (it && it != PROTO_NONE) {
                const proto::ProtoString* nextS = env ? env->getNextString() : getInternalString(context, "__next__");
                const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
                if (nextM && nextM->asMethod(context)) {
                    for (;;) {
                        const proto::ProtoObject* pairObj = nextM->asMethod(context)(context, it, nullptr, emptyL, nullptr);
                        if (get_env_diag()) {
                            fprintf(stderr, "DEBUG: py_dict_call next() returned %p\n", (void*)pairObj);
                            if (env && env->peekPendingException()) {
                                const proto::ProtoObject* exc = env->peekPendingException();
                                const proto::ProtoObject* cls = exc ? exc->getAttribute(context, env->getClassString()) : nullptr;
                                const proto::ProtoObject* clsName = cls ? cls->getAttribute(context, env->getNameString()) : nullptr;
                                std::string nStr;
                                if (clsName && clsName->isString(context)) clsName->asString(context)->toUTF8String(context, nStr);
                                fprintf(stderr, "DEBUG: py_dict_call next() threw exception %p (type: %s)\n", (void*)exc, nStr.c_str());
                            }
                        }
                        if (env && env->peekPendingException()) {
                            if (env->isStopIteration(context, env->peekPendingException())) {
                                env->clearPendingException();
                                break;
                            }
                            break;
                        }
                        if (!pairObj || pairObj == PROTO_NONE || (env && pairObj == env->getNonePrototype())) {
                            if (get_env_diag()) {
                                fprintf(stderr, "DEBUG: py_dict_call breaking because pairObj is None (or null)\n");
                            }
                            break;
                        }
                        
                        const proto::ProtoList* pairL = pairObj->asList(context);
                        const proto::ProtoTuple* pairT = pairObj->asTuple(context);
                        const proto::ProtoObject* k = nullptr;
                        const proto::ProtoObject* v = nullptr;
                        if (pairL && pairL->getSize(context) == 2) {
                            k = pairL->getAt(context, 0);
                            v = pairL->getAt(context, 1);
                        } else if (pairT && pairT->getSize(context) == 2) {
                            k = pairT->getAt(context, 0);
                            v = pairT->getAt(context, 1);
                        }
                        
                        if (k && v) {
                            unsigned long hash = k->getHash(context);
                            if (!d->has(context, hash)) {
                                keysList = const_cast<proto::ProtoList*>(keysList->appendLast(context, k));
                            }
                            d = const_cast<proto::ProtoSparseList*>(d->setAt(context, hash, v));
                        }
                    }
                }
            }
        }
    }
    
    const proto::ProtoTuple* kwNames = env ? env->getCurrentKwNames() : nullptr;
    if (kwNames && keywordParameters) {
        unsigned long sz = kwNames->getSize(context);
        for (unsigned long i = 0; i < sz; ++i) {
            const proto::ProtoObject* keyObj = kwNames->getAt(context, static_cast<int>(i));
            if (keyObj && keyObj->asString(context)) {
                const proto::ProtoString* ks = keyObj->asString(context);
                unsigned long hash = ks->getHash(context);
                const proto::ProtoObject* val = keywordParameters->getAt(context, hash);
                if (val) {
                    if (!d->has(context, hash)) {
                        keysList = const_cast<proto::ProtoList*>(keysList->appendLast(context, keyObj));
                    }
                    d = const_cast<proto::ProtoSparseList*>(d->setAt(context, hash, val));
                }
            }
        }
    }

    instance->setAttribute(context, dataName, d->asObject(context));
    instance->setAttribute(context, keysName, keysList->asObject(context));
    return instance;
}

static const proto::ProtoObject* py_str_maketrans(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)context; (void)self; (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    // return a dummy mapping or None if just satisfying import
    return context->newSparseList()->asObject(context);
}

static const proto::ProtoObject* py_set_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoObject* data = self->getAttribute(context, env ? env->getDataString() : getInternalString(context, "__data__"));
        s = data ? data->asSet(context) : nullptr;
    }
    if (!s) return context->fromInteger(0);
    return context->fromInteger(s->getSize(context));
}

static const proto::ProtoObject* py_set_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoObject* data = self->getAttribute(context, env ? env->getDataString() : getInternalString(context, "__data__"));
        s = data ? data->asSet(context) : nullptr;
    }
    if (!s || positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    return s->has(context, positionalParameters->getAt(context, 0));
}

static const proto::ProtoObject* py_set_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoObject* data = self->getAttribute(context, env ? env->getDataString() : getInternalString(context, "__data__"));
        s = data ? data->asSet(context) : nullptr;
    }
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSet* s = data && data->asSet(context) ? data->asSet(context) : context->newSet();
    const proto::ProtoSet* newSet = s->remove(context, positionalParameters->getAt(context, 0));
    self->setAttribute(context, dataName, newSet->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_set_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env && self == env->getSetPrototype()) {
        return py_type_repr(context, self, parentLink, positionalParameters, keywordParameters);
    }
    const proto::ProtoSet* s = self->asSet(context);
    if (!s || s->getSize(context) == 0) return context->fromUTF8String("set()");

    std::string out = "{";
    const proto::ProtoSetIterator* it = s->getIterator(context);
    bool first = true;
    while (it && it->hasNext(context)) {
        if (!first) out += ", ";
        first = false;
        const proto::ProtoObject* item = it->next(context);
        const proto::ProtoObject* repr = py_object_repr(context, item, nullptr, nullptr, nullptr);
        std::string s_item;
        repr->asString(context)->toUTF8String(context, s_item);
        out += s_item;
        it = it->advance(context);
    }
    out += "}";
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_set_discard(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) return PROTO_NONE;
    const proto::ProtoObject* elem = positionalParameters->getAt(context, 0);
    if (s->has(context, elem) != PROTO_TRUE) return PROTO_NONE;
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoSet* newSet = s->remove(context, elem);
    self->setAttribute(context, dataName, newSet->asObject(context));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_set_copy(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) return PROTO_NONE;
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    self->setAttribute(context, dataName, context->newSet()->asObject(context));
    return PROTO_NONE;
}

static void add_iterable_to_set(proto::ProtoContext* context, const proto::ProtoObject* iterable, proto::ProtoSet*& acc) {
    if (!iterable || iterable == PROTO_NONE) return;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : getInternalString(context, "__iter__");
    const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
    if (!iterM || !iterM->asMethod(context)) return;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it || it == PROTO_NONE) return;
    const proto::ProtoString* nextS = env ? env->getNextString() : getInternalString(context, "__next__");
    const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
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
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) return PROTO_NONE;
    proto::ProtoSet* acc = const_cast<proto::ProtoSet*>(context->newSet());
    const proto::ProtoSetIterator* it = s->getIterator(context);
    if (it) {
        while (it && it->hasNext(context)) {
            acc = const_cast<proto::ProtoSet*>(acc->add(context, it->next(context)));
            it = it->advance(context);
        }
    }
    for (unsigned long i = 0; i < posArgs->getSize(context); ++i) {
        add_iterable_to_set(context, posArgs->getAt(context, static_cast<int>(i)), acc);
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* parent = env->getSetPrototype();
    if (!parent) return PROTO_NONE;
    proto::ProtoObject* result = const_cast<proto::ProtoObject*>(parent->newChild(context, true));
    result->setAttribute(context, getInternalString(context, "__data__"), acc->asObject(context));
    return result;
}

static const proto::ProtoObject* py_set_intersection(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) return PROTO_NONE;
    proto::ProtoSet* acc = const_cast<proto::ProtoSet*>(context->newSet());
    if (posArgs->getSize(context) == 0) {
        const proto::ProtoSetIterator* it = s->getIterator(context);
        while (it && it->hasNext(context)) {
            acc = const_cast<proto::ProtoSet*>(acc->add(context, it->next(context)));
            it = it->advance(context);
        }
    } else {
        const proto::ProtoSetIterator* it = s->getIterator(context);
        while (it && it->hasNext(context)) {
            const proto::ProtoObject* val = it->next(context);
            bool in_all = true;
            for (unsigned long i = 0; i < posArgs->getSize(context) && in_all; ++i) {
                const proto::ProtoObject* other = posArgs->getAt(context, static_cast<int>(i));
                const proto::ProtoSet* os = other->asSet(context);
                if (os && os->has(context, val) == PROTO_TRUE) continue;
                const proto::ProtoObject* containsM = other->getAttribute(context, getInternalString(context, "__contains__"));
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
    result->setAttribute(context, getInternalString(context, "__data__"), acc->asObject(context));
    return result;
}

static const proto::ProtoObject* py_set_difference(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) return PROTO_NONE;
    proto::ProtoSet* acc = const_cast<proto::ProtoSet*>(context->newSet());
    const proto::ProtoSetIterator* it = s->getIterator(context);
    while (it && it->hasNext(context)) {
        acc = const_cast<proto::ProtoSet*>(acc->add(context, it->next(context)));
        it = it->advance(context);
    }
    for (unsigned long i = 0; i < posArgs->getSize(context); ++i) {
        const proto::ProtoObject* other = posArgs->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* iterM = other->getAttribute(context, getInternalString(context, "__iter__"));
        if (!iterM || !iterM->asMethod(context)) continue;
        const proto::ProtoObject* it2 = iterM->asMethod(context)(context, other, nullptr, context->newList(), nullptr);
        if (!it2) continue;
        const proto::ProtoObject* nextM = it2->getAttribute(context, getInternalString(context, "__next__"));
        if (!nextM || !nextM->asMethod(context)) continue;
        for (;;) {
            const proto::ProtoObject* val = nextM->asMethod(context)(context, it2, nullptr, context->newList(), nullptr);
            if (!val || val == PROTO_NONE) break;
            if (acc->has(context, val) == PROTO_TRUE) acc = const_cast<proto::ProtoSet*>(acc->remove(context, val));
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* parent = env->getSetPrototype();
    if (!parent) return PROTO_NONE;
    proto::ProtoObject* result = const_cast<proto::ProtoObject*>(parent->newChild(context, true));
    result->setAttribute(context, getInternalString(context, "__data__"), acc->asObject(context));
    return result;
}

static const proto::ProtoObject* py_set_symmetric_difference(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) return PROTO_NONE;
    proto::ProtoSet* acc = const_cast<proto::ProtoSet*>(context->newSet());
    const proto::ProtoSetIterator* it = s->getIterator(context);
    while (it && it->hasNext(context)) {
        const proto::ProtoObject* val = it->next(context);
        acc = const_cast<proto::ProtoSet*>(acc->add(context, val));
        it = it->advance(context);
    }
    for (unsigned long i = 0; i < posArgs->getSize(context); ++i) {
        const proto::ProtoObject* other = posArgs->getAt(context, static_cast<int>(i));
        const proto::ProtoObject* iterM = other->getAttribute(context, getInternalString(context, "__iter__"));
        if (!iterM || !iterM->asMethod(context)) continue;
        const proto::ProtoObject* it2 = iterM->asMethod(context)(context, other, nullptr, context->newList(), nullptr);
        if (!it2) continue;
        const proto::ProtoObject* nextM = it2->getAttribute(context, getInternalString(context, "__next__"));
        if (!nextM || !nextM->asMethod(context)) continue;
        for (;;) {
            const proto::ProtoObject* val = nextM->asMethod(context)(context, it2, nullptr, context->newList(), nullptr);
            if (!val || val == PROTO_NONE) break;
            if (acc->has(context, val) == PROTO_TRUE) acc = const_cast<proto::ProtoSet*>(acc->remove(context, val));
            else acc = const_cast<proto::ProtoSet*>(acc->add(context, val));
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* parent = env->getSetPrototype();
    if (!parent) return PROTO_NONE;
    proto::ProtoObject* result = const_cast<proto::ProtoObject*>(parent->newChild(context, true));
    result->setAttribute(context, getInternalString(context, "__data__"), acc->asObject(context));
    return result;
}

static const proto::ProtoObject* py_set_or(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(context) != 1) return PROTO_NONE;
    return py_set_union(context, self, parent, args, kwargs);
}

static const proto::ProtoObject* py_set_and(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(context) != 1) return PROTO_NONE;
    return py_set_intersection(context, self, parent, args, kwargs);
}

static const proto::ProtoObject* py_set_sub(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(context) != 1) return PROTO_NONE;
    return py_set_difference(context, self, parent, args, kwargs);
}

static const proto::ProtoObject* py_set_xor(
    proto::ProtoContext* context, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    if (args->getSize(context) != 1) return PROTO_NONE;
    return py_set_symmetric_difference(context, self, parent, args, kwargs);
}

static bool set_contains_all(proto::ProtoContext* context, const proto::ProtoObject* container, const proto::ProtoSet* elements) {
    const proto::ProtoSetIterator* it = elements->getIterator(context);
    while (it && it->hasNext(context)) {
        const proto::ProtoObject* val = it->next(context);
        const proto::ProtoSet* cs = container->asSet(context);
        if (cs && cs->has(context, val) == PROTO_TRUE) { it = it->advance(context); continue; }
        const proto::ProtoObject* containsM = container->getAttribute(context, getInternalString(context, "__contains__"));
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
    const proto::ProtoSet* s = self->asSet(context);
    if (!s || posArgs->getSize(context) < 1) return PROTO_NONE;
    return set_contains_all(context, posArgs->getAt(context, 0), s) ? PROTO_TRUE : PROTO_FALSE;
}

static bool iterable_contained_in(proto::ProtoContext* context, const proto::ProtoObject* container, const proto::ProtoObject* iterable) {
    const proto::ProtoObject* iterM = iterable->getAttribute(context, getInternalString(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return false;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it) return false;
    const proto::ProtoObject* nextM = it->getAttribute(context, getInternalString(context, "__next__"));
    if (!nextM || !nextM->asMethod(context)) return false;
    for (;;) {
        const proto::ProtoObject* val = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        const proto::ProtoSet* cs = container->asSet(context);
        if (cs && cs->has(context, val) == PROTO_TRUE) continue;
        const proto::ProtoObject* containsM = container->getAttribute(context, getInternalString(context, "__contains__"));
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
    const proto::ProtoSet* s = self->asSet(context);
    if (!s || s->getSize(context) == 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseKeyError(context, context->fromUTF8String("pop from empty set"));
        return PROTO_NONE;
    }
    const proto::ProtoSetIterator* it = s->getIterator(context);
    if (!it || !it->hasNext(context)) return PROTO_NONE;
    const proto::ProtoObject* value = it->next(context);
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoSet* s = self->asSet(context);
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
    if (!itObj || !itObj->asSetIterator(context)) return nullptr;
    const proto::ProtoSetIterator* it = itObj->asSetIterator(context);
    if (!it || !it->hasNext(context)) return nullptr;
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
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) return context->fromInteger(0);
    return context->fromInteger(s->getSize(context));
}

static const proto::ProtoObject* py_frozenset_contains(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s || positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* item = positionalParameters->getAt(context, 0);
    const proto::ProtoObject* res = s->has(context, item);
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: frozenset.__contains__ item=%p repr=%s res=%s\n", (void*)item, PythonEnvironment::reprObject(context, item).c_str(), (res == PROTO_TRUE ? "True" : "False"));
        fflush(stderr);
    }
    return res;
}

static const proto::ProtoObject* py_frozenset_bool(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = self->asSet(context);
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
    const proto::ProtoSet* s = self->asSet(context);
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
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) return context->fromInteger(0);
    unsigned long h = 0x345678UL;
    const proto::ProtoSetIterator* it = s->getIterator(context);
    while (it && it->hasNext(context)) {
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
    const proto::ProtoObject* iterAttr = itObj->getAttribute(context, getInternalString(context, "__iter__"));
    if (!iterAttr || !iterAttr->asMethod(context)) return PROTO_NONE;
    const proto::ProtoList* empty = context->newList();
    const proto::ProtoObject* iterResult = iterAttr->asMethod(context)(context, itObj, nullptr, empty, nullptr);
    if (!iterResult) return PROTO_NONE;
    const proto::ProtoObject* nextAttr = iterResult->getAttribute(context, getInternalString(context, "__next__"));
    if (!nextAttr || !nextAttr->asMethod(context)) return PROTO_NONE;

    const proto::ProtoSet* acc = context->newSet();
    const proto::ProtoList* nextArgs = context->newList();
    for (;;) {
        const proto::ProtoObject* item = nextAttr->asMethod(context)(context, iterResult, nullptr, nextArgs, nullptr);
        if (!item || item == PROTO_NONE) break;
        acc = acc->add(context, item);
    }
    const proto::ProtoObject* fs = self->newChild(context, true);
    fs->setAttribute(context, getInternalString(context, "__data__"), acc->asObject(context));
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

static const proto::ProtoObject* py_int_bit_count(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    unsigned long long u = static_cast<unsigned long long>(self->asLong(context));
    int count = 0;
    while (u) { count += static_cast<int>(u & 1); u >>= 1; }
    return context->fromInteger(count);
}

static const proto::ProtoObject* py_int_hash(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return context->fromInteger(self->asLong(context));
}

static const proto::ProtoString* bytes_from_object(proto::ProtoContext* context, const proto::ProtoObject* obj) {
    if (obj->isString(context)) return obj->asString(context);
    const proto::ProtoObject* data = obj->getAttribute(context, getInternalString(context, "__data__"));
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
    b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(out.c_str()));
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asTuple(context)) return nullptr; // Fallback to __class_getitem__
    const proto::ProtoTuple* tuple = data->asTuple(context);
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    long long index = positionalParameters->getAt(context, 0)->asLong(context);
    long long size = static_cast<long long>(tuple->getSize(context));
    if (index < 0) index += size;
    if (index < 0 || index >= size) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseIndexError(context, "tuple index out of range");
        return PROTO_NONE;
    }
    return tuple->getAt(context, static_cast<int>(index));
}

static const proto::ProtoObject* py_tuple_iter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = env ? env->getAttribute(context, self, iterProtoName) : self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;

    const proto::ProtoTuple* tuple = self->asTuple(context);
    const proto::ProtoObject* data = self;
    if (!tuple) {
        const proto::ProtoString* dataName = env ? env->getDataString() : getInternalString(context, "__data__");
        data = self->getAttribute(context, dataName);
        if (data) tuple = data->asTuple(context);
    }
    if (!tuple) return PROTO_NONE;

    const proto::ProtoObject* iterObj = iterProto->newChild(context, true);
    const proto::ProtoString* iterTupleName = proto::ProtoString::fromUTF8String(context, "__iter_tuple__");
    const proto::ProtoString* iterIndexName = proto::ProtoString::fromUTF8String(context, "__iter_index__");
    iterObj = const_cast<proto::ProtoObject*>(iterObj->setAttribute(context, iterTupleName, data));
    iterObj = const_cast<proto::ProtoObject*>(iterObj->setAttribute(context, iterIndexName, context->fromInteger(0)));
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
    if (static_cast<unsigned long>(index) >= size) return nullptr;

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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.c_str()));
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
    st->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.c_str()));
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
    b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.c_str()));
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
    } else if (sub->isString(context)) {
        sub->asString(context)->toUTF8String(context, needle);
    } else if (sub->getAttribute(context, getInternalString(context, "__data__"))) {
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
    } else if (sub->getAttribute(context, getInternalString(context, "__data__"))) {
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
    } else if (arg->isString(context)) {
        arg->asString(context)->toUTF8String(context, out);
    } else if (arg->getAttribute(context, getInternalString(context, "__data__"))) {
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
    } else if (sub->getAttribute(context, getInternalString(context, "__data__"))) {
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
    b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(out.c_str()));
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

static const proto::ProtoObject* py_bytes_isascii(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s) return PROTO_FALSE;
    std::string raw;
    s->toUTF8String(context, raw);
    for (unsigned char c : raw)
        if (c > 127) return PROTO_FALSE;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_bytes_removeprefix(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s || !posArgs || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string raw;
    s->toUTF8String(context, raw);
    std::string prefix;
    const proto::ProtoString* pre = bytes_data(context, posArgs->getAt(context, 0));
    if (pre) pre->toUTF8String(context, prefix);
    if (prefix.size() <= raw.size() && raw.compare(0, prefix.size(), prefix) == 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (!env) return PROTO_NONE;
        const proto::ProtoObject* bytesProto = env->getBytesPrototype();
        if (!bytesProto) return PROTO_NONE;
        proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
        b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.substr(prefix.size()).c_str()));
        return b;
    }
    return const_cast<proto::ProtoObject*>(self);
}

static const proto::ProtoObject* py_bytes_removesuffix(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* s = bytes_data(context, self);
    if (!s || !posArgs || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string raw;
    s->toUTF8String(context, raw);
    std::string suffix;
    const proto::ProtoString* suf = bytes_data(context, posArgs->getAt(context, 0));
    if (suf) suf->toUTF8String(context, suffix);
    if (suffix.size() <= raw.size() && raw.compare(raw.size() - suffix.size(), suffix.size(), suffix) == 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (!env) return PROTO_NONE;
        const proto::ProtoObject* bytesProto = env->getBytesPrototype();
        if (!bytesProto) return PROTO_NONE;
        proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
        b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.substr(0, raw.size() - suffix.size()).c_str()));
        return b;
    }
    return const_cast<proto::ProtoObject*>(self);
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
    if (posArgs && posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->getAttribute(context, getInternalString(context, "__data__"))) {
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
    b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.substr(start).c_str()));
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
    if (posArgs && posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->getAttribute(context, getInternalString(context, "__data__"))) {
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
    b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.substr(0, end).c_str()));
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
    if (posArgs && posArgs->getSize(context) >= 1 && posArgs->getAt(context, 0)->getAttribute(context, getInternalString(context, "__data__"))) {
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
    b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.substr(start, end - start).c_str()));
    return b;
}

static std::string bytes_sep_from_arg(proto::ProtoContext* context, const proto::ProtoObject* arg) {
    if (!arg || arg->isInteger(context)) {
        long long v = arg && arg->isInteger(context) ? arg->asLong(context) : 32;
        if (v < 0 || v > 255) return " ";
        return std::string(1, static_cast<char>(static_cast<unsigned char>(v)));
    }
    if (arg->getAttribute(context, getInternalString(context, "__data__"))) {
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
            b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.substr(start).c_str()));
            result = result->appendLast(context, b);
            break;
        }
        proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
        b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(raw.substr(start, pos - start).c_str()));
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
    const proto::ProtoObject* iterM = iterable->getAttribute(context, getInternalString(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* nextM = it->getAttribute(context, getInternalString(context, "__next__"));
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
        } else if (item->getAttribute(context, getInternalString(context, "__data__"))) {
            const proto::ProtoString* bs = bytes_data(context, item);
            if (bs) { std::string p; bs->toUTF8String(context, p); out += p; }
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* bytesProto = env->getBytesPrototype();
    if (!bytesProto) return PROTO_NONE;
    proto::ProtoObject* b = const_cast<proto::ProtoObject*>(bytesProto->newChild(context, true));
    b->setAttribute(context, getInternalString(context, "__data__"), context->fromUTF8String(out.c_str()));
    return b;
}

static const proto::ProtoString* str_from_self(proto::ProtoContext* context, const proto::ProtoObject* self) {
    if (self->isString(context)) return self->asString(context);
    const proto::ProtoObject* data = self->getAttribute(context, getInternalString(context, "__data__"));
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

static bool is_ascii_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static bool char_in_chars(unsigned char c, const std::string& ch) {
    if (ch.empty()) return is_ascii_whitespace(c);
    return ch.find(c) != std::string::npos;
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!subObj->isString(context)) {
        if (env) env->raiseTypeError(context, "substring must be str");
        return context->fromInteger(-1);
    }
    std::string needle;
    subObj->asString(context)->toUTF8String(context, needle);

    long long start = 0;
    long long end = static_cast<long long>(haystack.size());
    if (positionalParameters->getSize(context) >= 2) {
        const proto::ProtoObject* sObj = positionalParameters->getAt(context, 1);
        if (sObj->isInteger(context)) {
            start = sObj->asLong(context);
            if (start < 0) start += static_cast<long long>(haystack.size());
            if (start < 0) start = 0;
        }
    }
    if (positionalParameters->getSize(context) >= 3) {
        const proto::ProtoObject* eObj = positionalParameters->getAt(context, 2);
        if (eObj->isInteger(context)) {
            end = eObj->asLong(context);
            if (end < 0) end += static_cast<long long>(haystack.size());
            if (end > static_cast<long long>(haystack.size())) end = static_cast<long long>(haystack.size());
        }
    }

    if (start > static_cast<long long>(haystack.size())) return context->fromInteger(-1);
    if (end < start) return context->fromInteger(-1);

    std::string searchSpace = haystack.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
    size_t pos = searchSpace.find(needle);
    return context->fromInteger(pos == std::string::npos ? -1 : static_cast<long long>(pos + start));
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* startName = env ? env->getStartString() : proto::ProtoString::fromUTF8String(context, "start");
    const proto::ProtoString* stopName = env ? env->getStopString() : proto::ProtoString::fromUTF8String(context, "stop");
    const proto::ProtoString* stepName = env ? env->getStepString() : proto::ProtoString::fromUTF8String(context, "step");
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
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env && self == env->getSliceType()) {
        return py_type_repr(context, self, parentLink, positionalParameters, keywordParameters);
    }
    const proto::ProtoObject* start = self->getAttribute(context, env ? env->getStartString() : proto::ProtoString::fromUTF8String(context, "start"));
    const proto::ProtoObject* stop = self->getAttribute(context, env ? env->getStopString() : proto::ProtoString::fromUTF8String(context, "stop"));
    const proto::ProtoObject* step = self->getAttribute(context, env ? env->getStepString() : proto::ProtoString::fromUTF8String(context, "step"));
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
                const proto::ProtoObject* strM = obj->getAttribute(context, getInternalString(context, "__str__"));
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

    const proto::ProtoObject* sepObj = (posArgs && posArgs->getSize(context) >= 1) ? posArgs->getAt(context, 0) : nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    bool sepIsNone = !sepObj || (env && sepObj == env->getNonePrototype());

    std::string sep;
    if (!sepIsNone) {
        if (!sepObj->isString(context)) {
            if (env) env->raiseTypeError(context, "split arg 1 must be str or None");
            return PROTO_NONE;
        }
        sepObj->asString(context)->toUTF8String(context, sep);
        if (sep.empty()) {
            if (env) env->raiseValueError(context, context->fromUTF8String("empty separator"));
            return PROTO_NONE;
        }
    }

    long long maxsplit = -1;
    if (posArgs && posArgs->getSize(context) >= 2) {
        const proto::ProtoObject* msObj = posArgs->getAt(context, 1);
        if (msObj->isInteger(context)) maxsplit = msObj->asLong(context);
    }

    const proto::ProtoList* result = context->newList();
    if (sepIsNone) {
        // Whitespace split
        size_t start = 0;
        int count = 0;
        while (start < s.size() && (maxsplit < 0 || count < maxsplit)) {
            while (start < s.size() && is_ascii_whitespace(s[start])) start++;
            if (start >= s.size()) break;
            size_t end = start;
            while (end < s.size() && !is_ascii_whitespace(s[end])) end++;
            result = result->appendLast(context, context->fromUTF8String(s.substr(start, end - start).c_str()));
            start = end;
            count++;
        }
        if (maxsplit >= 0 && count >= maxsplit && start < s.size()) {
            while (start < s.size() && is_ascii_whitespace(s[start])) start++;
            if (start < s.size()) {
                result = result->appendLast(context, context->fromUTF8String(s.substr(start).c_str()));
            }
        }
    } else {
        // Separator split
        size_t start = 0;
        int count = 0;
        while (maxsplit < 0 || count < maxsplit) {
            size_t pos = s.find(sep, start);
            if (pos == std::string::npos) break;
            result = result->appendLast(context, context->fromUTF8String(s.substr(start, pos - start).c_str()));
            start = pos + sep.size();
            count++;
        }
        result = result->appendLast(context, context->fromUTF8String(s.substr(start).c_str()));
    }

    if (!env) return result->asObject(context);
    proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(env->getListPrototype()->newChild(context, true));
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    listObj->setAttribute(context, dataName, result->asObject(context));
    return listObj;
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



static const proto::ProtoObject* py_str_strip(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);
    std::string chars;
    if (posArgs && posArgs->getSize(context) >= 1) {
        const proto::ProtoObject* charsObj = posArgs->getAt(context, 0);
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (charsObj && !(env && charsObj == env->getNonePrototype())) {
            if (charsObj->isString(context)) charsObj->asString(context)->toUTF8String(context, chars);
            else if (env) {
                env->raiseTypeError(context, "strip arg must be str or None");
                return PROTO_NONE;
            }
        }
    }
    size_t start = 0;
    while (start < s.size() && char_in_chars(static_cast<unsigned char>(s[start]), chars)) start++;
    size_t end = s.size();
    while (end > start && char_in_chars(static_cast<unsigned char>(s[end - 1]), chars)) end--;
    return context->fromUTF8String(s.substr(start, end - start).c_str());
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
    if (!str || !posArgs || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);

    long long start = 0;
    long long end = static_cast<long long>(s.size());
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isInteger(context)) {
        start = posArgs->getAt(context, 1)->asLong(context);
        if (start < 0) start += static_cast<long long>(s.size());
        if (start < 0) start = 0;
    }
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context)) {
        end = posArgs->getAt(context, 2)->asLong(context);
        if (end < 0) end += static_cast<long long>(s.size());
        if (end > static_cast<long long>(s.size())) end = static_cast<long long>(s.size());
    }
    if (start >= static_cast<long long>(s.size()) || start >= end) return PROTO_FALSE;

    std::string sub = s.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
    const proto::ProtoObject* prefixObj = posArgs->getAt(context, 0);

    auto check_prefix = [&](const proto::ProtoObject* pObj) -> bool {
        if (!pObj->isString(context)) return false;
        std::string p;
        pObj->asString(context)->toUTF8String(context, p);
        if (p.size() > sub.size()) return false;
        return sub.compare(0, p.size(), p) == 0;
    };

    if (prefixObj->isString(context)) {
        return check_prefix(prefixObj) ? PROTO_TRUE : PROTO_FALSE;
    } else {
        const proto::ProtoTuple* t = prefixObj->asTuple(context);
        if (t) {
            unsigned long tSize = t->getSize(context);
            for (unsigned long i = 0; i < tSize; ++i) {
                if (check_prefix(t->getAt(context, static_cast<int>(i)))) return PROTO_TRUE;
            }
            return PROTO_FALSE;
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) env->raiseTypeError(context, "startswith arg must be str or tuple of str");
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_str_endswith(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || !posArgs || posArgs->getSize(context) < 1) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);

    long long start = 0;
    long long end = static_cast<long long>(s.size());
    if (posArgs->getSize(context) >= 2 && posArgs->getAt(context, 1)->isInteger(context)) {
        start = posArgs->getAt(context, 1)->asLong(context);
        if (start < 0) start += static_cast<long long>(s.size());
        if (start < 0) start = 0;
    }
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context)) {
        end = posArgs->getAt(context, 2)->asLong(context);
        if (end < 0) end += static_cast<long long>(s.size());
        if (end > static_cast<long long>(s.size())) end = static_cast<long long>(s.size());
    }
    if (start >= static_cast<long long>(s.size()) || start >= end) return PROTO_FALSE;

    std::string sub = s.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
    const proto::ProtoObject* suffixObj = posArgs->getAt(context, 0);

    auto check_suffix = [&](const proto::ProtoObject* pObj) -> bool {
        if (!pObj->isString(context)) return false;
        std::string p;
        pObj->asString(context)->toUTF8String(context, p);
        if (p.size() > sub.size()) return false;
        return sub.compare(sub.size() - p.size(), p.size(), p) == 0;
    };

    if (suffixObj->isString(context)) {
        return check_suffix(suffixObj) ? PROTO_TRUE : PROTO_FALSE;
    } else {
        const proto::ProtoTuple* t = suffixObj->asTuple(context);
        if (t) {
            unsigned long tSize = t->getSize(context);
            for (unsigned long i = 0; i < tSize; ++i) {
                if (check_suffix(t->getAt(context, static_cast<int>(i)))) return PROTO_TRUE;
            }
            return PROTO_FALSE;
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) env->raiseTypeError(context, "endswith arg must be str or tuple of str");
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_str_replace(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* str = str_from_self(context, self);
    if (!str || !posArgs || posArgs->getSize(context) < 2) return PROTO_NONE;
    std::string s;
    str->toUTF8String(context, s);

    const proto::ProtoObject* oldObj = posArgs->getAt(context, 0);
    const proto::ProtoObject* newObj = posArgs->getAt(context, 1);
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (!oldObj->isString(context) || !newObj->isString(context)) {
        if (env) env->raiseTypeError(context, "replace args 1 and 2 must be str");
        return PROTO_NONE;
    }

    std::string oldStr, newStr;
    oldObj->asString(context)->toUTF8String(context, oldStr);
    newObj->asString(context)->toUTF8String(context, newStr);

    int count = -1;
    if (posArgs->getSize(context) >= 3 && posArgs->getAt(context, 2)->isInteger(context)) {
        count = static_cast<int>(posArgs->getAt(context, 2)->asLong(context));
    }

    std::string result;
    if (oldStr.empty()) {
        // Python behavior for empty old string: insert newStr between every character
        int replaced = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            if (count >= 0 && replaced >= count) {
                result += s.substr(i);
                break;
            }
            result += newStr;
            result += s[i];
            replaced++;
        }
        if (count < 0 || replaced < count) result += newStr;
        return context->fromUTF8String(result.c_str());
    }

    size_t pos = 0;
    int replaced = 0;
    while (pos < s.size() && (count < 0 || replaced < count)) {
        size_t found = s.find(oldStr, pos);
        if (found == std::string::npos) {
            result += s.substr(pos);
            pos = s.size();
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
    if (!sep || !posArgs || posArgs->getSize(context) < 1) {
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_str_join invalid args sep=%p posArgs=%p size=%lu\n", (void*)sep, (void*)posArgs, posArgs ? posArgs->getSize(context) : 0);
        return nullptr;
    }
    std::string sepStr;
    sep->toUTF8String(context, sepStr);
    const proto::ProtoObject* iterable = posArgs->getAt(context, 0);
    const proto::ProtoString* iterS = getInternalString(context, "__iter__");
    const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
    if (!iterM || !iterM->asMethod(context)) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseTypeError(context, "join() arg must be an iterable");
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_str_join not iterable\n");
        return nullptr;
    }
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it || it == PROTO_NONE) {
        return context->fromUTF8String("");
    }
    const proto::ProtoString* nextS = getInternalString(context, "__next__");
    const proto::ProtoObject* nextM = it->getAttribute(context, nextS);
    if (!nextM || !nextM->asMethod(context)) {
        return context->fromUTF8String("");
    }
    auto nextFn = nextM->asMethod(context);
    std::string out;
    bool first = true;
    for (int i = 0; ; i++) {
        const proto::ProtoObject* item = nextFn(context, it, nullptr, context->newList(), nullptr);
        if (!item) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env && env->hasPendingException()) {
                if (env->isStopIteration(context, env->peekPendingException())) {
                    env->clearPendingException();
                    break;
                }
                if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_str_join exception\n");
                return nullptr;
            }
            if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_str_join unexpected stop (!item no exc)\n");
            break;
        }
        if (item == PROTO_NONE) break;
        const proto::ProtoString* partObj = str_from_self(context, item);
        if (!partObj) {
            if (std::getenv("PROTO_ENV_DIAG")) {
                fprintf(stderr, "DEBUG: py_str_join: non-string item at index %d, pointer=%p\n", i, (void*)item);
                fflush(stderr);
            }
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) env->raiseTypeError(context, "sequence item: expected str instance");
            if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: py_str_join not string instance\n");
            return nullptr;
        }
        if (!first) out += sepStr;
        first = false;
        std::string part;
        partObj->toUTF8String(context, part);
        out += part;
    }
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_dict_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env && self == env->getDictPrototype()) {
        return py_type_repr(context, self, parentLink, positionalParameters, keywordParameters);
    }
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();

    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
        out += PythonEnvironment::reprObject(context, key);
        out += ": ";
        out += PythonEnvironment::reprObject(context, value);
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
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
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();

    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
    const proto::ProtoList* keys = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();

    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoObject* res = items->asObject(context);
    return res;
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = data && data->asSparseList(context) ? data->asSparseList(context) : nullptr;
    if (!dict) return defaultVal;
    unsigned long hash = key->getHash(context);
    if (dict->has(context, hash)) {
        return dict->getAt(context, hash);
    }
    return defaultVal;
}

static const proto::ProtoObject* py_dict_update(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* other = positionalParameters->getAt(context, 0);
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoString* dataName = getInternalString(context, "__data__");

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

            // If it's a module, also set as attribute
            if (key->isString(context)) {
                PythonEnvironment* env = PythonEnvironment::fromContext(context);
                if (env) env->setAttribute(context, self, key->asString(context), value);
                else const_cast<proto::ProtoObject*>(self)->setAttribute(context, key->asString(context), value);
            }
        }
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (env) {
        env->setAttribute(context, self, keysName, keys->asObject(context));
        env->setAttribute(context, self, dataName, dict->asObject(context));
    } else {
        const_cast<proto::ProtoObject*>(self)->setAttribute(context, keysName, keys->asObject(context));
        const_cast<proto::ProtoObject*>(self)->setAttribute(context, dataName, dict->asObject(context));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_dict_clear(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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

    const proto::ProtoObject* iterM = iterable->getAttribute(context, getInternalString(context, "__iter__"));
    if (!iterM || !iterM->asMethod(context)) return PROTO_NONE;
    const proto::ProtoObject* itObj = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!itObj) return PROTO_NONE;

    const proto::ProtoObject* nextM = itObj->getAttribute(context, getInternalString(context, "__next__"));
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

    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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

static const proto::ProtoObject* py_dict_ior(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* other = posArgs->getAt(context, 0);
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
    proto::ProtoObject* mutSelf = const_cast<proto::ProtoObject*>(self);
    mutSelf->setAttribute(context, keysName, keys->asObject(context));
    mutSelf->setAttribute(context, dataName, dict->asObject(context));
    return self;
}

static const proto::ProtoObject* py_dict_iror(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return py_dict_ror(context, self, nullptr, posArgs, nullptr);
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
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoSparseList* dict = data && data->asSparseList(context) ? data->asSparseList(context) : nullptr;
    if (!dict) return PROTO_NONE;
    unsigned long hash = key->getHash(context);
    
    if (dict->has(context, hash)) {
        return dict->getAt(context, hash);
    }
    
    const proto::ProtoSparseList* newDict = dict->setAt(context, hash, defaultVal);
    self->setAttribute(context, dataName, newDict->asObject(context));

    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
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
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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
        unsigned long kh = k->getHash(context);
        if (kh != hash)
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
    const proto::ProtoString* keysName = getInternalString(context, "__keys__");
    const proto::ProtoString* dataName = getInternalString(context, "__data__");
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

/** thread_local member initialization */
thread_local PythonEnvironment* PythonEnvironment::s_threadEnv = nullptr;
thread_local proto::ProtoContext* PythonEnvironment::s_threadContext = nullptr;
thread_local int PythonEnvironment::s_recursionDepth = 0;
thread_local bool PythonEnvironment::s_inRecursionError = false;
thread_local const proto::ProtoObject* PythonEnvironment::s_currentFrame = nullptr;
std::thread::id PythonEnvironment::s_mainThreadId;
thread_local const proto::ProtoObject* PythonEnvironment::s_currentGlobals = nullptr;
thread_local const proto::ProtoObject* PythonEnvironment::s_currentCodeObject = nullptr;

/** Thread-local trace function and pending exception have been moved to py_thread. */
static thread_local const proto::ProtoObject* s_currentPyThread = nullptr;

/** Global dictionary holding all py_threads to root them in GC. Mutated lock-free via setAttribute. */
static const proto::ProtoObject* s_globalThreadRootsDict = nullptr;

/** Per-thread resolve cache; generation check makes invalidation lock-free. */
static thread_local std::unordered_map<std::string, const proto::ProtoObject*> s_threadResolveCache;
static thread_local uint64_t s_threadResolveCacheGeneration = 0;

static std::string getPyThreadIdStr() {
    std::stringstream ss;
    ss << "thread_" << std::this_thread::get_id();
    return ss.str();
}

static const proto::ProtoObject* getPyThread(proto::ProtoContext* ctx) {
    if (s_currentPyThread) return s_currentPyThread;
    s_currentPyThread = ctx->newObject(true); // mutable
    if (s_globalThreadRootsDict) {
        const proto::ProtoString* k = proto::ProtoString::fromUTF8String(ctx, getPyThreadIdStr().c_str());
        const_cast<proto::ProtoObject*>(s_globalThreadRootsDict)->setAttribute(ctx, k, s_currentPyThread);
    }
    return s_currentPyThread;
}

void PythonEnvironment::registerContext(proto::ProtoContext* ctx, PythonEnvironment* env) {
    if (std::getenv("PROTO_THREAD_DIAG")) {
    }
    s_threadEnv = env;
    s_threadContext = ctx;
}

void PythonEnvironment::unregisterContext(proto::ProtoContext* ctx) {
    if (std::getenv("PROTO_THREAD_DIAG")) {
    }
    s_threadEnv = nullptr;
    s_threadContext = nullptr;
}

void PythonEnvironment::setCurrentFrame(const proto::ProtoObject* frame) {
    s_currentFrame = frame;
}

const proto::ProtoObject* PythonEnvironment::getCurrentFrame() {
    return s_currentFrame;
}

void PythonEnvironment::setCurrentGlobals(const proto::ProtoObject* globals) {
    s_currentGlobals = globals;
}

PythonEnvironment* PythonEnvironment::getCurrentEnvironment() {
    return s_threadEnv;
}

const proto::ProtoObject* PythonEnvironment::getCurrentGlobals() {
    return s_currentGlobals;
}

void PythonEnvironment::setCurrentCodeObject(const proto::ProtoObject* code) {
    s_currentCodeObject = code;
}

const proto::ProtoObject* PythonEnvironment::getCurrentCodeObject() {
    return s_currentCodeObject;
}

PythonEnvironment* PythonEnvironment::fromContext(proto::ProtoContext* ctx) {
    if (!s_threadEnv && std::getenv("PROTO_THREAD_DIAG")) {
        std::cerr << "[proto-thread] fromContext: s_threadEnv is NULL for context " << ctx << "\n" << std::flush;
    }
    return s_threadEnv;
}

void PythonEnvironment::setTraceFunction(const proto::ProtoObject* func) {
    if (!s_threadContext) return;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_trace_func");
    const_cast<proto::ProtoObject*>(getPyThread(s_threadContext))->setAttribute(s_threadContext, key, func ? func : PROTO_NONE);
}

const proto::ProtoObject* PythonEnvironment::getTraceFunction() const {
    if (!s_threadContext) return nullptr;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_trace_func");
    const proto::ProtoObject* func = getPyThread(s_threadContext)->getAttribute(s_threadContext, key);
    return func == PROTO_NONE ? nullptr : func;
}

void PythonEnvironment::setPendingException(const proto::ProtoObject* exc) {
    if (!s_threadContext) return;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_pending_exc");
    const_cast<proto::ProtoObject*>(getPyThread(s_threadContext))->setAttribute(s_threadContext, key, exc ? exc : PROTO_NONE);
}

const proto::ProtoObject* PythonEnvironment::takePendingException() {
    if (!s_threadContext) return nullptr;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_pending_exc");
    const proto::ProtoObject* e = getPyThread(s_threadContext)->getAttribute(s_threadContext, key);
    if (e == PROTO_NONE) e = nullptr;
    const_cast<proto::ProtoObject*>(getPyThread(s_threadContext))->setAttribute(s_threadContext, key, PROTO_NONE);
    return e;
}

bool PythonEnvironment::hasPendingException() const {
    if (!s_threadContext) return false;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_pending_exc");
    const proto::ProtoObject* e = getPyThread(s_threadContext)->getAttribute(s_threadContext, key);
    return e && e != PROTO_NONE;
}

const proto::ProtoObject* PythonEnvironment::peekPendingException() const {
    if (!s_threadContext) return nullptr;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_pending_exc");
    const proto::ProtoObject* e = getPyThread(s_threadContext)->getAttribute(s_threadContext, key);
    return e == PROTO_NONE ? nullptr : e;
}

void PythonEnvironment::clearPendingException() {
    if (!s_threadContext) return;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_pending_exc");
    const_cast<proto::ProtoObject*>(getPyThread(s_threadContext))->setAttribute(s_threadContext, key, PROTO_NONE);
}

void PythonEnvironment::pushActiveException(const proto::ProtoObject* exc) {
    if (!s_threadContext) return;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_active_excs");
    const proto::ProtoObject* listObj = getPyThread(s_threadContext)->getAttribute(s_threadContext, key);
    const proto::ProtoList* l = (listObj && listObj != PROTO_NONE && listObj->asList(s_threadContext)) ? listObj->asList(s_threadContext) : s_threadContext->newList();
    l = l->appendLast(s_threadContext, exc ? exc : PROTO_NONE);
    const_cast<proto::ProtoObject*>(getPyThread(s_threadContext))->setAttribute(s_threadContext, key, l->asObject(s_threadContext));
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG EXCEPTION: pushed active exception %p. New size = %lu\n", (void*)exc, l->getSize(s_threadContext));
        fflush(stderr);
    }
}

void PythonEnvironment::popActiveException() {
    if (!s_threadContext) return;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_active_excs");
    const proto::ProtoObject* listObj = getPyThread(s_threadContext)->getAttribute(s_threadContext, key);
    if (listObj && listObj != PROTO_NONE && listObj->asList(s_threadContext)) {
        const proto::ProtoList* l = listObj->asList(s_threadContext);
        size_t size = l->getSize(s_threadContext);
        if (size > 0) {
            l = l->removeAt(s_threadContext, size - 1);
            const_cast<proto::ProtoObject*>(getPyThread(s_threadContext))->setAttribute(s_threadContext, key, l->asObject(s_threadContext));
            if (std::getenv("PROTO_ENV_DIAG")) {
                fprintf(stderr, "DEBUG EXCEPTION: popped active exception. New size = %lu\n", size - 1);
                fflush(stderr);
            }
        }
    }
}

const proto::ProtoObject* PythonEnvironment::getActiveException() {
    if (!s_threadContext) return nullptr;
    const proto::ProtoString* key = proto::ProtoString::fromUTF8String(s_threadContext, "_active_excs");
    const proto::ProtoObject* listObj = getPyThread(s_threadContext)->getAttribute(s_threadContext, key);
    if (listObj && listObj != PROTO_NONE && listObj->asList(s_threadContext)) {
        const proto::ProtoList* l = listObj->asList(s_threadContext);
        size_t size = l->getSize(s_threadContext);
        if (size > 0) {
            const proto::ProtoObject* e = l->getAt(s_threadContext, size - 1);
            if (std::getenv("PROTO_ENV_DIAG")) {
                fprintf(stderr, "DEBUG EXCEPTION: getActiveException() returning %p, list size=%lu\n", (void*)e, size);
                fflush(stderr);
            }
            return e == PROTO_NONE ? nullptr : e;
        }
    }
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG EXCEPTION: getActiveException() returning nullptr. listObj=%p\n", (void*)listObj);
        fflush(stderr);
    }
    return nullptr;
}

bool PythonEnvironment::isStopIteration(proto::ProtoContext* ctx, const proto::ProtoObject* exc) const {
    if (std::getenv("PROTO_ENV_DIAG")) {
    }
    if (!exc || !stopIterationType) return false;
    bool res = false;
    if (exc == stopIterationType) res = true;
    else if (exc->getPrototype(ctx) == stopIterationType) res = true;
    else {
        res = (exc->isInstanceOf(ctx, stopIterationType) == PROTO_TRUE);
    }
    
    if (std::getenv("PROTO_ENV_DIAG")) {
    }
    return res;
}

const proto::ProtoObject* PythonEnvironment::getStopIterationValue(proto::ProtoContext* ctx, const proto::ProtoObject* exc) const {
    if (std::getenv("PROTO_ENV_DIAG")) {
    }
    if (!exc) return PROTO_NONE;
    
    const proto::ProtoString* valS = proto::ProtoString::fromUTF8String(ctx, "value");
    if (exc->hasAttribute(ctx, valS) == PROTO_TRUE) {
        const proto::ProtoObject* val = exc->getAttribute(ctx, valS);
        if (std::getenv("PROTO_ENV_DIAG")) {
        }
        if (val && val != PROTO_NONE) return val;
    }
    
    const proto::ProtoString* argsS = proto::ProtoString::fromUTF8String(ctx, "args");
    if (exc->hasAttribute(ctx, argsS) == PROTO_TRUE) {
        const proto::ProtoObject* argsObj = exc->getAttribute(ctx, argsS);
        if (argsObj) {
            if (argsObj->asList(ctx)) {
                const proto::ProtoList* args = argsObj->asList(ctx);
                if (args->getSize(ctx) > 0) return args->getAt(ctx, 0);
            } else if (argsObj->isTuple(ctx)) {
                const proto::ProtoTuple* args = argsObj->asTuple(ctx);
                if (args->getSize(ctx) > 0) return args->getAt(ctx, 0);
            }
        }
    }
    
    return PROTO_NONE;
}

proto::ProtoSpace* PythonEnvironment::getProcessSpace() {
    static proto::ProtoSpace s_processSpace;
    return &s_processSpace;
}

/** Singleton enforcement: L-Shape mandates one ProtoSpace per process. Log if multiple PythonEnvironment instances exist. */
static std::atomic<int> s_pythonEnvInstanceCount{0};

PythonEnvironment::PythonEnvironment(const std::string& stdLibPath, const std::vector<std::string>& searchPaths,
                                     const std::vector<std::string>& argv) : space_(getProcessSpace()), rootContext_(new proto::ProtoContext(space_)), argv_(argv), stdLibPath_(stdLibPath) {
    int prev = s_pythonEnvInstanceCount.fetch_add(1, std::memory_order_relaxed);
    // Multiple instances check removed for silence
    s_mainThreadId = std::this_thread::get_id();
    registerContext(rootContext_, this);
    initializeRootObjects(stdLibPath, searchPaths);
}

PythonEnvironment::~PythonEnvironment() {
    // Unregister roots from ProtoSpace to prevent dangling pointers in GC
    if (space_) {
        auto& roots = space_->moduleRoots;
        
        auto remove_if_match = [&](const proto::ProtoObject* obj) {
            if (!obj) return;
            roots.erase(std::remove(roots.begin(), roots.end(), obj), roots.end());
        };

        remove_if_match(objectPrototype);
        remove_if_match(typePrototype);
        remove_if_match(intPrototype);
        remove_if_match(strPrototype);
        remove_if_match(listPrototype);
        remove_if_match(dictPrototype);
        remove_if_match(tuplePrototype);
        remove_if_match(setPrototype);
        remove_if_match(bytesPrototype);
        remove_if_match(nonePrototype);
        remove_if_match(sliceType);
        remove_if_match(frozensetPrototype);
        remove_if_match(floatPrototype);
        remove_if_match(boolPrototype);
        remove_if_match(sysModule);
        remove_if_match(builtinsModule);
        
        remove_if_match(keyErrorType);
        remove_if_match(valueErrorType);
        remove_if_match(nameErrorType);
        remove_if_match(attributeErrorType);
        remove_if_match(syntaxErrorType);
        remove_if_match(typeErrorType);
        remove_if_match(importErrorType);
        remove_if_match(keyboardInterruptType);
        remove_if_match(systemExitType);
        remove_if_match(assertionErrorType);
        remove_if_match(recursionErrorType);
        remove_if_match(stopIterationType);
        remove_if_match(zeroDivisionErrorType);
        remove_if_match(indexErrorType);

        remove_if_match((iterString)->asObject(rootContext_));
        remove_if_match((nextString)->asObject(rootContext_));
        remove_if_match((emptyList)->asObject(rootContext_));
        remove_if_match((rangeCurString)->asObject(rootContext_));
        remove_if_match((rangeStopString)->asObject(rootContext_));
        remove_if_match((rangeStepString)->asObject(rootContext_));
        remove_if_match((mapFuncString)->asObject(rootContext_));
        remove_if_match((mapIterString)->asObject(rootContext_));
        remove_if_match((enumIterString)->asObject(rootContext_));
        remove_if_match((enumIdxString)->asObject(rootContext_));
        remove_if_match((revObjString)->asObject(rootContext_));
        remove_if_match((revIdxString)->asObject(rootContext_));
        remove_if_match((zipItersString)->asObject(rootContext_));
        remove_if_match((filterFuncString)->asObject(rootContext_));
        remove_if_match((filterIterString)->asObject(rootContext_));
        remove_if_match((classString)->asObject(rootContext_));
        remove_if_match((nameString)->asObject(rootContext_));
        remove_if_match((callString)->asObject(rootContext_));
        remove_if_match((getItemString)->asObject(rootContext_));
        remove_if_match((lenString)->asObject(rootContext_));
        remove_if_match((boolString)->asObject(rootContext_));
        remove_if_match((intString)->asObject(rootContext_));
        remove_if_match((floatString)->asObject(rootContext_));
        remove_if_match((strString)->asObject(rootContext_));
        remove_if_match((reprString)->asObject(rootContext_));
        remove_if_match((hashString)->asObject(rootContext_));
        remove_if_match((powString)->asObject(rootContext_));
        remove_if_match((containsString)->asObject(rootContext_));
        remove_if_match((addString)->asObject(rootContext_));
        remove_if_match((formatString)->asObject(rootContext_));
        remove_if_match((dictString)->asObject(rootContext_));
        remove_if_match((docString)->asObject(rootContext_));
        remove_if_match((matMulString)->asObject(rootContext_));
        remove_if_match((imatmulString)->asObject(rootContext_));
        remove_if_match((rmatmulString)->asObject(rootContext_));
        remove_if_match((reversedString)->asObject(rootContext_));
        remove_if_match((enumProtoS)->asObject(rootContext_));
        remove_if_match((revProtoS)->asObject(rootContext_));
        remove_if_match((zipProtoS)->asObject(rootContext_));
        remove_if_match((filterProtoS)->asObject(rootContext_));
        remove_if_match((mapProtoS)->asObject(rootContext_));
        remove_if_match((rangeProtoS)->asObject(rootContext_));
        remove_if_match((boolTypeS)->asObject(rootContext_));
        remove_if_match((filterBoolS)->asObject(rootContext_));
        
        remove_if_match((__code__)->asObject(rootContext_));
        remove_if_match((__globals__)->asObject(rootContext_));
        remove_if_match((co_varnames)->asObject(rootContext_));
        remove_if_match((co_nparams)->asObject(rootContext_));
        remove_if_match((co_automatic_count)->asObject(rootContext_));
        remove_if_match((co_is_generator)->asObject(rootContext_));
        remove_if_match((co_flags)->asObject(rootContext_));
        remove_if_match((co_consts)->asObject(rootContext_));
        remove_if_match((co_names)->asObject(rootContext_));
        remove_if_match((co_code)->asObject(rootContext_));
        remove_if_match((sendString)->asObject(rootContext_));
        remove_if_match((throwString)->asObject(rootContext_));
        remove_if_match((closeString)->asObject(rootContext_));
        remove_if_match((f_back)->asObject(rootContext_));
        remove_if_match((f_code)->asObject(rootContext_));
        remove_if_match((f_globals)->asObject(rootContext_));
        remove_if_match((f_locals)->asObject(rootContext_));
        remove_if_match((__closure__)->asObject(rootContext_));
        remove_if_match((gi_code)->asObject(rootContext_));
        remove_if_match((gi_frame)->asObject(rootContext_));
        remove_if_match((gi_running)->asObject(rootContext_));
        remove_if_match((gi_yieldfrom)->asObject(rootContext_));
        remove_if_match((gi_pc)->asObject(rootContext_));
        remove_if_match((gi_stack)->asObject(rootContext_));
        remove_if_match((gi_locals)->asObject(rootContext_));
        remove_if_match((py_eq_s)->asObject(rootContext_));
        remove_if_match((py_ne_s)->asObject(rootContext_));
        remove_if_match((py_lt_s)->asObject(rootContext_));
        remove_if_match((py_le_s)->asObject(rootContext_));
        remove_if_match((py_gt_s)->asObject(rootContext_));
        remove_if_match((py_ge_s)->asObject(rootContext_));
        remove_if_match((getDunderString)->asObject(rootContext_));
        remove_if_match((setDunderString)->asObject(rootContext_));
        remove_if_match((delDunderString)->asObject(rootContext_));
        
        remove_if_match((__code__)->asObject(rootContext_));
        remove_if_match((__globals__)->asObject(rootContext_));
        remove_if_match((co_varnames)->asObject(rootContext_));
        remove_if_match((co_nparams)->asObject(rootContext_));
        remove_if_match((co_automatic_count)->asObject(rootContext_));
        remove_if_match((co_is_generator)->asObject(rootContext_));
        remove_if_match((co_flags)->asObject(rootContext_));
        remove_if_match((__iadd__)->asObject(rootContext_));
        remove_if_match((__isub__)->asObject(rootContext_));
        remove_if_match((__imul__)->asObject(rootContext_));
        remove_if_match((__itruediv__)->asObject(rootContext_));
        remove_if_match((__ifloordiv__)->asObject(rootContext_));
        remove_if_match((__imod__)->asObject(rootContext_));
        remove_if_match((__ipow__)->asObject(rootContext_));
        remove_if_match((__ilshift__)->asObject(rootContext_));
        remove_if_match((__irshift__)->asObject(rootContext_));
        remove_if_match((__iand__)->asObject(rootContext_));
        remove_if_match((__ior__)->asObject(rootContext_));
        remove_if_match((__ixor__)->asObject(rootContext_));
        
        remove_if_match((__and__)->asObject(rootContext_));
        remove_if_match((__rand__)->asObject(rootContext_));
        remove_if_match((__or__)->asObject(rootContext_));
        remove_if_match((__ror__)->asObject(rootContext_));
        remove_if_match((__xor__)->asObject(rootContext_));
        remove_if_match((__rxor__)->asObject(rootContext_));
        
        remove_if_match((__invert__)->asObject(rootContext_));
        remove_if_match((__pos__)->asObject(rootContext_));
        
        remove_if_match((setItemString)->asObject(rootContext_));
        remove_if_match((delItemString)->asObject(rootContext_));
        remove_if_match((dataString)->asObject(rootContext_));
        remove_if_match((keysString)->asObject(rootContext_));
        
        remove_if_match((startString)->asObject(rootContext_));
        remove_if_match((stopString)->asObject(rootContext_));
        remove_if_match((stepString)->asObject(rootContext_));
        
        remove_if_match((ioModuleString)->asObject(rootContext_));
        remove_if_match((openString)->asObject(rootContext_));

        remove_if_match(zeroInteger);
        remove_if_match(oneInteger);
        
        remove_if_match((listS)->asObject(rootContext_));
        remove_if_match((dictS)->asObject(rootContext_));
        remove_if_match((tupleS)->asObject(rootContext_));
        remove_if_match((setS)->asObject(rootContext_));
        remove_if_match((intS)->asObject(rootContext_));
        remove_if_match((floatS)->asObject(rootContext_));
        remove_if_match((strS)->asObject(rootContext_));
        remove_if_match((boolS)->asObject(rootContext_));
        remove_if_match((objectS)->asObject(rootContext_));
        remove_if_match((typeS)->asObject(rootContext_));
        remove_if_match((dictString)->asObject(rootContext_));
    }

    unregisterContext(rootContext_);
    delete rootContext_;
    s_pythonEnvInstanceCount.fetch_sub(1, std::memory_order_relaxed);
}

void PythonEnvironment::raiseKeyError(proto::ProtoContext* ctx, const proto::ProtoObject* key) {
    if (std::getenv("PROTO_ENV_DIAG")) {
    }
    if (!keyErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
    const proto::ProtoObject* exc = keyErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), keyErrorType, args, nullptr);
    if (!exc || exc == PROTO_NONE) {
        // Fallback exception creation
        exc = ctx->newObject(true);
        exc = exc->addParent(ctx, keyErrorType);
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "args"), args->asObject(ctx));
    }
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseValueError(proto::ProtoContext* ctx, const proto::ProtoObject* msg) {
    if (!valueErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, msg);
    const proto::ProtoObject* exc = valueErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), valueErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

static int levenshtein_distance(const std::string& s1, const std::string& s2) {
    const size_t len1 = s1.size(), len2 = s2.size();
    if (len1 == 0) return (int)len2;
    if (len2 == 0) return (int)len1;
    std::vector<int> col(len2 + 1), prevCol(len2 + 1);
    for (int i = 0; i <= (int)len2; i++) prevCol[i] = i;
    for (int i = 0; i < (int)len1; i++) {
        col[0] = i + 1;
        for (int j = 0; j < (int)len2; j++) {
            col[j + 1] = std::min({ prevCol[1 + j] + 1, col[j] + 1, prevCol[j] + (s1[i] == s2[j] ? 0 : 1) });
        }
        std::swap(col, prevCol);
    }
    return prevCol[len2];
}

static std::string suggestSimilarName(proto::ProtoContext* ctx, const std::string& name, const proto::ProtoObject* scope) {
    if (!scope) return "";
    const proto::ProtoSparseList* attrs = scope->getAttributes(ctx);
    if (!attrs) return "";
    
    std::string bestMatch;
    int bestDist = 3; // Max distance to suggest
    
    proto::ProtoSparseListIterator* it = const_cast<proto::ProtoSparseListIterator*>(attrs->getIterator(ctx));
    while (it && it->hasNext(ctx)) {
        unsigned long key = it->nextKey(ctx);
        const proto::ProtoString* s = reinterpret_cast<const proto::ProtoString*>(key);
        if (s) {
            std::string candidate;
            s->toUTF8String(ctx, candidate);
            if (candidate.size() >= 2) {
                int d = levenshtein_distance(name, candidate);
                if (d < bestDist) {
                    bestDist = d;
                    bestMatch = candidate;
                }
            }
        }
        it = const_cast<proto::ProtoSparseListIterator*>(it->advance(ctx));
    }
    return bestMatch;
}

void PythonEnvironment::raiseImportError(const std::string& msg) {
    if (!importErrorType) return;
    proto::ProtoContext* ctx = rootContext_;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(msg.c_str()));
    const proto::ProtoObject* exc = importErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), importErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseNameError(proto::ProtoContext* ctx, const std::string& name) {
    if (!nameErrorType) return;
    std::string msg = "name '" + name + "' is not defined";

    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(msg.c_str()));
    const proto::ProtoObject* exc = nameErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), nameErrorType, args, nullptr);
    if (exc) {
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "name"), ctx->fromUTF8String(name.c_str()));
        setPendingException(exc);
    }
}

void PythonEnvironment::raiseAttributeError(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const std::string& attr) {
    if (attr == "__func__") {
        std::cerr << "\n############################################\n"
                  << "TRAPPING __func__ lookup on object " << obj << "\n"
                  << "Current context: " << ctx << "\n"
                  << "############################################\n" << std::endl;
        abort(); // intentionally crash
    }
    if (!attributeErrorType) return;
    std::string typeName = "object";
    const proto::ProtoObject* cls = obj->getAttribute(ctx, getInternalString(ctx, "__class__"));
    if (cls) {
        const proto::ProtoObject* nameAttr = cls->getAttribute(ctx, getInternalString(ctx, "__name__"));
        if (nameAttr && nameAttr->isString(ctx)) nameAttr->asString(ctx)->toUTF8String(ctx, typeName);
    } else {
        if (obj->isInteger(ctx)) typeName = "int";
        else if (obj->isString(ctx)) typeName = "str";
        else if (obj->asList(ctx)) typeName = "list";
    }
    std::string msg = "'" + typeName + "' object has no attribute '" + attr + "'";
    
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(msg.c_str()));
    const proto::ProtoObject* exc = attributeErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), attributeErrorType, args, nullptr);
    if (exc) {
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "name"), ctx->fromUTF8String(attr.c_str()));
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "obj"), obj);
        setPendingException(exc);
    }
}

void PythonEnvironment::raiseTypeError(proto::ProtoContext* ctx, const std::string& msg) {
    if (!typeErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(msg.c_str()));
    const proto::ProtoObject* exc = typeErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), typeErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseRuntimeError(proto::ProtoContext* ctx, const std::string& msg) {
    if (!runtimeErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(msg.c_str()));
    const proto::ProtoObject* exc = runtimeErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), runtimeErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseImportError(proto::ProtoContext* ctx, const std::string& msg) {
    if (!importErrorType) return;
    std::string hintMsg = msg;
    
    // Step 1337: ImportError Hints (search path)
    if (sysModule) {
        const proto::ProtoObject* pathObj = sysModule->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"));
        if (pathObj && pathObj->asList(ctx)) {
            hintMsg += "\nSearch path: [";
            const proto::ProtoList* pathList = pathObj->asList(ctx);
            for (unsigned long i = 0; i < pathList->getSize(ctx); ++i) {
                if (i > 0) hintMsg += ", ";
                std::string p;
                const proto::ProtoObject* item = pathList->getAt(ctx, static_cast<int>(i));
                if (item && item->isString(ctx)) {
                    item->asString(ctx)->toUTF8String(ctx, p);
                    hintMsg += "'" + p + "'";
                }
            }
            hintMsg += "]";
        }
    }

    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(hintMsg.c_str()));
    const proto::ProtoObject* exc = importErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), importErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseKeyboardInterrupt(proto::ProtoContext* ctx) {
    if (!keyboardInterruptType) return;
    const proto::ProtoList* args = ctx->newList();
    const proto::ProtoObject* exc = keyboardInterruptType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), keyboardInterruptType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseSyntaxError(proto::ProtoContext* ctx, const std::string& msg, int lineno, int offset, const std::string& text) {
    if (!syntaxErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(msg.c_str()));
    const proto::ProtoObject* exc = syntaxErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), syntaxErrorType, args, nullptr);
    if (exc) {
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "lineno"), ctx->fromInteger(lineno));
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "offset"), ctx->fromInteger(offset));
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "text"), ctx->fromUTF8String(text.c_str()));
        setPendingException(exc);
    }
}

void PythonEnvironment::raiseEOFError(proto::ProtoContext* ctx) {
    if (!eofErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String("EOF when reading a line"));
    const proto::ProtoObject* exc = eofErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), eofErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseSystemExit(proto::ProtoContext* ctx, int code) {
    if (!systemExitType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromInteger(code));
    const proto::ProtoObject* exc = systemExitType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), systemExitType, args, nullptr);
    if (exc) {
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "code"), ctx->fromInteger(code));
        setPendingException(exc);
    }
}

void PythonEnvironment::raiseRecursionError(proto::ProtoContext* ctx) {
    if (!recursionErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String("maximum recursion depth exceeded"));
    const proto::ProtoObject* exc = recursionErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), recursionErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseAssertionError(proto::ProtoContext* ctx, const proto::ProtoObject* msg) {
    if (!assertionErrorType) return;
    const proto::ProtoList* args = ctx->newList();
    if (msg) args = args->appendLast(ctx, msg);
    const proto::ProtoObject* exc = assertionErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), assertionErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseZeroDivisionError(proto::ProtoContext* ctx) {
    if (!zeroDivisionErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String("division by zero"));
    const proto::ProtoObject* exc = zeroDivisionErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), zeroDivisionErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseIndexError(proto::ProtoContext* ctx, const std::string& msg) {
    if (!indexErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(msg.c_str()));
    const proto::ProtoObject* exc = indexErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), indexErrorType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseStopIteration(proto::ProtoContext* ctx, const proto::ProtoObject* value) {
    if (!stopIterationType) return;
    if (std::getenv("PROTO_ENV_DIAG")) {
    }
    const proto::ProtoList* args = ctx->newList();
    if (value) args = args->appendLast(ctx, value);
    const proto::ProtoObject* exc = stopIterationType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), stopIterationType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}

void PythonEnvironment::raiseStopAsyncIteration(proto::ProtoContext* ctx) {
    if (!stopAsyncIterationType) return;
    const proto::ProtoList* args = ctx->newList();
    const proto::ProtoObject* exc = stopAsyncIterationType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), stopAsyncIterationType, args, nullptr);
    if (exc && exc != PROTO_NONE) setPendingException(exc);
}


static const proto::ProtoObject* py_getset_get(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*, 
    const proto::ProtoList* args, 
    const proto::ProtoSparseList*) {

    if (args->getSize(context) < 1) return self; 
    const proto::ProtoObject* instance = args->getAt(context, 0);
    
    if (instance == PROTO_NONE) {
        return self;
    }
    
    const proto::ProtoString* fgetS = proto::ProtoString::fromUTF8String(context, "fget"); // optimize?
    const proto::ProtoObject* getter = self->getAttribute(context, fgetS);
    
    if (getter && getter->isMethod(context)) {
         const proto::ProtoList* callArgs = context->newList()->appendLast(context, instance);
         return getter->asMethod(context)(context, instance, nullptr, callArgs, nullptr);
    }
    return PROTO_NONE;
}

void PythonEnvironment::initializeRootObjects(const std::string& stdLibPath, const std::vector<std::string>& searchPaths) {
    s_threadEnv = this;
    s_globalThreadRootsDict = rootContext_->newObject(true);
    space_->moduleRoots.push_back(s_globalThreadRootsDict);
    __code__ = proto::ProtoString::fromUTF8String(rootContext_, "__code__");
    __globals__ = proto::ProtoString::fromUTF8String(rootContext_, "__globals__");
    co_varnames = proto::ProtoString::fromUTF8String(rootContext_, "co_varnames");
    co_nparams = proto::ProtoString::fromUTF8String(rootContext_, "co_nparams");
    co_kwonlyargcount = proto::ProtoString::fromUTF8String(rootContext_, "co_kwonlyargcount");
    co_automatic_count = proto::ProtoString::fromUTF8String(rootContext_, "co_automatic_count");
    co_is_generator = proto::ProtoString::fromUTF8String(rootContext_, "co_is_generator");
    co_flags = proto::ProtoString::fromUTF8String(rootContext_, "co_flags");
    co_consts = proto::ProtoString::fromUTF8String(rootContext_, "co_consts");
    co_names = proto::ProtoString::fromUTF8String(rootContext_, "co_names");
    co_code = proto::ProtoString::fromUTF8String(rootContext_, "co_code");
    sendString = proto::ProtoString::fromUTF8String(rootContext_, "send");
    throwString = proto::ProtoString::fromUTF8String(rootContext_, "throw");
    closeString = proto::ProtoString::fromUTF8String(rootContext_, "close");
    selfDunder = proto::ProtoString::fromUTF8String(rootContext_, "__self__");
    funcDunder = proto::ProtoString::fromUTF8String(rootContext_, "__func__");
    f_back = proto::ProtoString::fromUTF8String(rootContext_, "f_back");
    f_code = proto::ProtoString::fromUTF8String(rootContext_, "f_code");
    f_globals = proto::ProtoString::fromUTF8String(rootContext_, "f_globals");
    f_locals = proto::ProtoString::fromUTF8String(rootContext_, "f_locals");
    __closure__ = proto::ProtoString::fromUTF8String(rootContext_, "__closure__");
    __defaults__ = proto::ProtoString::fromUTF8String(rootContext_, "__defaults__");
    __kwdefaults__ = proto::ProtoString::fromUTF8String(rootContext_, "__kwdefaults__");
    co_name = proto::ProtoString::fromUTF8String(rootContext_, "co_name");
    gi_code = proto::ProtoString::fromUTF8String(rootContext_, "gi_code");
    gi_frame = proto::ProtoString::fromUTF8String(rootContext_, "gi_frame");
    gi_running = proto::ProtoString::fromUTF8String(rootContext_, "gi_running");
    gi_yieldfrom = proto::ProtoString::fromUTF8String(rootContext_, "gi_yieldfrom");
    gi_pc = proto::ProtoString::fromUTF8String(rootContext_, "gi_pc");
    gi_stack = proto::ProtoString::fromUTF8String(rootContext_, "gi_stack");
    gi_blocks = proto::ProtoString::fromUTF8String(rootContext_, "gi_blocks");
    gi_locals = proto::ProtoString::fromUTF8String(rootContext_, "gi_locals");
    giNativeCallbackString = proto::ProtoString::fromUTF8String(rootContext_, "gi_native_callback");
    rangeS = proto::ProtoString::fromUTF8String(rootContext_, "range");
    itemsS = proto::ProtoString::fromUTF8String(rootContext_, "items");
    valuesS = proto::ProtoString::fromUTF8String(rootContext_, "values");
    keysS = proto::ProtoString::fromUTF8String(rootContext_, "keys");
    assertionErrorS = proto::ProtoString::fromUTF8String(rootContext_, "AssertionError");
    runtimeErrorS = proto::ProtoString::fromUTF8String(rootContext_, "RuntimeError");
    typeErrorS = proto::ProtoString::fromUTF8String(rootContext_, "TypeError");
    keyErrorS = proto::ProtoString::fromUTF8String(rootContext_, "KeyError");
    valueErrorS = proto::ProtoString::fromUTF8String(rootContext_, "ValueError");
    stopIterationS = proto::ProtoString::fromUTF8String(rootContext_, "StopIteration");
    pathS = proto::ProtoString::fromUTF8String(rootContext_, "path");
    modulesS = proto::ProtoString::fromUTF8String(rootContext_, "modules");
    stopAsyncIterationS = proto::ProtoString::fromUTF8String(rootContext_, "StopAsyncIteration");
    exceptionS = proto::ProtoString::fromUTF8String(rootContext_, "Exception");
    nameErrorS = proto::ProtoString::fromUTF8String(rootContext_, "NameError");
    attributeErrorS = proto::ProtoString::fromUTF8String(rootContext_, "AttributeError");
    syntaxErrorS = proto::ProtoString::fromUTF8String(rootContext_, "SyntaxError");
    importErrorS = proto::ProtoString::fromUTF8String(rootContext_, "ImportError");
    indexErrorS = proto::ProtoString::fromUTF8String(rootContext_, "IndexError");
    osErrorS = proto::ProtoString::fromUTF8String(rootContext_, "OSError");
    blockingIOErrorS = proto::ProtoString::fromUTF8String(rootContext_, "BlockingIOError");
    exceptionRootS = proto::ProtoString::fromUTF8String(rootContext_, "__exception_root__");

    {
    }

    __iadd__ = proto::ProtoString::fromUTF8String(rootContext_, "__iadd__");
    __isub__ = proto::ProtoString::fromUTF8String(rootContext_, "__isub__");
    __imul__ = proto::ProtoString::fromUTF8String(rootContext_, "__imul__");
    __itruediv__ = proto::ProtoString::fromUTF8String(rootContext_, "__itruediv__");
    __ifloordiv__ = proto::ProtoString::fromUTF8String(rootContext_, "__ifloordiv__");
    __imod__ = proto::ProtoString::fromUTF8String(rootContext_, "__imod__");
    __ipow__ = proto::ProtoString::fromUTF8String(rootContext_, "__ipow__");
    __ilshift__ = proto::ProtoString::fromUTF8String(rootContext_, "__ilshift__");
    __irshift__ = proto::ProtoString::fromUTF8String(rootContext_, "__irshift__");
    __iand__ = proto::ProtoString::fromUTF8String(rootContext_, "__iand__");
    __ior__ = proto::ProtoString::fromUTF8String(rootContext_, "__ior__");
    __ixor__ = proto::ProtoString::fromUTF8String(rootContext_, "__ixor__");

    __and__ = proto::ProtoString::fromUTF8String(rootContext_, "__and__");
    __rand__ = proto::ProtoString::fromUTF8String(rootContext_, "__rand__");
    __or__ = proto::ProtoString::fromUTF8String(rootContext_, "__or__");
    __ror__ = proto::ProtoString::fromUTF8String(rootContext_, "__ror__");
    __xor__ = proto::ProtoString::fromUTF8String(rootContext_, "__xor__");
    __rxor__ = proto::ProtoString::fromUTF8String(rootContext_, "__rxor__");

    __invert__ = proto::ProtoString::fromUTF8String(rootContext_, "__invert__");
    __pos__ = proto::ProtoString::fromUTF8String(rootContext_, "__pos__");
    awaitString = proto::ProtoString::fromUTF8String(rootContext_, "__await__");
    aiterString = proto::ProtoString::fromUTF8String(rootContext_, "__aiter__");
    anextString = proto::ProtoString::fromUTF8String(rootContext_, "__anext__");
    aenterString = proto::ProtoString::fromUTF8String(rootContext_, "__aenter__");
    aexitString = proto::ProtoString::fromUTF8String(rootContext_, "__aexit__");

    iterString = proto::ProtoString::fromUTF8String(rootContext_, "__iter__");
    nextString = proto::ProtoString::fromUTF8String(rootContext_, "__next__");
    emptyList = rootContext_->newList();
    emptySparseList = rootContext_->newSparseList();
    recursionLimit_ = 1000;
    rangeCurString = proto::ProtoString::fromUTF8String(rootContext_, "_cur");
    rangeStopString = proto::ProtoString::fromUTF8String(rootContext_, "_stop");
    rangeStepString = proto::ProtoString::fromUTF8String(rootContext_, "_step");
    mapFuncString = proto::ProtoString::fromUTF8String(rootContext_, "_func");
    mapIterString = proto::ProtoString::fromUTF8String(rootContext_, "_iter");
    enumIterString = proto::ProtoString::fromUTF8String(rootContext_, "_iter");
    enumIdxString = proto::ProtoString::fromUTF8String(rootContext_, "_idx");
    revObjString = proto::ProtoString::fromUTF8String(rootContext_, "_obj");
    revIdxString = proto::ProtoString::fromUTF8String(rootContext_, "_idx");
    zipItersString = proto::ProtoString::fromUTF8String(rootContext_, "__zip_iters__");
    
    coFilenameString = proto::ProtoString::fromUTF8String(rootContext_, "co_filename");
    coFirstLinenoString = proto::ProtoString::fromUTF8String(rootContext_, "co_firstlineno");
    coLnotabString = proto::ProtoString::fromUTF8String(rootContext_, "co_lnotab");
    
    filterFuncString = proto::ProtoString::fromUTF8String(rootContext_, "__filter_func__");
    filterIterString = proto::ProtoString::fromUTF8String(rootContext_, "_iter");
    enterString = proto::ProtoString::fromUTF8String(rootContext_, "__enter__");
    exitString = proto::ProtoString::fromUTF8String(rootContext_, "__exit__");
    classString = proto::ProtoString::fromUTF8String(rootContext_, "__class__");
    executedString = proto::ProtoString::fromUTF8String(rootContext_, "__executed__");
    nameString = proto::ProtoString::fromUTF8String(rootContext_, "__name__");
    callString = proto::ProtoString::fromUTF8String(rootContext_, "__call__");
    getItemString = proto::ProtoString::fromUTF8String(rootContext_, "__getitem__");
    setItemString = proto::ProtoString::fromUTF8String(rootContext_, "__setitem__");
    delItemString = proto::ProtoString::fromUTF8String(rootContext_, "__delitem__");
    initString = proto::ProtoString::fromUTF8String(rootContext_, "__init__");
    lenString = proto::ProtoString::fromUTF8String(rootContext_, "__len__");
    boolString = proto::ProtoString::fromUTF8String(rootContext_, "__bool__");
    intString = proto::ProtoString::fromUTF8String(rootContext_, "__int__");
    floatString = proto::ProtoString::fromUTF8String(rootContext_, "__float__");
    strString = proto::ProtoString::fromUTF8String(rootContext_, "__str__");
    reprString = proto::ProtoString::fromUTF8String(rootContext_, "__repr__");
    hashString = proto::ProtoString::fromUTF8String(rootContext_, "__hash__");
    containsString = proto::ProtoString::fromUTF8String(rootContext_, "__contains__");
    addString = proto::ProtoString::fromUTF8String(rootContext_, "__add__");
    matMulString = proto::ProtoString::fromUTF8String(rootContext_, "__matmul__");
    imatmulString = proto::ProtoString::fromUTF8String(rootContext_, "__imatmul__");
    rmatmulString = proto::ProtoString::fromUTF8String(rootContext_, "__rmatmul__");
    powString = proto::ProtoString::fromUTF8String(rootContext_, "__pow__");
    formatString = proto::ProtoString::fromUTF8String(rootContext_, "__format__");
    dictString = proto::ProtoString::fromUTF8String(rootContext_, "__dict__");
    docString = proto::ProtoString::fromUTF8String(rootContext_, "__doc__");
    reversedString = proto::ProtoString::fromUTF8String(rootContext_, "__reversed__");
    addS = proto::ProtoString::fromUTF8String(rootContext_, "add");
    enumProtoS = proto::ProtoString::fromUTF8String(rootContext_, "__enumerate_proto__");
    revProtoS = proto::ProtoString::fromUTF8String(rootContext_, "__reversed_proto__");
    zipProtoS = proto::ProtoString::fromUTF8String(rootContext_, "__zip_proto__");
    filterProtoS = proto::ProtoString::fromUTF8String(rootContext_, "__filter_proto__");
    mapProtoS = proto::ProtoString::fromUTF8String(rootContext_, "__map_proto__");
    rangeProtoS = proto::ProtoString::fromUTF8String(rootContext_, "__range_proto__");
    boolTypeS = proto::ProtoString::fromUTF8String(rootContext_, "bool");
    filterBoolS = proto::ProtoString::fromUTF8String(rootContext_, "_bool");
    getDunderString = proto::ProtoString::fromUTF8String(rootContext_, "__get__");
    setDunderString = proto::ProtoString::fromUTF8String(rootContext_, "__set__");
    delDunderString = proto::ProtoString::fromUTF8String(rootContext_, "__delete__");
    dataString = proto::ProtoString::fromUTF8String(rootContext_, "__data__");
    space_->literalData = const_cast<proto::ProtoString*>(dataString);
    keysString = proto::ProtoString::fromUTF8String(rootContext_, "__keys__");
    startString = proto::ProtoString::fromUTF8String(rootContext_, "start");
    stopString = proto::ProtoString::fromUTF8String(rootContext_, "stop");
    stepString = proto::ProtoString::fromUTF8String(rootContext_, "step");
    openString = proto::ProtoString::fromUTF8String(rootContext_, "open");
    moduleString = proto::ProtoString::fromUTF8String(rootContext_, "__module__");
    builtinsString = proto::ProtoString::fromUTF8String(rootContext_, "builtins");
    listS = proto::ProtoString::fromUTF8String(rootContext_, "list");
    dictS = proto::ProtoString::fromUTF8String(rootContext_, "dict");
    tupleS = proto::ProtoString::fromUTF8String(rootContext_, "tuple");
    setS = proto::ProtoString::fromUTF8String(rootContext_, "set");
    intS = proto::ProtoString::fromUTF8String(rootContext_, "int");
    floatS = proto::ProtoString::fromUTF8String(rootContext_, "float");
    strS = proto::ProtoString::fromUTF8String(rootContext_, "str");
    boolS = proto::ProtoString::fromUTF8String(rootContext_, "bool");
    objectS = proto::ProtoString::fromUTF8String(rootContext_, "object");
    typeS = proto::ProtoString::fromUTF8String(rootContext_, "type");
    fileDunderS = proto::ProtoString::fromUTF8String(rootContext_, "__file__");
    pathDunderS = proto::ProtoString::fromUTF8String(rootContext_, "__path__");

    zeroInteger = rootContext_->fromInteger(0);
    oneInteger = rootContext_->fromInteger(1);

    ioModuleString = proto::ProtoString::fromUTF8String(rootContext_, "__io_module__");

    // Use member variables for initialization
    const proto::ProtoString* py_init = getInternalString(rootContext_, "__init__");
    const proto::ProtoString* py_repr = reprString;
    const proto::ProtoString* py_str = strString;
    const proto::ProtoString* py_class = classString;
    const proto::ProtoString* py_name = nameString;
    const proto::ProtoString* py_module = getModuleString();
    const proto::ProtoObject* builtinsVal = builtinsString->asObject(rootContext_);
    const proto::ProtoString* py_append = proto::ProtoString::fromUTF8String(rootContext_, "append");
    const proto::ProtoString* py_getitem = getItemString;
    const proto::ProtoString* py_setitem = setItemString;
    const proto::ProtoString* py_len = lenString;
    const proto::ProtoString* py_iter = iterString;
    const proto::ProtoString* py_next = nextString;
    const proto::ProtoString* py_iter_proto = proto::ProtoString::fromUTF8String(rootContext_, "__iter_prototype__");
    const proto::ProtoString* py_contains = containsString;
    py_eq_s = proto::ProtoString::fromUTF8String(rootContext_, "__eq__");
    py_ne_s = proto::ProtoString::fromUTF8String(rootContext_, "__ne__");
    py_lt_s = proto::ProtoString::fromUTF8String(rootContext_, "__lt__");
    py_le_s = proto::ProtoString::fromUTF8String(rootContext_, "__le__");
    py_gt_s = proto::ProtoString::fromUTF8String(rootContext_, "__gt__");
    py_ge_s = proto::ProtoString::fromUTF8String(rootContext_, "__ge__");
    const proto::ProtoString* py_eq = py_eq_s;
    const proto::ProtoString* py_ne = py_ne_s;
    const proto::ProtoString* py_lt = py_lt_s;
    const proto::ProtoString* py_le = py_le_s;
    const proto::ProtoString* py_gt = py_gt_s;
    const proto::ProtoString* py_ge = py_ge_s;
    const proto::ProtoString* py_bool = boolString;
    const proto::ProtoString* py_pop = proto::ProtoString::fromUTF8String(rootContext_, "pop");
    const proto::ProtoString* py_extend = proto::ProtoString::fromUTF8String(rootContext_, "extend");
    const proto::ProtoString* py_insert = proto::ProtoString::fromUTF8String(rootContext_, "insert");
    const proto::ProtoString* py_remove = proto::ProtoString::fromUTF8String(rootContext_, "remove");
    const proto::ProtoString* py_keys = proto::ProtoString::fromUTF8String(rootContext_, "keys");
    const proto::ProtoString* py_values = proto::ProtoString::fromUTF8String(rootContext_, "values");
    const proto::ProtoString* py_items = proto::ProtoString::fromUTF8String(rootContext_, "items");
    const proto::ProtoString* py_get = proto::ProtoString::fromUTF8String(rootContext_, "get");
    const proto::ProtoString* py_setdefault = proto::ProtoString::fromUTF8String(rootContext_, "setdefault");
    const proto::ProtoString* py_update = proto::ProtoString::fromUTF8String(rootContext_, "update");
    const proto::ProtoString* py_clear = proto::ProtoString::fromUTF8String(rootContext_, "clear");
    const proto::ProtoString* py_reverse = proto::ProtoString::fromUTF8String(rootContext_, "reverse");
    const proto::ProtoString* py_sort = proto::ProtoString::fromUTF8String(rootContext_, "sort");
    const proto::ProtoString* py_copy = proto::ProtoString::fromUTF8String(rootContext_, "copy");
    const proto::ProtoString* py_upper = proto::ProtoString::fromUTF8String(rootContext_, "upper");
    const proto::ProtoString* py_lower = proto::ProtoString::fromUTF8String(rootContext_, "lower");
    const proto::ProtoString* py_format = proto::ProtoString::fromUTF8String(rootContext_, "format");
    const proto::ProtoString* py_split = proto::ProtoString::fromUTF8String(rootContext_, "split");
    const proto::ProtoString* py_join = proto::ProtoString::fromUTF8String(rootContext_, "join");
    const proto::ProtoString* py_strip = proto::ProtoString::fromUTF8String(rootContext_, "strip");
    const proto::ProtoString* py_lstrip = proto::ProtoString::fromUTF8String(rootContext_, "lstrip");
    const proto::ProtoString* py_rstrip = proto::ProtoString::fromUTF8String(rootContext_, "rstrip");
    const proto::ProtoString* py_replace = proto::ProtoString::fromUTF8String(rootContext_, "replace");
    const proto::ProtoString* py_startswith = proto::ProtoString::fromUTF8String(rootContext_, "startswith");
    const proto::ProtoString* py_endswith = proto::ProtoString::fromUTF8String(rootContext_, "endswith");
    const proto::ProtoString* py_maketrans = proto::ProtoString::fromUTF8String(rootContext_, "maketrans");
    const proto::ProtoString* py_add = getAddS();

    // 1. Create 'object' base
    objectPrototype = rootContext_->newObject(false);
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("object"));
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_module, builtinsVal);

    const proto::ProtoString* py_format_dunder = proto::ProtoString::fromUTF8String(rootContext_, "__format__");
    const proto::ProtoString* py_hash_dunder = proto::ProtoString::fromUTF8String(rootContext_, "__hash__");
    
    // Explicitly expose py_object_new from builtins
    const proto::ProtoObject* py_object_new = rootContext_->fromMethod(nullptr, protoPython::builtins::py_object_new);

    objectPrototype = objectPrototype->setAttribute(rootContext_, py_init, rootContext_->fromMethod(nullptr, py_object_init));
    // Core FIX: Bind __new__ directly immediately to prevent type fallback overshadowing it due to cache invalidations!
    objectPrototype = objectPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__new__"), py_object_new);
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_object_repr));
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_str, rootContext_->fromMethod(nullptr, py_object_str));
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_format_dunder, rootContext_->fromMethod(nullptr, py_object_format));
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_hash_dunder, rootContext_->fromMethod(nullptr, py_object_hash));
    objectPrototype = objectPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(nullptr, py_object_call));

    // 2. Create 'type'
    typePrototype = objectPrototype->newChild(rootContext_, true);
    typePrototype = typePrototype->setAttribute(rootContext_, py_class, typePrototype);
    typePrototype = typePrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("type"));
    typePrototype = typePrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    typePrototype = typePrototype->setAttribute(rootContext_, py_module, builtinsVal);
    typePrototype = typePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(nullptr, protoPython::builtins::py_type));
    typePrototype = typePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__new__"), rootContext_->fromMethod(nullptr, protoPython::builtins::py_type));
    typePrototype = typePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__prepare__"), rootContext_->fromMethod(nullptr, protoPython::builtins::py_type_prepare));
    typePrototype = typePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__class_getitem__"), rootContext_->fromMethod(nullptr, py_type_class_getitem));
    typePrototype = typePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__or__"), rootContext_->fromMethod(nullptr, py_type_or));

    // Initialize mappingproxy
    mappingProxyPrototype = objectPrototype->newChild(rootContext_, true);
    mappingProxyPrototype = mappingProxyPrototype->setAttribute(rootContext_, py_class, typePrototype);
    mappingProxyPrototype = mappingProxyPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("mappingproxy"));
    mappingProxyPrototype = mappingProxyPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_mappingproxy_repr));
    
    // Initialize UnionType
    unionTypePrototype = objectPrototype->newChild(rootContext_, true);
    unionTypePrototype = unionTypePrototype->setAttribute(rootContext_, py_class, typePrototype);
    unionTypePrototype = unionTypePrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("UnionType"));
    unionTypePrototype = unionTypePrototype->setAttribute(rootContext_, py_module, builtinsVal);
    unionTypePrototype = unionTypePrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_union_type_repr));
    mappingProxyPrototype = mappingProxyPrototype->setAttribute(rootContext_, getItemString, rootContext_->fromMethod(nullptr, py_mappingproxy_getitem));
    mappingProxyPrototype = mappingProxyPrototype->setAttribute(rootContext_, py_module, builtinsVal);

    // Initialize getset_descriptor
    getSetDescriptorPrototype = objectPrototype->newChild(rootContext_, true);
    getSetDescriptorPrototype = getSetDescriptorPrototype->setAttribute(rootContext_, py_class, typePrototype);
    getSetDescriptorPrototype = getSetDescriptorPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("getset_descriptor"));
    getSetDescriptorPrototype = getSetDescriptorPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromUTF8String("<getset_descriptor>")); // Placeholder
    getSetDescriptorPrototype = getSetDescriptorPrototype->setAttribute(rootContext_, getDunderString, rootContext_->fromMethod(nullptr, py_getset_get));


    // Register __dict__ on type as a property/descriptor (STEP 15502 FIX)
    // We create an instance of getset_descriptor and set its fget to py_type_get_dict
    proto::ProtoObject* dictDescr = const_cast<proto::ProtoObject*>(getSetDescriptorPrototype->newChild(rootContext_, true));
    dictDescr->setAttribute(rootContext_, py_class, getSetDescriptorPrototype);
    dictDescr->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "fget"), rootContext_->fromMethod(nullptr, py_type_get_dict)); 
    dictDescr->setAttribute(rootContext_, py_name, dictString->asObject(rootContext_));

    typePrototype = typePrototype->setAttribute(rootContext_, dictString, dictDescr);

    // 3. Circularity: object's class is type
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_class, typePrototype);

    // 4. Create 'frame' prototype
    framePrototype = objectPrototype->newChild(rootContext_, true);
    framePrototype = framePrototype->setAttribute(rootContext_, py_class, typePrototype);
    framePrototype = framePrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("frame"));
    framePrototype = framePrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_frame_repr));
    framePrototype = framePrototype->setAttribute(rootContext_, py_module, builtinsVal);

    // 5. Create 'generator' prototype
    generatorPrototype = objectPrototype->newChild(rootContext_, true);
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_class, typePrototype);
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("generator"));
    
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_generator_repr));
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_module, builtinsVal);
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_self_iter));
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_next, rootContext_->fromMethod(nullptr, py_generator_next));
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "send"), rootContext_->fromMethod(nullptr, py_generator_send));
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "throw"), rootContext_->fromMethod(nullptr, py_generator_throw));
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "close"), rootContext_->fromMethod(nullptr, py_generator_close));

    // V72: Create 'function' prototype
    functionPrototype = objectPrototype->newChild(rootContext_, true);
    functionPrototype = functionPrototype->setAttribute(rootContext_, py_class, typePrototype);
    functionPrototype = functionPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("function"));
    functionPrototype = functionPrototype->setAttribute(rootContext_, py_module, builtinsVal);

    // 6. Basic types
    intPrototype = objectPrototype->newChild(rootContext_, true);
    const proto::ProtoString* py_hash = proto::ProtoString::fromUTF8String(rootContext_, "__hash__");
    intPrototype = intPrototype->setAttribute(rootContext_, py_class, typePrototype);
    intPrototype = intPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("int"));
    intPrototype = intPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    intPrototype = intPrototype->setAttribute(rootContext_, py_module, builtinsVal);
    intPrototype = intPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(nullptr, py_int_call));
    intPrototype = intPrototype->setAttribute(rootContext_, py_hash, rootContext_->fromMethod(nullptr, py_int_hash));
    intPrototype = intPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(nullptr, py_int_bool));
    intPrototype = intPrototype->setAttribute(rootContext_, py_format_dunder, rootContext_->fromMethod(nullptr, py_int_format));
    const proto::ProtoString* py_bit_length = proto::ProtoString::fromUTF8String(rootContext_, "bit_length");
    intPrototype = intPrototype->setAttribute(rootContext_, py_bit_length, rootContext_->fromMethod(nullptr, py_int_bit_length));
    intPrototype = intPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "bit_count"), rootContext_->fromMethod(nullptr, py_int_bit_count));
    const proto::ProtoString* py_from_bytes = proto::ProtoString::fromUTF8String(rootContext_, "from_bytes");
    const proto::ProtoString* py_to_bytes = proto::ProtoString::fromUTF8String(rootContext_, "to_bytes");
    intPrototype = intPrototype->setAttribute(rootContext_, py_from_bytes, rootContext_->fromMethod(nullptr, py_int_from_bytes));
    intPrototype = intPrototype->setAttribute(rootContext_, py_to_bytes, rootContext_->fromMethod(nullptr, py_int_to_bytes));

    complexPrototype = objectPrototype->newChild(rootContext_, true);
    complexPrototype = complexPrototype->setAttribute(rootContext_, py_class, typePrototype);
    complexPrototype = complexPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("complex"));
    complexPrototype = complexPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    complexPrototype = complexPrototype->setAttribute(rootContext_, py_str, rootContext_->fromMethod(nullptr, py_type_repr));
    complexPrototype = complexPrototype->setAttribute(rootContext_, py_module, builtinsVal);
    complexPrototype = complexPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(nullptr, protoPython::builtins::py_complex));

    strPrototype = objectPrototype->newChild(rootContext_, true);
    strPrototype = strPrototype->setAttribute(rootContext_, py_class, typePrototype);
    strPrototype = strPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("str"));
    strPrototype = strPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    strPrototype = strPrototype->setAttribute(rootContext_, py_module, builtinsVal);
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(nullptr, py_str_call));
    strPrototype = strPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_str_iter));
    strPrototype = strPrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(nullptr, py_str_getitem));
    strPrototype = strPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(nullptr, py_str_contains));
    strPrototype = strPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(nullptr, py_str_bool));
    strPrototype = strPrototype->setAttribute(rootContext_, py_upper, rootContext_->fromMethod(nullptr, py_str_upper));
    strPrototype = strPrototype->setAttribute(rootContext_, py_lower, rootContext_->fromMethod(nullptr, py_str_lower));
    strPrototype = strPrototype->setAttribute(rootContext_, py_format, rootContext_->fromMethod(nullptr, py_str_format));
    strPrototype = strPrototype->setAttribute(rootContext_, py_format_dunder, rootContext_->fromMethod(nullptr, py_str_format_dunder));
    strPrototype = strPrototype->setAttribute(rootContext_, py_hash, rootContext_->fromMethod(nullptr, py_str_hash));
    strPrototype = strPrototype->setAttribute(rootContext_, py_split, rootContext_->fromMethod(nullptr, py_str_split));
    strPrototype = strPrototype->setAttribute(rootContext_, py_join, rootContext_->fromMethod(nullptr, py_str_join));
    strPrototype = strPrototype->setAttribute(rootContext_, py_strip, rootContext_->fromMethod(nullptr, py_str_strip));
    strPrototype = strPrototype->setAttribute(rootContext_, py_lstrip, rootContext_->fromMethod(nullptr, py_str_lstrip));
    strPrototype = strPrototype->setAttribute(rootContext_, py_rstrip, rootContext_->fromMethod(nullptr, py_str_rstrip));
    strPrototype = strPrototype->setAttribute(rootContext_, py_replace, rootContext_->fromMethod(nullptr, py_str_replace));
    strPrototype = strPrototype->setAttribute(rootContext_, py_startswith, rootContext_->fromMethod(nullptr, py_str_startswith));
    strPrototype = strPrototype->setAttribute(rootContext_, py_endswith, rootContext_->fromMethod(nullptr, py_str_endswith));
    strPrototype = strPrototype->setAttribute(rootContext_, py_maketrans, rootContext_->fromMethod(nullptr, py_str_maketrans));
    const proto::ProtoString* py_find = proto::ProtoString::fromUTF8String(rootContext_, "find");
    const proto::ProtoString* py_index = proto::ProtoString::fromUTF8String(rootContext_, "index");
    strPrototype = strPrototype->setAttribute(rootContext_, py_find, rootContext_->fromMethod(nullptr, py_str_find));
    strPrototype = strPrototype->setAttribute(rootContext_, py_index, rootContext_->fromMethod(nullptr, py_str_index));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rfind"), rootContext_->fromMethod(nullptr, py_str_rfind));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rindex"), rootContext_->fromMethod(nullptr, py_str_rindex));
    const proto::ProtoString* py_count = proto::ProtoString::fromUTF8String(rootContext_, "count");
    strPrototype = strPrototype->setAttribute(rootContext_, py_count, rootContext_->fromMethod(nullptr, py_str_count));
    const proto::ProtoString* py_rsplit = proto::ProtoString::fromUTF8String(rootContext_, "rsplit");
    strPrototype = strPrototype->setAttribute(rootContext_, py_rsplit, rootContext_->fromMethod(nullptr, py_str_rsplit));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "splitlines"), rootContext_->fromMethod(nullptr, py_str_splitlines));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "removeprefix"), rootContext_->fromMethod(nullptr, py_str_removeprefix));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "removesuffix"), rootContext_->fromMethod(nullptr, py_str_removesuffix));
    const proto::ProtoString* py_center = proto::ProtoString::fromUTF8String(rootContext_, "center");
    const proto::ProtoString* py_ljust = proto::ProtoString::fromUTF8String(rootContext_, "ljust");
    const proto::ProtoString* py_rjust = proto::ProtoString::fromUTF8String(rootContext_, "rjust");
    const proto::ProtoString* py_zfill = proto::ProtoString::fromUTF8String(rootContext_, "zfill");
    const proto::ProtoString* py_partition = proto::ProtoString::fromUTF8String(rootContext_, "partition");
    const proto::ProtoString* py_rpartition = proto::ProtoString::fromUTF8String(rootContext_, "rpartition");
    strPrototype = strPrototype->setAttribute(rootContext_, py_center, rootContext_->fromMethod(nullptr, py_str_center));
    strPrototype = strPrototype->setAttribute(rootContext_, py_ljust, rootContext_->fromMethod(nullptr, py_str_ljust));
    strPrototype = strPrototype->setAttribute(rootContext_, py_rjust, rootContext_->fromMethod(nullptr, py_str_rjust));
    strPrototype = strPrototype->setAttribute(rootContext_, py_zfill, rootContext_->fromMethod(nullptr, py_str_zfill));
    strPrototype = strPrototype->setAttribute(rootContext_, py_partition, rootContext_->fromMethod(nullptr, py_str_partition));
    strPrototype = strPrototype->setAttribute(rootContext_, py_rpartition, rootContext_->fromMethod(nullptr, py_str_rpartition));
    const proto::ProtoString* py_expandtabs = proto::ProtoString::fromUTF8String(rootContext_, "expandtabs");
    strPrototype = strPrototype->setAttribute(rootContext_, py_expandtabs, rootContext_->fromMethod(nullptr, py_str_expandtabs));
    const proto::ProtoString* py_capitalize = proto::ProtoString::fromUTF8String(rootContext_, "capitalize");
    const proto::ProtoString* py_title = proto::ProtoString::fromUTF8String(rootContext_, "title");
    const proto::ProtoString* py_swapcase = proto::ProtoString::fromUTF8String(rootContext_, "swapcase");
    strPrototype = strPrototype->setAttribute(rootContext_, py_capitalize, rootContext_->fromMethod(nullptr, py_str_capitalize));
    strPrototype = strPrototype->setAttribute(rootContext_, py_title, rootContext_->fromMethod(nullptr, py_str_title));
    strPrototype = strPrototype->setAttribute(rootContext_, py_swapcase, rootContext_->fromMethod(nullptr, py_str_swapcase));
    const proto::ProtoString* py_casefold = proto::ProtoString::fromUTF8String(rootContext_, "casefold");
    const proto::ProtoString* py_isalpha = proto::ProtoString::fromUTF8String(rootContext_, "isalpha");
    const proto::ProtoString* py_isdigit = proto::ProtoString::fromUTF8String(rootContext_, "isdigit");
    const proto::ProtoString* py_isspace = proto::ProtoString::fromUTF8String(rootContext_, "isspace");
    const proto::ProtoString* py_isalnum = proto::ProtoString::fromUTF8String(rootContext_, "isalnum");
    strPrototype = strPrototype->setAttribute(rootContext_, py_casefold, rootContext_->fromMethod(nullptr, py_str_casefold));
    strPrototype = strPrototype->setAttribute(rootContext_, py_isalpha, rootContext_->fromMethod(nullptr, py_str_isalpha));
    strPrototype = strPrototype->setAttribute(rootContext_, py_isdigit, rootContext_->fromMethod(nullptr, py_str_isdigit));
    strPrototype = strPrototype->setAttribute(rootContext_, py_isspace, rootContext_->fromMethod(nullptr, py_str_isspace));
    strPrototype = strPrototype->setAttribute(rootContext_, py_isalnum, rootContext_->fromMethod(nullptr, py_str_isalnum));
    const proto::ProtoString* py_isupper = proto::ProtoString::fromUTF8String(rootContext_, "isupper");
    const proto::ProtoString* py_islower = proto::ProtoString::fromUTF8String(rootContext_, "islower");
    strPrototype = strPrototype->setAttribute(rootContext_, py_isupper, rootContext_->fromMethod(nullptr, py_str_isupper));
    strPrototype = strPrototype->setAttribute(rootContext_, py_islower, rootContext_->fromMethod(nullptr, py_str_islower));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isidentifier"), rootContext_->fromMethod(nullptr, py_str_isidentifier));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isprintable"), rootContext_->fromMethod(nullptr, py_str_isprintable));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isascii"), rootContext_->fromMethod(nullptr, py_str_isascii));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isdecimal"), rootContext_->fromMethod(nullptr, py_str_isdecimal));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isnumeric"), rootContext_->fromMethod(nullptr, py_str_isnumeric));

    listPrototype = objectPrototype->newChild(rootContext_, true);
    listPrototype = listPrototype->setAttribute(rootContext_, py_class, typePrototype);
    listPrototype = listPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("list"));
    listPrototype = listPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    listPrototype = listPrototype->setAttribute(rootContext_, py_module, builtinsVal);
    listPrototype = listPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(nullptr, py_list_call));
    listPrototype = listPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    listPrototype = listPrototype->setAttribute(rootContext_, py_append, rootContext_->fromMethod(nullptr, py_list_append));
    listPrototype = listPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(nullptr, py_list_len));
    listPrototype = listPrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(nullptr, py_list_getitem));
    listPrototype = listPrototype->setAttribute(rootContext_, py_setitem, rootContext_->fromMethod(nullptr, py_list_setitem));
    listPrototype = listPrototype->setAttribute(rootContext_, delItemString, rootContext_->fromMethod(nullptr, py_list_delitem));
    listPrototype = listPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_list_iter));
    listPrototype = listPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(nullptr, py_list_contains));
    listPrototype = listPrototype->setAttribute(rootContext_, py_eq, rootContext_->fromMethod(nullptr, py_list_eq));
    listPrototype = listPrototype->setAttribute(rootContext_, py_lt, rootContext_->fromMethod(nullptr, py_list_lt));
    listPrototype = listPrototype->setAttribute(rootContext_, py_le, rootContext_->fromMethod(nullptr, py_list_le));
    listPrototype = listPrototype->setAttribute(rootContext_, py_gt, rootContext_->fromMethod(nullptr, py_list_gt));
    listPrototype = listPrototype->setAttribute(rootContext_, py_ge, rootContext_->fromMethod(nullptr, py_list_ge));
    listPrototype = listPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_list_repr));
    listPrototype = listPrototype->setAttribute(rootContext_, py_str, rootContext_->fromMethod(nullptr, py_list_str));
    listPrototype = listPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(nullptr, py_list_bool));
    listPrototype = listPrototype->setAttribute(rootContext_, py_pop, rootContext_->fromMethod(nullptr, py_list_pop));
    listPrototype = listPrototype->setAttribute(rootContext_, py_extend, rootContext_->fromMethod(nullptr, py_list_extend));
    listPrototype = listPrototype->setAttribute(rootContext_, py_insert, rootContext_->fromMethod(nullptr, py_list_insert));
    listPrototype = listPrototype->setAttribute(rootContext_, py_remove, rootContext_->fromMethod(nullptr, py_list_remove));
    listPrototype = listPrototype->setAttribute(rootContext_, py_clear, rootContext_->fromMethod(nullptr, py_list_clear));
    listPrototype = listPrototype->setAttribute(rootContext_, py_reverse, rootContext_->fromMethod(nullptr, py_list_reverse));
    listPrototype = listPrototype->setAttribute(rootContext_, py_sort, rootContext_->fromMethod(nullptr, py_list_sort));
    listPrototype = listPrototype->setAttribute(rootContext_, py_copy, rootContext_->fromMethod(nullptr, py_list_copy));
    const proto::ProtoString* py_list_index_name = proto::ProtoString::fromUTF8String(rootContext_, "index");
    const proto::ProtoString* py_list_count_name = proto::ProtoString::fromUTF8String(rootContext_, "count");
    listPrototype = listPrototype->setAttribute(rootContext_, py_list_index_name, rootContext_->fromMethod(nullptr, py_list_index));
    listPrototype = listPrototype->setAttribute(rootContext_, py_list_count_name, rootContext_->fromMethod(nullptr, py_list_count));
    const proto::ProtoString* py_mul = proto::ProtoString::fromUTF8String(rootContext_, "__mul__");
    const proto::ProtoString* py_rmul = proto::ProtoString::fromUTF8String(rootContext_, "__rmul__");
    listPrototype = listPrototype->setAttribute(rootContext_, py_mul, rootContext_->fromMethod(nullptr, py_list_mul));
    listPrototype = listPrototype->setAttribute(rootContext_, py_rmul, rootContext_->fromMethod(nullptr, py_list_mul));
    listPrototype = listPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__iadd__"), rootContext_->fromMethod(nullptr, py_list_iadd));

    const proto::ProtoObject* listIterProto = objectPrototype->newChild(rootContext_, true);
    listIterProto = listIterProto->setAttribute(rootContext_, py_class, typePrototype);
    listIterProto = listIterProto->setAttribute(rootContext_, py_name, proto::ProtoString::fromUTF8String(rootContext_, "list_iterator")->asObject(rootContext_));
    listIterProto = listIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(nullptr, py_list_iter_next));
    listIterProto = listIterProto->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_self_iter));
    listPrototype = listPrototype->setAttribute(rootContext_, py_iter_proto, listIterProto);
    const proto::ProtoObject* listReverseIterProto = objectPrototype->newChild(rootContext_, true);
    listReverseIterProto = listReverseIterProto->setAttribute(rootContext_, py_class, typePrototype);
    listReverseIterProto = listReverseIterProto->setAttribute(rootContext_, py_name, proto::ProtoString::fromUTF8String(rootContext_, "list_reverseiterator")->asObject(rootContext_));
    listReverseIterProto = listReverseIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(nullptr, py_list_reversed_next));
    listReverseIterProto = listReverseIterProto->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_self_iter));
    listPrototype = listPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__reversed_prototype__"), listReverseIterProto);
    listPrototype = listPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__reversed__"), rootContext_->fromMethod(nullptr, py_list_reversed));

    dictPrototype = objectPrototype->newChild(rootContext_, true);
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_class, typePrototype);
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("dict"));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(nullptr, py_dict_call));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_module, builtinsVal);
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(nullptr, py_dict_getitem));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_setitem, rootContext_->fromMethod(nullptr, py_dict_setitem));
    dictPrototype = dictPrototype->setAttribute(rootContext_, delItemString, rootContext_->fromMethod(nullptr, py_dict_delitem));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(nullptr, py_dict_len));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_dict_iter));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_iter_proto, listIterProto);
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(nullptr, py_dict_contains));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_eq, rootContext_->fromMethod(nullptr, py_dict_eq));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_lt, rootContext_->fromMethod(nullptr, py_dict_lt));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_le, rootContext_->fromMethod(nullptr, py_dict_le));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_gt, rootContext_->fromMethod(nullptr, py_dict_gt));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_ge, rootContext_->fromMethod(nullptr, py_dict_ge));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_dict_repr));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_str, rootContext_->fromMethod(nullptr, py_dict_str));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(nullptr, py_dict_bool));
    dictPrototype = dictPrototype->setAttribute(rootContext_, keysS, rootContext_->fromMethod(nullptr, py_dict_keys));
    dictPrototype = dictPrototype->setAttribute(rootContext_, valuesS, rootContext_->fromMethod(nullptr, py_dict_values));
    dictPrototype = dictPrototype->setAttribute(rootContext_, itemsS, rootContext_->fromMethod(nullptr, py_dict_items));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_get, rootContext_->fromMethod(nullptr, py_dict_get));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_setdefault, rootContext_->fromMethod(nullptr, py_dict_setdefault));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_pop, rootContext_->fromMethod(nullptr, py_dict_pop));
    const proto::ProtoString* py_popitem = proto::ProtoString::fromUTF8String(rootContext_, "popitem");
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_popitem, rootContext_->fromMethod(nullptr, py_dict_popitem));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_update, rootContext_->fromMethod(nullptr, py_dict_update));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_clear, rootContext_->fromMethod(nullptr, py_dict_clear));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_copy, rootContext_->fromMethod(nullptr, py_dict_copy));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "fromkeys"), rootContext_->fromMethod(nullptr, py_dict_fromkeys));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__or__"), rootContext_->fromMethod(nullptr, py_dict_or));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__ror__"), rootContext_->fromMethod(nullptr, py_dict_ror));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__ior__"), rootContext_->fromMethod(nullptr, py_dict_ior));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__iror__"), rootContext_->fromMethod(nullptr, py_dict_iror));

    tuplePrototype = objectPrototype->newChild(rootContext_, true);
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_class, typePrototype);
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("tuple"));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_module, builtinsVal);
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(nullptr, py_tuple_call));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(nullptr, py_tuple_len));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(nullptr, py_tuple_getitem));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_tuple_iter));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(nullptr, py_tuple_contains));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(nullptr, py_tuple_bool));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_hash, rootContext_->fromMethod(nullptr, py_tuple_hash));
    const proto::ProtoString* py_tuple_index_name = proto::ProtoString::fromUTF8String(rootContext_, "index");
    const proto::ProtoString* py_tuple_count_name = proto::ProtoString::fromUTF8String(rootContext_, "count");
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_tuple_index_name, rootContext_->fromMethod(nullptr, py_tuple_index));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_tuple_count_name, rootContext_->fromMethod(nullptr, py_tuple_count));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_tuple_repr));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_eq, rootContext_->fromMethod(nullptr, py_tuple_eq));

    const proto::ProtoObject* tupleIterProto = objectPrototype->newChild(rootContext_, true);
    tupleIterProto = tupleIterProto->setAttribute(rootContext_, py_class, typePrototype);
    tupleIterProto = tupleIterProto->setAttribute(rootContext_, py_name, proto::ProtoString::fromUTF8String(rootContext_, "tuple_iterator")->asObject(rootContext_));
    tupleIterProto = tupleIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(nullptr, py_tuple_iter_next));
    tupleIterProto = tupleIterProto->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_self_iter));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_iter_proto, tupleIterProto);

    setPrototype = objectPrototype->newChild(rootContext_, true);
    setPrototype = setPrototype->setAttribute(rootContext_, py_class, typePrototype);
    setPrototype = setPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("set"));
    setPrototype = setPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    setPrototype = setPrototype->setAttribute(rootContext_, py_module, builtinsVal);
    setPrototype = setPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(nullptr, py_set_len));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(nullptr, py_set_call));
    setPrototype = setPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(nullptr, py_set_contains));
    setPrototype = setPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(nullptr, py_set_bool));
    setPrototype = setPrototype->setAttribute(rootContext_, py_add, rootContext_->fromMethod(nullptr, py_set_add));
    setPrototype = setPrototype->setAttribute(rootContext_, py_remove, rootContext_->fromMethod(nullptr, py_set_remove));
    setPrototype = setPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_set_repr));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "pop"), rootContext_->fromMethod(nullptr, py_set_pop));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "discard"), rootContext_->fromMethod(nullptr, py_set_discard));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "copy"), rootContext_->fromMethod(nullptr, py_set_copy));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "clear"), rootContext_->fromMethod(nullptr, py_set_clear));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "union"), rootContext_->fromMethod(nullptr, py_set_union));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "intersection"), rootContext_->fromMethod(nullptr, py_set_intersection));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "difference"), rootContext_->fromMethod(nullptr, py_set_difference));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "symmetric_difference"), rootContext_->fromMethod(nullptr, py_set_symmetric_difference));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "issubset"), rootContext_->fromMethod(nullptr, py_set_issubset));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "issuperset"), rootContext_->fromMethod(nullptr, py_set_issuperset));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__or__"), rootContext_->fromMethod(nullptr, py_set_or));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__and__"), rootContext_->fromMethod(nullptr, py_set_and));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__sub__"), rootContext_->fromMethod(nullptr, py_set_sub));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__xor__"), rootContext_->fromMethod(nullptr, py_set_xor));
    setPrototype = setPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_set_iter));

    const proto::ProtoObject* setIterProto = objectPrototype->newChild(rootContext_, true);
    setIterProto = setIterProto->setAttribute(rootContext_, py_class, typePrototype);
    setIterProto = setIterProto->setAttribute(rootContext_, py_name, proto::ProtoString::fromUTF8String(rootContext_, "set_iterator")->asObject(rootContext_));
    setIterProto = setIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(nullptr, py_set_iter_next));
    setIterProto = setIterProto->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_self_iter));
    setPrototype = setPrototype->setAttribute(rootContext_, py_iter_proto, setIterProto);

    const proto::ProtoString* py_call = proto::ProtoString::fromUTF8String(rootContext_, "__call__");
    frozensetPrototype = objectPrototype->newChild(rootContext_, true);
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_class, typePrototype);
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("frozenset"));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_call, rootContext_->fromMethod(nullptr, py_frozenset_call));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(nullptr, py_frozenset_len));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(nullptr, py_frozenset_contains));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(nullptr, py_frozenset_bool));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_frozenset_iter));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_hash, rootContext_->fromMethod(nullptr, py_frozenset_hash));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_iter_proto, setIterProto);

    bytesPrototype = objectPrototype->newChild(rootContext_, true);
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_class, typePrototype);
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("bytes"));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(nullptr, py_bytes_len));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_bytes_repr));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(nullptr, py_bytes_getitem));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_bytes_iter));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_call, rootContext_->fromMethod(nullptr, py_bytes_call));

    const proto::ProtoObject* bytesIterProto = objectPrototype->newChild(rootContext_, true);
    bytesIterProto = bytesIterProto->setAttribute(rootContext_, py_class, typePrototype);
    bytesIterProto = bytesIterProto->setAttribute(rootContext_, py_name, proto::ProtoString::fromUTF8String(rootContext_, "bytes_iterator")->asObject(rootContext_));
    bytesIterProto = bytesIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(nullptr, py_bytes_iter_next));
    bytesIterProto = bytesIterProto->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(nullptr, py_self_iter));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_iter_proto, bytesIterProto);
    // Prototype initialization diagnostic removed
    const proto::ProtoString* py_encode = proto::ProtoString::fromUTF8String(rootContext_, "encode");
    const proto::ProtoString* py_decode = proto::ProtoString::fromUTF8String(rootContext_, "decode");
    strPrototype = strPrototype->setAttribute(rootContext_, py_encode, rootContext_->fromMethod(nullptr, py_str_encode));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_decode, rootContext_->fromMethod(nullptr, py_bytes_decode));
    const proto::ProtoString* py_hex = proto::ProtoString::fromUTF8String(rootContext_, "hex");
    const proto::ProtoString* py_fromhex = proto::ProtoString::fromUTF8String(rootContext_, "fromhex");
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_hex, rootContext_->fromMethod(nullptr, py_bytes_hex));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_fromhex, rootContext_->fromMethod(nullptr, py_bytes_fromhex));
    const proto::ProtoString* py_bytes_find_name = proto::ProtoString::fromUTF8String(rootContext_, "find");
    const proto::ProtoString* py_bytes_count_name = proto::ProtoString::fromUTF8String(rootContext_, "count");
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_bytes_find_name, rootContext_->fromMethod(nullptr, py_bytes_find));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_bytes_count_name, rootContext_->fromMethod(nullptr, py_bytes_count));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "index"), rootContext_->fromMethod(nullptr, py_bytes_index));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rfind"), rootContext_->fromMethod(nullptr, py_bytes_rfind));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rindex"), rootContext_->fromMethod(nullptr, py_bytes_rindex));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "startswith"), rootContext_->fromMethod(nullptr, py_bytes_startswith));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "endswith"), rootContext_->fromMethod(nullptr, py_bytes_endswith));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "strip"), rootContext_->fromMethod(nullptr, py_bytes_strip));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "lstrip"), rootContext_->fromMethod(nullptr, py_bytes_lstrip));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rstrip"), rootContext_->fromMethod(nullptr, py_bytes_rstrip));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "split"), rootContext_->fromMethod(nullptr, py_bytes_split));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "join"), rootContext_->fromMethod(nullptr, py_bytes_join));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "replace"), rootContext_->fromMethod(nullptr, py_bytes_replace));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isdigit"), rootContext_->fromMethod(nullptr, py_bytes_isdigit));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isalpha"), rootContext_->fromMethod(nullptr, py_bytes_isalpha));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isascii"), rootContext_->fromMethod(nullptr, py_bytes_isascii));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "removeprefix"), rootContext_->fromMethod(nullptr, py_bytes_removeprefix));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "removesuffix"), rootContext_->fromMethod(nullptr, py_bytes_removesuffix));

    sliceType = objectPrototype->newChild(rootContext_, true);
    sliceType = sliceType->setAttribute(rootContext_, py_class, typePrototype);
    sliceType = sliceType->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("slice"));
    sliceType = sliceType->setAttribute(rootContext_, py_module, builtinsVal);
    sliceType = sliceType->setAttribute(rootContext_, py_call, rootContext_->fromMethod(nullptr, py_slice_call));
    sliceType = sliceType->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_slice_repr));

    floatPrototype = objectPrototype->newChild(rootContext_, true);
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_class, typePrototype);
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("float"));
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_module, builtinsVal);
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(nullptr, py_float_bool));
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_call, rootContext_->fromMethod(nullptr, py_float_call));
    const proto::ProtoString* py_is_integer = proto::ProtoString::fromUTF8String(rootContext_, "is_integer");
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_is_integer, rootContext_->fromMethod(nullptr, py_float_is_integer));
    const proto::ProtoString* py_as_integer_ratio = proto::ProtoString::fromUTF8String(rootContext_, "as_integer_ratio");
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_as_integer_ratio, rootContext_->fromMethod(nullptr, py_float_as_integer_ratio));
    floatPrototype = floatPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "hex"), rootContext_->fromMethod(nullptr, py_float_hex));
    floatPrototype = floatPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "fromhex"), rootContext_->fromMethod(nullptr, py_float_fromhex));

    boolPrototype = objectPrototype->newChild(rootContext_, true);
    noneTypeProto = objectPrototype->newChild(rootContext_, true);
    noneTypeProto = noneTypeProto->setAttribute(rootContext_, py_class, typePrototype);
    noneTypeProto = noneTypeProto->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("NoneType"));
    noneTypeProto = noneTypeProto->setAttribute(rootContext_, py_module, builtinsVal);
    noneTypeProto = noneTypeProto->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));

    // Support for primitive None (PROTO_NONE) attributes via prototype
    noneTypeProto = noneTypeProto->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(nullptr, py_none_bool));
    noneTypeProto = noneTypeProto->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_none_repr));

    // Remove __call__ from NoneType instance prototype (Step V75: None is not callable)
    // noneTypeProto = noneTypeProto->setAttribute(rootContext_, py_call, rootContext_->fromMethod(nullptr, py_none_type_call));

    nonePrototype = noneTypeProto->newChild(rootContext_, false);
    nonePrototype = nonePrototype->setAttribute(rootContext_, py_class, noneTypeProto);
    space_->nonePrototype = const_cast<proto::ProtoObject*>(nonePrototype);

    // Initialize Ellipsis
    const proto::ProtoObject* ellipsisType = objectPrototype->newChild(rootContext_, true);
    ellipsisType = ellipsisType->setAttribute(rootContext_, py_class, typePrototype);
    ellipsisType = ellipsisType->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("ellipsis"));
    ellipsisType = ellipsisType->setAttribute(rootContext_, py_module, builtinsVal);
    ellipsisType = ellipsisType->setAttribute(rootContext_, py_repr, rootContext_->fromUTF8String("Ellipsis"));
    ellipsisType = ellipsisType->setAttribute(rootContext_, py_call, rootContext_->fromMethod(nullptr, py_ellipsis_type_call));
    ellipsisPrototype = ellipsisType->newChild(rootContext_, false);
    ellipsisPrototype = ellipsisPrototype->setAttribute(rootContext_, py_class, ellipsisType);

    // Initialize NotImplemented
    const proto::ProtoObject* notImplType = objectPrototype->newChild(rootContext_, true);
    notImplType = notImplType->setAttribute(rootContext_, py_class, typePrototype);
    notImplType = notImplType->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("NotImplementedType"));
    notImplType = notImplType->setAttribute(rootContext_, py_module, builtinsVal);
    notImplType = notImplType->setAttribute(rootContext_, py_repr, rootContext_->fromUTF8String("NotImplemented"));
    notImplType = notImplType->setAttribute(rootContext_, py_call, rootContext_->fromMethod(nullptr, py_notimplemented_type_call));
    notImplementedPrototype = notImplType->newChild(rootContext_, false);
    notImplementedPrototype = notImplementedPrototype->setAttribute(rootContext_, py_class, notImplType);

    boolPrototype = boolPrototype->addParent(rootContext_, intPrototype);
    boolPrototype = boolPrototype->setAttribute(rootContext_, py_class, typePrototype);
    boolPrototype = boolPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("bool"));
    boolPrototype = boolPrototype->setAttribute(rootContext_, py_module, builtinsVal);
    boolPrototype = boolPrototype->setAttribute(rootContext_, py_call, rootContext_->fromMethod(nullptr, py_bool_call));
    // Update boolean class name and repr
    boolPrototype = boolPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(nullptr, py_type_repr));
    
    // 4.5 Initialize modulePrototype
    modulePrototype = objectPrototype->newChild(rootContext_, true);
    modulePrototype = modulePrototype->setAttribute(rootContext_, getClassString(), typePrototype);
    modulePrototype = modulePrototype->setAttribute(rootContext_, getNameString(), rootContext_->fromUTF8String("module"));
    modulePrototype = modulePrototype->setAttribute(rootContext_, getModuleString(), builtinsVal);
    modulePrototype = modulePrototype->setAttribute(rootContext_, getIterString(), rootContext_->fromMethod(nullptr, py_dict_iter));
    modulePrototype = modulePrototype->setAttribute(rootContext_, py_iter_proto, listIterProto);
    modulePrototype = modulePrototype->setAttribute(rootContext_, getGetItemString(), rootContext_->fromMethod(nullptr, py_dict_getitem));
    modulePrototype = modulePrototype->setAttribute(rootContext_, getSetItemString(), rootContext_->fromMethod(nullptr, py_dict_setitem));
    modulePrototype = modulePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "keys"), rootContext_->fromMethod(nullptr, py_dict_keys));
    modulePrototype = modulePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "values"), rootContext_->fromMethod(nullptr, py_dict_values));
    modulePrototype = modulePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "items"), rootContext_->fromMethod(nullptr, py_dict_items));
    modulePrototype = modulePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "update"), rootContext_->fromMethod(nullptr, py_dict_update));
    
    // V75: Initialize specific prototypes for better type identity
    methodPrototype = objectPrototype->newChild(rootContext_, true);
    methodPrototype = methodPrototype->setAttribute(rootContext_, py_class, typePrototype);
    methodPrototype = methodPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("method"));
    methodPrototype = methodPrototype->setAttribute(rootContext_, py_module, builtinsVal);

    tracebackPrototype = objectPrototype->newChild(rootContext_, true);
    tracebackPrototype = tracebackPrototype->setAttribute(rootContext_, py_class, typePrototype);
    tracebackPrototype = tracebackPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("traceback"));
    tracebackPrototype = tracebackPrototype->setAttribute(rootContext_, py_module, builtinsVal);

    cellPrototype = objectPrototype->newChild(rootContext_, true);
    cellPrototype = cellPrototype->setAttribute(rootContext_, py_class, typePrototype);
    cellPrototype = cellPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("cell"));
    cellPrototype = cellPrototype->setAttribute(rootContext_, py_module, builtinsVal);

    codePrototype = objectPrototype->newChild(rootContext_, true);
    codePrototype = codePrototype->setAttribute(rootContext_, py_class, typePrototype);
    codePrototype = codePrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("code"));
    codePrototype = codePrototype->setAttribute(rootContext_, py_module, builtinsVal);

    // V75: Provide class-level attributes for types.py
    functionPrototype = functionPrototype->setAttribute(rootContext_, __code__, PROTO_NONE);
    functionPrototype = functionPrototype->setAttribute(rootContext_, __globals__, PROTO_NONE);
    functionPrototype = functionPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__doc__"), PROTO_NONE);
    functionPrototype = functionPrototype->setAttribute(rootContext_, classString, typePrototype); // Ensure class is set
    
    methodPrototype = methodPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__doc__"), PROTO_NONE);
    modulePrototype = modulePrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__doc__"), PROTO_NONE);
    
    tracebackPrototype = tracebackPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "tb_frame"), PROTO_NONE);
    tracebackPrototype = tracebackPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "tb_next"), PROTO_NONE);
    tracebackPrototype = tracebackPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "tb_lasti"), PROTO_NONE);
    tracebackPrototype = tracebackPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "tb_lineno"), PROTO_NONE);

    // V75: Ensure all protoCore built-in iterator prototypes have __class__ = type
    if (space_->stringIteratorPrototype) space_->stringIteratorPrototype = const_cast<proto::ProtoObject*>(space_->stringIteratorPrototype->setAttribute(rootContext_, py_class, typePrototype));
    if (space_->listIteratorPrototype) space_->listIteratorPrototype = const_cast<proto::ProtoObject*>(space_->listIteratorPrototype->setAttribute(rootContext_, py_class, typePrototype));
    if (space_->tupleIteratorPrototype) space_->tupleIteratorPrototype = const_cast<proto::ProtoObject*>(space_->tupleIteratorPrototype->setAttribute(rootContext_, py_class, typePrototype));
    if (space_->sparseListIteratorPrototype) space_->sparseListIteratorPrototype = const_cast<proto::ProtoObject*>(space_->sparseListIteratorPrototype->setAttribute(rootContext_, py_class, typePrototype));
    if (space_->setIteratorPrototype) space_->setIteratorPrototype = const_cast<proto::ProtoObject*>(space_->setIteratorPrototype->setAttribute(rootContext_, py_class, typePrototype));
    if (space_->multisetIteratorPrototype) space_->multisetIteratorPrototype = const_cast<proto::ProtoObject*>(space_->multisetIteratorPrototype->setAttribute(rootContext_, py_class, typePrototype));
    if (space_->rangeIteratorPrototype) space_->rangeIteratorPrototype = const_cast<proto::ProtoObject*>(space_->rangeIteratorPrototype->setAttribute(rootContext_, py_class, typePrototype));

    space_->objectPrototype = const_cast<proto::ProtoObject*>(objectPrototype);
    space_->stringPrototype = const_cast<proto::ProtoObject*>(strPrototype);
    space_->smallIntegerPrototype = const_cast<proto::ProtoObject*>(intPrototype);
    space_->doublePrototype = const_cast<proto::ProtoObject*>(floatPrototype);
    space_->booleanPrototype = const_cast<proto::ProtoObject*>(boolPrototype);
    space_->listPrototype = const_cast<proto::ProtoObject*>(listPrototype);
    space_->tuplePrototype = const_cast<proto::ProtoObject*>(tuplePrototype);
    space_->sparseListPrototype = const_cast<proto::ProtoObject*>(dictPrototype);
    space_->methodPrototype = const_cast<proto::ProtoObject*>(methodPrototype);
    // V75: Keep these in PythonEnvironment, not ProtoSpace

    // 5. Initialize Native Module Provider
    auto nativeProvider = std::make_unique<NativeModuleProvider>();

    // Construct search paths early for sys.path
    std::vector<std::string> allPaths;
    if (!stdLibPath.empty()) allPaths.push_back(stdLibPath);
    else allPaths.push_back("../lib/python3.14");
    for (const auto& p : searchPaths) allPaths.push_back(p);

    // sys module (argv set later via setArgv before executeModule)
    sysModule = sys::initialize(rootContext_, this, &argv_);
    if (modulePrototype) {
        sysModule = sysModule->addParent(rootContext_, modulePrototype);
        sysModule = sysModule->setAttribute(rootContext_, py_class, modulePrototype);
    }

    // Populate sys.path IMMEDIATELY before registration/caching
    const proto::ProtoObject* pathListObj = sysModule->getAttribute(rootContext_, pathS);
    const proto::ProtoList* pList = (pathListObj && pathListObj->asList(rootContext_)) 
        ? pathListObj->asList(rootContext_) : rootContext_->newList();
    
    const proto::ProtoObject* dataAttrP = pathListObj ? pathListObj->getAttribute(rootContext_, dataString) : nullptr;
    if (dataAttrP && dataAttrP->asList(rootContext_)) pList = dataAttrP->asList(rootContext_);

    for (const auto& p : allPaths) {
        const proto::ProtoObject* strObj = rootContext_->fromUTF8String(p.c_str());
        if (strPrototype) strObj = strObj->addParent(rootContext_, strPrototype);
        pList = pList->appendLast(rootContext_, strObj);
    }
    
    proto::ProtoObject* newListObj = const_cast<proto::ProtoObject*>(rootContext_->newObject(true));
    if (listPrototype) {
        newListObj = const_cast<proto::ProtoObject*>(newListObj->addParent(rootContext_, listPrototype));
        newListObj = const_cast<proto::ProtoObject*>(newListObj->setAttribute(rootContext_, getInternalString(rootContext_, "__class__"), listPrototype));
    }
    newListObj = const_cast<proto::ProtoObject*>(newListObj->setAttribute(rootContext_, dataString, pList->asObject(rootContext_)));
    sysModule = sysModule->setAttribute(rootContext_, pathS, newListObj);


    nativeProvider->registerModule("sys", [this](proto::ProtoContext* ctx) { return sysModule; });

    // _io module (created before builtins so open() can delegate)
    const proto::ProtoObject* ioModule = io::initialize(rootContext_);
    if (modulePrototype) {
        ioModule = ioModule->addParent(rootContext_, modulePrototype);
        ioModule = ioModule->setAttribute(rootContext_, py_class, modulePrototype);
    }
    nativeProvider->registerModule("_io", [ioModule](proto::ProtoContext*) { 
        return ioModule; 
    });

    // builtins module
    builtinsModule = builtins::initialize(rootContext_, objectPrototype, typePrototype, intPrototype, strPrototype, listPrototype, dictPrototype, tuplePrototype, setPrototype, bytesPrototype, nonePrototype, ellipsisPrototype, notImplementedPrototype, sliceType, frozensetPrototype, floatPrototype, boolPrototype, complexPrototype, ioModule);
    if (modulePrototype) {
        builtinsModule = builtinsModule->addParent(rootContext_, modulePrototype);
        builtinsModule = builtinsModule->setAttribute(rootContext_, py_class, modulePrototype);
    }
    nativeProvider->registerModule("builtins", [this](proto::ProtoContext* ctx) { return builtinsModule; });

    // _collections module
    nativeProvider->registerModule("_collections", [this](proto::ProtoContext* ctx) { return collections::initialize(ctx, this); });
    nativeProvider->registerModule("logging", [](proto::ProtoContext* ctx) { return logging::initialize(ctx); });
    nativeProvider->registerModule("operator", [](proto::ProtoContext* ctx) { return operator_::initialize(ctx); });
    nativeProvider->registerModule("_operator", [](proto::ProtoContext* ctx) { return operator_::initialize(ctx); });
    nativeProvider->registerModule("math", [](proto::ProtoContext* ctx) { return math::initialize(ctx); });
    nativeProvider->registerModule("time", [](proto::ProtoContext* ctx) { return time_module::initialize(ctx); });
    nativeProvider->registerModule("_os", [this](proto::ProtoContext* ctx) { return os_module::initialize(ctx, this); });
    nativeProvider->registerModule("posix", [this](proto::ProtoContext* ctx) { return os_module::initialize(ctx, this); });
    nativeProvider->registerModule("nt", [this](proto::ProtoContext* ctx) { return os_module::initialize(ctx, this); });
    nativeProvider->registerModule("_signal", [](proto::ProtoContext* ctx) { return signal_module::initialize(ctx); });
    nativeProvider->registerModule("_thread", [](proto::ProtoContext* ctx) { return thread_module::initialize(ctx); });
    nativeProvider->registerModule("functools", [](proto::ProtoContext* ctx) { return functools::initialize(ctx); });
    nativeProvider->registerModule("itertools", [](proto::ProtoContext* ctx) { return itertools::initialize(ctx); });
    nativeProvider->registerModule("re", [](proto::ProtoContext* ctx) { return re::initialize(ctx); });
    nativeProvider->registerModule("json", [](proto::ProtoContext* ctx) { return json::initialize(ctx); });
    nativeProvider->registerModule("os.path", [](proto::ProtoContext* ctx) { return os_path::initialize(ctx); });
    nativeProvider->registerModule("pathlib", [](proto::ProtoContext* ctx) { return pathlib::initialize(ctx); });
    // nativeProvider->registerModule("collections.abc", [](proto::ProtoContext* ctx) { return collections_abc::initialize(ctx); });
    // nativeProvider->registerModule("_collections_abc", [](proto::ProtoContext* ctx) { return collections_abc::initialize(ctx); });
    nativeProvider->registerModule("atexit", [](proto::ProtoContext* ctx) { return atexit_module::initialize(ctx); });
    nativeProvider->registerModule("_weakref", [](proto::ProtoContext* ctx) { return weakref::initialize(ctx); });

    const proto::ProtoObject* exceptionsMod = exceptions::initialize(rootContext_, objectPrototype, typePrototype);
    nativeProvider->registerModule("exceptions", [exceptionsMod](proto::ProtoContext* ctx) { return exceptionsMod; });

    const proto::ProtoObject* codecsMod = codecs::initialize(rootContext_, objectPrototype, typePrototype);
    nativeProvider->registerModule("_codecs", [codecsMod](proto::ProtoContext* ctx) { return codecsMod; });

    nativeProvider->registerModule("_ast", [](proto::ProtoContext* ctx) { return ast::initialize(ctx); });
    nativeProvider->registerModule("errno", [](proto::ProtoContext* ctx) { return errno_module::initialize(ctx); });
    nativeProvider->registerModule("stat", [](proto::ProtoContext* ctx) { return stat_module::initialize(ctx); });
    nativeProvider->registerModule("_struct", [](proto::ProtoContext* ctx) { return struct_module::initialize(ctx); });
    // Optional native modules that might not be fully tracked yet
    // nativeProvider->registerModule("_contextvars", [](proto::ProtoContext* ctx) { return contextvars::initialize(ctx); });

    exceptionType = exceptionsMod->getAttribute(rootContext_, exceptionS);
    keyErrorType = exceptionsMod->getAttribute(rootContext_, keyErrorS);
    valueErrorType = exceptionsMod->getAttribute(rootContext_, valueErrorS);
    nameErrorType = exceptionsMod->getAttribute(rootContext_, nameErrorS);
    attributeErrorType = exceptionsMod->getAttribute(rootContext_, attributeErrorS);
    syntaxErrorType = exceptionsMod->getAttribute(rootContext_, syntaxErrorS);
    typeErrorType = exceptionsMod->getAttribute(rootContext_, typeErrorS);
    importErrorType = exceptionsMod->getAttribute(rootContext_, importErrorS);
    keyboardInterruptType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "KeyboardInterrupt"));
    systemExitType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "SystemExit"));
    builtinsModule = builtinsModule->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "float"), floatPrototype);
    builtinsModule = builtinsModule->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "complex"), complexPrototype);
    builtinsModule = builtinsModule->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "str"), strPrototype);
    recursionErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "RecursionError"));
    runtimeErrorType = exceptionsMod->getAttribute(rootContext_, runtimeErrorS);
    stopIterationType = exceptionsMod->getAttribute(rootContext_, stopIterationS);
    if (std::getenv("PROTO_ENV_DIAG")) {
    }
    stopAsyncIterationType = exceptionsMod->getAttribute(rootContext_, stopAsyncIterationS);
    eofErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "EOFError"));
    assertionErrorType = exceptionsMod->getAttribute(rootContext_, assertionErrorS);
    if (!assertionErrorType || assertionErrorType == PROTO_NONE) {
        fprintf(stderr, "DEBUG: assertionErrorType is missing from exceptionsMod!\n");
        fflush(stderr);
    } else {
        fprintf(stderr, "DEBUG: assertionErrorType loaded successfully: %p\n", (void*)assertionErrorType);
        fflush(stderr);
    }
    zeroDivisionErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "ZeroDivisionError"));
    indexErrorType = exceptionsMod->getAttribute(rootContext_, indexErrorS);
    systemErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "SystemError"));
    osErrorType = exceptionsMod->getAttribute(rootContext_, osErrorS);
    blockingIOErrorType = exceptionsMod->getAttribute(rootContext_, blockingIOErrorS);
    baseExceptionType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "BaseException"));
    warningType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "Warning"));
    userWarningType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "UserWarning"));
    deprecationWarningType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "DeprecationWarning"));
    runtimeWarningType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "RuntimeWarning"));
    pendingDeprecationWarningType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "PendingDeprecationWarning"));
    importWarningType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "ImportWarning"));
    bytesWarningType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "BytesWarning"));
    resourceWarningType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "ResourceWarning"));
    encodingWarningType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "EncodingWarning"));

    // Expose common exceptions in builtins using cached strings
    if (builtinsModule) {
        builtinsModule = builtinsModule->setAttribute(rootContext_, exceptionS, exceptionType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, keyErrorS, keyErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, valueErrorS, valueErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, nameErrorS, nameErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, attributeErrorS, attributeErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, osErrorS, osErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, blockingIOErrorS, blockingIOErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, syntaxErrorS, syntaxErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, typeErrorS, typeErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, importErrorS, importErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, runtimeErrorS, runtimeErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, stopIterationS, stopIterationType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, stopAsyncIterationS, stopAsyncIterationType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, assertionErrorS, assertionErrorType);
        builtinsModule = builtinsModule->setAttribute(rootContext_, indexErrorS, indexErrorType);

        // Fallback for others not yet cached
        static const std::vector<std::pair<std::string, const proto::ProtoObject**>> excMap = {
            {"KeyboardInterrupt", &keyboardInterruptType},
            {"SystemExit", &systemExitType},
            {"RecursionError", &recursionErrorType},
            {"EOFError", &eofErrorType},
            {"ZeroDivisionError", &zeroDivisionErrorType},
            {"SystemError", &systemErrorType},
            {"BaseException", &baseExceptionType},
            {"Warning", &warningType},
            {"UserWarning", &userWarningType},
            {"DeprecationWarning", &deprecationWarningType},
            {"RuntimeWarning", &runtimeWarningType},
            {"PendingDeprecationWarning", &pendingDeprecationWarningType},
            {"ImportWarning", &importWarningType},
            {"BytesWarning", &bytesWarningType},
            {"ResourceWarning", &resourceWarningType},
            {"EncodingWarning", &encodingWarningType}
        };
        for (const auto& pair : excMap) {
            if (*pair.second) {
                builtinsModule = builtinsModule->setAttribute(rootContext_, 
                    proto::ProtoString::fromUTF8String(rootContext_, pair.first.c_str()), 
                    *pair.second);
            }
        }
    }

    // V72: strings and roots already initialized at top of function.

    proto::ProviderRegistry::instance().registerProvider(std::move(nativeProvider));

    // 6. Initialize StdLib Module Provider (already have allPaths from above)
    proto::ProviderRegistry::instance().registerProvider(std::make_unique<PythonModuleProvider>(allPaths));
    proto::ProviderRegistry::instance().registerProvider(std::make_unique<CompiledModuleProvider>(allPaths));
    proto::ProviderRegistry::instance().registerProvider(std::make_unique<HPyModuleProvider>(allPaths));
    
    // 7. sys.modules population (using interned strings)
    // a. Create sys.modules and add sys/builtins
    const proto::ProtoObject* modulesDictObj = sysModule->getAttribute(rootContext_, modulesS);
    if (modulesDictObj) {
        // Step V74: modulesDictObj is a Python dict, we must populate __data__ (SparseList)
        // and __keys__ (ProtoList) to maintain dictionary invariants.
        const proto::ProtoString* sysS = proto::ProtoString::fromUTF8String(rootContext_, "sys");
        const proto::ProtoString* builtinsS = proto::ProtoString::fromUTF8String(rootContext_, "builtins");
        
        const proto::ProtoObject* dataAttr = modulesDictObj->getAttribute(rootContext_, dataString);
        proto::ProtoSparseList* d = (dataAttr && dataAttr != PROTO_NONE && dataAttr->asSparseList(rootContext_)) 
            ? const_cast<proto::ProtoSparseList*>(dataAttr->asSparseList(rootContext_))
            : const_cast<proto::ProtoSparseList*>(rootContext_->newSparseList());
            
        d = const_cast<proto::ProtoSparseList*>(d->setAt(rootContext_, sysS->getHash(rootContext_), sysModule));
        d = const_cast<proto::ProtoSparseList*>(d->setAt(rootContext_, builtinsS->getHash(rootContext_), builtinsModule));
        
        modulesDictObj = modulesDictObj->setAttribute(rootContext_, dataString, d->asObject(rootContext_));
        
        const proto::ProtoObject* keysAttr = modulesDictObj->getAttribute(rootContext_, keysString);
        proto::ProtoList* kl = (keysAttr && keysAttr != PROTO_NONE && keysAttr->asList(rootContext_))
            ? const_cast<proto::ProtoList*>(keysAttr->asList(rootContext_))
            : const_cast<proto::ProtoList*>(rootContext_->newList());
            
        // Check if keys already exist to avoid duplicates
        bool hasSys = false, hasBuiltins = false;
        for (unsigned long i = 0; i < kl->getSize(rootContext_); ++i) {
            const proto::ProtoObject* k = kl->getAt(rootContext_, i);
            if (k->isString(rootContext_)) {
                std::string ks;
                k->asString(rootContext_)->toUTF8String(rootContext_, ks);
                if (ks == "sys") hasSys = true;
                if (ks == "builtins") hasBuiltins = true;
            }
        }
        if (!hasSys) kl = const_cast<proto::ProtoList*>(kl->appendLast(rootContext_, sysS->asObject(rootContext_)));
        if (!hasBuiltins) kl = const_cast<proto::ProtoList*>(kl->appendLast(rootContext_, builtinsS->asObject(rootContext_)));
        
        modulesDictObj = modulesDictObj->setAttribute(rootContext_, keysString, kl->asObject(rootContext_));
        sysModule = sysModule->setAttribute(rootContext_, modulesS, modulesDictObj);
    }

    // 8. Prepend to resolution chain: ensure provider:native is first so native modules
    //    (_thread, _os, etc.) resolve before any file-based lookup.
    const proto::ProtoObject* chainObj = rootContext_->space->getResolutionChain();
    const proto::ProtoList* chain = (chainObj && chainObj != PROTO_NONE)
        ? chainObj->asList(rootContext_) : nullptr;
    if (chain) {
        chain = chain->insertAt(rootContext_, 0, rootContext_->fromUTF8String("provider:python_stdlib"));
        chain = chain->insertAt(rootContext_, 0, rootContext_->fromUTF8String("provider:hpy"));
        chain = chain->insertAt(rootContext_, 0, rootContext_->fromUTF8String("provider:compiled"));
        chain = chain->insertAt(rootContext_, 0, rootContext_->fromUTF8String("provider:native"));
        rootContext_->space->setResolutionChain(chain->asObject(rootContext_));
    }

    // Register all root objects in ProtoSpace to prevent garbage collection
    {
        auto& roots = space_->moduleRoots;
        auto addRoot = [&](const proto::ProtoObject* obj) {
            if (obj) roots.push_back(obj);
        };

        addRoot(objectPrototype);
        addRoot(typePrototype);
        addRoot(intPrototype);
        addRoot(strPrototype);
        addRoot(listPrototype);
        addRoot(dictPrototype);
        addRoot(tuplePrototype);
        addRoot(setPrototype);
        addRoot(bytesPrototype);
        addRoot(nonePrototype);
        addRoot(sliceType);
        addRoot(frozensetPrototype);
        addRoot(floatPrototype);
        addRoot(boolPrototype);
        addRoot(sysModule);
        addRoot(builtinsModule);
        
        addRoot(keyErrorType);
        addRoot(valueErrorType);
        addRoot(nameErrorType);
        addRoot(attributeErrorType);
        addRoot(syntaxErrorType);
        addRoot(typeErrorType);
        addRoot(importErrorType);
        addRoot(keyboardInterruptType);
        addRoot(systemExitType);
        addRoot(recursionErrorType);
        addRoot(stopIterationType);
        addRoot(eofErrorType);
        addRoot(assertionErrorType);
        addRoot(zeroDivisionErrorType);
        addRoot(indexErrorType);

        addRoot(iterString ? iterString->asObject(rootContext_) : nullptr);
        addRoot(nextString ? nextString->asObject(rootContext_) : nullptr);
        addRoot(taskQueue ? taskQueue->asObject(rootContext_) : nullptr);
        if (emptyList) addRoot(emptyList->asObject(rootContext_));
        if (rangeCurString) addRoot(rangeCurString->asObject(rootContext_));
        if (rangeStopString) addRoot(rangeStopString->asObject(rootContext_));
        if (rangeStepString) addRoot(rangeStepString->asObject(rootContext_));
        if (mapFuncString) addRoot(mapFuncString->asObject(rootContext_));
        if (mapIterString) addRoot(mapIterString->asObject(rootContext_));
        if (enumIterString) addRoot(enumIterString->asObject(rootContext_));
        if (enumIdxString) addRoot(enumIdxString->asObject(rootContext_));
        if (revObjString) addRoot(revObjString->asObject(rootContext_));
        if (revIdxString) addRoot(revIdxString->asObject(rootContext_));
        if (zipItersString) addRoot(zipItersString->asObject(rootContext_));
        if (filterFuncString) addRoot(filterFuncString->asObject(rootContext_));
        if (filterIterString) addRoot(filterIterString->asObject(rootContext_));
        if (classString) addRoot(classString->asObject(rootContext_));
        if (nameString) addRoot(nameString->asObject(rootContext_));
        if (callString) addRoot(callString->asObject(rootContext_));
        if (getItemString) addRoot(getItemString->asObject(rootContext_));
        if (lenString) addRoot(lenString->asObject(rootContext_));
        if (boolString) addRoot(boolString->asObject(rootContext_));
        if (intString) addRoot(intString->asObject(rootContext_));
        if (floatString) addRoot(floatString->asObject(rootContext_));
        if (strString) addRoot(strString->asObject(rootContext_));
        if (reprString) addRoot(reprString->asObject(rootContext_));
        if (hashString) addRoot(hashString->asObject(rootContext_));
        if (powString) addRoot(powString->asObject(rootContext_));
        if (containsString) addRoot(containsString->asObject(rootContext_));
        if (addString) addRoot(addString->asObject(rootContext_));
        if (formatString) addRoot(formatString->asObject(rootContext_));
        if (dictString) addRoot(dictString->asObject(rootContext_));
        if (docString) addRoot(docString->asObject(rootContext_));
        if (reversedString) addRoot(reversedString->asObject(rootContext_));
        if (enumProtoS) addRoot(enumProtoS->asObject(rootContext_));
        if (revProtoS) addRoot(revProtoS->asObject(rootContext_));
        if (zipProtoS) addRoot(zipProtoS->asObject(rootContext_));
        if (filterProtoS) addRoot(filterProtoS->asObject(rootContext_));
        if (mapProtoS) addRoot(mapProtoS->asObject(rootContext_));
        if (rangeProtoS) addRoot(rangeProtoS->asObject(rootContext_));
        if (boolTypeS) addRoot(boolTypeS->asObject(rootContext_));
        if (filterBoolS) addRoot(filterBoolS->asObject(rootContext_));
        
        addRoot((__code__)->asObject(rootContext_));
        addRoot((__globals__)->asObject(rootContext_));
        addRoot((co_varnames)->asObject(rootContext_));
        addRoot((co_nparams)->asObject(rootContext_));
        addRoot((co_automatic_count)->asObject(rootContext_));
        addRoot((co_is_generator)->asObject(rootContext_));
        addRoot((co_flags)->asObject(rootContext_));
        addRoot((co_consts)->asObject(rootContext_));
        addRoot((co_names)->asObject(rootContext_));
        addRoot((co_code)->asObject(rootContext_));
        addRoot((giNativeCallbackString)->asObject(rootContext_));
        addRoot((sendString)->asObject(rootContext_));
        addRoot((throwString)->asObject(rootContext_));
        addRoot((closeString)->asObject(rootContext_));
        addRoot((f_back)->asObject(rootContext_));
        addRoot((f_code)->asObject(rootContext_));
        addRoot((f_globals)->asObject(rootContext_));
        addRoot((f_locals)->asObject(rootContext_));
        addRoot((__closure__)->asObject(rootContext_));
        addRoot((gi_code)->asObject(rootContext_));
        addRoot((gi_frame)->asObject(rootContext_));
        addRoot((gi_running)->asObject(rootContext_));
        addRoot((gi_yieldfrom)->asObject(rootContext_));
        addRoot((gi_pc)->asObject(rootContext_));
        addRoot((gi_stack)->asObject(rootContext_));
        addRoot((gi_locals)->asObject(rootContext_));
        addRoot((py_eq_s)->asObject(rootContext_));
        addRoot((py_ne_s)->asObject(rootContext_));
        addRoot((py_lt_s)->asObject(rootContext_));
        addRoot((py_le_s)->asObject(rootContext_));
        addRoot((py_gt_s)->asObject(rootContext_));
        addRoot((py_ge_s)->asObject(rootContext_));
        addRoot((getDunderString)->asObject(rootContext_));
        addRoot((setDunderString)->asObject(rootContext_));
        addRoot((delDunderString)->asObject(rootContext_));
        
        addRoot((__iadd__)->asObject(rootContext_));
        addRoot((__isub__)->asObject(rootContext_));
        addRoot((__imul__)->asObject(rootContext_));
        addRoot((__itruediv__)->asObject(rootContext_));
        addRoot((__ifloordiv__)->asObject(rootContext_));
        addRoot((__imod__)->asObject(rootContext_));
        addRoot((__ipow__)->asObject(rootContext_));
        addRoot((__ilshift__)->asObject(rootContext_));
        addRoot((__irshift__)->asObject(rootContext_));
        addRoot((__iand__)->asObject(rootContext_));
        addRoot((__ior__)->asObject(rootContext_));
        addRoot((__ixor__)->asObject(rootContext_));
        
        addRoot((__and__)->asObject(rootContext_));
        addRoot((__rand__)->asObject(rootContext_));
        addRoot((__or__)->asObject(rootContext_));
        addRoot((__ror__)->asObject(rootContext_));
        addRoot((__xor__)->asObject(rootContext_));
        addRoot((__rxor__)->asObject(rootContext_));
        
        addRoot((__invert__)->asObject(rootContext_));
        addRoot((__pos__)->asObject(rootContext_));
        
        addRoot((setItemString)->asObject(rootContext_));
        addRoot((delItemString)->asObject(rootContext_));
        addRoot((dataString)->asObject(rootContext_));
        addRoot((keysString)->asObject(rootContext_));
        
        addRoot((startString)->asObject(rootContext_));
        addRoot((stopString)->asObject(rootContext_));
        addRoot((stepString)->asObject(rootContext_));
        
        addRoot((ioModuleString)->asObject(rootContext_));
        addRoot((openString)->asObject(rootContext_));

        addRoot(zeroInteger);
        addRoot(oneInteger);
        
        addRoot(listS->asObject(rootContext_));
        addRoot(dictS->asObject(rootContext_));
        addRoot(tupleS->asObject(rootContext_));
        addRoot(setS->asObject(rootContext_));
        addRoot(intS->asObject(rootContext_));
        addRoot(floatS->asObject(rootContext_));
        addRoot(strS->asObject(rootContext_));
        addRoot(boolS->asObject(rootContext_));
        addRoot(objectS->asObject(rootContext_));
        addRoot(typeS->asObject(rootContext_));
        addRoot(dictString->asObject(rootContext_));
    }
    // Final prototype initialization diagnostic removed
}

const proto::ProtoObject* PythonEnvironment::getGlobals() const {
    if (s_currentGlobals) return s_currentGlobals;
    proto::ProtoContext* ctx = s_threadContext ? s_threadContext : const_cast<PythonEnvironment*>(this)->rootContext_;
    return const_cast<PythonEnvironment*>(this)->resolve("__main__", ctx);
}

int PythonEnvironment::runModuleMain(const std::string& moduleName) {
    proto::ProtoContext* context = s_threadContext ? s_threadContext : rootContext_;
    const proto::ProtoObject* m = resolve(moduleName, context);
    if (m == nullptr || m == PROTO_NONE)
        return -1;
    const proto::ProtoString* mainName = proto::ProtoString::fromUTF8String(context, "main");
    const proto::ProtoObject* mainAttr = m->getAttribute(context, mainName);
    if (mainAttr == nullptr || mainAttr == PROTO_NONE)
        return 0;
    const proto::ProtoList* emptyArgs = context->newList();
    if (mainAttr->isMethod(context)) {
        mainAttr->asMethod(context)(context, const_cast<proto::ProtoObject*>(m), nullptr, emptyArgs, nullptr);
        return 0;
    }
    /* User-defined function: call via __call__ (self = function object). */
    const proto::ProtoString* callName = proto::ProtoString::fromUTF8String(context, "__call__");
    const proto::ProtoObject* callAttr = mainAttr->getAttribute(context, callName);
    if (callAttr && callAttr->asMethod(context)) {
        callAttr->asMethod(context)(context, const_cast<proto::ProtoObject*>(mainAttr), nullptr, emptyArgs, nullptr);
        return 0;
    }
    return 0;
}

int PythonEnvironment::executeString(const std::string& source, const std::string& name) {
    proto::ProtoContext* context = s_threadContext ? s_threadContext : rootContext_;
    ContextScope scope(this, context);
    const proto::ProtoObject* mod = resolve("__main__", context);
    if (mod == nullptr || mod == PROTO_NONE) {
        // Create a dummy __main__ if it doesn't exist
        const proto::ProtoString* mainName = proto::ProtoString::fromUTF8String(context, "__main__");
        mod = builtinsModule->newChild(context, true);
        mod = mod->setAttribute(context, getInternalString(context, "__name__"), mainName->asObject(context));
        mod = mod->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__file__"), context->fromUTF8String(name.c_str()));

        // Add to sys.modules (Step V72)
        const proto::ProtoObject* modules = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "modules"));
        if (modules) {
            const proto::ProtoObject* newModules = modules->setAttribute(context, mainName, mod);
            sysModule = sysModule->setAttribute(context, proto::ProtoString::fromUTF8String(context, "modules"), newModules);
        }
    }

    // Always ensure __builtins__ is available in __main__ globals
    mod = mod->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__builtins__"), builtinsModule);

    int result = 0;
    if (builtinsModule) {
        const proto::ProtoObject* execFn = builtinsModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "exec"));
        if (execFn) {
            const proto::ProtoList* args = context->newList()
                ->appendLast(context, context->fromUTF8String(source.c_str()))
                ->appendLast(context, const_cast<proto::ProtoObject*>(mod));
            execFn->asMethod(context)(context, const_cast<proto::ProtoObject*>(builtinsModule), nullptr, args, nullptr);
            if (hasPendingException()) {
                result = -2;
            }
        }
    }
    return result;
}

int PythonEnvironment::executeModule(const std::string& moduleName, bool asMain, proto::ProtoContext* ctx) {
    SafeImportLock lock(this, ctx);
    if (!ctx) ctx = s_threadContext;
    if (!ctx) ctx = rootContext_;
    ContextScope scope(this, ctx);
    
    // 1. Get/Load module object via ProtoSpace directly to avoid recursion with resolve()
    const proto::ProtoObject* modWrapper = ctx->space->getImportModule(ctx, moduleName.c_str(), "val");
    if (!modWrapper || modWrapper == PROTO_NONE) {
        return -1;
    }
    const proto::ProtoObject* mod = modWrapper->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"));
    
    static const proto::ProtoString* executedKeyS = getExecutedString();
    if (!mod || mod == PROTO_NONE) {
        // If the wrapper exists but val is None, this module doesn't exist.
        // Still mark it as "executed" (or "resolved/failed") to prevent loops if something keeps asking for it.
        // We use the wrapper itself if mod is null, but usually we want to mark the fact that we tried.
        return -1;
    }
    
    if (mod->getAttribute(ctx, executedKeyS) == PROTO_TRUE && !asMain) {
        return 0; // Already done
    }

    if (asMain) {
        const proto::ProtoString* nameS = getInternalString(ctx, "__name__");
        const proto::ProtoObject* mainS = ctx->fromUTF8String("__main__");
        mod = mod->setAttribute(ctx, nameS, mainS);
        const_cast<proto::ProtoObject*>(modWrapper)->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"), mod);
    }
    
    if (modulePrototype && mod->getPrototype(ctx) != modulePrototype) {
        mod = mod->addParent(ctx, modulePrototype);
        mod = mod->setAttribute(ctx, getClassString(), modulePrototype);
    }

    const proto::ProtoString* fileKey = proto::ProtoString::fromUTF8String(ctx, "__file__");
    const proto::ProtoString* executedKey = getExecutedString();
    const proto::ProtoObject* fileObj = mod->getAttribute(ctx, fileKey);
    const proto::ProtoObject* execVal = mod->getAttribute(ctx, executedKey);
    const bool willExec = fileObj && fileObj->isString(ctx) && (!execVal || execVal == PROTO_NONE || execVal == PROTO_FALSE);
    
    // Set executed flag early to prevent double entry/infinite recursion in resolve()
    const_cast<proto::ProtoObject*>(mod)->setAttribute(ctx, executedKey, PROTO_TRUE);
    
    // Set __builtins__ if missing (CRITICAL for module globals)
    if (mod->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__builtins__")) == PROTO_NONE) {
        const_cast<proto::ProtoObject*>(mod)->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__builtins__"), builtinsModule);
    }

    if (willExec) {
        std::string path;
        fileObj->asString(ctx)->toUTF8String(ctx, path);
        if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".py") == 0) {
            std::ifstream f(path);
            if (f) {
                std::stringstream buf;
                buf << f.rdbuf();
                std::string source = buf.str();
                f.close();
                bool diagnostics = std::getenv("PROTO_ENV_DIAG") != nullptr;
                if (diagnostics) {
                }
                Parser parser(source);
                std::unique_ptr<ModuleNode> node = parser.parseModule();
                if (diagnostics) {
                }
                if (!parser.hasError() && node) {
                    if (diagnostics) {
                    }
                    Compiler compiler(ctx, path);
                    bool compileOk = compiler.compileModule(node.get());
                    if (diagnostics) {
                    }
                    if (compileOk) {
                        if (diagnostics) {
                        }
                        const proto::ProtoObject* codeObj = makeCodeObject(ctx, compiler.getConstants(), compiler.getNames(), compiler.getBytecode(), ctx->fromUTF8String(path.c_str())->asString(ctx), nullptr, 0, 0, 0, 0, false, ctx->fromUTF8String("<module>")->asString(ctx), compiler.getFirstLine(), compiler.getLnotab());
                        if (codeObj) {
                            proto::ProtoObject* mutableMod = const_cast<proto::ProtoObject*>(mod);
                            // Modules no longer inherit from dictPrototype (CPython modules are not dicts)
                            // if (dictPrototype) {
                            if (modulePrototype) {
                                mutableMod = const_cast<proto::ProtoObject*>(mutableMod->addParent(ctx, modulePrototype));
                                // Set __class__ explicitly so type(mod) is <class 'module'>, not <class 'type'>
                                mutableMod->setAttribute(ctx, getClassString(), modulePrototype);
                            }
                            // }
                            if (get_env_diag()) { printf("DEBUG: executeModule initDictStorage modulePrototype=%p\n", (void*)modulePrototype); }
                            initDictStorage(ctx, mutableMod);
                            if (modulePrototype) {
                                if (get_env_diag()) { printf("DEBUG: executeModule setting __class__\n"); }
                                mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, getClassString(), modulePrototype));
                            }
                            
                            // Batch 1: Set frame attributes on module object
                            if (get_env_diag()) { printf("DEBUG: executeModule setting frame attributes\n"); }
                            mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, getFBackString(), PythonEnvironment::getCurrentFrame()));
                            if (get_env_diag()) { printf("DEBUG: executeModule setting f_code\n"); }
                            mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, getFCodeString(), codeObj));
                            mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, getFGlobalsString(), mutableMod));
                            mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, getFLocalsString(), mutableMod));

                            const proto::ProtoObject* oldGlobals = getCurrentGlobals();
                            setCurrentGlobals(mutableMod);
                            
                            // Update sys.modules BEFORE execution to handle cyclic imports (CPython behavior)
                            if (sysModule) {
                                const proto::ProtoObject* mods = sysModule->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modules"));
                                if (mods && mods != PROTO_NONE) {
                                    const proto::ProtoString* modNameS = proto::ProtoString::fromUTF8String(ctx, moduleName.c_str());
                                    // 1. Set as attribute (internal lookup fallback)
                                    mods->setAttribute(ctx, modNameS, mutableMod);
                                    
                                    // 2. Set as dict item (Python-side lookup)
                                    const proto::ProtoObject* dataAttr = mods->getAttribute(ctx, getDataString());
                                    if (dataAttr && dataAttr != PROTO_NONE) {
                                        const proto::ProtoSparseList* dict = dataAttr->asSparseList(ctx);
                                        if (dict) {
                                            const proto::ProtoSparseList* newDict = dict->setAt(ctx, modNameS->getHash(ctx), mutableMod);
                                            mods->setAttribute(ctx, getDataString(), newDict->asObject(ctx));
                                        }
                                    }
                                }
                            }

                            s_threadResolveCache[moduleName] = mutableMod;
                            const proto::ProtoObject* oldMod = mutableMod;
                            runCodeObject(ctx, codeObj, mutableMod);
                            
                            setCurrentGlobals(oldGlobals);
                            mod = mutableMod;
                            // Re-cache if it changed
                            if (oldMod != mutableMod) {
                                s_threadResolveCache[moduleName] = mutableMod;
                            }
                            const proto::ProtoObject* excAfterExec = peekPendingException();
                            if (excAfterExec && excAfterExec != PROTO_NONE) {
                                if (asMain) {
                                    std::cerr << "protopy: unhandled exception in module execution:\n" 
                                              << formatException(excAfterExec, nullptr) << std::endl;
                                    clearPendingException();
                                }
                                return -2;
                            }
                            // Update the wrapper's "val" so future resolves for this module see it as populated
                            const_cast<proto::ProtoObject*>(modWrapper)->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"), mod);
                        }
                    } else {
                        std::cerr << "protopy: compilation error in '" << path << "'\n";
                        return -2;
                    }
                } else if (parser.hasError()) {
                    std::cerr << "protopy: syntax error in '" << path << "': " << parser.getLastErrorMsg() << " at line " << parser.getLastErrorLine() << ":" << parser.getLastErrorColumn() << "\n";
                    return -1;
                }
            }
        }
    }
    

    if (executionHook) executionHook(moduleName, 0);
    {
        const proto::ProtoObject* tf = getTraceFunction();
        if (tf) {
            const proto::ProtoObject* callAttr = tf->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"));
            if (callAttr && callAttr->asMethod(ctx)) {
                const proto::ProtoList* args = ctx->newList()
                    ->appendLast(ctx, PROTO_NONE)
                    ->appendLast(ctx, ctx->fromUTF8String("call"))
                    ->appendLast(ctx, PROTO_NONE);
                callAttr->asMethod(ctx)(ctx, tf, nullptr, args, nullptr);
            }
        }
    }

    int ret = runModuleMain(moduleName);
    if (ret != 0) {
        if (executionHook) executionHook(moduleName, 1);
        return ret == -1 ? -1 : -2;
    }

    const proto::ProtoObject* exc = peekPendingException();
    if (exc) {
        if (executionHook) executionHook(moduleName, 1);
        if (asMain) {
            clearPendingException(); // only clear if running as main
        }
        return -2;
    }

    {
        const proto::ProtoObject* tf = getTraceFunction();
        if (tf) {
            const proto::ProtoObject* callAttr = tf->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"));
            if (callAttr && callAttr->asMethod(ctx)) {
                const proto::ProtoList* args = ctx->newList()
                    ->appendLast(ctx, PROTO_NONE)
                    ->appendLast(ctx, ctx->fromUTF8String("return"))
                    ->appendLast(ctx, PROTO_NONE);
                callAttr->asMethod(ctx)(ctx, tf, nullptr, args, nullptr);
            }
        }
    }
    if (executionHook) executionHook(moduleName, 1);
    runExitHandlers();
    return exitRequested_ != 0 ? -3 : 0;
}

void PythonEnvironment::runExitHandlers() {
    proto::ProtoContext* ctx = s_threadContext ? s_threadContext : rootContext_;
    const proto::ProtoObject* atexitMod = resolve("atexit", ctx);
    if (!atexitMod || atexitMod == PROTO_NONE) return;
    const proto::ProtoObject* runFn = atexitMod->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_run_exitfuncs"));
    if (!runFn) return;
    const proto::ProtoList* emptyArgs = ctx->newList();
    protoPython::invokePythonCallable(ctx, runFn, emptyArgs, nullptr);
}

#include <signal.h>

std::atomic<bool> PythonEnvironment::s_sigintReceived{false};

static void sigint_handler(int) {
    PythonEnvironment::s_sigintReceived.store(true);
}

std::string PythonEnvironment::formatTraceback(const proto::ProtoContext* ctx) {
    if (!ctx) ctx = s_threadContext ? s_threadContext : rootContext_;
    proto::ProtoContext* nonConstCtx = const_cast<proto::ProtoContext*>(ctx);
    std::string out = "Traceback (most recent call last):\n";
    
    // Walk the frame stack using f_back
    const proto::ProtoObject* frame = getCurrentFrame();
    std::vector<const proto::ProtoObject*> frames;
    while (frame && frame != PROTO_NONE) {
        frames.push_back(frame);
        frame = frame->getAttribute(nonConstCtx, getFBackString());
        if (frames.size() > 50) break; // Safety limit
    }
    
    // Process frames in reverse (oldest first)
    std::reverse(frames.begin(), frames.end());
    
    for (const auto* f : frames) {
        std::string filename = "<unknown>";
        std::string funcName = "<module>";
        int lineno = 0;
        
        const proto::ProtoObject* codeObj = f->getAttribute(nonConstCtx, getFCodeString());
        if (codeObj && codeObj != PROTO_NONE) {
            const proto::ProtoObject* fileObj = codeObj->getAttribute(nonConstCtx, getCoFilenameString());
            if (fileObj && fileObj->isString(nonConstCtx)) {
                fileObj->asString(nonConstCtx)->toUTF8String(nonConstCtx, filename);
            }
            
            const proto::ProtoObject* nameObj = codeObj->getAttribute(nonConstCtx, proto::ProtoString::fromUTF8String(nonConstCtx, "co_name"));
            if (nameObj && nameObj->isString(nonConstCtx)) {
                nameObj->asString(nonConstCtx)->toUTF8String(nonConstCtx, funcName);
            }
            // we don't extract line numbers correctly yet from frame objects here, but we can set up the structure
        }
        
        if (filename == "<unknown>") {
            if (ctx->currentFileName) filename = ctx->currentFileName;
            lineno = ctx->currentLineNumber;
        }

        std::string lineStr = lineno > 0 ? " (line " + std::to_string(lineno) + ")" : "";
        out += "  File \"" + filename + "\"" + lineStr + ", in " + funcName + "\n";
    }
    return out;
}

std::vector<std::string> PythonEnvironment::collectCandidates(const proto::ProtoObject* frame, const proto::ProtoObject* targetObj) {
    proto::ProtoContext* context = s_threadContext ? s_threadContext : rootContext_;
    std::unordered_set<std::string> uniqueCandidates = {
        "print", "len", "range", "str", "int", "float", "list", "dict", "tuple", "set", "exit", "quit", "help", "input", "eval", "exec", "open",
        "append", "extend", "insert", "remove", "pop", "clear", "index", "count", "sort", "reverse", "copy",
        "keys", "values", "items", "get", "update", "split", "join", "strip", "replace", "find", "lower", "upper",
        "True", "False", "None"
    };
    
    auto collectFromObj = [&](const proto::ProtoObject* obj) {
        if (!obj || obj == PROTO_NONE) return;
        
        std::vector<const proto::ProtoObject*> worklist = {obj};
        std::unordered_set<const proto::ProtoObject*> seen;
        int depth = 0;
        
        while (!worklist.empty() && depth < 3) {
            std::vector<const proto::ProtoObject*> nextLayer;
            for (const auto* current : worklist) {
                if (!current || !seen.insert(current).second) continue;
                
                const proto::ProtoSparseList* attrs = current->getOwnAttributes(context);
                if (attrs) {
                    auto* it = const_cast<proto::ProtoSparseListIterator*>(attrs->getIterator(context));
                    while (it && it->hasNext(context)) {
                        unsigned long key = it->nextKey(context);
                        const proto::ProtoString* s = reinterpret_cast<const proto::ProtoObject*>(key)->asString(context);
                        if (s) {
                            std::string name;
                            s->toUTF8String(context, name);
                            if (!name.empty() && name[0] != '_') uniqueCandidates.insert(name);
                        }
                        it = const_cast<proto::ProtoSparseListIterator*>(it->advance(context));
                    }
                }
                
                const proto::ProtoList* parents = current->getParents(context);
                if (parents) {
                    for (unsigned long i = 0; i < parents->getSize(context); ++i) {
                        nextLayer.push_back(parents->getAt(context, static_cast<int>(i)));
                    }
                }
            }
            worklist = std::move(nextLayer);
            depth++;
        }
    };

    if (targetObj) {
        collectFromObj(targetObj);
    } else {
        collectFromObj(builtinsModule);
        collectFromObj(frame);
        collectFromObj(getCurrentGlobals());
    }
    
    return std::vector<std::string>(uniqueCandidates.begin(), uniqueCandidates.end());
}

void PythonEnvironment::handleException(const proto::ProtoObject* exc, const proto::ProtoObject* frame, std::ostream& out) {
    if (!exc) return;
    proto::ProtoContext* context = s_threadContext ? s_threadContext : rootContext_;
    
    // Step 1335: sys.last_type, sys.last_value, sys.last_traceback
    const proto::ProtoObject* type = exc->getAttribute(context, getInternalString(context, "__class__"));
    if (sysModule) {
        sysModule->setAttribute(context, proto::ProtoString::fromUTF8String(context, "last_type"), type ? type : PROTO_NONE);
        sysModule->setAttribute(context, proto::ProtoString::fromUTF8String(context, "last_value"), const_cast<proto::ProtoObject*>(exc));
        sysModule->setAttribute(context, proto::ProtoString::fromUTF8String(context, "last_traceback"), PROTO_NONE);
    }

    // Step 1340: sys.excepthook
    if (sysModule) {
        const proto::ProtoObject* hook = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "excepthook"));
        if (hook && hook != PROTO_NONE && hook->asMethod(context)) {
            const proto::ProtoList* args = context->newList()
                ->appendLast(context, type ? type : PROTO_NONE)
                ->appendLast(context, const_cast<proto::ProtoObject*>(exc))
                ->appendLast(context, PROTO_NONE); // traceback
            hook->asMethod(context)(context, const_cast<proto::ProtoObject*>(sysModule), nullptr, args, nullptr);
            return;
        }
    }

    std::string typeName = "Exception";
    if (type) {
        const proto::ProtoObject* nameObj = type->getAttribute(context, getInternalString(context, "__name__"));
        if (nameObj && nameObj->isString(context)) nameObj->asString(context)->toUTF8String(context, typeName);
    }

    if (typeName == "SystemExit") {
        const proto::ProtoObject* codeObj = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "code"));
        int code = (codeObj && codeObj->isInteger(context)) ? static_cast<int>(codeObj->asLong(context)) : 0;
        setExitRequested(code);
        return;
    }

    out << formatException(exc, frame) << std::flush;
}


std::string PythonEnvironment::formatException(const proto::ProtoObject* exc, const proto::ProtoObject* frame) {
    if (!exc) return "";
    proto::ProtoContext* context = s_threadContext ? s_threadContext : rootContext_;

    std::string reset = "\033[0m";
    std::string bold = "\033[1m";
    std::string red = "\033[91m";
    std::string blue = "\033[94m";
    std::string yellow = "\033[93m";

    const char* noColorEnv = std::getenv("NO_COLOR");
    if (noColorEnv || !isatty(fileno(stderr))) {
        reset = bold = red = blue = yellow = "";
    }

    std::string out;

    // Step 1334: Exception Chaining (__cause__ and __context__)
    const proto::ProtoObject* cause = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__cause__"));
    if (cause && cause != PROTO_NONE) {
        out += formatException(cause, frame) + "\nThe above exception was the direct cause of the following exception:\n\n";
    } else {
        const proto::ProtoObject* context_exc = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__context__"));
        if (context_exc && context_exc != PROTO_NONE) {
            out += formatException(context_exc, frame) + "\nDuring handling of the above exception, another exception occurred:\n\n";
        }
    }

    const proto::ProtoObject* py_class = exc->getAttribute(context, getInternalString(context, "__class__"));
    const proto::ProtoObject* py_name = py_class ? py_class->getAttribute(context, getInternalString(context, "__name__")) : nullptr;
    
    std::string typeName = "Exception";
    if (py_name && py_name->isString(context)) {
        py_name->asString(context)->toUTF8String(context, typeName);
    }

    std::string msg;
    const proto::ProtoObject* argsObj = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "args"));
    if (argsObj && argsObj->isTuple(context)) {
        const proto::ProtoTuple* args = argsObj->asTuple(context);
        if (args->getSize(context) > 0) {
            const proto::ProtoObject* first = args->getAt(context, 0);
            if (first && first->isString(context)) {
                first->asString(context)->toUTF8String(context, msg);
            }
        }
    }

    // Prepend Traceback (Step 1329)
    if (typeName != "SyntaxError" && typeName != "KeyboardInterrupt" && typeName != "SystemExit") {
        out += formatTraceback(context);
    }

    // Header: Type: Message
    out += red + bold + typeName + reset + ": " + msg + "\n";

    // Step 1333: Multi-line Error Context (REPL virtual file state)
    if (isInteractive_ && !replHistory_.empty()) {
        const proto::ProtoObject* linenoObj = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "lineno"));
        if (linenoObj && linenoObj->isInteger(context)) {
            int lineIdx = static_cast<int>(linenoObj->asLong(context)) - 1;
            if (lineIdx >= 0 && static_cast<size_t>(lineIdx) < replHistory_.size()) {
                out += "  File \"<stdin>\", line " + std::to_string(lineIdx + 1) + "\n";
                out += "    " + replHistory_[lineIdx] + "\n";
            }
        }
    }

    // Suggestions (Step 1327)
    if (typeName == "NameError" || typeName == "AttributeError") {
        std::string target;
        const proto::ProtoObject* targetObj = nullptr;
        if (typeName == "NameError") {
            const proto::ProtoObject* nameObj = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "name"));
            if (nameObj && nameObj->isString(context)) nameObj->asString(context)->toUTF8String(context, target);
        } else {
            const proto::ProtoObject* nameObj = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "name"));
            if (nameObj && nameObj->isString(context)) nameObj->asString(context)->toUTF8String(context, target);
            targetObj = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "obj"));
        }

        if (!target.empty()) {
            std::vector<std::string> candidates = collectCandidates(frame, targetObj);
            std::string best;
            int bestDist = 100;
            for (const auto& c : candidates) {
                int d = levenshtein_distance(target, c);
                if (d < bestDist && d <= 2) {
                    bestDist = d;
                    best = c;
                }
            }
            if (!best.empty()) {
                out += yellow + "Did you mean: '" + best + "'?" + reset + "\n";
            }
        }
    }

    // Step 1326: Line pointers (SyntaxError context)
    const proto::ProtoObject* linenoObj = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "lineno"));
    const proto::ProtoObject* textObj = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "text"));
    const proto::ProtoObject* offsetObj = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "offset"));
    
    if (typeName == "SyntaxError" && linenoObj && textObj && offsetObj) {
        std::string line;
        textObj->asString(context)->toUTF8String(context, line);
        int offset = static_cast<int>(offsetObj->asLong(context));
        out += "  File \"<stdin>\", line " + std::to_string(linenoObj->asLong(context)) + "\n";
        out += "    " + line + "\n";
        out += "    " + std::string(offset > 0 ? offset : 0, ' ') + "^\n";
    }

    return out;
}

void PythonEnvironment::runRepl(std::istream& in, std::ostream& out) {
    proto::ProtoContext* context = s_threadContext ? s_threadContext : rootContext_;
    if (!context || !builtinsModule) return;
    ContextScope scope(this, context);
    setInteractive(true);
    
    // Install SIGINT handler (Step 1310)
    auto oldHandler = signal(SIGINT, sigint_handler);
    
    const proto::ProtoString* py_eval = proto::ProtoString::fromUTF8String(context, "eval");
    const proto::ProtoString* py_exec = proto::ProtoString::fromUTF8String(context, "exec");
    const proto::ProtoString* py_repr = proto::ProtoString::fromUTF8String(context, "repr");
    const proto::ProtoObject* evalFn = builtinsModule->getAttribute(context, py_eval);
    const proto::ProtoObject* execFn = builtinsModule->getAttribute(context, py_exec);
    const proto::ProtoObject* reprFn = builtinsModule->getAttribute(context, py_repr);
    
    if (!evalFn || !execFn || !reprFn) {
        signal(SIGINT, oldHandler);
        return;
    }
    
    // Step 1319: sys.ps1 and sys.ps2
    if (sysModule) {
        sysModule->setAttribute(context, proto::ProtoString::fromUTF8String(context, "ps1"), context->fromUTF8String(primaryPrompt_.c_str()));
        sysModule->setAttribute(context, proto::ProtoString::fromUTF8String(context, "ps2"), context->fromUTF8String(secondaryPrompt_.c_str()));
    }
    
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(context->newObject(true));
    out << "protoPython 0.1.0 (" << __DATE__ << ") [HPy Integrated]\n"
        << "Type \"help\", \"copyright\", \"credits\" or \"license\" for more information.\n";
        
    // Step 1429: History persistence
    std::string historyDir = std::getenv("HOME") ? std::getenv("HOME") : ".";
    std::string historyPath = historyDir + "/.protopy_history";
    {
        std::ifstream hf(historyPath);
        if (hf) {
            std::string hline;
            while (std::getline(hf, hline)) {
                if (!hline.empty()) replHistory_.push_back(hline + "\n");
            }
        }
    }
    std::ofstream historyOut(historyPath, std::ios::app);
    if (!historyOut) {
        // log removed
    } else {
        // log removed
    }
        
    std::string buffer;
    std::string line;
    std::string currentIndent = "";

    // Step 1348: PROTOPYSTARTUP support (Move after execFn is ready)
    const char* startup = std::getenv("PROTOPYSTARTUP");
    if (startup && startup[0] != '\0') {
        std::ifstream f(startup);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            std::string src = ss.str();
            f.close();
            const proto::ProtoList* startupArgs = context->newList()->appendLast(context, context->fromUTF8String(src.c_str()))->appendLast(context, frame);
            execFn->asMethod(context)(context, const_cast<proto::ProtoObject*>(builtinsModule), nullptr, startupArgs, nullptr);
            if (const proto::ProtoObject* startupExc = takePendingException()) {
                handleException(startupExc, frame, out);
            }
        }
    }
    
    while (true) {
        if (s_sigintReceived.exchange(false)) {
            out << "KeyboardInterrupt\n";
            buffer.clear();
            currentIndent = "";
        }
        
        // Refresh prompts from sys if they exist (Step 1319)
        if (sysModule) {
            auto ps1Obj = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "ps1"));
            if (ps1Obj && ps1Obj->isString(context)) ps1Obj->asString(context)->toUTF8String(context, primaryPrompt_);
            auto ps2Obj = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "ps2"));
            if (ps2Obj && ps2Obj->isString(context)) ps2Obj->asString(context)->toUTF8String(context, secondaryPrompt_);
        }

        out << (buffer.empty() ? primaryPrompt_ : secondaryPrompt_) << std::flush;
        
        if (!std::getline(in, line)) {
            if (in.eof()) break;
            in.clear();
            continue;
        }
        
        if (line == "exit()" || line == "quit()") break;
        
        // Step 1354: System Commands
        if (!line.empty() && line[0] == '!') {
            std::string cmd = line.substr(1);
            if (!cmd.empty()) {
                int status = std::system(cmd.c_str());
                (void)status;
            }
            continue;
        }

        // Step 1353: Clear Screen
        if (line == "%clear" || line == "clear()") {
            out << "\x1b[2J\x1b[H" << std::flush;
            continue;
        }

        // Step 1356: History View
        if (line == "%history") {
            for (size_t i = 0; i < replHistory_.size(); ++i) {
                out << i << ": " << replHistory_[i];
            }
            continue;
        }

        // Step 1433: %complete magic
        if (line.compare(0, 10, "%complete ") == 0) {
            std::string prefix = line.substr(10);
            const proto::ProtoObject* completeFn = builtinsModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "_complete"));
            if (completeFn && completeFn->asMethod(context)) {
                const proto::ProtoList* args = context->newList()->appendLast(context, context->fromUTF8String(prefix.c_str()))->appendLast(context, frame);
                const proto::ProtoObject* resp = completeFn->asMethod(context)(context, const_cast<proto::ProtoObject*>(builtinsModule), nullptr, args, nullptr);
                if (resp && resp->asList(context)) {
                    const proto::ProtoList* rl = resp->asList(context);
                    for (unsigned long i = 0; i < rl->getSize(context); ++i) {
                        std::string n;
                        rl->getAt(context, static_cast<int>(i))->asString(context)->toUTF8String(context, n);
                        out << n << " ";
                    }
                    out << "\n";
                }
            }
            continue;
        }
        
        // Step 1349: %debug magic
        if (line == "%debug") {
            if (sysModule) {
                const proto::ProtoObject* lastExc = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "last_value"));
                if (lastExc && lastExc != PROTO_NONE) {
                    out << "Post-mortem debugger (stub):\n";
                    out << formatException(lastExc, frame) << "\n";
                } else {
                    out << "No exception to debug.\n";
                }
            }
            continue;
        }
        
        // Step 1315: Empty Line Handling
        if (buffer.empty() && line.empty()) continue;
        
        buffer += line + "\n";
        
        if (!isCompleteBlock(buffer)) {
            continue;
        }
        
        currentIndent = "";
        
        // Step 1311: History (Simple in-memory for now)
        replHistory_.push_back(buffer);
        if (historyOut) {
            historyOut << buffer;
            if (buffer.back() != '\n') historyOut << "\n";
            historyOut.flush();
        }
        
        const proto::ProtoObject* source = context->fromUTF8String(buffer.c_str());
        const proto::ProtoList* args = context->newList()->appendLast(context, source)->appendLast(context, frame)->appendLast(context, frame);
        const proto::ProtoObject* result = PROTO_NONE;

        if (std::getenv("PROTO_REPL_DIAG")) {
            // log removed
        }
        
        try {
            // Check for pending exceptions before execution
            if (const proto::ProtoObject* pending = takePendingException()) {
                handleException(pending, frame, out);
            }

            result = evalFn->asMethod(context)(context, const_cast<proto::ProtoObject*>(builtinsModule), nullptr, args, nullptr);
            
            const proto::ProtoObject* pending = takePendingException();
            if (pending && pending != PROTO_NONE) {
                // If eval failed with SyntaxError, try exec. Otherwise report and stop.
                std::string typeName = "Exception";
                const proto::ProtoObject* type = pending->getAttribute(context, getInternalString(context, "__class__"));
                if (type) {
                    const proto::ProtoObject* nameObj = type->getAttribute(context, getInternalString(context, "__name__"));
                    if (nameObj && nameObj->isString(context)) nameObj->asString(context)->toUTF8String(context, typeName);
                }

                if (typeName == "SyntaxError") {
                    // Try exec
                    execFn->asMethod(context)(context, const_cast<proto::ProtoObject*>(builtinsModule), nullptr, args, nullptr);
                    if (const proto::ProtoObject* execPending = takePendingException()) {
                        handleException(execPending, frame, out);
                    }
                } else {
                    handleException(pending, frame, out);
                }
            } else if (result && result != PROTO_NONE) {
                const proto::ProtoList* reprArgs = context->newList()->appendLast(context, result);
                const proto::ProtoObject* reprResult = reprFn->asMethod(context)(context, const_cast<proto::ProtoObject*>(builtinsModule), nullptr, reprArgs, nullptr);
                if (reprResult && reprResult->isString(context)) {
                    std::string s;
                    reprResult->asString(context)->toUTF8String(context, s);
                    out << s << "\n";
                }
            }
        } catch (const proto::ProtoObject* e) {
            handleException(e, frame, out);
        } catch (const std::overflow_error& e) {
            raiseRecursionError(context);
            if (const proto::ProtoObject* pending = takePendingException()) handleException(pending, frame, out);
        } catch (const std::exception& e) {
            const char* RED = std::getenv("NO_COLOR") ? "" : "\x1b[31;1m";
            const char* RESET = std::getenv("NO_COLOR") ? "" : "\x1b[0m";
            out << RED << "Runtime Error: " << e.what() << RESET << "\n";
        } catch (...) {
            const char* RED = std::getenv("NO_COLOR") ? "" : "\x1b[31;1m";
            const char* RESET = std::getenv("NO_COLOR") ? "" : "\x1b[0m";
            out << RED << "Internal Runtime Error" << RESET << "\n";
        }
        
        buffer.clear();
    }
    
    signal(SIGINT, oldHandler);
    runExitHandlers();
}

void PythonEnvironment::incrementSysStats(const char* key) {
    if (!sysModule) return;
    proto::ProtoContext* context = s_threadContext ? s_threadContext : rootContext_;
    const proto::ProtoObject* stats = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "stats"));
    if (!stats) return;
    const proto::ProtoObject* val = stats->getAttribute(context, proto::ProtoString::fromUTF8String(context, key));
    long long n = (val && val->isInteger(context)) ? val->asLong(context) : 0;
    const proto::ProtoObject* newStats = stats->setAttribute(context, proto::ProtoString::fromUTF8String(context, key), context->fromInteger(n + 1));
    sysModule = sysModule->setAttribute(context, proto::ProtoString::fromUTF8String(context, "stats"), newStats);
}

void PythonEnvironment::enableDefaultTrace() {
    if (!sysModule) return;
    proto::ProtoContext* context = s_threadContext ? s_threadContext : rootContext_;
    const proto::ProtoObject* def = sysModule->getAttribute(context, proto::ProtoString::fromUTF8String(context, "_trace_default"));
    if (def && def != PROTO_NONE) setTraceFunction(def);
}

void PythonEnvironment::invalidateResolveCache() {
    resolveCacheGeneration_.fetch_add(1, std::memory_order_release);
}

bool PythonEnvironment::isCompleteBlock(const std::string& code) {
    if (code.empty()) return true;
    int p = 0, s = 0, c = 0;
    bool inQuote = false;
    bool inTripleQuote = false;
    char quoteChar = 0;
    bool seenTopLevelColon = false;
    bool hasPendingBackslash = false;
    
    for (size_t i = 0; i < code.size(); ++i) {
        char ch = code[i];
        
        if (inTripleQuote) {
            if (ch == quoteChar && i + 2 < code.size() && code[i+1] == quoteChar && code[i+2] == quoteChar) {
                inTripleQuote = false;
                i += 2;
            }
        } else if (inQuote) {
            if (ch == quoteChar && (i == 0 || code[i-1] != '\\')) inQuote = false;
        } else {
            if (ch == '\\') {
                // Simple heuristic: if we see a backslash, assume it's a continuation 
                // until we see a newline or non-whitespace.
                hasPendingBackslash = true;
            } else if ((ch == '\'' || ch == '"') && i + 2 < code.size() && code[i+1] == ch && code[i+2] == ch) {
                inTripleQuote = true;
                quoteChar = ch;
                i += 2;
                hasPendingBackslash = false;
            } else if (ch == '\'' || ch == '"') {
                inQuote = true;
                quoteChar = ch;
                hasPendingBackslash = false;
            } else if (ch == '(') { p++; hasPendingBackslash = false; }
            else if (ch == ')') { p--; hasPendingBackslash = false; }
            else if (ch == '[') { s++; hasPendingBackslash = false; }
            else if (ch == ']') { s--; hasPendingBackslash = false; }
            else if (ch == '{') { c++; hasPendingBackslash = false; }
            else if (ch == '}') { c--; hasPendingBackslash = false; }
            else if (ch == '#' && !inQuote && !inTripleQuote) {
                // Skip comment until end of line
                while (i < code.size() && code[i] != '\n') i++;
            }
            else if (ch == ':' && p == 0 && s == 0 && c == 0) {
                // Check if this : is at the end of a line (started a compound block)
                bool foundOnLine = false;
                for (size_t j = i + 1; j < code.size(); ++j) {
                    if (code[j] == '\n') break;
                    if (!isspace(code[j]) && code[j] != '#') {
                        foundOnLine = true;
                        break;
                    }
                }
                if (!foundOnLine) seenTopLevelColon = true;
                hasPendingBackslash = false;
            } else if (ch == '\n') {
                // Keep hasPendingBackslash true if it was set on this line
            } else if (!isspace(ch)) {
                hasPendingBackslash = false;
            }
        }
    }
    
    if (inTripleQuote || inQuote || hasPendingBackslash) return false;
    if (p > 0 || s > 0 || c > 0) return false;
    
    size_t last = code.find_last_not_of(" \n\r\t");
    if (last != std::string::npos && code[last] == ':') return false;
    
    // If we started a block (top-level colon followed by newline), 
    // we require an empty line to terminate the block in the REPL.
    if (seenTopLevelColon) {
        if (code.size() < 2 || code[code.size() - 2] != '\n') {
            return false;
        }
    }
    
    return true;
}

const proto::ProtoObject* PythonEnvironment::getType(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj) return nullptr;
    if (obj == PROTO_NONE) return getNoneTypePrototype();
    
    // Priority 1: Explicit __class__ attribute (avoids recursion)
    const proto::ProtoString* classS = getClassString() ? getClassString() : proto::ProtoString::fromUTF8String(ctx, "__class__");
    if (obj->hasOwnAttribute(ctx, classS) == PROTO_TRUE) {
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: getType priority 1 __class__ match\n");
        return obj->getAttribute(ctx, classS);
    }

    // Priority 2: Look for the structural parent marked as a Python class (has its own __class__)
    const proto::ProtoList* parents = obj->getParents(ctx);
    if (parents) {
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: getType checking %lu parents\n", parents->getSize(ctx));
        for (size_t i = 0; i < parents->getSize(ctx); ++i) {
            const proto::ProtoObject* p = parents->getAt(ctx, i);
            if (p && p->hasOwnAttribute(ctx, classS) == PROTO_TRUE) {
                if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: getType returning parent %lu as class\n", i);
                return p;
            }
        }
        if (parents->getSize(ctx) == 1) {
            if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: getType 1 parent fallback\n");
            return parents->getAt(ctx, 0);
        }
    }
    
    // Priority 3: Fallback lookup
    const proto::ProtoObject* proto = obj->getPrototype(ctx);
    if (proto) {
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: getType proto fallback\n");
        return proto;
    }
    
    if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: getType fallback attribute lookup\n");
    return getAttribute(ctx, obj, classS);
}

const proto::ProtoObject* PythonEnvironment::getAttribute(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* name) {
    if (!obj || !name) return nullptr;
    
    // Recursion Guard for binding logic
    static thread_local int getAttrDepth = 0;
    if (getAttrDepth > 20) {
        if (std::getenv("PROTO_ENV_DIAG")) fprintf(stderr, "DEBUG: Env::getAttribute RECURSION LIMIT depth=%d\n", getAttrDepth);
        return obj->getAttribute(ctx, name);
    }
    getAttrDepth++;
    
    if (obj == PROTO_NONE) {
        if (noneTypeProto) {
            getAttrDepth--;
            return noneTypeProto->getAttribute(ctx, name);
        }
        getAttrDepth--;
        return nullptr;
    }
    
    bool isClass = false;
    const proto::ProtoString* isPyClassS = proto::ProtoString::fromUTF8String(ctx, "__is_python_class__");
    if (obj->hasOwnAttribute(ctx, isPyClassS) == PROTO_TRUE) {
        isClass = true;
    }
    
    bool foundOnClassOrMro = false;
    
    // 1. Get the raw value from the primitive object hierarchy
    const proto::ProtoObject* val = obj->getAttribute(ctx, name);
    if (val && val != PROTO_NONE) {
        if (!isClass) {
            foundOnClassOrMro = true;
        } else {
            if (obj->hasOwnAttribute(ctx, name) == PROTO_TRUE) {
                foundOnClassOrMro = true;
            } else {
                const proto::ProtoObject* mroObj = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__mro__"));
                if (mroObj) {
                    const proto::ProtoList* mroList = mroObj->asList(ctx);
                    if (mroList) {
                        for (unsigned long i = 0; i < mroList->getSize(ctx); ++i) {
                            const proto::ProtoObject* baseCls = mroList->getAt(ctx, i);
                            if (baseCls && baseCls->hasOwnAttribute(ctx, name) == PROTO_TRUE) {
                                foundOnClassOrMro = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    
    if (!val || val == PROTO_NONE) {
        // Fallback logic starts here
    }


    
    // 1.1 MRO Lookup (Inheritance Prototype Chain)
    // For classes, we must search the MRO (base classes) BEFORE looking into the metaclass (`__class__`).
    // For regular objects, `getParents` is usually empty natively, so this resolves quickly.
    if (!val || val == PROTO_NONE) {
        const proto::ProtoList* mro = obj->getParents(ctx);
        if (mro) {
            for (size_t i = 0; i < mro->getSize(ctx); ++i) {
                const proto::ProtoObject* baseCls = mro->getAt(ctx, i);
                val = baseCls->proto::ProtoObject::getAttribute(ctx, name);
                if (val && val != PROTO_NONE) {
                    foundOnClassOrMro = true;
                    break;
                }
            }
        }
    }

    // 1.2 Metaclass / Class Lookup Fallback
    // If not found on the object itself or via its MRO, check the object's class natively (`type(obj)`).
    if (!val || val == PROTO_NONE) {
        const proto::ProtoString* clsS = this->getClassString();
        if (clsS) {
            const proto::ProtoObject* cls = obj->getAttribute(ctx, clsS);
            if (std::getenv("PROTO_RESOLVE_DIAG")) {
                 fprintf(stderr, "DEBUG: getAttribute checking obj's %p __class__ = %p (== PROTO_NONE: %d)\n", (void*)obj, (void*)cls, cls == PROTO_NONE);
            }
            if (cls && cls != PROTO_NONE && cls != obj) {
                // Direct lookup on the class itself
                val = cls->proto::ProtoObject::getAttribute(ctx, name);
                
                // Fallback: check parents (MRO) of the class
                if (!val || val == PROTO_NONE) {
                    const proto::ProtoList* clsMro = cls->getParents(ctx);
                    if (clsMro) {
                        for (size_t i = 0; i < clsMro->getSize(ctx); ++i) {
                            const proto::ProtoObject* baseCls = clsMro->getAt(ctx, i);
                            val = baseCls->proto::ProtoObject::getAttribute(ctx, name);
                            if (val && val != PROTO_NONE) {
                                break;
                            }
                        }
                    }
                }
            }
        }
    }


    // 1.5 Descriptor Protocol Check (__get__)
    // If the attribute found (val) has a __get__ method, invoke it.
    // This allows properties, methods, and other descriptors to work correctly.
    // Note: We skip this for method cells that are handled by specialized binding logic later,
    // unless those method cells specifically have __get__ (which they usually don't in current protoCore).
    const proto::ProtoString* dunderGet = this->getGetDunderString();
    if (dunderGet && val->getAttribute(ctx, dunderGet)) {
        // Do not bind if obj is a module! Modules are namespaces, not classes.
        const proto::ProtoObject* objClass = obj->getAttribute(ctx, this->getClassString());
        if (!this->modulePrototype || (obj != this->modulePrototype && objClass != this->modulePrototype)) {
            const proto::ProtoObject* getM = val->getAttribute(ctx, dunderGet);
            if (getM && getM->isMethod(ctx)) {
                 const proto::ProtoObject* instance = obj;
                 const proto::ProtoObject* owner = obj->getAttribute(ctx, this->getClassString());
                 
                 // If obj is a class and the attribute was found on the class itself (or its bases),
                 // we are accessing an attribute ON a class. So instance = None, owner = the class (obj).
                 if (isClass && foundOnClassOrMro) {
                     instance = PROTO_NONE;
                     owner = obj;
                 }
                 
                 const proto::ProtoList* args = ctx->newList()->appendLast(ctx, instance)->appendLast(ctx, owner ? owner : PROTO_NONE);
                 const proto::ProtoObject* res = getM->asMethod(ctx)(ctx, val, nullptr, args, nullptr);
                 getAttrDepth--;
                 return res;
            }
        }
    }

    // 2. Minimalist Binding logic: if it's a method cell from a prototype, bind it to 'obj'
    if (val->isCell(ctx) && val->isMethod(ctx)) {
        // Skip binding for if looking up module markers themselves
        if (fileDunderS && pathDunderS && name != fileDunderS && name != pathDunderS) {
            // Do not bind if obj is a module! Modules are namespaces, not classes.
            const proto::ProtoObject* objClass = obj->getAttribute(ctx, this->getClassString());
            if (this->modulePrototype && (obj == this->modulePrototype || objClass == this->modulePrototype)) {
                // Return unbound module function
                getAttrDepth--;
                return val;
            }
            // If it's a method cell, bind it to 'obj'.
            // In a pure protoCore.h world, we can't easily check if it's already bound,
            // so we bind it here for consistency with instance method access.
            const proto::ProtoObject* bound = ctx->fromMethod(const_cast<proto::ProtoObject*>(obj), val->asMethod(ctx));
            
            // Native built-in bound methods need standard introspection primitives identically to user functions.
            const proto::ProtoString* py_name_s = this->getNameString();
            if (!py_name_s) py_name_s = proto::ProtoString::fromUTF8String(ctx, "__name__");
            bound = bound->setAttribute(ctx, py_name_s, name->asObject(ctx));
            bound = bound->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__qualname__"), name->asObject(ctx));
            
            getAttrDepth--;
            return bound;
        }
    }
    
    // Fallback: __getattr__
    // This is required for objects like 'super' proxies that rely on __getattr__
    if ((!val || val == PROTO_NONE)) {
        if (getAttrDepth < 10) {
            const proto::ProtoString* getattrS = proto::ProtoString::fromUTF8String(ctx, "__getattr__");
            // Ensure we ONLY do this for actual instances, not classes/types, or strictly for super proxies.
            // Since we lack a perfect type check here, we check if the object's class has __getattr__.
            // Wait, Python's __getattr__ is ONLY called from the class: `type(obj).__getattr__(obj, name)`.
            const proto::ProtoObject* cls = obj->getAttribute(ctx, this->getClassString());
            if (cls && cls != PROTO_NONE && cls->hasOwnAttribute(ctx, getattrS) == PROTO_TRUE) {
                const proto::ProtoObject* getattrM = cls->getAttribute(ctx, getattrS);
                if (getattrM && getattrM->isMethod(ctx)) {
                     const proto::ProtoList* args = ctx->newList()->appendLast(ctx, obj)->appendLast(ctx, name->asObject(ctx));
                     const proto::ProtoObject* res = getattrM->asMethod(ctx)(ctx, cls, nullptr, args, nullptr);
                     getAttrDepth--;
                     return res;
                }
            } else if (obj->hasOwnAttribute(ctx, getattrS) == PROTO_TRUE) {
                // Legacy support for super proxy which might hold __getattr__ on the object itself in protoCore
                const proto::ProtoObject* getattrM = obj->getAttribute(ctx, getattrS);
                if (getattrM && getattrM->isMethod(ctx)) {
                     const proto::ProtoList* args = ctx->newList()->appendLast(ctx, name->asObject(ctx));
                     const proto::ProtoObject* res = getattrM->asMethod(ctx)(ctx, obj, nullptr, args, nullptr);
                     getAttrDepth--;
                     return res;
                }
            }
        }
    }

    getAttrDepth--;
    if (!val && name) {
        std::string attrStr;
        name->toUTF8String(ctx, attrStr);
        if (attrStr == "update") {
            fprintf(stderr, "DEBUG TRAP: Failed to find 'update' on object %p\n", (void*)obj);
            if (obj && obj->isInstanceOf(ctx, getTypePrototype()) == PROTO_TRUE) {
                fprintf(stderr, "DEBUG TRAP: Object is a TYPE!\n");
            }
        }
    }
    return val;
}

const proto::ProtoObject* PythonEnvironment::setAttribute(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* name, const proto::ProtoObject* value) {
    if (!obj || isEmbeddedValue(obj) || !name) return obj;

    // In Python, data descriptors (with __set__) shadow instance attributes
    if (obj->hasOwnAttribute(ctx, name) == PROTO_FALSE) {
        const proto::ProtoObject* descr = obj->getAttribute(ctx, name);
        if (descr && descr != PROTO_NONE && descr->isCell(ctx)) {
            const proto::ProtoObject* setM = descr->getAttribute(ctx, setDunderString);
            if (setM && setM != PROTO_NONE && setM->asMethod(ctx)) {
                const proto::ProtoList* setArgs = ctx->newList()->appendLast(ctx, obj)->appendLast(ctx, value);
                setM->asMethod(ctx)(ctx, descr, nullptr, setArgs, nullptr);
                return obj;
            }
        }
    }

    return obj->setAttribute(ctx, name, value);
}

const proto::ProtoObject* PythonEnvironment::resolve(const std::string& name, proto::ProtoContext* ctx) {
    if (!ctx) ctx = s_threadContext ? s_threadContext : rootContext_;
    
    // Check per-thread cache first (Lock-free)
    uint64_t gen = resolveCacheGeneration_.load(std::memory_order_acquire);
    if (s_threadResolveCacheGeneration != gen) {
        s_threadResolveCache.clear();
        s_threadResolveCacheGeneration = gen;
    }
    auto it = s_threadResolveCache.find(name);
    if (it != s_threadResolveCache.end()) {
        if (std::getenv("PROTO_ENV_DIAG") && (name == "type" || name == "object")) {
             fprintf(stderr, "DEBUG: resolve CACHE name=%s obj=%p\n", name.c_str(), (void*)it->second);
             fflush(stderr);
        }
        return it->second;
    }

    const proto::ProtoString* nameS = proto::ProtoString::fromUTF8String(ctx, name.c_str());
    const proto::ProtoObject* res = resolve(nameS, ctx);
    s_threadResolveCache[name] = res;
    if (std::getenv("PROTO_ENV_DIAG") && (name == "type" || name == "object")) {
         fprintf(stderr, "DEBUG: resolve NEW name=%s obj=%p\n", name.c_str(), (void*)res);
         fflush(stderr);
    }
    return res;
}

const proto::ProtoObject* PythonEnvironment::resolve(const proto::ProtoString* nameObj, proto::ProtoContext* ctx) {
    if (!nameObj) return nullptr;
    if (!ctx) ctx = s_threadContext ? s_threadContext : rootContext_;

    std::string nameStr;
    nameObj->toUTF8String(ctx, nameStr);

    if (std::getenv("PROTO_RESOLVE_DIAG")) {
    }
    
    static thread_local int resolveDepth = 0;
    if (++resolveDepth > 100) {
        --resolveDepth;
        return nullptr;
    }
    struct DepthGuard {
        int& d;
        DepthGuard(int& d) : d(d) {}
        ~DepthGuard() { --d; }
    } dg(resolveDepth);

    // 1. Try current module's globals (Lock-free)
    if (s_currentGlobals) {
        if (s_currentGlobals->hasAttribute(ctx, nameObj) == PROTO_TRUE) {
            const proto::ProtoObject* result = s_currentGlobals->getAttribute(ctx, nameObj);
            if (get_env_diag() && (nameStr == "tuple" || nameStr == "map" || nameStr == "None")) {
                fprintf(stderr, "DEBUG: resolve GLOBALS name=%s result=%p repr=%s\n", nameStr.c_str(), (void*)result, PythonEnvironment::reprObject(ctx, result).c_str());
                fflush(stderr);
            }
            return result;
        }
    }

    // 2. Builtins (Lock-free)
    if (builtinsModule) {
        if (builtinsModule->hasAttribute(ctx, nameObj) == PROTO_TRUE) {
            const proto::ProtoObject* result = builtinsModule->getAttribute(ctx, nameObj);
            if (get_env_diag() && (nameStr == "tuple" || nameStr == "map" || nameStr == "None")) {
                fprintf(stderr, "DEBUG: resolve BUILTINS name=%s result=%p repr=%s\n", nameStr.c_str(), (void*)result, PythonEnvironment::reprObject(ctx, result).c_str());
                fflush(stderr);
            }
            if (std::getenv("PROTO_ENV_DIAG") && (nameStr == "type" || nameStr == "object")) {
                 fprintf(stderr, "DEBUG: resolve BUILTINS name=%s obj=%p\n", nameStr.c_str(), (void*)result);
                 fflush(stderr);
            }
            return result;
        }
    }

    // 3. Literals Quick-Path (Lock-free)
    {
        const proto::ProtoObject* result = nullptr;
        if (nameStr == "None") result = PROTO_NONE;
        else if (nameStr == "True") result = PROTO_TRUE;
        else if (nameStr == "False") result = PROTO_FALSE;
        else if (nameStr == "object") result = objectPrototype;
        else if (nameStr == "type") result = typePrototype;
        else if (nameStr == "int") result = intPrototype;
        else if (nameStr == "str") result = strPrototype;
        else if (nameStr == "list") result = listPrototype;
        else if (nameStr == "dict") result = dictPrototype;
        else if (nameStr == "tuple") result = tuplePrototype;
        else if (nameStr == "bool") result = boolPrototype;
        else if (nameStr == "complex") result = complexPrototype;
        
        if (result) {
            if (std::getenv("PROTO_ENV_DIAG")) {
                fprintf(stderr, "DEBUG: resolve Quick-Path name=%s obj=%p\n", nameStr.c_str(), (void*)result);
                fflush(stderr);
            }
            return result;
        }
        
        // 4. Fallback to Imports/Sys (Locked)
        SafeImportLock lock(this, ctx);
        
        // Re-check sys.modules
        if (sysModule) {
            const proto::ProtoObject* modules = sysModule->getAttribute(ctx, modulesS);
            if (modules && modules != PROTO_NONE) {
                // Step V74: sys.modules is a dict, so we check __data__ first if it exists
                const proto::ProtoObject* dataAttr = modules->getAttribute(ctx, dataString);
                const proto::ProtoSparseList* dict = (dataAttr && dataAttr != PROTO_NONE) ? dataAttr->asSparseList(ctx) : modules->asSparseList(ctx);
                
                if (dict && dict->has(ctx, nameObj->getHash(ctx))) {
                    const proto::ProtoObject* mod = dict->getAt(ctx, nameObj->getHash(ctx));
                    return mod;
                }
            }
        }

        // Module import search
        const proto::ProtoObject* modWrapper = ctx->space->getImportModule(ctx, nameStr.c_str(), "val");
        if (modWrapper && modWrapper != PROTO_NONE) {
            const proto::ProtoObject* result = modWrapper->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"));
            if (result && result != nullptr) {
                const proto::ProtoObject* executedKey = result->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__executed__"));
                if (!executedKey || executedKey == PROTO_FALSE || executedKey == PROTO_NONE) {
                    if (std::getenv("PROTO_RESOLVE_DIAG")) {
                        // fprintf(stderr, "DEBUG: resolve(%s) executing module %p\n", nameStr.c_str(), (void*)result);
                    }
                    int ret = executeModule(nameStr, false, ctx);
                    if (ret != 0) return nullptr;
                    result = modWrapper->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"));
                }
                if (std::getenv("PROTO_RESOLVE_DIAG")) {
                    // fprintf(stderr, "DEBUG: resolve(%s) returning %p (type %p)\n", nameStr.c_str(), (void*)result, (void*)(result ? result->getAttribute(ctx, getClassString()) : nullptr));
                }
                return result;
            }
        }
    }

    return nullptr;
}

bool PythonEnvironment::isResolved(const std::string& name, proto::ProtoContext* ctx) {
    if (!ctx) ctx = s_threadContext;
    if (!ctx) ctx = rootContext_;
    if (name == "None" || name == "True" || name == "False") return true;
    // Check resolve cache or builtins
    const proto::ProtoObject* r = resolve(name, ctx);
    return r != nullptr;
}

const proto::ProtoObject* PythonEnvironment::compareObjects(proto::ProtoContext* ctx, const proto::ProtoObject* a, const proto::ProtoObject* b, int op) {
    if (!a || !b) return PROTO_FALSE;

    // Check for dunder comparison methods
    if (op >= 0 && op <= 5) {
        const proto::ProtoString* dunder = nullptr;
        if (op == 0) dunder = py_eq_s;
        else if (op == 1) dunder = py_ne_s;
        else if (op == 2) dunder = py_lt_s;
        else if (op == 3) dunder = py_le_s;
        else if (op == 4) dunder = py_gt_s;
        else if (op == 5) dunder = py_ge_s;

        if (dunder) {
            const proto::ProtoObject* method = a->getAttribute(ctx, dunder);
            if (method && method->asMethod(ctx)) {
                const proto::ProtoList* args = ctx->newList();
                args = args->appendLast(ctx, b);
                const proto::ProtoObject* res = method->asMethod(ctx)(ctx, a, nullptr, args, getEmptySparseList());
                if (res && res != PROTO_NONE) return res;
            }
        }
    }

    int c = 0;
    if (a->isString(ctx) && b->isString(ctx)) {
        // Robust string comparison avoids protoCore pointer-based hash matching
        std::string s1, s2;
        a->asString(ctx)->toUTF8String(ctx, s1);
        b->asString(ctx)->toUTF8String(ctx, s2);
        c = s1.compare(s2);
    } else {
        c = a->compare(ctx, b);
    }
    bool result = false;
    if (op == 0) result = (c == 0);
    else if (op == 1) result = (c != 0);
    else if (op == 2) result = (c < 0);
    else if (op == 3) result = (c <= 0);
    else if (op == 4) result = (c > 0);
    else if (op == 5) result = (c >= 0);
    
    return result ? PROTO_TRUE : PROTO_FALSE;
}

bool PythonEnvironment::objectsEqual(proto::ProtoContext* ctx, const proto::ProtoObject* a, const proto::ProtoObject* b) {
    const proto::ProtoObject* res = compareObjects(ctx, a, b, 0);
    return res && res->isBoolean(ctx) && res->asBoolean(ctx);
}



const proto::ProtoObject* PythonEnvironment::binaryOp(const proto::ProtoObject* a, TokenType op, const proto::ProtoObject* b) {
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;
    if (!a || !b) return PROTO_NONE;

    if (a->isInteger(ctx) && b->isInteger(ctx)) {
        long long av = a->asLong(ctx);
        long long bv = b->asLong(ctx);
        switch (op) {
            case TokenType::Plus: return ctx->fromInteger(av + bv);
            case TokenType::Minus: return ctx->fromInteger(av - bv);
            case TokenType::Star: return ctx->fromInteger(av * bv);
            case TokenType::Slash: return (bv != 0) ? ctx->fromDouble((double)av / bv) : (raiseZeroDivisionError(ctx), PROTO_NONE);
            case TokenType::Modulo: return (bv != 0) ? ctx->fromInteger(av % bv) : (raiseZeroDivisionError(ctx), PROTO_NONE);
            default: break;
        }
    } else if (a->isDouble(ctx) || b->isDouble(ctx)) {
        double av = a->isDouble(ctx) ? a->asDouble(ctx) : (double)a->asLong(ctx);
        double bv = b->isDouble(ctx) ? b->asDouble(ctx) : (double)b->asLong(ctx);
        switch (op) {
            case TokenType::Plus: return ctx->fromDouble(av + bv);
            case TokenType::Minus: return ctx->fromDouble(av - bv);
            case TokenType::Star: return ctx->fromDouble(av * bv);
            case TokenType::Slash: return (bv != 0) ? ctx->fromDouble(av / bv) : (raiseZeroDivisionError(ctx), PROTO_NONE);
            default: break;
        }
    }

    // Fallback: look for dunder methods
    const proto::ProtoString* dunder = nullptr;
    switch (op) {
        case TokenType::Plus: dunder = proto::ProtoString::fromUTF8String(ctx, "__add__"); break;
        case TokenType::Minus: dunder = proto::ProtoString::fromUTF8String(ctx, "__sub__"); break;
        case TokenType::Star: dunder = proto::ProtoString::fromUTF8String(ctx, "__mul__"); break;
        case TokenType::Slash: dunder = proto::ProtoString::fromUTF8String(ctx, "__truediv__"); break;
        case TokenType::Modulo: dunder = proto::ProtoString::fromUTF8String(ctx, "__mod__"); break;
        default: break;
    }

    if (dunder) {
        const proto::ProtoObject* method = a->getAttribute(ctx, dunder);
        if (method && method->asMethod(ctx)) {
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, b);
            return method->asMethod(ctx)(ctx, a, nullptr, args, nullptr);
        }
    }

    // Handle comparisons
    int compOp = -1;
    switch (op) {
        case TokenType::EqEqual: compOp = 0; break;
        case TokenType::NotEqual: compOp = 1; break;
        case TokenType::Less: compOp = 2; break;
        case TokenType::LessEqual: compOp = 3; break;
        case TokenType::Greater: compOp = 4; break;
        case TokenType::GreaterEqual: compOp = 5; break;
        default: break;
    }
    if (compOp != -1) {
        return compareObjects(ctx, a, b, compOp);
    }

    return PROTO_NONE;
}

const proto::ProtoObject* PythonEnvironment::lookupName(const std::string& name) {
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;
    const proto::ProtoObject* frame = getCurrentFrame();
    const proto::ProtoString* nameS = proto::ProtoString::fromUTF8String(ctx, name.c_str());
    if (frame) {
        const proto::ProtoObject* val = frame->getAttribute(ctx, nameS);
        if (val && val != PROTO_NONE) return val;
    }
    const proto::ProtoObject* result = resolve(name, ctx);
    if (!result || result == PROTO_NONE) {
        raiseNameError(ctx, name);
    }
    return result;
}

const proto::ProtoObject* PythonEnvironment::buildString(const proto::ProtoObject** parts, size_t count) {
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;
    std::string result;
    result.reserve(count * 16);
    for (size_t i = 0; i < count; ++i) {
        const proto::ProtoObject* obj = parts[i];
        if (!obj || obj == PROTO_NONE) {
            result += "None";
        } else if (obj->isString(ctx)) {
            std::string s; obj->asString(ctx)->toUTF8String(ctx, s);
            result += s;
        } else if (obj->isInteger(ctx)) {
            result += std::to_string(obj->asLong(ctx));
        } else if (obj->isDouble(ctx)) {
            result += std::to_string(obj->asDouble(ctx));
        } else if (obj == PROTO_TRUE) {
            result += "True";
        } else if (obj == PROTO_FALSE) {
            result += "False";
        } else {
            const proto::ProtoObject* strFunc = resolve("str", ctx);
            if (strFunc) {
                const proto::ProtoObject* sObj = callObject(strFunc, {obj});
                if (sObj && sObj->isString(ctx)) {
                    std::string s; sObj->asString(ctx)->toUTF8String(ctx, s);
                    result += s;
                } else {
                    result += "<object>";
                }
            } else {
                result += "<object>";
            }
        }
    }
    return proto::ProtoString::fromUTF8String(ctx, result.c_str())->asObject(ctx);
}

void PythonEnvironment::storeName(const std::string& name, const proto::ProtoObject* val) {
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(getCurrentFrame());
    if (frame) {
        frame->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, name.c_str()), val);
    } else {
        // Store in globals as fallback
        const_cast<proto::ProtoObject*>(getGlobals())->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, name.c_str()), val);
    }
}

const proto::ProtoObject* PythonEnvironment::callObject(const proto::ProtoObject* callable, const std::vector<const proto::ProtoObject*>& args) {
    return callObjectEx(callable, args, {});
}

const proto::ProtoObject* PythonEnvironment::callObjectEx(const proto::ProtoObject* callable, 
                                      const std::vector<const proto::ProtoObject*>& args,
                                      const std::vector<std::pair<std::string, const proto::ProtoObject*>>& keywords,
                                      const proto::ProtoObject* starargs,
                                      const proto::ProtoObject* kwargs) {
    proto::ProtoContext* ctx = rootContext_;
    const proto::ProtoList* plArgs = ctx->newList();
    for (auto* arg : args) plArgs = plArgs->appendLast(ctx, arg);
    
    if (starargs && starargs != PROTO_NONE) {
        const proto::ProtoObject* it = this->iter(starargs);
        if (it) {
            while (const proto::ProtoObject* val = this->next(it)) {
                if (this->hasPendingException()) break;
                if (!val) break;
                plArgs = plArgs->appendLast(ctx, val);
            }
        }
    }

    const proto::ProtoSparseList* psKwargs = ctx->newSparseList();
    const proto::ProtoList* kwNames = ctx->newList();

    for (const auto& kw : keywords) {
        const proto::ProtoString* keyStr = proto::ProtoString::fromUTF8String(ctx, kw.first.c_str());
        psKwargs = psKwargs->setAt(ctx, keyStr->getHash(ctx), kw.second);
        kwNames = kwNames->appendLast(ctx, keyStr->asObject(ctx));
    }

    if (kwargs && kwargs != PROTO_NONE) {
        const proto::ProtoObject* keys = kwargs->getAttribute(ctx, getKeysString());
        if (keys && keys->asList(ctx)) {
            const proto::ProtoList* kList = keys->asList(ctx);
            for (unsigned long i = 0; i < kList->getSize(ctx); ++i) {
                const proto::ProtoObject* k = kList->getAt(ctx, i);
                if (k && k->isString(ctx)) {
                    const proto::ProtoString* ks = k->asString(ctx);
                    psKwargs = psKwargs->setAt(ctx, ks->getHash(ctx), this->getItem(kwargs, k));
                    kwNames = kwNames->appendLast(ctx, ks->asObject(ctx));
                }
            }
        }
    }

    bool pushed = false;
    if (kwNames->getSize(ctx) > 0) {
        this->pushKwNames(ctx->newTupleFromList(kwNames));
        pushed = true;
    }

    const proto::ProtoObject* result = invokePythonCallable(ctx, callable, plArgs, psKwargs);

    if (pushed) this->popKwNames();
    return result ? result : PROTO_NONE;
}

const proto::ProtoObject* PythonEnvironment::getItem(const proto::ProtoObject* container, const proto::ProtoObject* key) {
    proto::ProtoContext* ctx = rootContext_;
    if (!container || !key) return PROTO_NONE;
    
    // No shortcuts here to ensure we always trigger __getitem__ which handles slices and exceptions
    
    const proto::ProtoObject* method = container->getAttribute(ctx, getItemString);
    if (method && method->asMethod(ctx)) {
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
        return method->asMethod(ctx)(ctx, container, nullptr, args, getEmptySparseList());
    }
    return PROTO_NONE;
}

void PythonEnvironment::setItem(const proto::ProtoObject* container, const proto::ProtoObject* key, const proto::ProtoObject* value) {
    proto::ProtoContext* ctx = rootContext_;
    if (!container || !key || !value) return;

    // No shortcuts here to ensure we always trigger __setitem__ or fallback

    const proto::ProtoObject* method = container->getAttribute(ctx, setItemString);
    if (method && method->asMethod(ctx)) {
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, value);
        method->asMethod(ctx)(ctx, container, nullptr, args, nullptr);
    }
}

const proto::ProtoObject* PythonEnvironment::getAttr(const proto::ProtoObject* obj, const std::string& attr) {
    proto::ProtoContext* ctx = rootContext_;
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* result = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, attr.c_str()));
    if (!result || result == PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseAttributeError(ctx, obj, attr);
    }
    return result;
}

void PythonEnvironment::setAttr(const proto::ProtoObject* obj, const std::string& attr, const proto::ProtoObject* val) {
    proto::ProtoContext* ctx = rootContext_;
    if (!obj) return;
    const_cast<proto::ProtoObject*>(obj)->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, attr.c_str()), val);
}

const proto::ProtoObject* PythonEnvironment::unaryOp(TokenType op, const proto::ProtoObject* a) {
    proto::ProtoContext* ctx = rootContext_;
    if (!a) return PROTO_NONE;

    if (a->isInteger(ctx)) {
        long long v = a->asLong(ctx);
        switch (op) {
            case TokenType::Plus: return a;
            case TokenType::Minus: return ctx->fromInteger(-v);
            case TokenType::Tilde: return ctx->fromInteger(~v);
            default: break;
        }
    } else if (a->isDouble(ctx)) {
        double v = a->asDouble(ctx);
        switch (op) {
            case TokenType::Plus: return a;
            case TokenType::Minus: return ctx->fromDouble(-v);
            default: break;
        }
    }

    // Fallback to dunder methods
    const proto::ProtoString* dunder = nullptr;
    switch (op) {
        case TokenType::Plus: dunder = proto::ProtoString::fromUTF8String(ctx, "__pos__"); break;
        case TokenType::Minus: dunder = proto::ProtoString::fromUTF8String(ctx, "__neg__"); break;
        case TokenType::Tilde: dunder = proto::ProtoString::fromUTF8String(ctx, "__invert__"); break;
        case TokenType::Not: {
            return isTrue(a) ? PROTO_FALSE : PROTO_TRUE;
        }
        default: break;
    }

    if (dunder) {
        const proto::ProtoObject* method = a->getAttribute(ctx, dunder);
        if (method && method->asMethod(ctx)) {
            return method->asMethod(ctx)(ctx, a, nullptr, getEmptyList(), nullptr);
        }
    }

    return PROTO_NONE;
}

const proto::ProtoObject* PythonEnvironment::iter(const proto::ProtoObject* obj) {
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;

    if (!obj || obj == PROTO_NONE || obj == nonePrototype || (obj && obj->isNone(ctx)) || 
        (obj && obj->isCell(ctx) && obj->hasParent(ctx, noneTypeProto))) {
        if (std::getenv("PROTO_ENV_DIAG")) {
            fprintf(stderr, "DEBUG: env->iter matched NoneType obj=%p isCell=%d noneTypeProto=%p hasParentNone=%d\n",
                    (void*)obj, (obj ? obj->isCell(ctx) : 0), (void*)noneTypeProto, (obj ? obj->hasParent(ctx, noneTypeProto) : 0));
            if (obj && obj->getParents(ctx)) {
                fprintf(stderr, "DEBUG: obj parents size=%zu\n", obj->getParents(ctx)->getSize(ctx));
                for(size_t i=0; i<obj->getParents(ctx)->getSize(ctx); i++) {
                    fprintf(stderr, "DEBUG: parent[%zu]=%p\n", i, (void*)obj->getParents(ctx)->getAt(ctx, i));
                }
            }
            fflush(stderr);
        }
        raiseTypeError(ctx, "'NoneType' object is not iterable");
        return nullptr;
    }
    const proto::ProtoObject* method = getAttribute(ctx, obj, getIterString());
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: env->iter called on obj=%p, method=%p, asMethod=%p\n",
                (void*)obj, (void*)method, (void*)(method ? method->asMethod(ctx) : nullptr));
        fflush(stderr);
    }
    if (method && method->asMethod(ctx)) {
        return method->asMethod(ctx)(ctx, obj, nullptr, getEmptyList(), nullptr);
    }



    // Optimization: if it already has __next__, it's an iterator (return self)
    const proto::ProtoObject* nextMethod = obj->getAttribute(ctx, getNextString());
    if (nextMethod && nextMethod->asMethod(ctx)) {
        return obj;
    }
    
    // Fallback for raw protoCore containers that might not have prototypes set (common in built-in returns)
    if (obj->asList(ctx)) {
        const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(ctx, "__iter_prototype__");
        const proto::ProtoObject* iterProto = listPrototype ? listPrototype->getAttribute(ctx, iterProtoName) : nullptr;
        if (iterProto) {
            const proto::ProtoList* list = obj->asList(ctx);
            const proto::ProtoListIterator* it = list->getIterator(ctx);
            const proto::ProtoObject* iterObj = iterProto->newChild(ctx, true);
            iterObj = iterObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter_list__"), obj);
            iterObj = iterObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter_it__"), it->asObject(ctx));
            return iterObj;
        }
    } else if (obj->asTuple(ctx)) {
        const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(ctx, "__iter_prototype__");
        const proto::ProtoObject* iterProto = tuplePrototype ? tuplePrototype->getAttribute(ctx, iterProtoName) : nullptr;
        if (iterProto) {
            const proto::ProtoList* list = obj->asTuple(ctx)->asList(ctx);
            if (list) {
                const proto::ProtoListIterator* it = list->getIterator(ctx);
                const proto::ProtoObject* iterObj = iterProto->newChild(ctx, true);
                iterObj = iterObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter_list__"), obj);
                iterObj = iterObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter_it__"), it->asObject(ctx));
                return iterObj;
            }
        }
    } else if (obj->isString(ctx)) {
        const proto::ProtoString* str = obj->asString(ctx);
        const proto::ProtoStringIterator* it = str->getIterator(ctx);
        if (it) return it->asObject(ctx);
    } else if (obj->asSparseList(ctx)) {
        // Dict iteration (keys)
        const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(ctx, "__iter_prototype__");
        const proto::ProtoObject* iterProto = dictPrototype ? dictPrototype->getAttribute(ctx, iterProtoName) : nullptr;
        if (iterProto) {
            const proto::ProtoObject* keysObj = obj->getAttribute(ctx, getKeysString());
            const proto::ProtoList* keys = keysObj ? keysObj->asList(ctx) : nullptr;
            if (keys) {
                const proto::ProtoListIterator* it = keys->getIterator(ctx);
                const proto::ProtoObject* iterObj = iterProto->newChild(ctx, true);
                iterObj = iterObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter_list__"), keysObj);
                iterObj = iterObj->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter_it__"), it->asObject(ctx));
                return iterObj;
            }
        }
    } else if (obj->asSet(ctx) || (obj->getPrototype(ctx) && obj->getPrototype(ctx) == setPrototype)) {
        const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(ctx, "__iter_prototype__");
        const proto::ProtoObject* iterProto = setPrototype ? setPrototype->getAttribute(ctx, iterProtoName) : nullptr;
        if (iterProto) {
            // ProtoSet in protoCore might not have asSet() if it's a raw object, but getPrototype() works
            // For now, let's assume it has an internal iterator we can use via a helper or just use __iter__ from proto
            const proto::ProtoObject* itM = setPrototype->getAttribute(ctx, getIterString());
            if (itM && itM->asMethod(ctx)) {
                return itM->asMethod(ctx)(ctx, obj, nullptr, getEmptyList(), getEmptySparseList());
            }
        }
    } else if (obj->getPrototype(ctx) && obj->getPrototype(ctx) == bytesPrototype) {
        const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(ctx, "__iter_prototype__");
        const proto::ProtoObject* iterProto = bytesPrototype ? bytesPrototype->getAttribute(ctx, iterProtoName) : nullptr;
        if (iterProto) {
            const proto::ProtoObject* itM = bytesPrototype->getAttribute(ctx, getIterString());
            if (itM && itM->asMethod(ctx)) {
                return itM->asMethod(ctx)(ctx, obj, nullptr, getEmptyList(), getEmptySparseList());
            }
        }
        // We should return a wrapper iterator, but for now let's return the object if it looks iterable
        return obj; 
    }
    
    // Python allows objects with __getitem__ but no __iter__ to be iterable (e.g. old sequences)
    const proto::ProtoObject* getitem = obj->getAttribute(ctx, getGetItemString());
    if (getitem && getitem->asMethod(ctx)) {
        // We should return a wrapper iterator, but for now let's return the object if it looks iterable
        return obj; 
    }

    raiseTypeError(ctx, "object is not iterable");
    return nullptr;
}

const proto::ProtoObject* PythonEnvironment::next(const proto::ProtoObject* obj) {
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;

    if (hasPendingException()) {
        const proto::ProtoObject* exc = peekPendingException();
        if (isStopIteration(ctx, exc)) {
            clearPendingException();
            return nullptr;
        }
        return nullptr;
    }

    if (!obj || obj == PROTO_NONE) return nullptr;
    
    const proto::ProtoObject* method = obj->getAttribute(ctx, getNextString());
    if (method && method->asMethod(ctx)) {
        const proto::ProtoObject* res = method->asMethod(ctx)(ctx, obj, nullptr, getEmptyList(), nullptr);
        
        if (hasPendingException()) {
            const proto::ProtoObject* exc = peekPendingException();
            if (isStopIteration(ctx, exc)) {
                clearPendingException();
                return nullptr; // Success exhaustion
            }
            return nullptr; // Other error
        }
        return res; // Can be PROTO_NONE (valid None value) or nullptr (if native caller explicitly returned it for exhaustion)
    }
    return nullptr;
}

void PythonEnvironment::raiseException(const proto::ProtoObject* exc) {
    if (exc) setPendingException(exc);
}

// V75: Construct traceback object (linked list of stack frames)
void PythonEnvironment::addTraceback(const proto::ProtoObject* exc, const proto::ProtoObject* frame, int lasti, int lineno) {
    if (!exc || !tracebackPrototype) return;
    
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: addTraceback exc=%p frame=%p lasti=%d lineno=%d\n", exc, frame, lasti, lineno);
    }
    
    const proto::ProtoString* tbName = proto::ProtoString::fromUTF8String(rootContext_, "__traceback__");
    const proto::ProtoObject* currentTb = exc->getAttribute(rootContext_, tbName);
    
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: addTraceback currentTb=%p\n", currentTb);
    }

    const proto::ProtoObject* newTb = tracebackPrototype->newChild(rootContext_, true);
    newTb = newTb->setAttribute(rootContext_, classString, tracebackPrototype);
    
    const proto::ProtoString* tbFrameName = proto::ProtoString::fromUTF8String(rootContext_, "tb_frame");
    const proto::ProtoString* tbLastiName = proto::ProtoString::fromUTF8String(rootContext_, "tb_lasti");
    const proto::ProtoString* tbLinenoName = proto::ProtoString::fromUTF8String(rootContext_, "tb_lineno");
    const proto::ProtoString* tbNextName = proto::ProtoString::fromUTF8String(rootContext_, "tb_next");
    
    newTb = newTb->setAttribute(rootContext_, tbFrameName, frame);
    newTb = newTb->setAttribute(rootContext_, tbLastiName, rootContext_->fromInteger(lasti));
    newTb = newTb->setAttribute(rootContext_, tbLinenoName, rootContext_->fromInteger(lineno));
    
    if (currentTb && currentTb != PROTO_NONE) {
        newTb = newTb->setAttribute(rootContext_, tbNextName, currentTb);
    } else {
        newTb = newTb->setAttribute(rootContext_, tbNextName, PROTO_NONE);
    }
    
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: addTraceback newTb=%p\n", newTb);
    }

    // Update exception's __traceback__
    const proto::ProtoObject* updatedExc = const_cast<proto::ProtoObject*>(exc)->setAttribute(rootContext_, tbName, newTb);
    
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: addTraceback updatedExc=%p\n", updatedExc);
    }

    if (updatedExc != exc) {
        setPendingException(updatedExc);
    }
}

bool PythonEnvironment::isException(const proto::ProtoObject* exc, const proto::ProtoObject* type) {
    if (!exc || !type || exc == PROTO_NONE || type == PROTO_NONE) return false;
    proto::ProtoContext* ctx = rootContext_;
    
    if (type->isTuple(ctx)) {
        const proto::ProtoTuple* tup = type->asTuple(ctx);
        for (unsigned long i = 0; i < tup->getSize(ctx); ++i) {
            if (isException(exc, tup->getAt(ctx, i))) return true;
        }
        return false;
    }
    
    return isTrue(exc->isInstanceOf(ctx, type));
}

void PythonEnvironment::augAssignName(const std::string& name, TokenType op, const proto::ProtoObject* value) {
    TokenType binOp;
    switch (op) {
        case TokenType::PlusAssign: binOp = TokenType::Plus; break;
        case TokenType::MinusAssign: binOp = TokenType::Minus; break;
        case TokenType::StarAssign: binOp = TokenType::Star; break;
        case TokenType::SlashAssign: binOp = TokenType::Slash; break;
        default: return;
    }
    const proto::ProtoObject* oldVal = lookupName(name);
    const proto::ProtoObject* newVal = binaryOp(oldVal, binOp, value);
    storeName(name, newVal);
}

void PythonEnvironment::augAssignAttr(const proto::ProtoObject* obj, const std::string& attr, TokenType op, const proto::ProtoObject* value) {
    TokenType binOp;
    switch (op) {
        case TokenType::PlusAssign: binOp = TokenType::Plus; break;
        case TokenType::MinusAssign: binOp = TokenType::Minus; break;
        case TokenType::StarAssign: binOp = TokenType::Star; break;
        case TokenType::SlashAssign: binOp = TokenType::Slash; break;
        default: return;
    }
    const proto::ProtoObject* oldVal = getAttr(obj, attr);
    const proto::ProtoObject* newVal = binaryOp(oldVal, binOp, value);
    setAttr(obj, attr, newVal);
}

void PythonEnvironment::augAssignItem(const proto::ProtoObject* container, const proto::ProtoObject* key, TokenType op, const proto::ProtoObject* value) {
    TokenType binOp;
    switch (op) {
        case TokenType::PlusAssign: binOp = TokenType::Plus; break;
        case TokenType::MinusAssign: binOp = TokenType::Minus; break;
        case TokenType::StarAssign: binOp = TokenType::Star; break;
        case TokenType::SlashAssign: binOp = TokenType::Slash; break;
        default: return;
    }
    const proto::ProtoObject* oldVal = getItem(container, key);
    const proto::ProtoObject* newVal = binaryOp(oldVal, binOp, value);
    setItem(container, key, newVal);
}

bool PythonEnvironment::isTrue(const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE || obj == PROTO_FALSE) return false;
    if (obj == PROTO_TRUE) return true;
    proto::ProtoContext* ctx = rootContext_;
    if (obj->isInteger(ctx)) return obj->asLong(ctx) != 0;
    if (obj->isDouble(ctx)) return obj->asDouble(ctx) != 0.0;
    if (obj->asList(ctx)) return obj->asList(ctx)->getSize(ctx) > 0;
    if (obj->asSparseList(ctx)) return obj->asSparseList(ctx)->getSize(ctx) > 0;
    if (obj->isTuple(ctx)) return obj->asTuple(ctx)->getSize(ctx) > 0;

    // Check for __bool__ or __len__
    const proto::ProtoObject* boolMethod = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__bool__"));
    if (boolMethod && boolMethod->asMethod(ctx)) {
        const proto::ProtoObject* res = boolMethod->asMethod(ctx)(ctx, obj, nullptr, getEmptyList(), nullptr);
        return res && res->isBoolean(ctx) && res->asBoolean(ctx);
    }
    const proto::ProtoObject* lenMethod = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__len__"));
    if (lenMethod && lenMethod->asMethod(ctx)) {
        const proto::ProtoObject* res = lenMethod->asMethod(ctx)(ctx, obj, nullptr, getEmptyList(), nullptr);
        return res && res->isInteger(ctx) && res->asLong(ctx) > 0;
    }

    return true; // Any non-empty object is true
}

const proto::ProtoObject* PythonEnvironment::importModule(const std::string& name, int level, const std::vector<std::string>& fromList) {
    proto::ProtoContext* ctx = s_threadContext ? s_threadContext : rootContext_;
    const proto::ProtoObject* imp = resolve("__import__", ctx);
    if (!imp) return PROTO_NONE;
    
    std::vector<const proto::ProtoObject*> args;
    args.push_back(ctx->fromUTF8String(name.c_str()));
    args.push_back(getGlobals()); // globals
    args.push_back(PROTO_NONE);    // locals
    
    const proto::ProtoList* fl = ctx->newList();
    for (const auto& s : fromList) {
        fl = fl->appendLast(ctx, ctx->fromUTF8String(s.c_str()));
    }
    args.push_back(fl->asObject(ctx)); // fromlist
    args.push_back(ctx->fromInteger(level));
    
    return callObject(imp, args);
}

void PythonEnvironment::importStar(const proto::ProtoObject* mod) {
    if (!mod || mod == PROTO_NONE) return;
    proto::ProtoContext* ctx = s_threadContext ? s_threadContext : rootContext_;
    const proto::ProtoObject* globals = getGlobals();
    if (!globals) return;
    
    // Copy all attributes from mod to globals, excluding those starting with _
    const proto::ProtoSparseList* attrs = mod->getAttributes(ctx);
    if (!attrs) return;
    
    auto* it = const_cast<proto::ProtoSparseListIterator*>(attrs->getIterator(ctx));
    while (it && it->hasNext(ctx)) {
        unsigned long key = it->nextKey(ctx);
        const proto::ProtoString* s = reinterpret_cast<const proto::ProtoObject*>(key)->asString(ctx);
        if (s) {
            std::string name;
            s->toUTF8String(ctx, name);
            if (!name.empty() && name[0] != '_') {
                const_cast<proto::ProtoObject*>(globals)->setAttribute(ctx, s, mod->getAttribute(ctx, s));
            }
        }
        it = const_cast<proto::ProtoSparseListIterator*>(it->advance(ctx));
    }
}

const proto::ProtoObject* PythonEnvironment::buildSlice(const proto::ProtoObject* start, const proto::ProtoObject* stop, const proto::ProtoObject* step) {
    proto::ProtoContext* ctx = rootContext_;
    proto::ProtoObject* sliceObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    sliceObj->setAttribute(ctx, startString, start ? start : PROTO_NONE);
    sliceObj->setAttribute(ctx, stopString, stop ? stop : PROTO_NONE);
    sliceObj->setAttribute(ctx, stepString, step ? step : PROTO_NONE);
    if (sliceType) sliceObj->addParent(ctx, sliceType);
    return sliceObj;
}

void PythonEnvironment::delItem(const proto::ProtoObject* container, const proto::ProtoObject* key) {
    proto::ProtoContext* ctx = rootContext_;
    if (!container || !key) return;
    
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
    const proto::ProtoObject* method = container->getAttribute(ctx, delItemString);
    if (method && method->asMethod(ctx)) {
        method->asMethod(ctx)(ctx, container, nullptr, args, nullptr);
    } else {
        const proto::ProtoObject* data = container->getAttribute(ctx, dataString);
        if (data && data->asSparseList(ctx)) {
            data->asSparseList(ctx)->removeAt(ctx, key->getHash(ctx));
        }
    }
}

void PythonEnvironment::delAttr(const proto::ProtoObject* obj, const std::string& attr) {
    proto::ProtoContext* ctx = rootContext_;
    if (!obj) return;
    const_cast<proto::ProtoObject*>(obj)->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, attr.c_str()), PROTO_NONE);
}

void PythonEnvironment::delName(const std::string& name) {
    proto::ProtoContext* ctx = rootContext_;
    const proto::ProtoObject* frame = getCurrentFrame();
    if (frame) {
        const_cast<proto::ProtoObject*>(frame)->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, name.c_str()), PROTO_NONE);
    }
    invalidateResolveCache();
}

void PythonEnvironment::pushKwNames(const proto::ProtoTuple* names) {
    kwNamesStack.push_back(names);
}

void PythonEnvironment::popKwNames() {
    if (!kwNamesStack.empty()) {
        kwNamesStack.pop_back();
    }
}

const proto::ProtoTuple* PythonEnvironment::getCurrentKwNames() const {
    if (kwNamesStack.empty()) return nullptr;
    return kwNamesStack.back();
}

const proto::ProtoObject* PythonEnvironment::runUntilComplete(const proto::ProtoObject* coro) {
    if (!coro) return PROTO_NONE;
    
    // Add the initial coroutine as a task
    addTask(coro);
    
    const proto::ProtoObject* lastResult = PROTO_NONE;
    
    while (taskQueue && taskQueue->getSize(rootContext_) > 0) {
        // Simple FIFO queue: pull the first task
        const proto::ProtoObject* task = taskQueue->getAt(rootContext_, 0);
        taskQueue = taskQueue->removeAt(rootContext_, 0);
        
        // Resume the coroutine by calling .send(None)
        const proto::ProtoObject* sendMethod = getAttr(task, "send");
        if (sendMethod && sendMethod != PROTO_NONE) {
            try {
                lastResult = callObject(sendMethod, {PROTO_NONE});
                
                // If callObject returns successfully, it means the coroutine yielded.
                // In a minimal loop, we just put it back at the end of the queue to be resumed later.
                addTask(task);
            } catch (const proto::ProtoObject* exc) {
                // If it raised StopIteration, the coroutine is finished.
                if (isStopIteration(rootContext_, exc)) {
                    lastResult = getStopIterationValue(rootContext_, exc);
                    // Finished - don't add back to queue.
                } else {
                    // A real error occurred - report and re-raise.
                    handleException(exc);
                    throw exc;
                }
            }
        }
    }
    
    return lastResult;
}

void PythonEnvironment::addTask(const proto::ProtoObject* coro) {
    if (!taskQueue) taskQueue = rootContext_->newList();
    taskQueue = taskQueue->appendLast(rootContext_, coro);
}

void PythonEnvironment::initDictStorage(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj) return;
    if (get_env_diag()) { printf("DEBUG: initDictStorage obj=%p\n", (void*)obj); }
    const proto::ProtoString* dataName = getInternalString(ctx, "__data__");
    const proto::ProtoString* keysName = getInternalString(ctx, "__keys__");

    if (get_env_diag()) { printf("DEBUG: initDictStorage dataName=%p keysName=%p\n", (void*)dataName, (void*)keysName); }
    const proto::ProtoObject* existingData = obj->getAttribute(ctx, dataName);
    if (!existingData || existingData == PROTO_NONE) {
        if (get_env_diag()) { printf("DEBUG: initDictStorage creating sparse list\n"); }
        const proto::ProtoObject* slist = ctx->newSparseList()->asObject(ctx);
        if (get_env_diag()) { printf("DEBUG: initDictStorage setting __data__\n"); }
        const_cast<proto::ProtoObject*>(obj)->setAttribute(ctx, dataName, slist);
    }
    if (get_env_diag()) { printf("DEBUG: initDictStorage checking keys\n"); }
    const proto::ProtoObject* existingKeys = obj->getAttribute(ctx, keysName);
    if (!existingKeys || existingKeys == PROTO_NONE) {
        if (get_env_diag()) { printf("DEBUG: initDictStorage creating keys list\n"); }
        const proto::ProtoObject* klist = ctx->newList()->asObject(ctx);
        if (get_env_diag()) { printf("DEBUG: initDictStorage setting __keys__\n"); }
        const_cast<proto::ProtoObject*>(obj)->setAttribute(ctx, keysName, klist);
    }
    if (get_env_diag()) { printf("DEBUG: initDictStorage done\n"); }
}

} // namespace protoPython
