// _mt19937: the MT19937 generator behind _random.Random, natively.
//
// _random.py keeps the Python-visible class (random.Random subclasses it)
// and holds one generator state from new_state(); every draw is one native
// call.  The generator and its seeding are CPython's (Modules/_randommodule.c),
// so a given seed yields the same stream as CPython.  The pure-Python
// generator this replaces cost about 25 microseconds per random() and made
// random-heavy code (test_collections' test_large_size draws 1.5 million
// letters) run for minutes.
#include <protoPython/MersenneTwisterModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <cstdint>

namespace protoPython {
namespace mersenne_twister {

namespace {

constexpr int N = 624;
constexpr int M = 397;
constexpr uint32_t MATRIX_A = 0x9908B0DFu;
constexpr uint32_t UPPER_MASK = 0x80000000u;
constexpr uint32_t LOWER_MASK = 0x7FFFFFFFu;

struct State {
    uint32_t mt[N];
    int mti = N + 1;
};

void finalizeState(void* p) { delete static_cast<State*>(p); }

void initGenrand(State* s, uint32_t seed) {
    s->mt[0] = seed;
    for (int i = 1; i < N; ++i)
        s->mt[i] = 1812433253u * (s->mt[i - 1] ^ (s->mt[i - 1] >> 30)) + static_cast<uint32_t>(i);
    s->mti = N;
}

uint32_t genrand(State* s) {
    static const uint32_t mag01[2] = {0u, MATRIX_A};
    uint32_t y;
    if (s->mti >= N) {
        if (s->mti == N + 1) initGenrand(s, 5489u);
        int kk;
        for (kk = 0; kk < N - M; ++kk) {
            y = (s->mt[kk] & UPPER_MASK) | (s->mt[kk + 1] & LOWER_MASK);
            s->mt[kk] = s->mt[kk + M] ^ (y >> 1) ^ mag01[y & 1u];
        }
        for (; kk < N - 1; ++kk) {
            y = (s->mt[kk] & UPPER_MASK) | (s->mt[kk + 1] & LOWER_MASK);
            s->mt[kk] = s->mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 1u];
        }
        y = (s->mt[N - 1] & UPPER_MASK) | (s->mt[0] & LOWER_MASK);
        s->mt[N - 1] = s->mt[M - 1] ^ (y >> 1) ^ mag01[y & 1u];
        s->mti = 0;
    }
    y = s->mt[s->mti++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    y ^= (y >> 18);
    return y;
}

State* stateArg(proto::ProtoContext* ctx, const proto::ProtoArguments* args) {
    if (!args || args->getSize(ctx) < 1) return nullptr;
    const proto::ProtoObject* h = args->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ext = h ? h->asExternalPointer(ctx) : nullptr;
    return ext ? static_cast<State*>(ext->getPointer(ctx)) : nullptr;
}

const proto::ProtoObject* fail(proto::ProtoContext* ctx, const char* msg) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) env->raiseTypeError(ctx, msg);
    return nullptr;
}

// new_state() -> handle, unseeded (the first draw seeds with 5489, as C does).
const proto::ProtoObject* py_new_state(proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    return ctx->fromExternalPointer(new State, finalizeState);
}

// init_by_array(handle, key): key is a sequence of 32-bit ints.
const proto::ProtoObject* py_init_by_array(proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    State* s = stateArg(ctx, args);
    if (!s || args->getSize(ctx) < 2) return fail(ctx, "init_by_array(state, key)");
    const proto::ProtoObject* keyObj = args->getAt(ctx, 1);
    TupleView kt = PythonEnvironment::tupleView(ctx, keyObj);
    const proto::ProtoList* kl = kt ? nullptr : listStorage(ctx, keyObj);
    const proto::proto_ulong keyLength = kt ? kt->getSize(ctx) : (kl ? kl->getSize(ctx) : 0);
    auto keyAt = [&](proto::proto_ulong j) -> uint32_t {
        const proto::ProtoObject* v = kt ? kt->getAt(ctx, static_cast<int>(j)) : kl->getAt(ctx, static_cast<int>(j));
        return (v && v->isInteger(ctx)) ? static_cast<uint32_t>(v->asLong(ctx)) : 0u;
    };
    initGenrand(s, 19650218u);
    if (keyLength == 0) return PROTO_NONE;
    int i = 1;
    proto::proto_ulong j = 0;
    proto::proto_ulong k = (static_cast<proto::proto_ulong>(N) > keyLength) ? N : keyLength;
    for (; k; --k) {
        s->mt[i] = (s->mt[i] ^ ((s->mt[i - 1] ^ (s->mt[i - 1] >> 30)) * 1664525u))
                   + keyAt(j) + static_cast<uint32_t>(j);
        ++i; ++j;
        if (i >= N) { s->mt[0] = s->mt[N - 1]; i = 1; }
        if (j >= keyLength) j = 0;
    }
    for (k = N - 1; k; --k) {
        s->mt[i] = (s->mt[i] ^ ((s->mt[i - 1] ^ (s->mt[i - 1] >> 30)) * 1566083941u))
                   - static_cast<uint32_t>(i);
        ++i;
        if (i >= N) { s->mt[0] = s->mt[N - 1]; i = 1; }
    }
    s->mt[0] = 0x80000000u;
    return PROTO_NONE;
}

// genrand_uint32(handle) -> int in [0, 2**32).
const proto::ProtoObject* py_genrand(proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    State* s = stateArg(ctx, args);
    if (!s) return fail(ctx, "genrand_uint32(state)");
    return ctx->fromInteger(static_cast<long long>(genrand(s)));
}

// random(handle) -> float in [0.0, 1.0) from 53 bits, CPython's recipe.
const proto::ProtoObject* py_random(proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    State* s = stateArg(ctx, args);
    if (!s) return fail(ctx, "random(state)");
    const uint32_t a = genrand(s) >> 5, b = genrand(s) >> 6;
    return ctx->fromDouble((a * 67108864.0 + b) * (1.0 / 9007199254740992.0));
}

// getrandbits32(handle, k) -> int with k random bits, 0 < k <= 32.
const proto::ProtoObject* py_getrandbits32(proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    State* s = stateArg(ctx, args);
    if (!s || args->getSize(ctx) < 2 || !args->getAt(ctx, 1)->isInteger(ctx))
        return fail(ctx, "getrandbits32(state, k)");
    const long long k = args->getAt(ctx, 1)->asLong(ctx);
    if (k <= 0 || k > 32) return fail(ctx, "getrandbits32: k must be in 1..32");
    return ctx->fromInteger(static_cast<long long>(genrand(s) >> (32 - k)));
}

// getstate(handle) -> tuple of 624 words and the position.
const proto::ProtoObject* py_getstate(proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    State* s = stateArg(ctx, args);
    if (!s) return fail(ctx, "getstate(state)");
    const proto::ProtoList* l = ctx->newList();
    for (int i = 0; i < N; ++i) l = l->appendLast(ctx, ctx->fromInteger(static_cast<long long>(s->mt[i])));
    l = l->appendLast(ctx, ctx->fromInteger(s->mti));
    return PythonEnvironment::tupleFromList(ctx, l);
}

// setstate(handle, words, position): words are 624 ints, already validated.
const proto::ProtoObject* py_setstate(proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    State* s = stateArg(ctx, args);
    if (!s || args->getSize(ctx) < 3) return fail(ctx, "setstate(state, words, position)");
    const proto::ProtoObject* w = args->getAt(ctx, 1);
    TupleView wt = PythonEnvironment::tupleView(ctx, w);
    const proto::ProtoList* wl = wt ? nullptr : listStorage(ctx, w);
    const proto::proto_ulong n = wt ? wt->getSize(ctx) : (wl ? wl->getSize(ctx) : 0);
    if (n != static_cast<proto::proto_ulong>(N)) return fail(ctx, "setstate: 624 words expected");
    for (int i = 0; i < N; ++i) {
        const proto::ProtoObject* v = wt ? wt->getAt(ctx, i) : wl->getAt(ctx, i);
        s->mt[i] = (v && v->isInteger(ctx)) ? static_cast<uint32_t>(v->asLong(ctx)) : 0u;
    }
    const proto::ProtoObject* pos = args->getAt(ctx, 2);
    s->mti = (pos && pos->isInteger(ctx)) ? static_cast<int>(pos->asLong(ctx)) : N;
    return PROTO_NONE;
}

}  // namespace

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    auto def = [&](const char* name, proto::ProtoMethod fn) {
        mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, name), ctx->fromMethod(nullptr, fn));
    };
    def("new_state", py_new_state);
    def("init_by_array", py_init_by_array);
    def("genrand_uint32", py_genrand);
    def("random", py_random);
    def("getrandbits32", py_getrandbits32);
    def("getstate", py_getstate);
    def("setstate", py_setstate);
    return mod;
}

}  // namespace mersenne_twister
}  // namespace protoPython
