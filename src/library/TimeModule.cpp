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

// time.perf_counter() — high-resolution counter, same source as
// monotonic on POSIX.  Required by timeit, profiling, asyncio.
static const proto::ProtoObject* py_perf_counter(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    auto now = std::chrono::steady_clock::now();
    auto epoch = now.time_since_epoch();
    double sec = std::chrono::duration<double>(epoch).count();
    return ctx->fromDouble(sec);
}

static const proto::ProtoObject* py_perf_counter_ns(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    auto now = std::chrono::steady_clock::now();
    auto epoch = now.time_since_epoch();
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch).count();
    return ctx->fromInteger(ns);
}

// time.process_time() — CPU time consumed by the current process,
// in seconds.  POSIX exposes this via clock_gettime(CLOCK_PROCESS_
// CPUTIME_ID) or via the user+system fields of ::times().
static const proto::ProtoObject* py_process_time(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
        double sec = ts.tv_sec + ts.tv_nsec / 1e9;
        return ctx->fromDouble(sec);
    }
#endif
    return ctx->fromDouble(0.0);
}

static const proto::ProtoObject* py_process_time_ns(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
        long long ns = static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
        return ctx->fromInteger(ns);
    }
#endif
    return ctx->fromInteger(0);
}

static const proto::ProtoObject* py_thread_time(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
        double sec = ts.tv_sec + ts.tv_nsec / 1e9;
        return ctx->fromDouble(sec);
    }
#endif
    return ctx->fromDouble(0.0);
}

// Build a struct_time-shaped object that supports BOTH indexing
// (st[0] == st.tm_year) and attribute access (st.tm_year).  CPython's
// `time.struct_time` is a namedtuple-derived sequence; consumers in the
// stdlib (_strptime, datetime, calendar) read both forms.
// protoCore's tuple-shape pointers can't carry user-stamped attributes
// through getType (the tuple shortcut wins), so wrap the 9-element
// tuple in an Object that stores it in __data__ (so subscript / iter /
// len work via tuple-protocol fallback) and attaches the named field
// values directly as own attributes.
static const proto::ProtoObject* build_struct_time(
    proto::ProtoContext* ctx,
    int year, int mon, int mday, int hour, int min_, int sec,
    int wday, int yday, int isdst) {
    const proto::ProtoList* result = ctx->newList();
    result = result->appendLast(ctx, ctx->fromInteger(year));
    result = result->appendLast(ctx, ctx->fromInteger(mon));
    result = result->appendLast(ctx, ctx->fromInteger(mday));
    result = result->appendLast(ctx, ctx->fromInteger(hour));
    result = result->appendLast(ctx, ctx->fromInteger(min_));
    result = result->appendLast(ctx, ctx->fromInteger(sec));
    result = result->appendLast(ctx, ctx->fromInteger(wday));
    result = result->appendLast(ctx, ctx->fromInteger(yday));
    result = result->appendLast(ctx, ctx->fromInteger(isdst));

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoTuple* tup = ctx->newTupleFromList(result);

    const proto::ProtoObject* obj = ctx->newObject(true);
    if (env && env->getTuplePrototype()) {
        obj = obj->addParent(ctx, env->getTuplePrototype());
        obj = obj->setAttribute(ctx, env->getClassString(), env->getTuplePrototype());
    }
    obj = obj->setAttribute(ctx,
        env ? env->getDataString() : PythonEnvironment::getInternedString(ctx, "__data__"),
        tup ? tup->asObject(ctx) : result->asObject(ctx));
    obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tm_year"), ctx->fromInteger(year));
    obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tm_mon"),  ctx->fromInteger(mon));
    obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tm_mday"), ctx->fromInteger(mday));
    obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tm_hour"), ctx->fromInteger(hour));
    obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tm_min"),  ctx->fromInteger(min_));
    obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tm_sec"),  ctx->fromInteger(sec));
    obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tm_wday"), ctx->fromInteger(wday));
    obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tm_yday"), ctx->fromInteger(yday));
    obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tm_isdst"),ctx->fromInteger(isdst));
    return obj;
}

static const proto::ProtoObject* make_struct_time(proto::ProtoContext* ctx, struct tm* tm_ptr) {
    return build_struct_time(ctx,
        tm_ptr->tm_year + 1900,
        tm_ptr->tm_mon + 1,
        tm_ptr->tm_mday,
        tm_ptr->tm_hour,
        tm_ptr->tm_min,
        tm_ptr->tm_sec,
        tm_ptr->tm_wday,
        tm_ptr->tm_yday + 1,
        tm_ptr->tm_isdst);
}

