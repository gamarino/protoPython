#include <protoPython/DatetimeModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <string>
#include <algorithm>
#include <ctime>

namespace protoPython {
namespace datetime {

static const proto::ProtoObject* getKwArg(proto::ProtoContext* ctx, const proto::ProtoSparseList* kwArgs, const std::string& name) {
    if (!kwArgs) return nullptr;
    const proto::ProtoString* nameS = PythonEnvironment::getInternedString(ctx, name.c_str());
    return kwArgs->getAt(ctx, nameS->getHash(ctx));
}

// timedelta
struct DeltaState {
    long long days;
    long long seconds;
    long long microseconds;
};

static void delta_finalizer(void* ptr) {
    delete static_cast<DeltaState*>(ptr);
}

static DeltaState* get_delta_state(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoString* key = PythonEnvironment::getInternedString(ctx, "__delta_ptr__");
    const proto::ProtoObject* ptrObj = self->getAttribute(ctx, key);
    if (ptrObj) {
        const proto::ProtoExternalPointer* ext = ptrObj->asExternalPointer(ctx);
        if (ext) {
            return static_cast<DeltaState*>(ext->getPointer(ctx));
        }
    }
    return nullptr;
}

static const proto::ProtoObject* create_timedelta_instance(proto::ProtoContext* ctx, const proto::ProtoObject* cls, long long d, long long s, long long us) {
    if (us >= 1000000 || us < 0) {
        s += us / 1000000;
        us %= 1000000;
        if (us < 0) { us += 1000000; s--; }
    }
    if (s >= 86400 || s < 0) {
        d += s / 86400;
        s %= 86400;
        if (s < 0) { s += 86400; d--; }
    }

    if (!cls || cls == PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        const proto::ProtoObject* mod = env->resolve("_datetime", ctx);
        if (mod) cls = mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "timedelta"));
    }
    
    const proto::ProtoObject* instance = cls->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), cls);
    DeltaState* state = new DeltaState{d, s, us};
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__delta_ptr__"),
                                    ctx->fromExternalPointer(state, delta_finalizer));
    
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "days"), ctx->fromInteger(d));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "seconds"), ctx->fromInteger(s));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "microseconds"), ctx->fromInteger(us));
    
    return instance;
}

static const proto::ProtoObject* py_timedelta_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs) {
    
    long long days = 0, seconds = 0, microseconds = 0, milliseconds = 0, minutes = 0, hours = 0, weeks = 0;
    
    if (posArgs) {
        size_t sz = posArgs->getSize(ctx);
        if (sz > 1) days = posArgs->getAt(ctx, 1)->asLong(ctx);
        if (sz > 2) seconds = posArgs->getAt(ctx, 2)->asLong(ctx);
        if (sz > 3) microseconds = posArgs->getAt(ctx, 3)->asLong(ctx);
        if (sz > 4) milliseconds = posArgs->getAt(ctx, 4)->asLong(ctx);
        if (sz > 5) minutes = posArgs->getAt(ctx, 5)->asLong(ctx);
        if (sz > 6) hours = posArgs->getAt(ctx, 6)->asLong(ctx);
        if (sz > 7) weeks = posArgs->getAt(ctx, 7)->asLong(ctx);
    }
    
    if (kwArgs) {
        const proto::ProtoObject* o;
        if ((o = getKwArg(ctx, kwArgs, "days")) && o != PROTO_NONE) days = o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "seconds")) && o != PROTO_NONE) seconds = o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "microseconds")) && o != PROTO_NONE) microseconds = o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "milliseconds")) && o != PROTO_NONE) milliseconds = o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "minutes")) && o != PROTO_NONE) minutes = o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "hours")) && o != PROTO_NONE) hours = o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "weeks")) && o != PROTO_NONE) weeks = o->asLong(ctx);
    }

    return create_timedelta_instance(ctx, self, days + weeks * 7, seconds + minutes * 60 + hours * 3600, microseconds + milliseconds * 1000);
}

static const proto::ProtoObject* py_timedelta_total_seconds(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DeltaState* state = get_delta_state(ctx, self);
    if (!state) return ctx->fromDouble(0.0);
    double total = (double)state->days * 86400.0 + (double)state->seconds + (double)state->microseconds / 1000000.0;
    return ctx->fromDouble(total);
}

