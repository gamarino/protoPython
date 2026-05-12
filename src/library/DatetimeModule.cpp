#include <protoPython/DatetimeModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/DiagUtils.h>
#include <protoCore.h>
#include <string>

#include <algorithm>
#include <ctime>
#include <cstring>

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

static const proto::ProtoObject* create_time_instance(proto::ProtoContext* ctx, const proto::ProtoObject* cls, int h, int min, int s, int ms) {
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

static const proto::ProtoObject* py_timedelta_mul(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    DeltaState* state = get_delta_state(ctx, self);
    if (!state) return PROTO_NONE;
    
    double factor = args->getAt(ctx, 0)->asDouble(ctx);
    long long total_us = state->days * 86400000000LL + state->seconds * 1000000LL + state->microseconds;
    total_us = (long long)(total_us * factor);
    
    return create_timedelta_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), 0, 0, total_us);
}

static const proto::ProtoObject* py_timedelta_abs(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DeltaState* s = get_delta_state(ctx, self);
    if (!s) return PROTO_NONE;
    if (s->days >= 0) return self;
    return create_timedelta_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), -s->days, -s->seconds, -s->microseconds);
}

static const proto::ProtoObject* py_timedelta_neg(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DeltaState* s = get_delta_state(ctx, self);
    if (!s) return PROTO_NONE;
    return create_timedelta_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), -s->days, -s->seconds, -s->microseconds);
}

static const proto::ProtoObject* py_timedelta_pos(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
}

static const proto::ProtoObject* py_timedelta_bool(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DeltaState* s = get_delta_state(ctx, self);
    if (!s) return PROTO_FALSE;
    return (s->days != 0 || s->seconds != 0 || s->microseconds != 0) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_timedelta_hash(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DeltaState* s = get_delta_state(ctx, self);
    if (!s) return ctx->fromInteger(0);
    long long h = s->days ^ s->seconds ^ s->microseconds;
    return ctx->fromInteger(h);
}

static const proto::ProtoObject* py_timedelta_truediv(

    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    DeltaState* state = get_delta_state(ctx, self);
    if (!state) return PROTO_NONE;
    
    const proto::ProtoObject* other = args->getAt(ctx, 0);
    long long total_us = state->days * 86400000000LL + state->seconds * 1000000LL + state->microseconds;
    
    DeltaState* otherState = get_delta_state(ctx, other);
    if (otherState) {
        long long other_us = otherState->days * 86400000000LL + otherState->seconds * 1000000LL + otherState->microseconds;
        if (other_us == 0) return PROTO_NONE; // Should raise ZeroDivisionError
        return ctx->fromDouble((double)total_us / other_us);
    } else {
        double factor = other->asDouble(ctx);
        if (factor == 0.0) return PROTO_NONE;
        return create_timedelta_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), 0, 0, (long long)(total_us / factor));
    }
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

static const proto::ProtoObject* py_date_replace(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* /*posArgs*/, const proto::ProtoSparseList* kwArgs) {
    
    DateState* state = get_date_state(ctx, self);
    if (!state) return PROTO_NONE;
    
    int y = state->year;
    int m = state->month;
    int d = state->day;
    
    if (kwArgs) {
        const proto::ProtoObject* o;
        if ((o = getKwArg(ctx, kwArgs, "year")) && o != PROTO_NONE) y = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "month")) && o != PROTO_NONE) m = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "day")) && o != PROTO_NONE) d = (int)o->asLong(ctx);
    }
    
    return create_date_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), y, m, d);
}

static const proto::ProtoObject* py_date_weekday(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateState* state = get_date_state(ctx, self);
    if (!state) return ctx->fromInteger(0);
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = state->year - 1900;
    t.tm_mon = state->month - 1;
    t.tm_mday = state->day;
    mktime(&t);
    return ctx->fromInteger((t.tm_wday + 6) % 7); // Mon=0, Sun=6
}

static const proto::ProtoObject* py_date_isoweekday(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateState* state = get_date_state(ctx, self);
    if (!state) return ctx->fromInteger(1);
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = state->year - 1900;
    t.tm_mon = state->month - 1;
    t.tm_mday = state->day;
    mktime(&t);
    return ctx->fromInteger(t.tm_wday == 0 ? 7 : t.tm_wday); // Mon=1, Sun=7
}

