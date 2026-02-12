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
#include <proto_internal.h>
#include <mutex>
#include <vector>
#include <unordered_set>

static bool get_thread_diag() {
    static bool diag = std::getenv("PROTO_THREAD_DIAG") != nullptr;
    return diag;
}

static bool get_env_diag() {
    static bool diag = std::getenv("PROTO_ENV_DIAG") != nullptr;
    return diag;
}

namespace protoPython {

static bool isEmbeddedValue(const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    return (reinterpret_cast<uintptr_t>(obj) & 0x3FUL) == POINTER_TAG_EMBEDDED_VALUE;
}

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
    const proto::ProtoSparseList* keywordParameters);

// --- SafeImportLock Implementation ---

static thread_local int s_importLockRecursionDepth = 0;

PythonEnvironment::SafeImportLock::SafeImportLock(PythonEnvironment* env, proto::ProtoContext* ctx)
    : env_(env), ctx_(ctx) {
    if (get_thread_diag()) std::cerr << "[proto-lock] SafeImportLock enter ctx=" << ctx << "\n" << std::flush;
    if (s_importLockRecursionDepth == 0 && ctx_ && ctx_->thread) {
        auto* threadImpl = proto::toImpl<proto::ProtoThreadImplementation>(ctx_->thread);
        auto* space = threadImpl->space;
        
        {
            std::lock_guard<std::mutex> lock(proto::ProtoSpace::globalMutex);
            space->parkedThreads++;
            space->gcCV.notify_all();
        }
        
        if (std::getenv("PROTO_THREAD_DIAG")) std::cerr << "[proto-lock] SafeImportLock waiting for importLock_\n" << std::flush;
        env_->importLock_.lock();
        if (std::getenv("PROTO_THREAD_DIAG")) std::cerr << "[proto-lock] SafeImportLock acquired importLock_\n" << std::flush;
        
        {
            std::unique_lock<std::mutex> lock(proto::ProtoSpace::globalMutex);
            if (space->stwFlag.load()) {
                if (std::getenv("PROTO_THREAD_DIAG")) std::cerr << "[proto-lock] SafeImportLock waiting for STW clear\n" << std::flush;
                space->gcCV.notify_all();
                space->stopTheWorldCV.wait(lock, [space] { return !space->stwFlag.load(); });
            }
            space->parkedThreads--;
        }
    } else {
        if (std::getenv("PROTO_THREAD_DIAG")) std::cerr << "[proto-lock] SafeImportLock nested lock depth=" << s_importLockRecursionDepth << "\n" << std::flush;
        env_->importLock_.lock();
    }
    s_importLockRecursionDepth++;
}

PythonEnvironment::SafeImportLock::~SafeImportLock() {
    s_importLockRecursionDepth--;
    if (s_importLockRecursionDepth == 0 && ctx_ && ctx_->thread) {
        // No special parking needed on unlock, but we must ensure we don't 
        // leave parkedThreads incremented if we weren't outermost (handled by if above).
    }
    env_->importLock_.unlock();
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

static const proto::ProtoObject* py_frame_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    if (get_env_diag()) std::cerr << "[proto-env] py_frame_repr self=" << self << " env=" << env << "\n" << std::flush;
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
    if (self == PROTO_NONE || (env && self == env->getNonePrototype())) return context->fromUTF8String("None");

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
            return PROTO_NONE;
        }
    }
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);

    if (self == PROTO_NONE || (env && self == env->getNonePrototype())) return context->fromUTF8String("None");
    if (self->isString(context)) return self;
    if (self->isInteger(context)) return context->fromUTF8String(std::to_string(self->asLong(context)).c_str());
    if (self->isDouble(context)) return context->fromUTF8String(std::to_string(self->asDouble(context)).c_str());
    if (self == PROTO_TRUE) return context->fromUTF8String("True");
    if (self == PROTO_FALSE) return context->fromUTF8String("False");

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
        const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__");
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__");
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__");
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__");
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
        const proto::ProtoString* keysName = env ? env->getKeysString() : proto::ProtoString::fromUTF8String(context, "__keys__");
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
            if (list->getAt(context, i)->getHash(context) == hash) {
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = env ? env->getAttribute(context, self, iterProtoName) : self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;

    const proto::ProtoList* keysList = nullptr;
    if (self->asSparseList(context)) {
        // Raw sparse list
        const proto::ProtoString* keysName = env ? env->getKeysString() : proto::ProtoString::fromUTF8String(context, "keys");
        const proto::ProtoObject* keysM = env ? env->getAttribute(context, self, keysName) : self->getAttribute(context, keysName);
        if (keysM && keysM->asMethod(context)) {
             const proto::ProtoObject* kObj = keysM->asMethod(context)(context, self, nullptr, env ? env->getEmptyList() : context->newList(), nullptr);
             if (kObj) keysList = kObj->asList(context);
        }
        if (!keysList) {
            // Fallback: extract keys manually? Or just return empty for now if it fails.
            keysList = context->newList();
        }
    } else {
        const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
        const proto::ProtoObject* keysObj = self->getAttribute(context, keysName);
        keysList = keysObj && keysObj->asList(context) ? keysObj->asList(context) : context->newList();
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
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    if (!data || !data->asSparseList(context)) return PROTO_FALSE;
    if (positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    const proto::ProtoObject* key = positionalParameters->getAt(context, 0);
    unsigned long h = key->getHash(context);
    bool found = data->asSparseList(context)->has(context, h);
    if (std::getenv("PROTO_ENV_DIAG")) {
        std::cerr << "[proto-dict] contains self=" << self << " key=" << (key->isString(context) ? "str" : "other") << " hash=" << h << " found=" << found << "\n" << std::flush;
    }
    return found ? PROTO_TRUE : PROTO_FALSE;
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

static std::string repr_object(proto::ProtoContext* context, const proto::ProtoObject* obj) {

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

static const proto::ProtoObject* py_tuple_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* data = self->getAttribute(context, dataName);
    const proto::ProtoTuple* tup = (data && data->asTuple(context)) ? data->asTuple(context) : self->asTuple(context);
    const proto::ProtoList* list = tup ? tup->asList(context) : nullptr;
    if (!list) return context->fromUTF8String("()");

    unsigned long size = list->getSize(context);
    unsigned long limit = 20;
    std::string out = "(";
    for (unsigned long i = 0; i < size && i < limit; ++i) {
        if (i > 0) out += ", ";
        out += repr_object(context, list->getAt(context, static_cast<int>(i)));
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
    const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__");
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
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
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
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
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
            const proto::ProtoObject* otherData = otherObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
            if (otherData) {
                otherList = otherData->asList(context);
                if (!otherList) otherTuple = otherData->asTuple(context);
            }
        }
    }
    
    // Get the current list from self
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
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
        const proto::ProtoObject* otherData = otherObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
        if (otherData) otherList = otherData->asList(context);
    }
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
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


static const proto::ProtoObject* py_set_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)keywordParameters;

    proto::ProtoObject* instance = const_cast<proto::ProtoObject*>(self->newChild(context, true));
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoSet* s = context->newSet();

    if (positionalParameters->getSize(context) >= 1) {
        const proto::ProtoObject* iterable = positionalParameters->getAt(context, 0);
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
        const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
        if (iterM && iterM->asMethod(context)) {
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : context->newList();
            const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, emptyL, nullptr);
            if (it && it != PROTO_NONE) {
                const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");
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

static const proto::ProtoObject* py_set_len(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        const proto::ProtoObject* data = self->getAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"));
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
        const proto::ProtoObject* data = self->getAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"));
        s = data ? data->asSet(context) : nullptr;
    }
    if (!s || positionalParameters->getSize(context) < 1) return PROTO_FALSE;
    return s->has(context, positionalParameters->getAt(context, 0)) ? PROTO_TRUE : PROTO_FALSE;
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
        const proto::ProtoObject* data = self->getAttribute(context, env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__"));
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

static const proto::ProtoObject* py_set_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoSet* s = self->asSet(context);
    if (!s || s->getSize(context) == 0) return context->fromUTF8String("set()");

    std::string out = "{";
    const proto::ProtoSetIterator* it = s->getIterator(context);
    bool first = true;
    while (it->hasNext(context)) {
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
    const proto::ProtoSet* s = self->asSet(context);
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
    if (!iterable || iterable == PROTO_NONE) return;
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoObject* iterM = iterable->getAttribute(context, iterS);
    if (!iterM || !iterM->asMethod(context)) return;
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it || it == PROTO_NONE) return;
    const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(context, "__next__");
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
        while (it->hasNext(context)) {
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
    result->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), acc->asObject(context));
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
                const proto::ProtoSet* os = other->asSet(context);
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
    const proto::ProtoSet* s = self->asSet(context);
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
    const proto::ProtoSet* s = self->asSet(context);
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
        const proto::ProtoSet* cs = container->asSet(context);
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
    const proto::ProtoSet* s = self->asSet(context);
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
        const proto::ProtoSet* cs = container->asSet(context);
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
    const proto::ProtoSet* s = self->asSet(context);
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
    return s->has(context, positionalParameters->getAt(context, 0));
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
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoString* iterProtoName = proto::ProtoString::fromUTF8String(context, "__iter_prototype__");
    const proto::ProtoObject* iterProto = env ? env->getAttribute(context, self, iterProtoName) : self->getAttribute(context, iterProtoName);
    if (!iterProto) return PROTO_NONE;

    const proto::ProtoTuple* tuple = self->asTuple(context);
    const proto::ProtoObject* data = self;
    if (!tuple) {
        const proto::ProtoString* dataName = env ? env->getDataString() : proto::ProtoString::fromUTF8String(context, "__data__");
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
    } else if (sub->isString(context)) {
        sub->asString(context)->toUTF8String(context, needle);
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
    } else if (arg->isString(context)) {
        arg->asString(context)->toUTF8String(context, out);
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
        b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.substr(prefix.size()).c_str()));
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
        b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String(raw.substr(0, raw.size() - suffix.size()).c_str()));
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
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
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
    std::cerr << "py_str_join: starting self=" << (void*)self << std::endl;
    std::cout << "DEBUG: py_str_join: starting self=" << (void*)self << std::endl;
    const proto::ProtoString* sep = str_from_self(context, self);
    if (!sep || !posArgs || posArgs->getSize(context) < 1) {
        std::cerr << "py_str_join: early return sep=" << (void*)sep << " posArgsSize=" << (posArgs ? posArgs->getSize(context) : -1) << std::endl;
        return PROTO_NONE;
    }
    std::string sepStr;
    sep->toUTF8String(context, sepStr);
    const proto::ProtoObject* iterable = posArgs->getAt(context, 0);
    std::cerr << "py_str_join: iterable=" << (void*)iterable << std::endl;
    const proto::ProtoObject* iterM = iterable->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    std::cerr << "py_str_join: iterM=" << (void*)iterM << std::endl;
    if (!iterM || !iterM->asMethod(context)) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseTypeError(context, "join() arg must be an iterable");
        return PROTO_NONE;
    }
    const proto::ProtoObject* it = iterM->asMethod(context)(context, iterable, nullptr, context->newList(), nullptr);
    if (!it || it == PROTO_NONE) {
        std::cerr << "py_str_join: it is null" << std::endl;
        std::cout << "DEBUG: py_str_join: it is null" << std::endl;
        return context->fromUTF8String("");
    }
    std::cerr << "py_str_join: got iterator" << std::endl;
    std::cout << "DEBUG: py_str_join: got iterator" << std::endl;
    const proto::ProtoObject* nextM = it->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    if (!nextM || !nextM->asMethod(context)) {
        std::cerr << "py_str_join: nextM is null or not a method" << std::endl;
        std::cout << "DEBUG: py_str_join: nextM is null" << std::endl;
        return context->fromUTF8String("");
    }
    std::cerr << "py_str_join: got nextM" << std::endl;
    std::cout << "DEBUG: py_str_join: got nextM" << std::endl;
    std::string out;
    bool first = true;
    for (int i = 0; ; i++) {
        std::cerr << "py_str_join: loop iter " << i << std::endl;
        std::cout << "DEBUG: py_str_join: loop iter " << i << std::endl;
        const proto::ProtoObject* item = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
        std::cerr << "py_str_join: iter " << i << " item: " << (void*)item << std::endl;
        std::cout << "DEBUG: py_str_join: iter " << i << " item: " << (void*)item << std::endl;
        if (!item || item == PROTO_NONE) break;
        if (!item->isString(context)) {
            std::cerr << "py_str_join: item is not string" << std::endl;
            std::cout << "DEBUG: py_str_join: item is not string" << std::endl;
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) env->raiseTypeError(context, "sequence item: expected str instance");
            return PROTO_NONE;
        }
        if (!first) out += sepStr;
        first = false;
        std::string part;
        item->asString(context)->toUTF8String(context, part);
        std::cerr << "py_str_join: iter " << i << " part: " << part << std::endl;
        std::cout << "DEBUG: py_str_join: iter " << i << " part: " << part << std::endl;
        out += part;
    }
    std::cerr << "py_str_join: done loop, out size: " << out.size() << std::endl;
    std::cout << "DEBUG: py_str_join: done loop, out size: " << out.size() << std::endl;
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
    unsigned long hash = key->getHash(context);
    if (std::getenv("PROTO_ENV_DIAG")) {
        std::cerr << "[proto-dict] get self=" << self << " key=" << (key->isString(context) ? "str" : "other") << " hash=" << hash << "\n" << std::flush;
    }
    const proto::ProtoObject* val = dict->getAt(context, hash);
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