static const proto::ProtoObject* py_timedelta_repr(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DeltaState* state = get_delta_state(ctx, self);
    if (!state) return PythonEnvironment::getInternedString(ctx, "timedelta(0)")->asObject(ctx);
    std::string s = "timedelta(days=" + std::to_string(state->days) + 
                    ", seconds=" + std::to_string(state->seconds) + 
                    ", microseconds=" + std::to_string(state->microseconds) + ")";
    return PythonEnvironment::getInternedString(ctx, s.c_str())->asObject(ctx);
}

static const proto::ProtoObject* py_timedelta_add(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    DeltaState* s1 = get_delta_state(ctx, self);
    DeltaState* s2 = get_delta_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_NONE;
    return create_timedelta_instance(ctx, nullptr, s1->days + s2->days, s1->seconds + s2->seconds, s1->microseconds + s2->microseconds);
}

static const proto::ProtoObject* py_timedelta_sub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    DeltaState* s1 = get_delta_state(ctx, self);
    DeltaState* s2 = get_delta_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_NONE;
    return create_timedelta_instance(ctx, nullptr, s1->days - s2->days, s1->seconds - s2->seconds, s1->microseconds - s2->microseconds);
}

static const proto::ProtoObject* py_timedelta_lt(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    DeltaState* s1 = get_delta_state(ctx, self);
    DeltaState* s2 = get_delta_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_FALSE;
    if (s1->days < s2->days) return PROTO_TRUE;
    if (s1->days > s2->days) return PROTO_FALSE;
    if (s1->seconds < s2->seconds) return PROTO_TRUE;
    if (s1->seconds > s2->seconds) return PROTO_FALSE;
    return (s1->microseconds < s2->microseconds) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_timedelta_eq(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    DeltaState* s1 = get_delta_state(ctx, self);
    DeltaState* s2 = get_delta_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_FALSE;
    return (s1->days == s2->days && s1->seconds == s2->seconds && s1->microseconds == s2->microseconds) ? PROTO_TRUE : PROTO_FALSE;
}

// date
struct DateState {
    int year;
    int month;
    int day;
};

static void date_finalizer(void* ptr) {
    delete static_cast<DateState*>(ptr);
}

static DateState* get_date_state(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoString* key = PythonEnvironment::getInternedString(ctx, "__date_ptr__");
    const proto::ProtoObject* ptrObj = self->getAttribute(ctx, key);
    if (ptrObj) {
        const proto::ProtoExternalPointer* ext = ptrObj->asExternalPointer(ctx);
        if (ext) {
            return static_cast<DateState*>(ext->getPointer(ctx));
        }
    }
    return nullptr;
}

static const proto::ProtoObject* create_date_instance(proto::ProtoContext* ctx, const proto::ProtoObject* cls, int y, int m, int d) {
    if (!cls || cls == PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        const proto::ProtoObject* mod = env->resolve("_datetime", ctx);
        if (mod) cls = mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "date"));
    }
    const proto::ProtoObject* instance = cls->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), cls);
    DateState* state = new DateState{y, m, d};
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__date_ptr__"),
                                    ctx->fromExternalPointer(state, date_finalizer));
    
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "year"), ctx->fromInteger(y));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "month"), ctx->fromInteger(m));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "day"), ctx->fromInteger(d));
    return instance;
}

static const proto::ProtoObject* py_date_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs) {
    
    int y = 1, m = 1, d = 1;
    if (posArgs) {
        size_t sz = posArgs->getSize(ctx);
        if (sz > 1) y = (int)posArgs->getAt(ctx, 1)->asLong(ctx);
        if (sz > 2) m = (int)posArgs->getAt(ctx, 2)->asLong(ctx);
        if (sz > 3) d = (int)posArgs->getAt(ctx, 3)->asLong(ctx);
    }
    
    if (kwArgs) {
        const proto::ProtoObject* o;
        if ((o = getKwArg(ctx, kwArgs, "year")) && o != PROTO_NONE) y = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "month")) && o != PROTO_NONE) m = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "day")) && o != PROTO_NONE) d = (int)o->asLong(ctx);
    }
    
    return create_date_instance(ctx, self, y, m, d);
}

static const proto::ProtoObject* py_date_isoformat(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateState* state = get_date_state(ctx, self);
    if (!state) return PythonEnvironment::getInternedString(ctx, "")->asObject(ctx);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", state->year, state->month, state->day);
    return PythonEnvironment::getInternedString(ctx, buf)->asObject(ctx);
}