static const proto::ProtoObject* py_date_isocalendar(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateState* state = get_date_state(ctx, self);
    if (!state) return PROTO_NONE;
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = state->year - 1900;
    t.tm_mon = state->month - 1;
    t.tm_mday = state->day;
    mktime(&t);
    
    // ISO calendar week calculation is a bit complex, but we can use strftime %G, %V, %u if available
    char y[8], w[8], d[8];
    std::strftime(y, sizeof(y), "%G", &t);
    std::strftime(w, sizeof(w), "%V", &t);
    std::strftime(d, sizeof(d), "%u", &t);
    
    const proto::ProtoList* tup = ctx->newList();
    tup = tup->appendLast(ctx, ctx->fromInteger(std::stoll(y)));
    tup = tup->appendLast(ctx, ctx->fromInteger(std::stoll(w)));
    tup = tup->appendLast(ctx, ctx->fromInteger(std::stoll(d)));
    return ctx->newTupleFromList(tup)->asObject(ctx);
}

static long long date_to_ordinal(int y, int m, int d) {
    if (m < 3) {
        y--;
        m += 12;
    }
    return (365LL * y) + (y / 4) - (y / 100) + (y / 400) + ((153LL * m + 8) / 5) + d - 306LL;
}

static const proto::ProtoObject* py_date_toordinal(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateState* state = get_date_state(ctx, self);
    if (!state) return ctx->fromInteger(1);
    return ctx->fromInteger(date_to_ordinal(state->year, state->month, state->day));
}

static const proto::ProtoObject* py_date_hash(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateState* s = get_date_state(ctx, self);
    if (!s) return ctx->fromInteger(0);
    long long h = s->year ^ (s->month << 20) ^ (s->day << 24);
    return ctx->fromInteger(h);
}

static const proto::ProtoObject* py_date_fromtimestamp(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList* /*kwArgs*/) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    double ts = args->getAt(ctx, 0)->asDouble(ctx);
    time_t t = (time_t)ts;
    struct tm* ltm = localtime(&t);
    return create_date_instance(ctx, self, ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday);
}

static const proto::ProtoObject* py_date_fromordinal(


    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList* /*kwArgs*/) {
    
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    long long n = args->getAt(ctx, 0)->asLong(ctx);
    
    // Simple iterative approach for fromordinal (could be improved)
    int y = (int)(n / 366);
    if (y < 1) y = 1;
    while (date_to_ordinal(y + 1, 1, 1) <= n) y++;
    int m = 1;
    while (date_to_ordinal(y, m + 1, 1) <= n) m++;
    int d = (int)(n - date_to_ordinal(y, m, 1) + 1);
    
    return create_date_instance(ctx, self, y, m, d);
}

static const proto::ProtoObject* py_date_add(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    DateState* ds = get_date_state(ctx, self);
    DeltaState* ts = get_delta_state(ctx, args->getAt(ctx, 0));
    if (!ds || !ts) return PROTO_NONE;
    
    long long ord = date_to_ordinal(ds->year, ds->month, ds->day);
    ord += ts->days;
    
    // fromordinal logic
    int y = (int)(ord / 366);
    if (y < 1) y = 1;
    while (date_to_ordinal(y + 1, 1, 1) <= ord) y++;
    int m = 1;
    while (date_to_ordinal(y, m + 1, 1) <= ord) m++;
    int d = (int)(ord - date_to_ordinal(y, m, 1) + 1);
    
    return create_date_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), y, m, d);
}

static const proto::ProtoObject* py_date_sub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* other = args->getAt(ctx, 0);
    DateState* ds1 = get_date_state(ctx, self);
    DateState* ds2 = get_date_state(ctx, other);
    
    if (ds1 && ds2) {
        long long o1 = date_to_ordinal(ds1->year, ds1->month, ds1->day);
        long long o2 = date_to_ordinal(ds2->year, ds2->month, ds2->day);
        return create_timedelta_instance(ctx, nullptr, o1 - o2, 0, 0);
    }
    
    DeltaState* ts = get_delta_state(ctx, other);
    if (ds1 && ts) {
        long long ord = date_to_ordinal(ds1->year, ds1->month, ds1->day);
        ord -= ts->days;
        
        int y = (int)(ord / 366);
        if (y < 1) y = 1;
        while (date_to_ordinal(y + 1, 1, 1) <= ord) y++;
        int m = 1;
        while (date_to_ordinal(y, m + 1, 1) <= ord) m++;
        int d = (int)(ord - date_to_ordinal(y, m, 1) + 1);
        
        return create_date_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), y, m, d);
    }
    
    return PROTO_NONE;
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