static const proto::ProtoObject* py_dict_ior(
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

/** thread_local member initialization */
thread_local PythonEnvironment* PythonEnvironment::s_threadEnv = nullptr;
thread_local proto::ProtoContext* PythonEnvironment::s_threadContext = nullptr;
thread_local const proto::ProtoObject* PythonEnvironment::s_currentFrame = nullptr;
std::thread::id PythonEnvironment::s_mainThreadId;
thread_local const proto::ProtoObject* PythonEnvironment::s_currentGlobals = nullptr;

/** Thread-local trace function and pending exception (no mutex in hot path). */
static thread_local const proto::ProtoObject* s_threadTraceFunction = nullptr;
static thread_local const proto::ProtoObject* s_threadPendingException = nullptr;

/** Per-thread resolve cache; generation check makes invalidation lock-free. */
static thread_local std::unordered_map<std::string, const proto::ProtoObject*> s_threadResolveCache;
static thread_local uint64_t s_threadResolveCacheGeneration = 0;

void PythonEnvironment::registerContext(proto::ProtoContext* ctx, PythonEnvironment* env) {
    if (std::getenv("PROTO_THREAD_DIAG")) std::cerr << "[proto-thread] registerContext ctx=" << ctx << " env=" << env << " tid=" << std::this_thread::get_id() << "\n" << std::flush;
    s_threadEnv = env;
    s_threadContext = ctx;
}

void PythonEnvironment::unregisterContext(proto::ProtoContext* ctx) {
    if (std::getenv("PROTO_THREAD_DIAG")) std::cerr << "[proto-thread] unregisterContext ctx=" << ctx << " tid=" << std::this_thread::get_id() << "\n" << std::flush;
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

thread_local const proto::ProtoObject* PythonEnvironment::s_currentCodeObject = nullptr;

void PythonEnvironment::setCurrentCodeObject(const proto::ProtoObject* code) {
    s_currentCodeObject = code;
}

const proto::ProtoObject* PythonEnvironment::getCurrentCodeObject() {
    return s_currentCodeObject;
}

PythonEnvironment* PythonEnvironment::fromContext(proto::ProtoContext* ctx) {
    if (!s_threadEnv && std::getenv("PROTO_THREAD_DIAG")) std::cerr << "[proto-thread] fromContext FAILED ctx=" << ctx << " tid=" << std::this_thread::get_id() << "\n" << std::flush;
    return s_threadEnv;
}

void PythonEnvironment::setTraceFunction(const proto::ProtoObject* func) {
    s_threadTraceFunction = func;
}

const proto::ProtoObject* PythonEnvironment::getTraceFunction() const {
    return s_threadTraceFunction;
}

void PythonEnvironment::setPendingException(const proto::ProtoObject* exc) {
    s_threadPendingException = exc;
}

const proto::ProtoObject* PythonEnvironment::takePendingException() {
    const proto::ProtoObject* e = s_threadPendingException;
    s_threadPendingException = nullptr;
    return e;
}

bool PythonEnvironment::hasPendingException() const {
    return s_threadPendingException != nullptr;
}

const proto::ProtoObject* PythonEnvironment::peekPendingException() const {
    return s_threadPendingException;
}

void PythonEnvironment::clearPendingException() {
    s_threadPendingException = nullptr;
}

bool PythonEnvironment::isStopIteration(const proto::ProtoObject* exc) const {
    if (!exc || !stopIterationType) return false;
    // We use rootContext_ as a base, but ideally we'd use the current context.
    const proto::ProtoObject* res = exc->isInstanceOf(rootContext_, stopIterationType);
    return res == PROTO_TRUE;
}

const proto::ProtoObject* PythonEnvironment::getStopIterationValue(proto::ProtoContext* ctx, const proto::ProtoObject* exc) const {
    if (!exc) return PROTO_NONE;
    const proto::ProtoObject* val = exc->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "value"));
    if (val && val != PROTO_NONE) return val;
    
    const proto::ProtoObject* argsObj = exc->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "args"));
    if (argsObj) {
        if (argsObj->asList(ctx)) {
            const proto::ProtoList* args = argsObj->asList(ctx);
            if (args->getSize(ctx) > 0) return args->getAt(ctx, 0);
        } else if (argsObj->isTuple(ctx)) {
            const proto::ProtoTuple* args = argsObj->asTuple(ctx);
            if (args->getSize(ctx) > 0) return args->getAt(ctx, 0);
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
                                     const std::vector<std::string>& argv) : space_(getProcessSpace()), rootContext_(new proto::ProtoContext(space_)), argv_(argv) {
    int prev = s_pythonEnvInstanceCount.fetch_add(1, std::memory_order_relaxed);
    if (prev > 0) {
        std::cerr << "[protoPython] WARNING: Multiple PythonEnvironment instances (count=" << (prev + 1)
                  << "). L-Shape mandates single ProtoSpace per process." << std::endl;
    }
    s_mainThreadId = std::this_thread::get_id();
    registerContext(rootContext_, this);
    initializeRootObjects(stdLibPath, searchPaths);
}

PythonEnvironment::~PythonEnvironment() {
    // Unregister roots from ProtoSpace to prevent dangling pointers in GC
    if (space_) {
        std::lock_guard<std::mutex> lock(space_->moduleRootsMutex);
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

        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(iterString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(nextString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(emptyList));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(rangeCurString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(rangeStopString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(rangeStepString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(mapFuncString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(mapIterString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(enumIterString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(enumIdxString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(revObjString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(revIdxString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(zipItersString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(filterFuncString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(filterIterString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(classString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(nameString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(callString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(getItemString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(lenString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(boolString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(intString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(floatString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(strString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(reprString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(hashString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(powString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(containsString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(addString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(formatString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(dictString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(docString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(reversedString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(enumProtoS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(revProtoS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(zipProtoS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(filterProtoS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(mapProtoS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(rangeProtoS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(boolTypeS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(filterBoolS));
        
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__code__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__globals__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(co_varnames));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(co_nparams));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(co_automatic_count));
        
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__iadd__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__isub__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__imul__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__itruediv__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__ifloordiv__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__imod__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__ipow__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__ilshift__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__irshift__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__iand__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__ior__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__ixor__));
        
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__and__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__rand__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__or__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__ror__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__xor__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__rxor__));
        
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__invert__));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(__pos__));
        
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(setItemString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(delItemString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(dataString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(keysString));
        
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(startString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(stopString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(stepString));
        
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(ioModuleString));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(openString));

        remove_if_match(zeroInteger);
        remove_if_match(oneInteger);
        
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(listS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(dictS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(tupleS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(setS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(intS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(floatS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(strS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(boolS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(objectS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(typeS));
        remove_if_match(reinterpret_cast<const proto::ProtoObject*>(dictString));
    }

    unregisterContext(rootContext_);
    delete rootContext_;
    s_pythonEnvInstanceCount.fetch_sub(1, std::memory_order_relaxed);
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
    if (!attributeErrorType) return;
    std::string typeName = "object";
    const proto::ProtoObject* cls = obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__class__"));
    if (cls) {
        const proto::ProtoObject* nameAttr = cls->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"));
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
    if (exc) setPendingException(exc);
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
    if (exc) setPendingException(exc);
}

void PythonEnvironment::raiseKeyboardInterrupt(proto::ProtoContext* ctx) {
    if (!keyboardInterruptType) return;
    const proto::ProtoList* args = ctx->newList();
    const proto::ProtoObject* exc = keyboardInterruptType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), keyboardInterruptType, args, nullptr);
    if (exc) setPendingException(exc);
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
    if (exc) setPendingException(exc);
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
    if (exc) setPendingException(exc);
}

void PythonEnvironment::raiseAssertionError(proto::ProtoContext* ctx, const proto::ProtoObject* msg) {
    if (!assertionErrorType) return;
    const proto::ProtoList* args = ctx->newList();
    if (msg) args = args->appendLast(ctx, msg);
    const proto::ProtoObject* exc = assertionErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), assertionErrorType, args, nullptr);
    if (exc) setPendingException(exc);
}

void PythonEnvironment::raiseZeroDivisionError(proto::ProtoContext* ctx) {
    if (!zeroDivisionErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String("division by zero"));
    const proto::ProtoObject* exc = zeroDivisionErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), zeroDivisionErrorType, args, nullptr);
    if (exc) setPendingException(exc);
}

void PythonEnvironment::raiseIndexError(proto::ProtoContext* ctx, const std::string& msg) {
    if (!indexErrorType) return;
    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(msg.c_str()));
    const proto::ProtoObject* exc = indexErrorType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), indexErrorType, args, nullptr);
    if (exc) setPendingException(exc);
}

void PythonEnvironment::raiseStopIteration(proto::ProtoContext* ctx, const proto::ProtoObject* value) {
    if (!stopIterationType) return;
    const proto::ProtoList* args = ctx->newList();
    if (value) args = args->appendLast(ctx, value);
    const proto::ProtoObject* exc = stopIterationType->call(ctx, nullptr, proto::ProtoString::fromUTF8String(ctx, "__call__"), stopIterationType, args, nullptr);
    if (exc) setPendingException(exc);
}

void PythonEnvironment::initializeRootObjects(const std::string& stdLibPath, const std::vector<std::string>& searchPaths) {
    __code__ = proto::ProtoString::fromUTF8String(rootContext_, "__code__");
    __globals__ = proto::ProtoString::fromUTF8String(rootContext_, "__globals__");
    co_varnames = proto::ProtoString::fromUTF8String(rootContext_, "co_varnames");
    co_nparams = proto::ProtoString::fromUTF8String(rootContext_, "co_nparams");
    co_automatic_count = proto::ProtoString::fromUTF8String(rootContext_, "co_automatic_count");
    co_is_generator = proto::ProtoString::fromUTF8String(rootContext_, "co_is_generator");
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
    gi_code = proto::ProtoString::fromUTF8String(rootContext_, "gi_code");
    gi_frame = proto::ProtoString::fromUTF8String(rootContext_, "gi_frame");
    gi_running = proto::ProtoString::fromUTF8String(rootContext_, "gi_running");
    gi_yieldfrom = proto::ProtoString::fromUTF8String(rootContext_, "gi_yieldfrom");
    gi_pc = proto::ProtoString::fromUTF8String(rootContext_, "gi_pc");
    gi_stack = proto::ProtoString::fromUTF8String(rootContext_, "gi_stack");
    gi_locals = proto::ProtoString::fromUTF8String(rootContext_, "gi_locals");

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

    iterString = proto::ProtoString::fromUTF8String(rootContext_, "__iter__");
    nextString = proto::ProtoString::fromUTF8String(rootContext_, "__next__");
    emptyList = rootContext_->newList();
    rangeCurString = proto::ProtoString::fromUTF8String(rootContext_, "_cur");
    rangeStopString = proto::ProtoString::fromUTF8String(rootContext_, "_stop");
    rangeStepString = proto::ProtoString::fromUTF8String(rootContext_, "_step");
    mapFuncString = proto::ProtoString::fromUTF8String(rootContext_, "_func");
    mapIterString = proto::ProtoString::fromUTF8String(rootContext_, "_iter");
    enumIterString = proto::ProtoString::fromUTF8String(rootContext_, "_iter");
    enumIdxString = proto::ProtoString::fromUTF8String(rootContext_, "_idx");
    revObjString = proto::ProtoString::fromUTF8String(rootContext_, "_obj");
    revIdxString = proto::ProtoString::fromUTF8String(rootContext_, "_idx");
    zipItersString = proto::ProtoString::fromUTF8String(rootContext_, "_iters");
    filterFuncString = proto::ProtoString::fromUTF8String(rootContext_, "_func");
    filterIterString = proto::ProtoString::fromUTF8String(rootContext_, "_iter");
    enterString = proto::ProtoString::fromUTF8String(rootContext_, "__enter__");
    exitString = proto::ProtoString::fromUTF8String(rootContext_, "__exit__");
    classString = proto::ProtoString::fromUTF8String(rootContext_, "__class__");
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
    powString = proto::ProtoString::fromUTF8String(rootContext_, "__pow__");
    formatString = proto::ProtoString::fromUTF8String(rootContext_, "__format__");
    dictString = proto::ProtoString::fromUTF8String(rootContext_, "__dict__");
    docString = proto::ProtoString::fromUTF8String(rootContext_, "__doc__");
    reversedString = proto::ProtoString::fromUTF8String(rootContext_, "__reversed__");
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
    keysString = proto::ProtoString::fromUTF8String(rootContext_, "__keys__");
    openString = proto::ProtoString::fromUTF8String(rootContext_, "open");
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

    zeroInteger = rootContext_->fromInteger(0);
    oneInteger = rootContext_->fromInteger(1);

    ioModuleString = proto::ProtoString::fromUTF8String(rootContext_, "__io_module__");

    // Use member variables for initialization
    const proto::ProtoString* py_init = proto::ProtoString::fromUTF8String(rootContext_, "__init__");
    const proto::ProtoString* py_repr = reprString;
    const proto::ProtoString* py_str = strString;
    const proto::ProtoString* py_class = classString;
    const proto::ProtoString* py_name = nameString;
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
    const proto::ProtoString* py_add = addString;

    // 1. Create 'object' base
    objectPrototype = rootContext_->newObject(true);

    const proto::ProtoString* py_format_dunder = proto::ProtoString::fromUTF8String(rootContext_, "__format__");
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_init, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_init));
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_repr));
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_str, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_str));
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_format_dunder, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_format));
    objectPrototype = objectPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(objectPrototype), py_object_call));

    // 2. Create 'type'
    typePrototype = rootContext_->newObject(true);
    typePrototype = typePrototype->addParent(rootContext_, objectPrototype);
    typePrototype = typePrototype->setAttribute(rootContext_, py_class, typePrototype);

    // 3. Circularity: object's class is type
    objectPrototype = objectPrototype->setAttribute(rootContext_, py_class, typePrototype);

    // 4. Create 'frame' prototype
    framePrototype = rootContext_->newObject(true);
    framePrototype = framePrototype->addParent(rootContext_, objectPrototype);
    framePrototype = framePrototype->setAttribute(rootContext_, py_class, typePrototype);
    framePrototype = framePrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("frame"));
    framePrototype = framePrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(framePrototype), py_frame_repr));

    // 5. Create 'generator' prototype
    generatorPrototype = rootContext_->newObject(true);
    generatorPrototype = generatorPrototype->addParent(rootContext_, objectPrototype);
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_class, typePrototype);
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("generator"));
    
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(generatorPrototype), py_generator_iter));
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, py_next, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(generatorPrototype), py_generator_next));
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "send"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(generatorPrototype), py_generator_send));
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "throw"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(generatorPrototype), py_generator_throw));
    generatorPrototype = generatorPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "close"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(generatorPrototype), py_generator_close));

    // 6. Basic types
    intPrototype = rootContext_->newObject(true);
    intPrototype = intPrototype->addParent(rootContext_, objectPrototype);
    const proto::ProtoString* py_hash = proto::ProtoString::fromUTF8String(rootContext_, "__hash__");
    intPrototype = intPrototype->setAttribute(rootContext_, py_class, typePrototype);
    intPrototype = intPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("int"));
    intPrototype = intPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_call));
    intPrototype = intPrototype->setAttribute(rootContext_, py_hash, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_hash));
    intPrototype = intPrototype->setAttribute(rootContext_, py_format_dunder, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_format));
    const proto::ProtoString* py_bit_length = proto::ProtoString::fromUTF8String(rootContext_, "bit_length");
    intPrototype = intPrototype->setAttribute(rootContext_, py_bit_length, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_bit_length));
    intPrototype = intPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "bit_count"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_bit_count));
    const proto::ProtoString* py_from_bytes = proto::ProtoString::fromUTF8String(rootContext_, "from_bytes");
    const proto::ProtoString* py_to_bytes = proto::ProtoString::fromUTF8String(rootContext_, "to_bytes");
    intPrototype = intPrototype->setAttribute(rootContext_, py_from_bytes, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_from_bytes));
    intPrototype = intPrototype->setAttribute(rootContext_, py_to_bytes, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(intPrototype), py_int_to_bytes));

    strPrototype = rootContext_->newObject(true);
    strPrototype = strPrototype->addParent(rootContext_, objectPrototype);
    strPrototype = strPrototype->setAttribute(rootContext_, py_class, typePrototype);
    strPrototype = strPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("str"));
    strPrototype = strPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_iter));
    strPrototype = strPrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_getitem));
    strPrototype = strPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_contains));
    strPrototype = strPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_bool));
    strPrototype = strPrototype->setAttribute(rootContext_, py_upper, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_upper));
    strPrototype = strPrototype->setAttribute(rootContext_, py_lower, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_lower));
    strPrototype = strPrototype->setAttribute(rootContext_, py_format, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_format));
    strPrototype = strPrototype->setAttribute(rootContext_, py_format_dunder, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_format_dunder));
    strPrototype = strPrototype->setAttribute(rootContext_, py_hash, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_hash));
    strPrototype = strPrototype->setAttribute(rootContext_, py_split, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_split));
    strPrototype = strPrototype->setAttribute(rootContext_, py_join, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_join));
    strPrototype = strPrototype->setAttribute(rootContext_, py_strip, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_strip));
    strPrototype = strPrototype->setAttribute(rootContext_, py_lstrip, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_lstrip));
    strPrototype = strPrototype->setAttribute(rootContext_, py_rstrip, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rstrip));
    strPrototype = strPrototype->setAttribute(rootContext_, py_replace, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_replace));
    strPrototype = strPrototype->setAttribute(rootContext_, py_startswith, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_startswith));
    strPrototype = strPrototype->setAttribute(rootContext_, py_endswith, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_endswith));
    const proto::ProtoString* py_find = proto::ProtoString::fromUTF8String(rootContext_, "find");
    const proto::ProtoString* py_index = proto::ProtoString::fromUTF8String(rootContext_, "index");
    strPrototype = strPrototype->setAttribute(rootContext_, py_find, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_find));
    strPrototype = strPrototype->setAttribute(rootContext_, py_index, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_index));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rfind"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rfind));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rindex"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rindex));
    const proto::ProtoString* py_count = proto::ProtoString::fromUTF8String(rootContext_, "count");
    strPrototype = strPrototype->setAttribute(rootContext_, py_count, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_count));
    const proto::ProtoString* py_rsplit = proto::ProtoString::fromUTF8String(rootContext_, "rsplit");
    strPrototype = strPrototype->setAttribute(rootContext_, py_rsplit, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rsplit));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "splitlines"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_splitlines));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "removeprefix"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_removeprefix));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "removesuffix"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_removesuffix));
    const proto::ProtoString* py_center = proto::ProtoString::fromUTF8String(rootContext_, "center");
    const proto::ProtoString* py_ljust = proto::ProtoString::fromUTF8String(rootContext_, "ljust");
    const proto::ProtoString* py_rjust = proto::ProtoString::fromUTF8String(rootContext_, "rjust");
    const proto::ProtoString* py_zfill = proto::ProtoString::fromUTF8String(rootContext_, "zfill");
    const proto::ProtoString* py_partition = proto::ProtoString::fromUTF8String(rootContext_, "partition");
    const proto::ProtoString* py_rpartition = proto::ProtoString::fromUTF8String(rootContext_, "rpartition");
    strPrototype = strPrototype->setAttribute(rootContext_, py_center, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_center));
    strPrototype = strPrototype->setAttribute(rootContext_, py_ljust, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_ljust));
    strPrototype = strPrototype->setAttribute(rootContext_, py_rjust, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rjust));
    strPrototype = strPrototype->setAttribute(rootContext_, py_zfill, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_zfill));
    strPrototype = strPrototype->setAttribute(rootContext_, py_partition, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_partition));
    strPrototype = strPrototype->setAttribute(rootContext_, py_rpartition, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_rpartition));
    const proto::ProtoString* py_expandtabs = proto::ProtoString::fromUTF8String(rootContext_, "expandtabs");
    strPrototype = strPrototype->setAttribute(rootContext_, py_expandtabs, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_expandtabs));
    const proto::ProtoString* py_capitalize = proto::ProtoString::fromUTF8String(rootContext_, "capitalize");
    const proto::ProtoString* py_title = proto::ProtoString::fromUTF8String(rootContext_, "title");
    const proto::ProtoString* py_swapcase = proto::ProtoString::fromUTF8String(rootContext_, "swapcase");
    strPrototype = strPrototype->setAttribute(rootContext_, py_capitalize, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_capitalize));
    strPrototype = strPrototype->setAttribute(rootContext_, py_title, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_title));
    strPrototype = strPrototype->setAttribute(rootContext_, py_swapcase, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_swapcase));
    const proto::ProtoString* py_casefold = proto::ProtoString::fromUTF8String(rootContext_, "casefold");
    const proto::ProtoString* py_isalpha = proto::ProtoString::fromUTF8String(rootContext_, "isalpha");
    const proto::ProtoString* py_isdigit = proto::ProtoString::fromUTF8String(rootContext_, "isdigit");
    const proto::ProtoString* py_isspace = proto::ProtoString::fromUTF8String(rootContext_, "isspace");
    const proto::ProtoString* py_isalnum = proto::ProtoString::fromUTF8String(rootContext_, "isalnum");
    strPrototype = strPrototype->setAttribute(rootContext_, py_casefold, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_casefold));
    strPrototype = strPrototype->setAttribute(rootContext_, py_isalpha, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isalpha));
    strPrototype = strPrototype->setAttribute(rootContext_, py_isdigit, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isdigit));
    strPrototype = strPrototype->setAttribute(rootContext_, py_isspace, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isspace));
    strPrototype = strPrototype->setAttribute(rootContext_, py_isalnum, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isalnum));
    const proto::ProtoString* py_isupper = proto::ProtoString::fromUTF8String(rootContext_, "isupper");
    const proto::ProtoString* py_islower = proto::ProtoString::fromUTF8String(rootContext_, "islower");
    strPrototype = strPrototype->setAttribute(rootContext_, py_isupper, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isupper));
    strPrototype = strPrototype->setAttribute(rootContext_, py_islower, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_islower));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isidentifier"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isidentifier));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isprintable"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isprintable));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isascii"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isascii));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isdecimal"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isdecimal));
    strPrototype = strPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isnumeric"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_isnumeric));

    listPrototype = rootContext_->newObject(true);
    listPrototype = listPrototype->addParent(rootContext_, objectPrototype);
    listPrototype = listPrototype->setAttribute(rootContext_, py_class, typePrototype);
    listPrototype = listPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("list"));
    listPrototype = listPrototype->setAttribute(rootContext_, py_append, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_append));
    listPrototype = listPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_len));
    listPrototype = listPrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_getitem));
    listPrototype = listPrototype->setAttribute(rootContext_, py_setitem, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_setitem));
    listPrototype = listPrototype->setAttribute(rootContext_, delItemString, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_delitem));
    listPrototype = listPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_iter));
    listPrototype = listPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_contains));
    listPrototype = listPrototype->setAttribute(rootContext_, py_eq, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_eq));
    listPrototype = listPrototype->setAttribute(rootContext_, py_lt, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_lt));
    listPrototype = listPrototype->setAttribute(rootContext_, py_le, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_le));
    listPrototype = listPrototype->setAttribute(rootContext_, py_gt, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_gt));
    listPrototype = listPrototype->setAttribute(rootContext_, py_ge, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_ge));
    listPrototype = listPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_repr));
    listPrototype = listPrototype->setAttribute(rootContext_, py_str, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_str));
    listPrototype = listPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_bool));
    listPrototype = listPrototype->setAttribute(rootContext_, py_pop, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_pop));
    listPrototype = listPrototype->setAttribute(rootContext_, py_extend, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_extend));
    listPrototype = listPrototype->setAttribute(rootContext_, py_insert, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_insert));
    listPrototype = listPrototype->setAttribute(rootContext_, py_remove, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_remove));
    listPrototype = listPrototype->setAttribute(rootContext_, py_clear, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_clear));
    listPrototype = listPrototype->setAttribute(rootContext_, py_reverse, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_reverse));
    listPrototype = listPrototype->setAttribute(rootContext_, py_sort, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_sort));
    listPrototype = listPrototype->setAttribute(rootContext_, py_copy, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_copy));
    const proto::ProtoString* py_list_index_name = proto::ProtoString::fromUTF8String(rootContext_, "index");
    const proto::ProtoString* py_list_count_name = proto::ProtoString::fromUTF8String(rootContext_, "count");
    listPrototype = listPrototype->setAttribute(rootContext_, py_list_index_name, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_index));
    listPrototype = listPrototype->setAttribute(rootContext_, py_list_count_name, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_count));
    const proto::ProtoString* py_mul = proto::ProtoString::fromUTF8String(rootContext_, "__mul__");
    const proto::ProtoString* py_rmul = proto::ProtoString::fromUTF8String(rootContext_, "__rmul__");
    listPrototype = listPrototype->setAttribute(rootContext_, py_mul, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_mul));
    listPrototype = listPrototype->setAttribute(rootContext_, py_rmul, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_mul));
    listPrototype = listPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__iadd__"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_iadd));

    const proto::ProtoObject* listIterProto = rootContext_->newObject(true);
    listIterProto = listIterProto->addParent(rootContext_, objectPrototype);
    listIterProto = listIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listIterProto), py_list_iter_next));
    listPrototype = listPrototype->setAttribute(rootContext_, py_iter_proto, listIterProto);
    const proto::ProtoObject* listReverseIterProto = rootContext_->newObject(true);
    listReverseIterProto = listReverseIterProto->addParent(rootContext_, objectPrototype);
    listReverseIterProto = listReverseIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listReverseIterProto), py_list_reversed_next));
    listPrototype = listPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__reversed_prototype__"), listReverseIterProto);
    listPrototype = listPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__reversed__"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(listPrototype), py_list_reversed));

    dictPrototype = rootContext_->newObject(true);
    dictPrototype = dictPrototype->addParent(rootContext_, objectPrototype);
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_class, typePrototype);
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("dict"));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_getitem));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_setitem, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_setitem));
    dictPrototype = dictPrototype->setAttribute(rootContext_, delItemString, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_delitem));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_len));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_iter));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_iter_proto, listIterProto);
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_contains));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_eq, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_eq));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_lt, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_lt));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_le, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_le));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_gt, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_gt));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_ge, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_ge));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_repr));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_str, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_str));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_bool));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_keys, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_keys));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_values, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_values));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_items, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_items));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_get, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_get));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_setdefault, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_setdefault));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_pop, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_pop));
    const proto::ProtoString* py_popitem = proto::ProtoString::fromUTF8String(rootContext_, "popitem");
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_popitem, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_popitem));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_update, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_update));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_clear, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_clear));
    dictPrototype = dictPrototype->setAttribute(rootContext_, py_copy, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_copy));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "fromkeys"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_fromkeys));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__or__"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_or));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__ror__"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_ror));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__ior__"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_ior));
    dictPrototype = dictPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__iror__"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(dictPrototype), py_dict_iror));

    tuplePrototype = rootContext_->newObject(true);
    tuplePrototype = tuplePrototype->addParent(rootContext_, objectPrototype);
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_class, typePrototype);
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("tuple"));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_len));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_getitem));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_iter));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_contains));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_bool));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_hash, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_hash));
    const proto::ProtoString* py_tuple_index_name = proto::ProtoString::fromUTF8String(rootContext_, "index");
    const proto::ProtoString* py_tuple_count_name = proto::ProtoString::fromUTF8String(rootContext_, "count");
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_tuple_index_name, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_index));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_tuple_count_name, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_count));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_repr));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_eq, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tuplePrototype), py_tuple_eq));

    const proto::ProtoObject* tupleIterProto = rootContext_->newObject(true);
    tupleIterProto = tupleIterProto->addParent(rootContext_, objectPrototype);
    tupleIterProto = tupleIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(tupleIterProto), py_tuple_iter_next));
    tuplePrototype = tuplePrototype->setAttribute(rootContext_, py_iter_proto, tupleIterProto);

    setPrototype = rootContext_->newObject(true);
    setPrototype = setPrototype->addParent(rootContext_, objectPrototype);
    setPrototype = setPrototype->setAttribute(rootContext_, py_class, typePrototype);
    setPrototype = setPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("set"));
    setPrototype = setPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_len));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "__call__"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_call));
    setPrototype = setPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_contains));
    setPrototype = setPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_bool));
    setPrototype = setPrototype->setAttribute(rootContext_, py_add, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_add));
    setPrototype = setPrototype->setAttribute(rootContext_, py_remove, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_remove));
    setPrototype = setPrototype->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_repr));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "pop"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_pop));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "discard"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_discard));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "copy"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_copy));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "clear"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_clear));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "union"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_union));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "intersection"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_intersection));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "difference"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_difference));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "symmetric_difference"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_symmetric_difference));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "issubset"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_issubset));
    setPrototype = setPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "issuperset"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_issuperset));
    setPrototype = setPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setPrototype), py_set_iter));

    const proto::ProtoObject* setIterProto = rootContext_->newObject(true);
    setIterProto = setIterProto->addParent(rootContext_, objectPrototype);
    setIterProto = setIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(setIterProto), py_set_iter_next));
    setPrototype = setPrototype->setAttribute(rootContext_, py_iter_proto, setIterProto);

    const proto::ProtoString* py_call = proto::ProtoString::fromUTF8String(rootContext_, "__call__");
    frozensetPrototype = rootContext_->newObject(true);
    frozensetPrototype = frozensetPrototype->addParent(rootContext_, objectPrototype);
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_class, typePrototype);
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("frozenset"));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_call, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_call));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_len));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_contains, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_contains));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_bool, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_bool));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_iter));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_hash, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(frozensetPrototype), py_frozenset_hash));
    frozensetPrototype = frozensetPrototype->setAttribute(rootContext_, py_iter_proto, setIterProto);

    bytesPrototype = rootContext_->newObject(true);
    bytesPrototype = bytesPrototype->addParent(rootContext_, objectPrototype);
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_class, typePrototype);
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("bytes"));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_len, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_len));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_getitem, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_getitem));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_iter, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_iter));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_call, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_call));

    const proto::ProtoObject* bytesIterProto = rootContext_->newObject(true);
    bytesIterProto = bytesIterProto->addParent(rootContext_, objectPrototype);
    bytesIterProto = bytesIterProto->setAttribute(rootContext_, py_next, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesIterProto), py_bytes_iter_next));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_iter_proto, bytesIterProto);
    const proto::ProtoString* py_encode = proto::ProtoString::fromUTF8String(rootContext_, "encode");
    const proto::ProtoString* py_decode = proto::ProtoString::fromUTF8String(rootContext_, "decode");
    strPrototype = strPrototype->setAttribute(rootContext_, py_encode, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(strPrototype), py_str_encode));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_decode, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_decode));
    const proto::ProtoString* py_hex = proto::ProtoString::fromUTF8String(rootContext_, "hex");
    const proto::ProtoString* py_fromhex = proto::ProtoString::fromUTF8String(rootContext_, "fromhex");
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_hex, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_hex));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_fromhex, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_fromhex));
    const proto::ProtoString* py_bytes_find_name = proto::ProtoString::fromUTF8String(rootContext_, "find");
    const proto::ProtoString* py_bytes_count_name = proto::ProtoString::fromUTF8String(rootContext_, "count");
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_bytes_find_name, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_find));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, py_bytes_count_name, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_count));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "index"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_index));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rfind"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_rfind));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rindex"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_rindex));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "startswith"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_startswith));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "endswith"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_endswith));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "strip"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_strip));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "lstrip"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_lstrip));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "rstrip"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_rstrip));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "split"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_split));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "join"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_join));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "replace"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_replace));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isdigit"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_isdigit));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isalpha"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_isalpha));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "isascii"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_isascii));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "removeprefix"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_removeprefix));
    bytesPrototype = bytesPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "removesuffix"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(bytesPrototype), py_bytes_removesuffix));

    sliceType = rootContext_->newObject(true);
    sliceType = sliceType->addParent(rootContext_, objectPrototype);
    sliceType = sliceType->setAttribute(rootContext_, py_class, typePrototype);
    sliceType = sliceType->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("slice"));
    sliceType = sliceType->setAttribute(rootContext_, py_call, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(sliceType), py_slice_call));
    sliceType = sliceType->setAttribute(rootContext_, py_repr, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(sliceType), py_slice_repr));

    floatPrototype = rootContext_->newObject(true);
    floatPrototype = floatPrototype->addParent(rootContext_, objectPrototype);
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_class, typePrototype);
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("float"));
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_call, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(floatPrototype), py_float_call));
    const proto::ProtoString* py_is_integer = proto::ProtoString::fromUTF8String(rootContext_, "is_integer");
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_is_integer, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(floatPrototype), py_float_is_integer));
    const proto::ProtoString* py_as_integer_ratio = proto::ProtoString::fromUTF8String(rootContext_, "as_integer_ratio");
    floatPrototype = floatPrototype->setAttribute(rootContext_, py_as_integer_ratio, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(floatPrototype), py_float_as_integer_ratio));
    floatPrototype = floatPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "hex"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(floatPrototype), py_float_hex));
    floatPrototype = floatPrototype->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "fromhex"), rootContext_->fromMethod(const_cast<proto::ProtoObject*>(floatPrototype), py_float_fromhex));

    boolPrototype = rootContext_->newObject(true);
    boolPrototype = boolPrototype->addParent(rootContext_, intPrototype);
    boolPrototype = boolPrototype->setAttribute(rootContext_, py_class, typePrototype);
    boolPrototype = boolPrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("bool"));
    boolPrototype = boolPrototype->setAttribute(rootContext_, py_call, rootContext_->fromMethod(const_cast<proto::ProtoObject*>(boolPrototype), py_bool_call));
    
    // V72: Register core prototypes in ProtoSpace so primitives can resolve methods
    nonePrototype = space_->nonePrototype;
    if (!nonePrototype || nonePrototype == PROTO_NONE) {
        nonePrototype = rootContext_->newObject(true);
        space_->nonePrototype = const_cast<proto::ProtoObject*>(nonePrototype);
    }
    nonePrototype = nonePrototype->addParent(rootContext_, objectPrototype);
    nonePrototype = nonePrototype->setAttribute(rootContext_, py_class, typePrototype);
    nonePrototype = nonePrototype->setAttribute(rootContext_, py_name, rootContext_->fromUTF8String("NoneType"));
    space_->nonePrototype = const_cast<proto::ProtoObject*>(nonePrototype);

    space_->objectPrototype = const_cast<proto::ProtoObject*>(objectPrototype);
    space_->stringPrototype = const_cast<proto::ProtoObject*>(strPrototype);
    space_->smallIntegerPrototype = const_cast<proto::ProtoObject*>(intPrototype);
    space_->doublePrototype = const_cast<proto::ProtoObject*>(floatPrototype);
    space_->booleanPrototype = const_cast<proto::ProtoObject*>(boolPrototype);
    space_->listPrototype = const_cast<proto::ProtoObject*>(listPrototype);
    space_->tuplePrototype = const_cast<proto::ProtoObject*>(tuplePrototype);
    space_->sparseListPrototype = const_cast<proto::ProtoObject*>(dictPrototype);
    space_->methodPrototype = const_cast<proto::ProtoObject*>(objectPrototype); // Methods inherit from object

    // 5. Initialize Native Module Provider
    auto& registry = proto::ProviderRegistry::instance();
    auto nativeProvider = std::make_unique<NativeModuleProvider>();

    // sys module (argv set later via setArgv before executeModule)
    sysModule = sys::initialize(rootContext_, this, &argv_);
    nativeProvider->registerModule("sys", [this](proto::ProtoContext* ctx) { return sysModule; });

    // _io module (created before builtins so open() can delegate)
    const proto::ProtoObject* ioModule = io::initialize(rootContext_);
    nativeProvider->registerModule("_io", [ioModule](proto::ProtoContext*) { return ioModule; });

    // builtins module
    builtinsModule = builtins::initialize(rootContext_, objectPrototype, typePrototype, intPrototype, strPrototype, listPrototype, dictPrototype, tuplePrototype, setPrototype, bytesPrototype, nonePrototype, sliceType, frozensetPrototype, floatPrototype, boolPrototype, ioModule);
    nativeProvider->registerModule("builtins", [this](proto::ProtoContext* ctx) { return builtinsModule; });

    // _collections module
    nativeProvider->registerModule("_collections", [this](proto::ProtoContext* ctx) { return collections::initialize(ctx, dictPrototype); });
    nativeProvider->registerModule("logging", [](proto::ProtoContext* ctx) { return logging::initialize(ctx); });
    nativeProvider->registerModule("operator", [](proto::ProtoContext* ctx) { return operator_::initialize(ctx); });
    nativeProvider->registerModule("math", [](proto::ProtoContext* ctx) { return math::initialize(ctx); });
    nativeProvider->registerModule("time", [](proto::ProtoContext* ctx) { return time_module::initialize(ctx); });
    nativeProvider->registerModule("_os", [](proto::ProtoContext* ctx) { return os_module::initialize(ctx); });
    nativeProvider->registerModule("_signal", [](proto::ProtoContext* ctx) { return signal_module::initialize(ctx); });
    nativeProvider->registerModule("_thread", [](proto::ProtoContext* ctx) { return thread_module::initialize(ctx); });
    nativeProvider->registerModule("functools", [](proto::ProtoContext* ctx) { return functools::initialize(ctx); });
    nativeProvider->registerModule("itertools", [](proto::ProtoContext* ctx) { return itertools::initialize(ctx); });
    nativeProvider->registerModule("re", [](proto::ProtoContext* ctx) { return re::initialize(ctx); });
    nativeProvider->registerModule("json", [](proto::ProtoContext* ctx) { return json::initialize(ctx); });
    nativeProvider->registerModule("os.path", [](proto::ProtoContext* ctx) { return os_path::initialize(ctx); });
    nativeProvider->registerModule("pathlib", [](proto::ProtoContext* ctx) { return pathlib::initialize(ctx); });
    nativeProvider->registerModule("collections.abc", [](proto::ProtoContext* ctx) { return collections_abc::initialize(ctx); });
    nativeProvider->registerModule("atexit", [](proto::ProtoContext* ctx) { return atexit_module::initialize(ctx); });

    const proto::ProtoObject* exceptionsMod = exceptions::initialize(rootContext_, objectPrototype, typePrototype);
    keyErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "KeyError"));
    valueErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "ValueError"));
    nameErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "NameError"));
    attributeErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "AttributeError"));
    syntaxErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "SyntaxError"));
    typeErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "TypeError"));
    importErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "ImportError"));
    keyboardInterruptType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "KeyboardInterrupt"));
    systemExitType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "SystemExit"));
    recursionErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "RecursionError"));
    stopIterationType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "StopIteration"));
    eofErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "EOFError"));
    assertionErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "AssertionError"));
    zeroDivisionErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "ZeroDivisionError"));
    indexErrorType = exceptionsMod->getAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "IndexError"));

    // Expose all exceptions in builtins
    if (builtinsModule) {
        static const std::vector<std::pair<std::string, const proto::ProtoObject**>> excMap = {
            {"KeyError", &keyErrorType},
            {"ValueError", &valueErrorType},
            {"NameError", &nameErrorType},
            {"AttributeError", &attributeErrorType},
            {"SyntaxError", &syntaxErrorType},
            {"TypeError", &typeErrorType},
            {"ImportError", &importErrorType},
            {"KeyboardInterrupt", &keyboardInterruptType},
            {"SystemExit", &systemExitType},
            {"RecursionError", &recursionErrorType},
            {"StopIteration", &stopIterationType},
            {"EOFError", &eofErrorType},
            {"AssertionError", &assertionErrorType},
            {"ZeroDivisionError", &zeroDivisionErrorType},
            {"IndexError", &indexErrorType}
        };
        for (const auto& pair : excMap) {
            if (*pair.second) {
                builtinsModule = builtinsModule->setAttribute(rootContext_, 
                    proto::ProtoString::fromUTF8String(rootContext_, pair.first.c_str()), 
                    *pair.second);
            }
        }
    }

    nativeProvider->registerModule("exceptions", [exceptionsMod](proto::ProtoContext*) { return exceptionsMod; });

    // V72: strings and roots already initialized at top of function.

    registry.registerProvider(std::move(nativeProvider));

    // 6. Initialize StdLib Module Provider
    std::vector<std::string> allPaths;
    if (!stdLibPath.empty()) allPaths.push_back(stdLibPath);
    else allPaths.push_back("./lib/python3.14");
    for (const auto& p : searchPaths) allPaths.push_back(p);
    
    registry.registerProvider(std::make_unique<PythonModuleProvider>(allPaths));
    registry.registerProvider(std::make_unique<CompiledModuleProvider>(allPaths));
    registry.registerProvider(std::make_unique<HPyModuleProvider>(allPaths));
    
    // 7. Populate sys.path and sys.modules
    const proto::ProtoString* py_path = proto::ProtoString::fromUTF8String(rootContext_, "path");
    const proto::ProtoString* py_modules = proto::ProtoString::fromUTF8String(rootContext_, "modules");
    
    // a. Update sys.path
    const proto::ProtoObject* pathListObj = sysModule->getAttribute(rootContext_, py_path);
    const proto::ProtoList* pList = (pathListObj && pathListObj->asList(rootContext_)) 
        ? pathListObj->asList(rootContext_) : rootContext_->newList();
    
    // If it's a Python list object, unwrap it
    const proto::ProtoObject* dataAttr = pathListObj ? pathListObj->getAttribute(rootContext_, dataString) : nullptr;
    if (dataAttr && dataAttr->asList(rootContext_)) pList = dataAttr->asList(rootContext_);

    for (const auto& p : allPaths) {
        pList = pList->appendLast(rootContext_, rootContext_->fromUTF8String(p.c_str()));
    }
    
    // Wrap back in a Python list object
    const proto::ProtoObject* newListObj = rootContext_->newObject(true);
    if (listPrototype) newListObj = newListObj->addParent(rootContext_, listPrototype);
    newListObj = newListObj->setAttribute(rootContext_, dataString, pList->asObject(rootContext_));
    sysModule = sysModule->setAttribute(rootContext_, py_path, newListObj);
    
    // b. Create sys.modules and add sys/builtins
    const proto::ProtoObject* modulesDictObj = sysModule->getAttribute(rootContext_, py_modules);
    if (modulesDictObj) {
        modulesDictObj = modulesDictObj->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "sys"), sysModule);
        modulesDictObj = modulesDictObj->setAttribute(rootContext_, proto::ProtoString::fromUTF8String(rootContext_, "builtins"), builtinsModule);
        sysModule = sysModule->setAttribute(rootContext_, py_modules, modulesDictObj);
    }

    // 8. Prepend to resolution chain: ensure provider:native is first so native modules
    //    (_thread, _os, etc.) resolve before any file-based lookup.
    const proto::ProtoObject* chainObj = rootContext_->space->getResolutionChain();
    const proto::ProtoList* chain = chainObj && chainObj != PROTO_NONE
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
        std::lock_guard<std::mutex> lock(space_->moduleRootsMutex);
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

        addRoot(reinterpret_cast<const proto::ProtoObject*>(iterString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(nextString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(emptyList));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(rangeCurString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(rangeStopString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(rangeStepString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(mapFuncString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(mapIterString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(enumIterString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(enumIdxString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(revObjString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(revIdxString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(zipItersString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(filterFuncString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(filterIterString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(classString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(nameString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(callString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(getItemString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(lenString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(boolString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(intString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(floatString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(strString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(reprString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(hashString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(powString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(containsString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(addString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(formatString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(dictString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(docString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(reversedString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(enumProtoS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(revProtoS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(zipProtoS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(filterProtoS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(mapProtoS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(rangeProtoS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(boolTypeS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(filterBoolS));
        
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__code__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__globals__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(co_varnames));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(co_nparams));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(co_automatic_count));
        
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__iadd__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__isub__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__imul__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__itruediv__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__ifloordiv__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__imod__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__ipow__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__ilshift__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__irshift__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__iand__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__ior__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__ixor__));
        
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__and__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__rand__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__or__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__ror__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__xor__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__rxor__));
        
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__invert__));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(__pos__));
        
        addRoot(reinterpret_cast<const proto::ProtoObject*>(setItemString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(delItemString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(dataString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(keysString));
        
        addRoot(reinterpret_cast<const proto::ProtoObject*>(startString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(stopString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(stepString));
        
        addRoot(reinterpret_cast<const proto::ProtoObject*>(ioModuleString));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(openString));

        addRoot(zeroInteger);
        addRoot(oneInteger);
        
        addRoot(reinterpret_cast<const proto::ProtoObject*>(listS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(dictS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(tupleS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(setS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(intS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(floatS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(strS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(boolS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(objectS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(typeS));
        addRoot(reinterpret_cast<const proto::ProtoObject*>(dictString));
    }
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
        if (std::getenv("PROTO_THREAD_DIAG"))
            std::cerr << "[proto-thread-diag] runModuleMain calling main (native)\n" << std::flush;
        mainAttr->asMethod(context)(context, const_cast<proto::ProtoObject*>(m), nullptr, emptyArgs, nullptr);
        return 0;
    }
    /* User-defined function: call via __call__ (self = function object). */
    const proto::ProtoString* callName = proto::ProtoString::fromUTF8String(context, "__call__");
    const proto::ProtoObject* callAttr = mainAttr->getAttribute(context, callName);
    if (callAttr && callAttr->asMethod(context)) {
        if (std::getenv("PROTO_THREAD_DIAG"))
            std::cerr << "[proto-thread-diag] runModuleMain calling main (user def)\n" << std::flush;
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
        mod = mod->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"), mainName->asObject(context));
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
            const proto::ProtoObject* excAfterExec = takePendingException();
            if (excAfterExec && excAfterExec != PROTO_NONE) {
                handleException(excAfterExec, mod, std::cerr);
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
    if (std::getenv("PROTO_ENV_DIAG"))
        std::cerr << "[proto-env] executeModule " << moduleName << " modWrapper=" << modWrapper << "\n" << std::flush;
    if (!modWrapper || modWrapper == PROTO_NONE) {
        if (std::getenv("PROTO_ENV_DIAG"))
            std::cerr << "[proto-env] executeModule: getImportModule failed for " << moduleName << "\n" << std::flush;
        return -1;
    }
    const proto::ProtoObject* mod = modWrapper->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"));
    if (std::getenv("PROTO_ENV_DIAG"))
        std::cerr << "[proto-env] executeModule " << moduleName << " mod=" << mod << "\n" << std::flush;
    if (!mod || mod == PROTO_NONE) return -1;

    if (asMain) {
        const proto::ProtoString* nameS = proto::ProtoString::fromUTF8String(ctx, "__name__");
        const proto::ProtoObject* mainS = ctx->fromUTF8String("__main__");
        mod = mod->setAttribute(ctx, nameS, mainS);
        // Step V74: Update the wrapper's "val" so future resolves for this module 
        // (even if called via the original module name) see it as __main__.
        const_cast<proto::ProtoObject*>(modWrapper)->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"), mod);
        
        if (std::getenv("PROTO_THREAD_DIAG")) {
            const proto::ProtoObject* nObj = mod->getAttribute(ctx, nameS);
            std::string nVal;
            if (nObj && nObj->isString(ctx)) nObj->asString(ctx)->toUTF8String(ctx, nVal);
            std::cerr << "[proto-thread-diag] executeModule " << moduleName << " SET asMain=true, new __name__=" << nVal << "\n" << std::flush;
        }
    }

    const proto::ProtoString* fileKey = proto::ProtoString::fromUTF8String(ctx, "__file__");
    const proto::ProtoString* executedKey = proto::ProtoString::fromUTF8String(ctx, "__executed__");
    const proto::ProtoObject* fileObj = mod->getAttribute(ctx, fileKey);
    const proto::ProtoObject* execVal = mod->getAttribute(ctx, executedKey);
    const bool willExec = fileObj && fileObj->isString(ctx) && (!execVal || execVal == PROTO_NONE || execVal == PROTO_FALSE);
    if (std::getenv("PROTO_THREAD_DIAG"))
        std::cerr << "[proto-thread-diag] executeModule " << moduleName << " willExec=" << (willExec ? 1 : 0) << "\n" << std::flush;
    if (willExec) {
        std::string path;
        fileObj->asString(ctx)->toUTF8String(ctx, path);
        if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".py") == 0) {
            std::ifstream f(path);
            if (std::getenv("PROTO_THREAD_DIAG"))
                std::cerr << "[proto-thread-diag] open path=" << path << " good=" << (f ? 1 : 0) << "\n" << std::flush;
            if (f) {
                std::stringstream buf;
                buf << f.rdbuf();
                std::string source = buf.str();
                f.close();
                Parser parser(source);
                std::unique_ptr<ModuleNode> node = parser.parseModule();
                if (std::getenv("PROTO_THREAD_DIAG")) {
                    std::cerr << "[proto-thread-diag] parseModule " << moduleName << " node=" << node.get() << " err=" << (parser.hasError() ? 1 : 0) << "\n" << std::flush;
                    if (parser.hasError()) {
                        std::cerr << "[proto-thread-diag] parseModule " << moduleName << " ERROR: " << parser.getLastErrorMsg() 
                                  << " at line " << parser.getLastErrorLine() << ":" << parser.getLastErrorColumn() << "\n" << std::flush;
                    }
                }
                if (!parser.hasError() && node) {
                    Compiler compiler(ctx, path);
                    bool compileOk = compiler.compileModule(node.get());
                    if (std::getenv("PROTO_THREAD_DIAG"))
                        std::cerr << "[proto-thread-diag] compileModule " << moduleName << " ok=" << (compileOk ? 1 : 0) << "\n" << std::flush;
                    if (compileOk) {
                        if (std::getenv("PROTO_ENV_DIAG"))
                            std::cerr << "[proto-env] executeModule: compiling " << moduleName << "\n" << std::flush;
                        const proto::ProtoObject* codeObj = makeCodeObject(ctx, compiler.getConstants(), compiler.getNames(), compiler.getBytecode(), ctx->fromUTF8String(path.c_str())->asString(ctx));
                        if (codeObj) {
                            if (std::getenv("PROTO_ENV_DIAG"))
                                std::cerr << "[proto-env] executeModule: about to run " << moduleName << "\n" << std::flush;
                            proto::ProtoObject* mutableMod = const_cast<proto::ProtoObject*>(mod);
                            
                            // Batch 1: Set frame attributes on module object
                            mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, getFBackString(), PythonEnvironment::getCurrentFrame()));
                            mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, getFCodeString(), codeObj));
                            mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, getFGlobalsString(), mutableMod));
                            mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, getFLocalsString(), mutableMod));

                            const proto::ProtoObject* oldGlobals = getCurrentGlobals();
                            setCurrentGlobals(mutableMod);
                            // Set executed flag early to prevent double execution if an error occurs
                            mutableMod = const_cast<proto::ProtoObject*>(mutableMod->setAttribute(ctx, executedKey, PROTO_TRUE));
                            s_threadResolveCache[moduleName] = mutableMod;
                            if (std::getenv("PROTO_THREAD_DIAG")) {
                                const proto::ProtoObject* nObj = mutableMod->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"));
                                std::string nVal;
                                if (nObj && nObj->isString(ctx)) nObj->asString(ctx)->toUTF8String(ctx, nVal);
                                std::cerr << "[proto-thread-diag] BEFORE runCodeObject " << moduleName << " __name__=" << nVal << "\n" << std::flush;
                            }
                            runCodeObject(ctx, codeObj, mutableMod);
                            setCurrentGlobals(oldGlobals);
                            mod = mutableMod;
                            const proto::ProtoObject* excAfterExec = takePendingException();
                            if (excAfterExec && excAfterExec != PROTO_NONE) {
                                handleException(excAfterExec, mod, std::cerr);
                                return -2;
                            }
                            // Update sys.modules
                            if (sysModule) {
                                const proto::ProtoObject* mods = sysModule->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modules"));
                                if (mods) {
                                    mods = mods->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, moduleName.c_str()), mod);
                                    sysModule = sysModule->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modules"), mods);
                                }
                            }
                            // Update the wrapper's "val" so future resolves for this module see it as populated
                            const_cast<proto::ProtoObject*>(modWrapper)->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"), mod);
                        }
                    }
                } else if (parser.hasError()) {
                    if (std::getenv("PROTO_ENV_DIAG"))
                        std::cerr << "[proto-env] executeModule: parser error for " << moduleName << "\n" << std::flush;
                    handleException(takePendingException(), mod, std::cerr);
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

    if (std::getenv("PROTO_THREAD_DIAG")) {
        const proto::ProtoObject* m = resolve(moduleName, ctx);
        const proto::ProtoObject* mainAttr = m && m != PROTO_NONE
            ? m->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "main")) : nullptr;
        std::cerr << "[proto-thread-diag] before runModuleMain hasMain=" << (mainAttr && mainAttr != PROTO_NONE ? 1 : 0)
                  << " isMethod=" << (mainAttr && mainAttr->isMethod(ctx) ? 1 : 0) << "\n" << std::flush;
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
        
        const proto::ProtoObject* codeObj = f->getAttribute(nonConstCtx, getFCodeString());
        if (codeObj && codeObj != PROTO_NONE) {
            const proto::ProtoObject* fileObj = codeObj->getAttribute(nonConstCtx, proto::ProtoString::fromUTF8String(nonConstCtx, "co_filename"));
            if (fileObj && fileObj->isString(nonConstCtx)) {
                fileObj->asString(nonConstCtx)->toUTF8String(nonConstCtx, filename);
            }
            
            const proto::ProtoObject* nameObj = codeObj->getAttribute(nonConstCtx, proto::ProtoString::fromUTF8String(nonConstCtx, "co_name"));
            if (nameObj && nameObj->isString(nonConstCtx)) {
                nameObj->asString(nonConstCtx)->toUTF8String(nonConstCtx, funcName);
            }
        }
        
        out += "  File \"" + filename + "\", in " + funcName + "\n";
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
                        const proto::ProtoString* s = reinterpret_cast<const proto::ProtoString*>(key);
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
    const proto::ProtoObject* type = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
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
        const proto::ProtoObject* nameObj = type->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"));
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

    const proto::ProtoObject* py_class = exc->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
    const proto::ProtoObject* py_name = py_class ? py_class->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__")) : nullptr;
    
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
        std::cerr << "[proto-repl-diag] FAILED to open history file: " << historyPath << "\n";
    } else {
        if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-repl-diag] Opened history file: " << historyPath << "\n";
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
            std::cerr << "[proto-repl-diag] command=\"" << buffer << "\" frame=" << frame << "\n" << std::flush;
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
                const proto::ProtoObject* type = pending->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"));
                if (type) {
                    const proto::ProtoObject* nameObj = type->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"));
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

const proto::ProtoObject* PythonEnvironment::getAttribute(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* name) {
    if (!obj) return nullptr;
    static bool envDiag = std::getenv("PROTO_ENV_DIAG") != nullptr;
    if (envDiag) {
        proto::ProtoObjectPointer p{};
        p.oid = reinterpret_cast<const proto::ProtoObject*>(name);
        std::cerr << "[getAttribute-diag] name=" << name << " tag=" << p.op.pointer_tag << "\n" << std::flush;
    }
    if (isEmbeddedValue(obj)) return obj->getAttribute(ctx, name);

    const proto::ProtoObject* val = name ? obj->getAttribute(ctx, name) : nullptr;
    if (!val && name && !isEmbeddedValue(obj)) {
        if (obj->asList(ctx)) {
            val = listPrototype ? listPrototype->getAttribute(ctx, name) : nullptr;
        } else if (obj->asSparseList(ctx)) {
            val = dictPrototype ? dictPrototype->getAttribute(ctx, name) : nullptr;
        } else if (obj->asTuple(ctx)) {
            val = tuplePrototype ? tuplePrototype->getAttribute(ctx, name) : nullptr;
        } else if (obj->isString(ctx)) {
            val = strPrototype ? strPrototype->getAttribute(ctx, name) : nullptr;
        }
    }

    if (!val && name) {
        if (envDiag) {
            std::string nameStr;
            name->toUTF8String(ctx, nameStr);
            std::cerr << "getAttribute failed for: " << nameStr << " on " << obj << std::endl;
        }
    }
    if (val && name) {
        // In Python, instance attributes are NOT automatically bound if they are functions/descriptors.
        // Only class attributes (retrieved via prototype chain) are bound.
        // ProtoCore's getAttribute returns the first found, so we check if it belongs to obj itself.
        const proto::ProtoObject* ownFlag = obj->hasOwnAttribute(ctx, name);
        bool isOwn = (ownFlag == PROTO_TRUE);
        
        // If it's a descriptor AND NOT an own attribute, call __get__
        if (!isOwn) {
            const proto::ProtoObject* getM = val->getAttribute(ctx, getDunderString);
            if (getM && getM->asMethod(ctx)) {
                const proto::ProtoObject* typeObj = obj->getAttribute(ctx, classString);
                const proto::ProtoList* getArgs = ctx->newList()->appendLast(ctx, obj)->appendLast(ctx, typeObj ? typeObj : PROTO_NONE);
                return getM->asMethod(ctx)(ctx, val, nullptr, getArgs, nullptr);
            }
        }
        
        bool isM = val->isMethod(ctx);
        if (get_thread_diag()) {
            std::string n; name->toUTF8String(ctx, n);
            std::cerr << "[proto-thread-diag] getAttribute name=" << n << " isM=" << isM << " isOwn=" << isOwn << " obj=" << obj << " val=" << val << " val_asM=" << (void*)val->asMethod(ctx) << "\n" << std::flush;
        }
        if (isM && !isOwn) {
             // This is a class method. Bind it.
             return ctx->fromMethod(const_cast<proto::ProtoObject*>(obj), val->asMethod(ctx));
        }
    }
    return val;
}

const proto::ProtoObject* PythonEnvironment::setAttribute(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* name, const proto::ProtoObject* value) {
    if (!obj) return nullptr;
    if (isEmbeddedValue(obj)) return obj;

    const proto::ProtoObject* descr = obj->getAttribute(ctx, name);
    const proto::ProtoObject* setM = descr ? descr->getAttribute(ctx, setDunderString) : nullptr;
    if (setM && setM->asMethod(ctx)) {
        const proto::ProtoList* setArgs = ctx->newList()->appendLast(ctx, obj)->appendLast(ctx, value);
        setM->asMethod(ctx)(ctx, descr, nullptr, setArgs, nullptr);
        return obj;
    } else {
        return obj->setAttribute(ctx, name, value);
    }
}

const proto::ProtoObject* PythonEnvironment::resolve(const std::string& name, proto::ProtoContext* ctx) {
    static thread_local int resolveDepth = 0;
    if (++resolveDepth > 100) {
        std::cerr << "[proto-env] resolve EXCEEDED DEPTH for " << name << "\n" << std::flush;
        --resolveDepth;
        return PROTO_NONE;
    }
    struct DepthGuard {
        int& d;
        DepthGuard(int& d) : d(d) {}
        ~DepthGuard() { --d; }
    } dg(resolveDepth);
    if (!ctx) ctx = s_threadContext;
    if (!ctx) {
        ctx = rootContext_;
        if (std::getenv("PROTO_THREAD_DIAG") && std::this_thread::get_id() != s_mainThreadId) {
             std::cerr << "[proto-thread-diag] WARNING: resolve('" << name << "') using rootContext_ from worker thread " << std::this_thread::get_id() << "\n" << std::flush;
        }
    }
    SafeImportLock lock(this, ctx);
    if (get_env_diag()) std::cerr << "[proto-env] resolve name=" << name << " START ctx=" << ctx << "\n" << std::flush;
    if (name == "None") return nonePrototype;
    // Type shortcuts always use current env (no cache) so multiple envs per thread stay correct.
    if (name == "object") return objectPrototype;
    if (name == "type") return typePrototype;
    if (name == "int") return intPrototype;
    if (name == "float") return floatPrototype;
    if (name == "bool") return boolPrototype;
    if (name == "str") return strPrototype;
    if (name == "list") return listPrototype;
    if (name == "dict") return dictPrototype;
    if (name == "set") return setPrototype;
    if (name == "frozenset") return frozensetPrototype;

    uint64_t gen = resolveCacheGeneration_.load(std::memory_order_acquire);
    if (s_threadResolveCacheGeneration != gen) {
        s_threadResolveCache.clear();
        s_threadResolveCacheGeneration = gen;
    }
    auto it = s_threadResolveCache.find(name);
    if (it != s_threadResolveCache.end() && it->second != nullptr)
        return it->second;

    const proto::ProtoObject* result = PROTO_NONE;

    // 1. Try module import so that names like "sys" resolve to the real module object,
    //    not a builtins attribute (e.g. a method) that would cause type mismatch in getAttribute
    if (!result || result == PROTO_NONE) {
        const proto::ProtoObject* modWrapper = ctx->space->getImportModule(ctx, name.c_str(), "val");
        if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-env] resolve name=" << name << " getImportModule returns " << modWrapper << "\n" << std::flush;
        if (modWrapper) {
            result = modWrapper->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"));
            if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-env] resolve name=" << name << " modWrapper=" << modWrapper << " result=" << result << "\n" << std::flush;
            if (result && result != PROTO_NONE) {
                // Check if it's a Python module that needs execution
                const proto::ProtoString* fileKey = proto::ProtoString::fromUTF8String(ctx, "__file__");
                const proto::ProtoString* executedKey = proto::ProtoString::fromUTF8String(ctx, "__executed__");
                const proto::ProtoObject* fileObj = result->getAttribute(ctx, fileKey);
                const proto::ProtoObject* execVal = result->getAttribute(ctx, executedKey);
                if (std::getenv("PROTO_ENV_DIAG")) {
                    std::string fpath;
                    if (fileObj && fileObj->isString(ctx)) fileObj->asString(ctx)->toUTF8String(ctx, fpath);
                    std::cerr << "[proto-env] resolve name=" << name << " fileObj=" << fileObj << " path=" << fpath << " executed=" << (execVal && execVal != PROTO_NONE && execVal != PROTO_FALSE ? 1 : 0) << "\n" << std::flush;
                }
                if (fileObj && fileObj->isString(ctx) && (!execVal || execVal == PROTO_NONE || execVal == PROTO_FALSE)) {
                    static thread_local std::unordered_set<std::string> resolving;
                    if (resolving.find(name) == resolving.end()) {
                        resolving.insert(name);
                        executeModule(name, false, ctx);
                        resolving.erase(name);
                        auto it2 = s_threadResolveCache.find(name);
                        if (it2 != s_threadResolveCache.end()) result = it2->second;
                        else result = modWrapper->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "val"));
                    }
                }

                if (sysModule) {
                    const proto::ProtoObject* mods = sysModule->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modules"));
                    if (mods) {
                        const proto::ProtoObject* updated = mods->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, name.c_str()), result);
                        sysModule = sysModule->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modules"), updated);
                    }
                }
            }
        }
    }

    // 2. Try current module's globals
    if (!result || result == PROTO_NONE) {
        if (s_currentGlobals) {
            result = s_currentGlobals->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, name.c_str()));
            if (!result || result == PROTO_NONE) {
                const proto::ProtoObject* data = s_currentGlobals->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
                if (data && data->asSparseList(ctx)) {
                    unsigned long h = proto::ProtoString::fromUTF8String(ctx, name.c_str())->getHash(ctx);
                    result = data->asSparseList(ctx)->getAt(ctx, h);
                    if (std::getenv("PROTO_ENV_DIAG")) {
                        std::cerr << "[proto-env] resolve(dict) name=" << name << " hash=" << h << " found=" << (result && result != PROTO_NONE) << "\n" << std::flush;
                    }
                }
            }
        }
    }

    // 3. Fall back to builtins (e.g. len, print, type as callable)
    if (!result || result == PROTO_NONE) {
        if (builtinsModule) {
            const proto::ProtoString* nameS = proto::ProtoString::fromUTF8String(ctx, name.c_str());
            const proto::ProtoObject* attr = builtinsModule->getAttribute(ctx, nameS);
            if (get_env_diag()) {
                std::cerr << "[proto-env] resolve name=" << name << " CHECK builtinsModule=" << builtinsModule << " attr=" << attr << "\n" << std::flush;
            }
            if (attr && attr != PROTO_NONE) result = attr;
        } else {
            if (get_env_diag()) {
                std::cerr << "[proto-env] resolve name=" << name << " builtinsModule is NULL\n" << std::flush;
            }
        }
    }

    // 3. Parent linkage for dotted modules (e.g. os.path)
    if (result && result != PROTO_NONE && name.find('.') != std::string::npos) {
        size_t lastDot = name.find_last_of('.');
        std::string parentName = name.substr(0, lastDot);
        std::string childName = name.substr(lastDot + 1);
        const proto::ProtoObject* parent = resolve(parentName, ctx);
        if (parent && parent != PROTO_NONE) {
            const proto::ProtoObject* newParent = parent->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, childName.c_str()), result);
            if (newParent != parent) {
                // Update sys.modules
                if (sysModule) {
                    const proto::ProtoObject* mods = sysModule->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modules"));
                    if (mods) {
                        const proto::ProtoObject* updated = mods->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, parentName.c_str()), newParent);
                        sysModule = sysModule->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modules"), updated);
                    }
                }
                // Update cache
                s_threadResolveCache[parentName] = newParent;
            }
        }
    }

    s_threadResolveCache[name] = result;
    return result;
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
                const proto::ProtoObject* res = method->asMethod(ctx)(ctx, a, nullptr, args, nullptr);
                if (res && res != PROTO_NONE) return res;
            }
        }
    }

    int c = a->compare(ctx, b);
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

    return PROTO_NONE;
}

const proto::ProtoObject* PythonEnvironment::lookupName(const std::string& name) {
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;
    const proto::ProtoObject* frame = getCurrentFrame();
    const proto::ProtoString* nameS = proto::ProtoString::fromUTF8String(ctx, name.c_str());
    if (frame) {
        const proto::ProtoObject* val = frame->getAttribute(ctx, nameS);
        if (val) return val;
    }
    return resolve(name, ctx);
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
    proto::ProtoContext* ctx = rootContext_;
    const proto::ProtoList* plArgs = ctx->newList();
    for (auto* arg : args) plArgs = plArgs->appendLast(ctx, arg);
    
    if (callable->asMethod(ctx)) {
        return callable->asMethod(ctx)(ctx, nullptr, nullptr, plArgs, nullptr);
    }

    const proto::ProtoObject* method = callable->getAttribute(ctx, getCallString());
    if (method && method->asMethod(ctx)) {
        return method->asMethod(ctx)(ctx, callable, nullptr, plArgs, nullptr);
    }
    
    return PROTO_NONE;
}

const proto::ProtoObject* PythonEnvironment::getItem(const proto::ProtoObject* container, const proto::ProtoObject* key) {
    proto::ProtoContext* ctx = rootContext_;
    if (!container || !key) return PROTO_NONE;
    
    if (container->asList(ctx)) {
        return container->asList(ctx)->getAt(ctx, (int)key->asLong(ctx));
    } else if (container->asSparseList(ctx)) {
        return container->asSparseList(ctx)->getAt(ctx, (unsigned long)key->asLong(ctx));
    } else if (container->isTuple(ctx)) {
        return container->asTuple(ctx)->getAt(ctx, (int)key->asLong(ctx));
    }
    
    const proto::ProtoObject* method = container->getAttribute(ctx, getItemString);
    if (method && method->asMethod(ctx)) {
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
        return method->asMethod(ctx)(ctx, container, nullptr, args, nullptr);
    }
    return PROTO_NONE;
}

void PythonEnvironment::setItem(const proto::ProtoObject* container, const proto::ProtoObject* key, const proto::ProtoObject* value) {
    proto::ProtoContext* ctx = rootContext_;
    if (!container || !key || !value) return;

    const proto::ProtoObject* data = container->getAttribute(ctx, dataString);
    if (data) {
        if (data->asSparseList(ctx)) {
            const proto::ProtoSparseList* newData = data->asSparseList(ctx)->setAt(ctx, (unsigned long)key->asLong(ctx), value);
            const_cast<proto::ProtoObject*>(container)->setAttribute(ctx, dataString, newData->asObject(ctx));
            return;
        }
    }

    const proto::ProtoObject* method = container->getAttribute(ctx, setItemString);
    if (method && method->asMethod(ctx)) {
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, value);
        method->asMethod(ctx)(ctx, container, nullptr, args, nullptr);
    }
}

const proto::ProtoObject* PythonEnvironment::getAttr(const proto::ProtoObject* obj, const std::string& attr) {
    proto::ProtoContext* ctx = rootContext_;
    if (!obj) return PROTO_NONE;
    return obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, attr.c_str()));
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
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;
    const proto::ProtoObject* method = obj->getAttribute(ctx, getIterString());
    if (method && method->asMethod(ctx)) {
        return method->asMethod(ctx)(ctx, obj, nullptr, getEmptyList(), nullptr);
    }
    return obj; 
}

const proto::ProtoObject* PythonEnvironment::next(const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;
    proto::ProtoContext* ctx = getCurrentContext();
    if (!ctx) ctx = rootContext_;
    
    // FAST PATH: Check if it's a native iterator that we can advance directly
    const proto::ProtoObject* method = obj->getAttribute(ctx, getNextString());
    if (method && method->asMethod(ctx)) {
        return method->asMethod(ctx)(ctx, obj, nullptr, getEmptyList(), nullptr);
    }
    return PROTO_NONE;
}

void PythonEnvironment::raiseException(const proto::ProtoObject* exc) {
    setPendingException(exc);
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
        const proto::ProtoString* s = reinterpret_cast<const proto::ProtoString*>(key);
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

} // namespace protoPython