static const proto::ProtoObject* py_date_repr(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateState* state = get_date_state(ctx, self);
    if (!state) return PythonEnvironment::getInternedString(ctx, "date(1, 1, 1)")->asObject(ctx);
    std::string s = "datetime.date(" + std::to_string(state->year) + ", " + 
                    std::to_string(state->month) + ", " + std::to_string(state->day) + ")";
    return PythonEnvironment::getInternedString(ctx, s.c_str())->asObject(ctx);
}

static const proto::ProtoObject* py_date_lt(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    DateState* s1 = get_date_state(ctx, self);
    DateState* s2 = get_date_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_FALSE;
    if (s1->year < s2->year) return PROTO_TRUE;
    if (s1->year > s2->year) return PROTO_FALSE;
    if (s1->month < s2->month) return PROTO_TRUE;
    if (s1->month > s2->month) return PROTO_FALSE;
    return (s1->day < s2->day) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_date_eq(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    DateState* s1 = get_date_state(ctx, self);
    DateState* s2 = get_date_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_FALSE;
    return (s1->year == s2->year && s1->month == s2->month && s1->day == s2->day) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_date_today(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    time_t t = time(nullptr);
    struct tm* ltm = localtime(&t);
    return create_date_instance(ctx, self, ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday);
}

// datetime
struct DateTimeState {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int microsecond;
};

static void datetime_finalizer(void* ptr) {
    delete static_cast<DateTimeState*>(ptr);
}

static DateTimeState* get_datetime_state(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoString* key = PythonEnvironment::getInternedString(ctx, "__datetime_ptr__");
    const proto::ProtoObject* ptrObj = self->getAttribute(ctx, key);
    if (ptrObj) {
        const proto::ProtoExternalPointer* ext = ptrObj->asExternalPointer(ctx);
        if (ext) {
            return static_cast<DateTimeState*>(ext->getPointer(ctx));
        }
    }
    return nullptr;
}

static const proto::ProtoObject* create_datetime_instance(proto::ProtoContext* ctx, const proto::ProtoObject* cls, int y, int m, int d, int h, int min, int s, int ms) {
    if (!cls || cls == PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        const proto::ProtoObject* mod = env->resolve("_datetime", ctx);
        if (mod) cls = mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "datetime"));
    }
    const proto::ProtoObject* instance = cls->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), cls);
    DateTimeState* state = new DateTimeState{y, m, d, h, min, s, ms};
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__datetime_ptr__"),
                                    ctx->fromExternalPointer(state, datetime_finalizer));
    
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "year"), ctx->fromInteger(y));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "month"), ctx->fromInteger(m));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "day"), ctx->fromInteger(d));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hour"), ctx->fromInteger(h));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "minute"), ctx->fromInteger(min));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "second"), ctx->fromInteger(s));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "microsecond"), ctx->fromInteger(ms));
    return instance;
}

static const proto::ProtoObject* py_datetime_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs) {
    
    int y = 1, m = 1, d = 1, h = 0, min = 0, s = 0, ms = 0;
    if (posArgs) {
        size_t sz = posArgs->getSize(ctx);
        if (sz > 1) y = (int)posArgs->getAt(ctx, 1)->asLong(ctx);
        if (sz > 2) m = (int)posArgs->getAt(ctx, 2)->asLong(ctx);
        if (sz > 3) d = (int)posArgs->getAt(ctx, 3)->asLong(ctx);
        if (sz > 4) h = (int)posArgs->getAt(ctx, 4)->asLong(ctx);
        if (sz > 5) min = (int)posArgs->getAt(ctx, 5)->asLong(ctx);
        if (sz > 6) s = (int)posArgs->getAt(ctx, 6)->asLong(ctx);
        if (sz > 7) ms = (int)posArgs->getAt(ctx, 7)->asLong(ctx);
    }
    
    if (kwArgs) {
        const proto::ProtoObject* o;
        if ((o = getKwArg(ctx, kwArgs, "year")) && o != PROTO_NONE) y = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "month")) && o != PROTO_NONE) m = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "day")) && o != PROTO_NONE) d = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "hour")) && o != PROTO_NONE) h = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "minute")) && o != PROTO_NONE) min = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "second")) && o != PROTO_NONE) s = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "microsecond")) && o != PROTO_NONE) ms = (int)o->asLong(ctx);
    }

    return create_datetime_instance(ctx, self, y, m, d, h, min, s, ms);
}