static const proto::ProtoObject* py_datetime_replace(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* /*posArgs*/, const proto::ProtoSparseList* kwArgs) {
    
    DateTimeState* state = get_datetime_state(ctx, self);
    if (!state) return PROTO_NONE;
    
    int y = state->year;
    int m = state->month;
    int d = state->day;
    int h = state->hour;
    int min = state->minute;
    int s = state->second;
    int ms = state->microsecond;
    
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
    
    return create_datetime_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), y, m, d, h, min, s, ms);
}

static const proto::ProtoObject* py_datetime_add(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    DateTimeState* ds = get_datetime_state(ctx, self);
    DeltaState* ts = get_delta_state(ctx, args->getAt(ctx, 0));
    if (!ds || !ts) return PROTO_NONE;
    
    long long ord = date_to_ordinal(ds->year, ds->month, ds->day);
    long long total_sec = (ord * 86400LL) + (ds->hour * 3600LL) + (ds->minute * 60LL) + ds->second + ts->seconds + (ts->days * 86400LL);
    long long total_us = (long long)ds->microsecond + ts->microseconds;
    
    if (total_us >= 1000000LL || total_us < 0) {
        total_sec += total_us / 1000000LL;
        total_us %= 1000000LL;
        if (total_us < 0) { total_us += 1000000LL; total_sec--; }
    }
    
    long long new_ord = total_sec / 86400LL;
    int remaining_sec = (int)(total_sec % 86400LL);
    if (remaining_sec < 0) { remaining_sec += 86400; new_ord--; }
    
    int h = remaining_sec / 3600;
    int m = (remaining_sec % 3600) / 60;
    int s = remaining_sec % 60;
    
    int y = (int)(new_ord / 366);
    if (y < 1) y = 1;
    while (date_to_ordinal(y + 1, 1, 1) <= new_ord) y++;
    int mon = 1;
    while (date_to_ordinal(y, mon + 1, 1) <= new_ord) mon++;
    int d = (int)(new_ord - date_to_ordinal(y, mon, 1) + 1);
    
    return create_datetime_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), y, mon, d, h, m, s, (int)total_us);
}

static const proto::ProtoObject* py_datetime_sub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* other = args->getAt(ctx, 0);
    DateTimeState* ds1 = get_datetime_state(ctx, self);
    DateTimeState* ds2 = get_datetime_state(ctx, other);
    
    if (ds1 && ds2) {
        long long o1 = date_to_ordinal(ds1->year, ds1->month, ds1->day);
        long long o2 = date_to_ordinal(ds2->year, ds2->month, ds2->day);
        long long d_days = o1 - o2;
        long long d_sec = (long long)(ds1->hour - ds2->hour) * 3600LL + (ds1->minute - ds2->minute) * 60LL + (ds1->second - ds2->second);
        long long d_us = (long long)(ds1->microsecond - ds2->microsecond);
        return create_timedelta_instance(ctx, nullptr, d_days, d_sec, d_us);
    }
    
    DeltaState* ts = get_delta_state(ctx, other);
    if (ds1 && ts) {
        long long ord = date_to_ordinal(ds1->year, ds1->month, ds1->day);
        long long total_sec = (ord * 86400LL) + (ds1->hour * 3600LL) + (ds1->minute * 60LL) + ds1->second - ts->seconds - (ts->days * 86400LL);
        long long total_us = (long long)ds1->microsecond - ts->microseconds;
        
        if (total_us >= 1000000LL || total_us < 0) {
            total_sec += total_us / 1000000LL;
            total_us %= 1000000LL;
            if (total_us < 0) { total_us += 1000000LL; total_sec--; }
        }
        
        long long new_ord = total_sec / 86400LL;
        int remaining_sec = (int)(total_sec % 86400LL);
        if (remaining_sec < 0) { remaining_sec += 86400; new_ord--; }
        
        int h = remaining_sec / 3600;
        int m = (remaining_sec % 3600) / 60;
        int s = remaining_sec % 60;
        
        int y = (int)(new_ord / 366);
        if (y < 1) y = 1;
        while (date_to_ordinal(y + 1, 1, 1) <= new_ord) y++;
        int mon = 1;
        while (date_to_ordinal(y, mon + 1, 1) <= new_ord) mon++;
        int d = (int)(new_ord - date_to_ordinal(y, mon, 1) + 1);
        
        return create_datetime_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), y, mon, d, h, m, s, (int)total_us);
    }
    
    return PROTO_NONE;
}

