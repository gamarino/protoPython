#include <protoPython/TimeModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/DiagUtils.h>
#include <chrono>
#include <thread>
#include <ctime>

namespace protoPython {
namespace time_module {

static double toDouble(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (obj->isDouble(ctx)) return obj->asDouble(ctx);
    if (obj->isInteger(ctx)) return static_cast<double>(obj->asLong(ctx));
    return 0.0;
}

static const proto::ProtoObject* py_time(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    double sec = std::chrono::duration<double>(epoch).count();
    return ctx->fromDouble(sec);
}

static const proto::ProtoObject* py_sleep(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    double sec = toDouble(ctx, posArgs->getAt(ctx, 0));
    if (sec > 0)
        std::this_thread::sleep_for(std::chrono::duration<double>(sec));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_monotonic(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    auto now = std::chrono::steady_clock::now();
    auto epoch = now.time_since_epoch();
    double sec = std::chrono::duration<double>(epoch).count();
    return ctx->fromDouble(sec);
}

static const proto::ProtoObject* make_struct_time(proto::ProtoContext* ctx, struct tm* tm_ptr) {
    // For now, return a simple tuple (list) to satisfy basic attribute access if treated as sequence
    const proto::ProtoList* result = ctx->newList();
    result = result->appendLast(ctx, ctx->fromInteger(tm_ptr->tm_year + 1900));
    result = result->appendLast(ctx, ctx->fromInteger(tm_ptr->tm_mon + 1));
    result = result->appendLast(ctx, ctx->fromInteger(tm_ptr->tm_mday));
    result = result->appendLast(ctx, ctx->fromInteger(tm_ptr->tm_hour));
    result = result->appendLast(ctx, ctx->fromInteger(tm_ptr->tm_min));
    result = result->appendLast(ctx, ctx->fromInteger(tm_ptr->tm_sec));
    result = result->appendLast(ctx, ctx->fromInteger(tm_ptr->tm_wday));
    result = result->appendLast(ctx, ctx->fromInteger(tm_ptr->tm_yday + 1));
    result = result->appendLast(ctx, ctx->fromInteger(tm_ptr->tm_isdst));
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        return env->newTuple(result);
    }
    return result->asObject(ctx);
}

static const proto::ProtoObject* py_localtime(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    time_t t;
    if (posArgs->getSize(ctx) > 0) {
        t = static_cast<time_t>(toDouble(ctx, posArgs->getAt(ctx, 0)));
    } else {
        t = std::time(nullptr);
    }
    struct tm* tm_ptr = std::localtime(&t);
    return make_struct_time(ctx, tm_ptr);
}

static const proto::ProtoObject* py_gmtime(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    time_t t;
    if (posArgs->getSize(ctx) > 0) {
        t = static_cast<time_t>(toDouble(ctx, posArgs->getAt(ctx, 0)));
    } else {
        t = std::time(nullptr);
    }
    struct tm* tm_ptr = std::gmtime(&t);
    return make_struct_time(ctx, tm_ptr);
}

static const proto::ProtoObject* py_strftime(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* fmtObj = posArgs->getAt(ctx, 0);
    if (!fmtObj->isString(ctx)) return PROTO_NONE;
    std::string fmt;
    fmtObj->asString(ctx)->toUTF8String(ctx, fmt);

    struct tm tm_val;
    bool found = false;
    if (posArgs->getSize(ctx) > 1) {
        const proto::ProtoObject* tObj = posArgs->getAt(ctx, 1);
        if (tObj->asList(ctx)) {
            const proto::ProtoList* l = tObj->asList(ctx);
            if (l->getSize(ctx) >= 9) {
                tm_val.tm_year = l->getAt(ctx, 0)->asLong(ctx) - 1900;
                tm_val.tm_mon = l->getAt(ctx, 1)->asLong(ctx) - 1;
                tm_val.tm_mday = l->getAt(ctx, 2)->asLong(ctx);
                tm_val.tm_hour = l->getAt(ctx, 3)->asLong(ctx);
                tm_val.tm_min = l->getAt(ctx, 4)->asLong(ctx);
                tm_val.tm_sec = l->getAt(ctx, 5)->asLong(ctx);
                tm_val.tm_wday = l->getAt(ctx, 6)->asLong(ctx);
                tm_val.tm_yday = l->getAt(ctx, 7)->asLong(ctx) - 1;
                tm_val.tm_isdst = l->getAt(ctx, 8)->asLong(ctx);
                found = true;
            }
        }
    }
    
    if (!found) {
        time_t t = std::time(nullptr);
        tm_val = *std::localtime(&t);
    }

    char buf[1024];
    size_t res = std::strftime(buf, sizeof(buf), fmt.c_str(), &tm_val);
    if (get_env_diag()) fprintf(stderr, "DEBUG_STRFTIME: fmt='%s' len=%zu res=%zu buf='%s'\n", fmt.c_str(), fmt.length(), res, buf);
    if (res == 0 && !fmt.empty()) return ctx->fromString("");
    return PythonEnvironment::getInternedString(ctx, buf)->asObject(ctx);
}

static const proto::ProtoObject* py_time_ns(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    auto now = std::chrono::system_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    return ctx->fromInteger(ns);
}

static const proto::ProtoObject* py_monotonic_ns(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    auto now = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    return ctx->fromInteger(ns);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "time"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_time));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "time_ns"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_time_ns));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "sleep"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_sleep));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "monotonic"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_monotonic));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "monotonic_ns"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_monotonic_ns));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "perf_counter"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_monotonic));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "localtime"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_localtime));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "gmtime"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_gmtime));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "strftime"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_strftime));
    return mod;
}

} // namespace time_module
} // namespace protoPython