static const proto::ProtoObject* py_datetime_isoformat(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateTimeState* state = get_datetime_state(ctx, self);
    if (!state) return PythonEnvironment::getInternedString(ctx, "")->asObject(ctx);
    char buf[32];
    if (state->microsecond > 0)
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06d", 
                 state->year, state->month, state->day, state->hour, state->minute, state->second, state->microsecond);
    else
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", 
                 state->year, state->month, state->day, state->hour, state->minute, state->second);
    return PythonEnvironment::getInternedString(ctx, buf)->asObject(ctx);
}

static const proto::ProtoObject* py_datetime_repr(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateTimeState* state = get_datetime_state(ctx, self);
    if (!state) return PythonEnvironment::getInternedString(ctx, "datetime(1, 1, 1)")->asObject(ctx);
    std::string s = "datetime.datetime(" + std::to_string(state->year) + ", " + 
                    std::to_string(state->month) + ", " + std::to_string(state->day) + ", " +
                    std::to_string(state->hour) + ", " + std::to_string(state->minute) + ", " +
                    std::to_string(state->second) + ", " + std::to_string(state->microsecond) + ")";
    return PythonEnvironment::getInternedString(ctx, s.c_str())->asObject(ctx);
}

static const proto::ProtoObject* py_datetime_lt(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    DateTimeState* s1 = get_datetime_state(ctx, self);
    DateTimeState* s2 = get_datetime_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_FALSE;
    if (s1->year < s2->year) return PROTO_TRUE;
    if (s1->year > s2->year) return PROTO_FALSE;
    if (s1->month < s2->month) return PROTO_TRUE;
    if (s1->month > s2->month) return PROTO_FALSE;
    if (s1->day < s2->day) return PROTO_TRUE;
    if (s1->day > s2->day) return PROTO_FALSE;
    if (s1->hour < s2->hour) return PROTO_TRUE;
    if (s1->hour > s2->hour) return PROTO_FALSE;
    if (s1->minute < s2->minute) return PROTO_TRUE;
    if (s1->minute > s2->minute) return PROTO_FALSE;
    if (s1->second < s2->second) return PROTO_TRUE;
    if (s1->second > s2->second) return PROTO_FALSE;
    return (s1->microsecond < s2->microsecond) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_datetime_eq(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    DateTimeState* s1 = get_datetime_state(ctx, self);
    DateTimeState* s2 = get_datetime_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_FALSE;
    return (s1->year == s2->year && s1->month == s2->month && s1->day == s2->day &&
            s1->hour == s2->hour && s1->minute == s2->minute && s1->second == s2->second &&
            s1->microsecond == s2->microsecond) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_datetime_now(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    time_t t = time(nullptr);
    struct tm* ltm = localtime(&t);
    return create_datetime_instance(ctx, self, ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday,
                                    ltm->tm_hour, ltm->tm_min, ltm->tm_sec, 0);
}

// time
struct TimeState {
    int hour;
    int minute;
    int second;
    int microsecond;
};

static void time_finalizer(void* ptr) {
    delete static_cast<TimeState*>(ptr);
}

static TimeState* get_time_state(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoString* key = PythonEnvironment::getInternedString(ctx, "__time_ptr__");
    const proto::ProtoObject* ptrObj = self->getAttribute(ctx, key);
    if (ptrObj) {
        const proto::ProtoExternalPointer* ext = ptrObj->asExternalPointer(ctx);
        if (ext) {
            return static_cast<TimeState*>(ext->getPointer(ctx));
        }
    }
    return nullptr;
}

static const proto::ProtoObject* py_time_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs) {
    
    int h = 0, min = 0, s = 0, ms = 0;
    if (posArgs) {
        size_t sz = posArgs->getSize(ctx);
        if (sz > 1) h = (int)posArgs->getAt(ctx, 1)->asLong(ctx);
        if (sz > 2) min = (int)posArgs->getAt(ctx, 2)->asLong(ctx);
        if (sz > 3) s = (int)posArgs->getAt(ctx, 3)->asLong(ctx);
        if (sz > 4) ms = (int)posArgs->getAt(ctx, 4)->asLong(ctx);
    }
    
    if (kwArgs) {
        const proto::ProtoObject* o;
        if ((o = getKwArg(ctx, kwArgs, "hour")) && o != PROTO_NONE) h = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "minute")) && o != PROTO_NONE) min = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "second")) && o != PROTO_NONE) s = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "microsecond")) && o != PROTO_NONE) ms = (int)o->asLong(ctx);
    }
    
    const proto::ProtoObject* cls = self;
    if (!cls || cls == PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        const proto::ProtoObject* mod = env->resolve("_datetime", ctx);
        if (mod) cls = mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "time"));
    }

    const proto::ProtoObject* instance = cls->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), cls);
    TimeState* state = new TimeState{h, min, s, ms};
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__time_ptr__"),
                                    ctx->fromExternalPointer(state, time_finalizer));
    
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hour"), ctx->fromInteger(h));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "minute"), ctx->fromInteger(min));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "second"), ctx->fromInteger(s));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "microsecond"), ctx->fromInteger(ms));
    
    return instance;
}