static const proto::ProtoObject* py_datetime_now(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    time_t t = time(nullptr);
    struct tm* ltm = localtime(&t);
    return create_datetime_instance(ctx, self, ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday,
                                    ltm->tm_hour, ltm->tm_min, ltm->tm_sec, 0);
}


// time methods
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

static const proto::ProtoObject* py_timezone_utcoffset(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_offset"));
}

static const proto::ProtoObject* py_timezone_dst(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_timezone_tzname(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* name = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_name"));
    if (name) return name;
    return PythonEnvironment::getInternedString(ctx, "UTC")->asObject(ctx);
}

static const proto::ProtoObject* py_timezone_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs) {
    
    const proto::ProtoObject* offset = nullptr;

    const proto::ProtoObject* name = nullptr;
    
    if (posArgs) {
        size_t sz = posArgs->getSize(ctx);
        if (sz > 1) {
            offset = posArgs->getAt(ctx, 1);
        }

        if (sz > 2) name = posArgs->getAt(ctx, 2);
    }
    
    if (kwArgs) {
        const proto::ProtoObject* o;
        if ((o = getKwArg(ctx, kwArgs, "offset")) && o != PROTO_NONE) offset = o;
        if ((o = getKwArg(ctx, kwArgs, "name")) && o != PROTO_NONE) name = o;
    }
    
    if (!offset) {
        return PROTO_NONE; // Should raise TypeError
    }

    
    const proto::ProtoObject* instance = self->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), self);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_offset"), offset);
    if (name) instance = instance->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_name"), name);
    
    return instance;
}


static const proto::ProtoObject* py_datetime_fromtimestamp(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList* /*kwArgs*/) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    double ts = args->getAt(ctx, 0)->asDouble(ctx);
    time_t t = (time_t)ts;
    struct tm* ltm = localtime(&t);
    int us = (int)((ts - (double)t) * 1000000.0);
    return create_datetime_instance(ctx, self, ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday,
                                    ltm->tm_hour, ltm->tm_min, ltm->tm_sec, us);
}

static const proto::ProtoObject* py_datetime_combine(

    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList* /*kwArgs*/) {
    
    if (!args || args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* dateObj = args->getAt(ctx, 0);
    const proto::ProtoObject* timeObj = args->getAt(ctx, 1);
    
    DateState* ds = get_date_state(ctx, dateObj);
    TimeState* ts = get_time_state(ctx, timeObj);
    if (!ds || !ts) return PROTO_NONE;
    
    return create_datetime_instance(ctx, self, ds->year, ds->month, ds->day, ts->hour, ts->minute, ts->second, ts->microsecond);
}

static const proto::ProtoObject* py_datetime_hash(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    DateTimeState* s = get_datetime_state(ctx, self);
    if (!s) return ctx->fromInteger(0);
    long long h = s->year ^ (s->month << 20) ^ (s->day << 24) ^ (s->hour << 10) ^ (s->minute << 15) ^ (s->second << 20) ^ (s->microsecond << 2);
    return ctx->fromInteger(h);
}

static const proto::ProtoObject* py_time_lt(

    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    TimeState* s1 = get_time_state(ctx, self);
    TimeState* s2 = get_time_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_FALSE;
    if (s1->hour < s2->hour) return PROTO_TRUE;
    if (s1->hour > s2->hour) return PROTO_FALSE;
    if (s1->minute < s2->minute) return PROTO_TRUE;
    if (s1->minute > s2->minute) return PROTO_FALSE;
    if (s1->second < s2->second) return PROTO_TRUE;
    if (s1->second > s2->second) return PROTO_FALSE;
    return (s1->microsecond < s2->microsecond) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_time_eq(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    TimeState* s1 = get_time_state(ctx, self);
    TimeState* s2 = get_time_state(ctx, args->getAt(ctx, 0));
    if (!s1 || !s2) return PROTO_FALSE;
    return (s1->hour == s2->hour && s1->minute == s2->minute && s1->second == s2->second &&
            s1->microsecond == s2->microsecond) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_time_replace(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* /*posArgs*/, const proto::ProtoSparseList* kwArgs) {

    
    TimeState* state = get_time_state(ctx, self);
    if (!state) return PROTO_NONE;
    
    int h = state->hour;
    int min = state->minute;
    int s = state->second;
    int ms = state->microsecond;
    
    if (kwArgs) {
        const proto::ProtoObject* o;
        if ((o = getKwArg(ctx, kwArgs, "hour")) && o != PROTO_NONE) h = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "minute")) && o != PROTO_NONE) min = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "second")) && o != PROTO_NONE) s = (int)o->asLong(ctx);
        if ((o = getKwArg(ctx, kwArgs, "microsecond")) && o != PROTO_NONE) ms = (int)o->asLong(ctx);
    }
    
    return create_time_instance(ctx, self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__")), h, min, s, ms);
}