// time.struct_time(seq) — construct a struct_time from a 9-element sequence.
// _strptime / calendar both call this with a tuple and then read the
// tm_year / tm_mon / … fields off the returned object.  Without it,
// `from time import struct_time` raised AttributeError immediately on
// _strptime line 13 and blocked the whole datetime / strftime audit chain.
static const proto::ProtoObject* py_struct_time(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* seq = posArgs->getAt(ctx, 0);
    auto getElem = [&](int i) -> long long {
        const proto::ProtoTuple* tup = seq ? seq->asTuple(ctx) : nullptr;
        if (!tup) {
            const proto::ProtoObject* data = seq ? seq->getAttribute(
                ctx, PythonEnvironment::getInternedString(ctx, "__data__")) : nullptr;
            if (data) tup = data->asTuple(ctx);
        }
        const proto::ProtoList* lst = tup ? nullptr : (seq ? seq->asList(ctx) : nullptr);
        if (tup && (size_t)i < tup->getSize(ctx)) {
            const proto::ProtoObject* v = tup->getAt(ctx, i);
            if (v && v->isInteger(ctx)) return v->asLong(ctx);
        } else if (lst && (size_t)i < lst->getSize(ctx)) {
            const proto::ProtoObject* v = lst->getAt(ctx, i);
            if (v && v->isInteger(ctx)) return v->asLong(ctx);
        }
        return 0;
    };
    return build_struct_time(ctx,
        (int)getElem(0), (int)getElem(1), (int)getElem(2),
        (int)getElem(3), (int)getElem(4), (int)getElem(5),
        (int)getElem(6), (int)getElem(7), (int)getElem(8));
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

// time.get_clock_info(name) -> namespace-like object with fields
//   implementation, monotonic, adjustable, resolution
static const proto::ProtoObject* py_get_clock_info(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    std::string name;
    if (posArgs && posArgs->getSize(ctx) > 0) {
        const proto::ProtoObject* a = posArgs->getAt(ctx, 0);
        if (a && a->isString(ctx)) a->asString(ctx)->toUTF8String(ctx, name);
    }
    bool monotonic = (name == "monotonic" || name == "perf_counter");
    bool adjustable = !monotonic;
    const proto::ProtoObject* info = ctx->newObject(false);
    info = info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "implementation"),
        PythonEnvironment::getInternedString(ctx,
            monotonic ? "clock_gettime(MONOTONIC)" : "clock_gettime(REALTIME)")->asObject(ctx));
    info = info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "monotonic"),
        monotonic ? PROTO_TRUE : PROTO_FALSE);
    info = info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "adjustable"),
        adjustable ? PROTO_TRUE : PROTO_FALSE);
    // CPython reports a ns-level nominal resolution; 1e-9 is the canonical value.
    info = info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "resolution"),
        ctx->fromDouble(1e-9));
    info = info->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__class__"),
        PythonEnvironment::getInternedString(ctx, "clock_info")->asObject(ctx));
    return info;
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
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_perf_counter));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "perf_counter_ns"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_perf_counter_ns));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "process_time"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_process_time));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "process_time_ns"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_process_time_ns));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "thread_time"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_thread_time));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "perf_counter"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_monotonic));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "localtime"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_localtime));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "gmtime"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_gmtime));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "strftime"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_strftime));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "get_clock_info"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_get_clock_info));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "struct_time"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_struct_time));
    // _strptime reads time._STRUCT_TM_ITEMS to slice an extended tuple
    // back to the 9-element struct_time before constructing one.  9 is
    // CPython's value when the platform doesn't expose tm_zone /
    // tm_gmtoff (the protoPython stub doesn't either).
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_STRUCT_TM_ITEMS"),
        ctx->fromInteger(9));
    // Static metadata _strptime / locale / datetime read at import time.
    // tzname is a 2-tuple of (standard, daylight); use the system's
    // tzname[] globals when available, fall back to ('UTC', 'UTC').
    {
        const char* std_name = "UTC";
        const char* dst_name = "UTC";
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        ::tzset();
        if (::tzname[0]) std_name = ::tzname[0];
        if (::tzname[1]) dst_name = ::tzname[1];
#endif
        const proto::ProtoList* tznameList = ctx->newList()
            ->appendLast(ctx, proto::ProtoString::fromUTF8(ctx, std_name)->asObject(ctx))
            ->appendLast(ctx, proto::ProtoString::fromUTF8(ctx, dst_name)->asObject(ctx));
        const proto::ProtoTuple* tzTup = ctx->newTupleFromList(tznameList);
        mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tzname"),
            tzTup ? tzTup->asObject(ctx) : tznameList->asObject(ctx));
    }
    // timezone / altzone: seconds west of UTC (CPython semantics).
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "timezone"),
        ctx->fromInteger(static_cast<long long>(::timezone)));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "altzone"),
        ctx->fromInteger(static_cast<long long>(::timezone - 3600)));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "daylight"),
        ctx->fromInteger(static_cast<long long>(::daylight)));
#else
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "timezone"), ctx->fromInteger(0));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "altzone"),  ctx->fromInteger(0));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "daylight"), ctx->fromInteger(0));
#endif
    return mod;
}

} // namespace time_module
} // namespace protoPython