static const proto::ProtoObject* py_time_isoformat(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    TimeState* state = get_time_state(ctx, self);
    if (!state) return PythonEnvironment::getInternedString(ctx, "")->asObject(ctx);
    char buf[16];
    if (state->microsecond > 0)
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%06d", state->hour, state->minute, state->second, state->microsecond);
    else
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", state->hour, state->minute, state->second);
    return PythonEnvironment::getInternedString(ctx, buf)->asObject(ctx);
}

static const proto::ProtoObject* py_time_repr(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    TimeState* state = get_time_state(ctx, self);
    if (!state) return PythonEnvironment::getInternedString(ctx, "time(0, 0, 0)")->asObject(ctx);
    std::string s = "datetime.time(" + std::to_string(state->hour) + ", " + 
                    std::to_string(state->minute) + ", " + std::to_string(state->second) + ", " +
                    std::to_string(state->microsecond) + ")";
    return PythonEnvironment::getInternedString(ctx, s.c_str())->asObject(ctx);
}

// Class call bridge to support calling native types as constructors
static const proto::ProtoObject* py_class_call_bridge(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    const proto::ProtoObject* newMethod = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__new__"));
    if (newMethod && newMethod->asMethod(ctx)) {
        // args for __new__: (cls, *args)
        const proto::ProtoList* newArgs = ctx->newList()->appendLast(ctx, self);
        for (size_t i = 0; i < args->getSize(ctx); ++i) newArgs = newArgs->appendLast(ctx, args->getAt(ctx, i));
        return newMethod->asMethod(ctx)(ctx, self, nullptr, newArgs, kwargs);
    }
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), 
                          PythonEnvironment::getInternedString(ctx, "_datetime")->asObject(ctx));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "MINYEAR"), ctx->fromInteger(1));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "MAXYEAR"), ctx->fromInteger(9999));

    const proto::ProtoObject* callBridge = ctx->fromMethod(nullptr, py_class_call_bridge);

    // tzinfo
    const proto::ProtoObject* tzinfoType = ctx->newObject(false);
    if (env && env->getTypePrototype()) tzinfoType = tzinfoType->setAttribute(ctx, env->getClassString(), env->getTypePrototype());
    if (env && env->getObjectPrototype()) tzinfoType = tzinfoType->addParent(ctx, env->getObjectPrototype());
    tzinfoType = tzinfoType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), 
                                        PythonEnvironment::getInternedString(ctx, "tzinfo")->asObject(ctx));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "tzinfo"), tzinfoType);

    // timezone
    const proto::ProtoObject* timezoneType = ctx->newObject(false);
    if (env && env->getTypePrototype()) timezoneType = timezoneType->setAttribute(ctx, env->getClassString(), env->getTypePrototype());
    timezoneType = timezoneType->addParent(ctx, tzinfoType);
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), 
                                            PythonEnvironment::getInternedString(ctx, "timezone")->asObject(ctx));

    // UTC (on module and on timezone class)
    const proto::ProtoObject* utc = timezoneType->newChild(ctx, true);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "UTC"), utc);
    // Important: set both lowercase and uppercase on the class!
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "utc"), utc);
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "UTC"), utc);
    
    // NOW add timezone to module
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "timezone"), timezoneType);

    // timedelta
    const proto::ProtoObject* timedeltaType = ctx->newObject(false);
    if (env && env->getTypePrototype()) timedeltaType = timedeltaType->setAttribute(ctx, env->getClassString(), env->getTypePrototype());
    if (env && env->getObjectPrototype()) timedeltaType = timedeltaType->addParent(ctx, env->getObjectPrototype());
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), 
                                              PythonEnvironment::getInternedString(ctx, "timedelta")->asObject(ctx));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__new__"), 
                                              ctx->fromMethod(nullptr, py_timedelta_new));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__call__"), callBridge);
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__add__"), 
                                              ctx->fromMethod(nullptr, py_timedelta_add));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__sub__"), 
                                              ctx->fromMethod(nullptr, py_timedelta_sub));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__lt__"), 
                                              ctx->fromMethod(nullptr, py_timedelta_lt));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__eq__"), 
                                              ctx->fromMethod(nullptr, py_timedelta_eq));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "total_seconds"), 
                                              ctx->fromMethod(nullptr, py_timedelta_total_seconds));
    timedeltaType = timedeltaType->setAttribute(ctx, env->getReprString(), 
                                              ctx->fromMethod(nullptr, py_timedelta_repr));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "timedelta"), timedeltaType);

    // date
    const proto::ProtoObject* dateType = ctx->newObject(false);
    if (env && env->getTypePrototype()) dateType = dateType->setAttribute(ctx, env->getClassString(), env->getTypePrototype());
    if (env && env->getObjectPrototype()) dateType = dateType->addParent(ctx, env->getObjectPrototype());
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), 
                                    PythonEnvironment::getInternedString(ctx, "date")->asObject(ctx));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__new__"), 
                                    ctx->fromMethod(nullptr, py_date_new));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__call__"), callBridge);
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__lt__"), 
                                    ctx->fromMethod(nullptr, py_date_lt));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__eq__"), 
                                    ctx->fromMethod(nullptr, py_date_eq));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isoformat"), 
                                    ctx->fromMethod(nullptr, py_date_isoformat));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "today"), 
                                    ctx->fromMethod(nullptr, py_date_today));
    dateType = dateType->setAttribute(ctx, env->getReprString(), 
                                    ctx->fromMethod(nullptr, py_date_repr));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "date"), dateType);

    // datetime
    const proto::ProtoObject* datetimeType = ctx->newObject(false);
    if (env && env->getTypePrototype()) datetimeType = datetimeType->setAttribute(ctx, env->getClassString(), env->getTypePrototype());
    datetimeType = datetimeType->addParent(ctx, dateType);
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), 
                                          PythonEnvironment::getInternedString(ctx, "datetime")->asObject(ctx));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__new__"), 
                                          ctx->fromMethod(nullptr, py_datetime_new));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__call__"), callBridge);
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__lt__"), 
                                          ctx->fromMethod(nullptr, py_datetime_lt));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__eq__"), 
                                          ctx->fromMethod(nullptr, py_datetime_eq));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isoformat"), 
                                          ctx->fromMethod(nullptr, py_datetime_isoformat));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "now"), 
                                          ctx->fromMethod(nullptr, py_datetime_now));
    datetimeType = datetimeType->setAttribute(ctx, env->getReprString(), 
                                          ctx->fromMethod(nullptr, py_datetime_repr));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "datetime"), datetimeType);

    // time
    const proto::ProtoObject* timeType = ctx->newObject(false);
    if (env && env->getTypePrototype()) timeType = timeType->setAttribute(ctx, env->getClassString(), env->getTypePrototype());
    if (env && env->getObjectPrototype()) timeType = timeType->addParent(ctx, env->getObjectPrototype());
    timeType = timeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), 
                                    PythonEnvironment::getInternedString(ctx, "time")->asObject(ctx));
    timeType = timeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__new__"), 
                                    ctx->fromMethod(nullptr, py_time_new));
    timeType = timeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__call__"), callBridge);
    timeType = timeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isoformat"), 
                                    ctx->fromMethod(nullptr, py_time_isoformat));
    timeType = timeType->setAttribute(ctx, env->getReprString(), 
                                    ctx->fromMethod(nullptr, py_time_repr));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "time"), timeType);

    return mod;
}

} // namespace datetime
} // namespace protoPython