static const proto::ProtoObject* py_date_strftime(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* /*kwArgs*/) {

    // Method dispatch may give us posArgs=[format] (self bound) or
    // posArgs=[self, format] depending on how the call site obtained
    // the bound method (LOAD_METHOD vs descriptor protocol).  Accept
    // either layout.  Previously the size<2 guard rejected the
    // bound-self case and returned None, which silently broke every
    // datetime.date(..).strftime(fmt) call — visible at calendar
    // module load (datetime.date(2001, i, 1).strftime list comp) and
    // therefore at every _strptime / locale-aware datetime path that
    // depended on calendar's _localized_month / _localized_day.
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* formatObj = nullptr;
    if (posArgs->getSize(ctx) >= 2) {
        formatObj = posArgs->getAt(ctx, 1);
    } else {
        formatObj = posArgs->getAt(ctx, 0);
    }
    std::string format;
    if (formatObj && formatObj->isString(ctx)) formatObj->asString(ctx)->toUTF8String(ctx, format);

    int y = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "year"))->asLong(ctx);
    int m = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "month"))->asLong(ctx);
    int d = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "day"))->asLong(ctx);

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;

    char buf[256];
    if (std::strftime(buf, sizeof(buf), format.c_str(), &t)) {
        return proto::ProtoString::fromUTF8(ctx, buf)->asObject(ctx);
    }
    return proto::ProtoString::fromUTF8(ctx, "")->asObject(ctx);
}

static const proto::ProtoObject* py_datetime_strftime(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* /*kwArgs*/) {

    // Same bound-self / unbound-call layout split as py_date_strftime.
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* formatObj = nullptr;
    if (posArgs->getSize(ctx) >= 2) {
        formatObj = posArgs->getAt(ctx, 1);
    } else {
        formatObj = posArgs->getAt(ctx, 0);
    }
    std::string format;
    if (formatObj && formatObj->isString(ctx)) formatObj->asString(ctx)->toUTF8String(ctx, format);
    
    int y = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "year"))->asLong(ctx);
    int m = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "month"))->asLong(ctx);
    int d = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "day"))->asLong(ctx);
    int hour = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hour"))->asLong(ctx);
    int min = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "minute"))->asLong(ctx);
    int sec = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "second"))->asLong(ctx);
    
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    
    char buf[256];
    if (std::strftime(buf, sizeof(buf), format.c_str(), &t)) {
        return proto::ProtoString::fromUTF8(ctx, buf)->asObject(ctx);
    }
    return proto::ProtoString::fromUTF8(ctx, "")->asObject(ctx);
}

static const proto::ProtoObject* py_date_timetuple(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* /*posArgs*/, const proto::ProtoSparseList* /*kwArgs*/) {
    
    int y = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "year"))->asLong(ctx);
    int m = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "month"))->asLong(ctx);
    int d = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "day"))->asLong(ctx);
    
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    mktime(&t); // Fill tm_wday, tm_yday
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* timeMod = env->resolve("time", ctx);
    if (!timeMod) return PROTO_NONE;
    const proto::ProtoObject* struct_time = timeMod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "struct_time"));
    if (!struct_time) return PROTO_NONE;
    
    const proto::ProtoList* args = ctx->newList();
    const proto::ProtoList* tup = ctx->newList();
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_year + 1900));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_mon + 1));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_mday));
    tup = tup->appendLast(ctx, ctx->fromInteger(0)); // hour
    tup = tup->appendLast(ctx, ctx->fromInteger(0)); // min
    tup = tup->appendLast(ctx, ctx->fromInteger(0)); // sec
    tup = tup->appendLast(ctx, ctx->fromInteger((t.tm_wday + 6) % 7)); // Mon=0
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_yday + 1));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_isdst));
    
    args = args->appendLast(ctx, ctx->newTupleFromList(tup)->asObject(ctx));
    return struct_time->call(ctx, nullptr, nullptr, struct_time, args, nullptr);
}

static const proto::ProtoObject* py_datetime_timetuple(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* /*posArgs*/, const proto::ProtoSparseList* /*kwArgs*/) {
    
    int y = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "year"))->asLong(ctx);
    int m = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "month"))->asLong(ctx);
    int d = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "day"))->asLong(ctx);
    int hour = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hour"))->asLong(ctx);
    int min = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "minute"))->asLong(ctx);
    int sec = (int)self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "second"))->asLong(ctx);
    
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    mktime(&t);
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* timeMod = env->resolve("time", ctx);
    if (!timeMod) return PROTO_NONE;
    const proto::ProtoObject* struct_time = timeMod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "struct_time"));
    
    const proto::ProtoList* tup = ctx->newList();
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_year + 1900));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_mon + 1));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_mday));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_hour));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_min));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_sec));
    tup = tup->appendLast(ctx, ctx->fromInteger((t.tm_wday + 6) % 7));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_yday + 1));
    tup = tup->appendLast(ctx, ctx->fromInteger(t.tm_isdst));
    
    const proto::ProtoList* args = ctx->newList();
    args = args->appendLast(ctx, ctx->newTupleFromList(tup)->asObject(ctx));
    return struct_time->call(ctx, nullptr, nullptr, struct_time, args, nullptr);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), 
                          PythonEnvironment::getInternedString(ctx, "_datetime")->asObject(ctx));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "MINYEAR"), ctx->fromInteger(1));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "MAXYEAR"), ctx->fromInteger(9999));

    const proto::ProtoObject* callBridge = ctx->fromMethod(nullptr, py_class_call_bridge);

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
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__mul__"), 
                                               ctx->fromMethod(nullptr, py_timedelta_mul));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__truediv__"), 
                                               ctx->fromMethod(nullptr, py_timedelta_truediv));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__hash__"), 
                                               ctx->fromMethod(nullptr, py_timedelta_hash));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__abs__"), 
                                               ctx->fromMethod(nullptr, py_timedelta_abs));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__neg__"), 
                                               ctx->fromMethod(nullptr, py_timedelta_neg));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__pos__"), 
                                               ctx->fromMethod(nullptr, py_timedelta_pos));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__bool__"), 
                                               ctx->fromMethod(nullptr, py_timedelta_bool));
    timedeltaType = timedeltaType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "total_seconds"), 
                                               ctx->fromMethod(nullptr, py_timedelta_total_seconds));

    timedeltaType = timedeltaType->setAttribute(ctx, env->getReprString(), 
                                              ctx->fromMethod(nullptr, py_timedelta_repr));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "timedelta"), timedeltaType);

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
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__new__"), 
                                            ctx->fromMethod(nullptr, py_timezone_new));
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__call__"), callBridge);
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "utcoffset"), 
                                            ctx->fromMethod(nullptr, py_timezone_utcoffset));
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "dst"), 
                                            ctx->fromMethod(nullptr, py_timezone_dst));
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "tzname"), 
                                            ctx->fromMethod(nullptr, py_timezone_tzname));
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), 
                                            PythonEnvironment::getInternedString(ctx, "timezone")->asObject(ctx));

    // UTC (on module and on timezone class)
    const proto::ProtoList* utcArgs = ctx->newList();
    utcArgs = utcArgs->appendLast(ctx, timezoneType); // cls
    utcArgs = utcArgs->appendLast(ctx, create_timedelta_instance(ctx, timedeltaType, 0, 0, 0)); // offset
    const proto::ProtoObject* utc = py_timezone_new(ctx, timezoneType, nullptr, utcArgs, nullptr);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "UTC"), utc);


    // Important: set both lowercase and uppercase on the class!
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "utc"), utc);
    timezoneType = timezoneType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "UTC"), utc);

    
    // NOW add timezone to module
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "timezone"), timezoneType);

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
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__add__"), 
                                    ctx->fromMethod(nullptr, py_date_add));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__sub__"), 
                                    ctx->fromMethod(nullptr, py_date_sub));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__hash__"), 
                                    ctx->fromMethod(nullptr, py_date_hash));


    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isoformat"), 
                                    ctx->fromMethod(nullptr, py_date_isoformat));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "toordinal"), 
                                    ctx->fromMethod(nullptr, py_date_toordinal));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "fromordinal"), 
                                    ctx->fromMethod(nullptr, py_date_fromordinal));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "fromtimestamp"), 
                                    ctx->fromMethod(nullptr, py_date_fromtimestamp));

    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isocalendar"), 
                                    ctx->fromMethod(nullptr, py_date_isocalendar));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "today"), 
                                    ctx->fromMethod(nullptr, py_date_today));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "replace"), 
                                    ctx->fromMethod(nullptr, py_date_replace));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "weekday"), 
                                    ctx->fromMethod(nullptr, py_date_weekday));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isoweekday"), 
                                    ctx->fromMethod(nullptr, py_date_isoweekday));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "timetuple"), 
                                    ctx->fromMethod(nullptr, py_date_timetuple));
    dateType = dateType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "strftime"), 
                                    ctx->fromMethod(nullptr, py_date_strftime));
    dateType = dateType->setAttribute(ctx, env->getReprString(),
                                    ctx->fromMethod(nullptr, py_date_repr));
    // CPython: `str(d)` == `d.isoformat()` for a date.  Without an
    // explicit __str__ on dateType, the MRO walk in
    // PythonEnvironment::reprObject would either fall through to
    // object.__str__ (producing `<date object at 0x...>`) or hit our
    // own __repr__ — but missing __mro__ on dateType caused the same
    // class of regression that the range class had: the str / repr
    // dispatcher couldn't find date.__repr__ at all when it walked
    // an empty MRO.  Install __str__ = isoformat and set __mro__ /
    // __bases__ so dunder lookups see the date level first.
    dateType = dateType->setAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "__str__"),
        ctx->fromMethod(nullptr, py_date_isoformat));
    if (env && env->getObjectPrototype()) {
        const proto::ProtoList* mroL = ctx->newList()
            ->appendLast(ctx, dateType)
            ->appendLast(ctx, env->getObjectPrototype());
        dateType = dateType->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__mro__"),
            ctx->newTupleFromList(mroL)->asObject(ctx));
        const proto::ProtoList* basesL = ctx->newList()
            ->appendLast(ctx, env->getObjectPrototype());
        dateType = dateType->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__bases__"),
            ctx->newTupleFromList(basesL)->asObject(ctx));
    }
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
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__add__"), 
                                          ctx->fromMethod(nullptr, py_datetime_add));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__sub__"), 
                                          ctx->fromMethod(nullptr, py_datetime_sub));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__hash__"), 
                                          ctx->fromMethod(nullptr, py_datetime_hash));


    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isoformat"), 
                                          ctx->fromMethod(nullptr, py_datetime_isoformat));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "now"), 
                                          ctx->fromMethod(nullptr, py_datetime_now));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "fromtimestamp"), 
                                          ctx->fromMethod(nullptr, py_datetime_fromtimestamp));

    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "replace"), 
                                          ctx->fromMethod(nullptr, py_datetime_replace));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "weekday"), 
                                          ctx->fromMethod(nullptr, py_date_weekday));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isoweekday"), 
                                          ctx->fromMethod(nullptr, py_date_isoweekday));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isocalendar"), 
                                          ctx->fromMethod(nullptr, py_date_isocalendar));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "timetuple"), 
                                          ctx->fromMethod(nullptr, py_datetime_timetuple));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "strftime"), 
                                          ctx->fromMethod(nullptr, py_datetime_strftime));
    datetimeType = datetimeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "combine"), 
                                          ctx->fromMethod(nullptr, py_datetime_combine));
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
    timeType = timeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__lt__"), 
                                    ctx->fromMethod(nullptr, py_time_lt));
    timeType = timeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__eq__"), 
                                    ctx->fromMethod(nullptr, py_time_eq));
    timeType = timeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "replace"), 
                                    ctx->fromMethod(nullptr, py_time_replace));

    timeType = timeType->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "isoformat"), 
                                    ctx->fromMethod(nullptr, py_time_isoformat));
    timeType = timeType->setAttribute(ctx, env->getReprString(), 
                                    ctx->fromMethod(nullptr, py_time_repr));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "time"), timeType);

    return mod;
}

} // namespace datetime
} // namespace protoPython
