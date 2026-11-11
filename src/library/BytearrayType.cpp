// bytearray: CPython's mutable byte sequence on protoCore.
//
// A bytearray keeps its content as a persistent ProtoList of byte chunks in
// the private attribute `__bytearray__`. Every chunk but the last holds
// kChunk bytes, so the byte at index i is in chunk i / kChunk; the last
// holds 1..kChunk bytes and there are no empty chunks. A chunk is a
// ProtoByteBuffer that is never written after it is published: a mutation
// builds new chunks and a new list (structural sharing keeps the rest) and
// publishes the list with a compare-and-swap on the attribute, retrying on a
// conflict, the discipline a list follows (publishListData). No lock is
// taken, and no update is lost when threads mutate one bytearray at once.
//
// Element 0 of the list is the export word, not a chunk (see "buffer
// exports" below): chunk c is at list index c + 1. It travels in the list so
// that a resize and an export are published by the same compare-and-swap.
//
// Appending copies at most one chunk and replaces one list path: O(kChunk +
// log n) per append, so building a bytearray of n bytes is linear in n
// (it was quadratic when each append copied the whole buffer). Operations
// that move bytes (insert, a slice replacement, delete) rebuild the content,
// O(n), as CPython's memmove does.
//
// bytes' read-only methods serve bytearray: each runs on a bytes snapshot of
// the content and a bytes result becomes a bytearray (ba.upper(), ba[1:]).

#include <protoPython/BytearrayType.h>
#include <protoPython/MemoryviewType.h>
#include <protoPython/BuiltinSignature.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <algorithm>
#include <cstdio>
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace protoPython {

namespace {

using PO = proto::ProtoObject;
using PL = proto::ProtoList;

constexpr size_t kChunk = 512;

// The content's key: internal (U+0001-prefixed), so Python code can neither
// read nor replace it (PythonEnvironment::pythonAttributeKey), interned once.
const proto::ProtoString* storageName(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    return env ? env->getBytearrayKey() : PythonEnvironment::getInternalString(ctx, "\001__bytearray__");
}

bool isCellObject(const PO* o) { return o && (reinterpret_cast<uintptr_t>(o) & 0x3F) == 0; }

// --- the chunked store ------------------------------------------------------

// A slot that is not a byte buffer is corruption, not data: SystemError,
// never a dereference.
[[noreturn]] void corruptStore(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) env->raiseSystemError(ctx, "bytearray storage is corrupt");
    throw std::runtime_error("bytearray storage is corrupt");
}

// --- buffer exports ----------------------------------------------------------
//
// The export word: the number of exports held now (a memoryview inside a
// `with` block or not yet released after one), a "watched" bit set once any
// memoryview has viewed the bytearray, and an epoch counting the resizes made
// while watched. A resize with exports held raises BufferError, as CPython's
// does; a view not held (protoCore has no reference counts to tell a live
// view from a dead one) remembers the epoch it was made at and raises
// BufferError when used after a resize, instead of reading shifted bytes.
constexpr long long kExportMask = (1LL << 20) - 1;
constexpr long long kWatched = 1LL << 20;
constexpr int kEpochShift = 21;

const PO* initialHead(proto::ProtoContext* ctx) { return ctx->fromInteger(0); }

long long headWord(proto::ProtoContext* ctx, const PL* l) {
    const PO* h = l->getSize(ctx) > 0 ? l->getAt(ctx, 0) : nullptr;
    if (!h || !h->isInteger(ctx)) corruptStore(ctx);
    return h->asLong(ctx);
}

size_t chunkCount(proto::ProtoContext* ctx, const PL* l) {
    const size_t k = l->getSize(ctx);
    if (k == 0) corruptStore(ctx);
    return k - 1;
}

const proto::ProtoByteBuffer* chunkAt(proto::ProtoContext* ctx, const PL* l, size_t i) {
    const PO* slot = l->getAt(ctx, static_cast<int>(i + 1));
    const proto::ProtoByteBuffer* bb = slot ? slot->asByteBuffer(ctx) : nullptr;
    if (!bb) corruptStore(ctx);
    return bb;
}

size_t storeSize(proto::ProtoContext* ctx, const PL* l) {
    const size_t k = chunkCount(ctx, l);
    return k == 0 ? 0 : (k - 1) * kChunk + chunkAt(ctx, l, k - 1)->getSize(ctx);
}

// Appends the bytes [from, to) of the store to `out`.
void storeRead(proto::ProtoContext* ctx, const PL* l, std::string& out, size_t from = 0,
               size_t to = static_cast<size_t>(-1)) {
    const size_t n = storeSize(ctx, l);
    if (to > n) to = n;
    if (from >= to) return;
    out.reserve(out.size() + (to - from));
    for (size_t c = from / kChunk; c * kChunk < to; ++c) {
        const proto::ProtoByteBuffer* bb = chunkAt(ctx, l, c);
        const size_t base = c * kChunk;
        const size_t a = std::max(from, base) - base;
        const size_t b = std::min(to, base + bb->getSize(ctx)) - base;
        out.append(bb->getBuffer(ctx) + a, b - a);
    }
}

const PL* storeAppend(proto::ProtoContext* ctx, const PL* l, const char* d, size_t n) {
    const size_t k = chunkCount(ctx, l);
    if (n == 0) return l;
    if (k > 0) {
        const proto::ProtoByteBuffer* last = chunkAt(ctx, l, k - 1);
        const size_t have = last->getSize(ctx);
        if (have < kChunk) {
            const size_t take = std::min(n, kChunk - have);
            std::string merged(last->getBuffer(ctx), have);
            merged.append(d, take);
            l = l->setAt(ctx, static_cast<int>(k),
                         ctx->newByteBuffer(merged.data(), merged.size())->asObject(ctx));
            d += take;
            n -= take;
        }
    }
    while (n > 0) {
        const size_t take = std::min(n, kChunk);
        l = l->appendLast(ctx, ctx->newByteBuffer(d, take)->asObject(ctx));
        d += take;
        n -= take;
    }
    return l;
}

// A store holding [d, d + n), with export word `head`.
const PL* storeFrom(proto::ProtoContext* ctx, const PO* head, const char* d, size_t n) {
    return storeAppend(ctx, ctx->newList()->appendLast(ctx, head), d, n);
}

unsigned char storeAt(proto::ProtoContext* ctx, const PL* l, size_t i) {
    return static_cast<unsigned char>(chunkAt(ctx, l, i / kChunk)->getBuffer(ctx)[i % kChunk]);
}

const PL* storeSetByte(proto::ProtoContext* ctx, const PL* l, size_t i, unsigned char b) {
    const proto::ProtoByteBuffer* c = chunkAt(ctx, l, i / kChunk);
    std::string copy(c->getBuffer(ctx), c->getSize(ctx));
    copy[i % kChunk] = static_cast<char>(b);
    return l->setAt(ctx, static_cast<int>(i / kChunk + 1), ctx->newByteBuffer(copy.data(), copy.size())->asObject(ctx));
}

// Overwrites [at, at + data.size()) without changing the size.
const PL* storeOverwrite(proto::ProtoContext* ctx, const PL* l, size_t at, const std::string& data) {
    size_t done = 0;
    while (done < data.size()) {
        const size_t i = at + done;
        const size_t c = i / kChunk;
        const proto::ProtoByteBuffer* bb = chunkAt(ctx, l, c);
        std::string copy(bb->getBuffer(ctx), bb->getSize(ctx));
        const size_t off = i % kChunk;
        const size_t take = std::min(data.size() - done, copy.size() - off);
        std::memcpy(&copy[off], data.data() + done, take);
        l = l->setAt(ctx, static_cast<int>(c + 1), ctx->newByteBuffer(copy.data(), copy.size())->asObject(ctx));
        done += take;
    }
    return l;
}

const PL* storePopLast(proto::ProtoContext* ctx, const PL* l) {
    const size_t k = chunkCount(ctx, l);
    const proto::ProtoByteBuffer* last = chunkAt(ctx, l, k - 1);
    const size_t have = last->getSize(ctx);
    if (have == 1) return l->removeLast(ctx);
    return l->setAt(ctx, static_cast<int>(k), ctx->newByteBuffer(last->getBuffer(ctx), have - 1)->asObject(ctx));
}

// --- instances ---------------------------------------------------------------

// A bytearray is recognised by its type -- an instance of bytearray or of a
// subclass -- and then by its content under the internal key.
const PO* bytearrayType(proto::ProtoContext* ctx);

const PL* stateOf(proto::ProtoContext* ctx, const PO* obj, const PO** raw = nullptr) {
    if (!isCellObject(obj)) return nullptr;
    const PO* cls = bytearrayType(ctx);
    if (!cls || obj == cls || obj->isInstanceOf(ctx, cls) != PROTO_TRUE) return nullptr;
    const PO* s = obj->getOwnAttributeDirect(ctx, storageName(ctx));
    if (raw) *raw = s;
    if (!s || s == PROTO_NONE) return nullptr;
    return s->asList(ctx);
}

const PO* bytearrayType(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    return env ? env->getBytearrayPrototype() : nullptr;
}

const PO* makeInstance(proto::ProtoContext* ctx, const PO* cls, const char* d, size_t n) {
    const PO* b = cls->newChild(ctx, true);
    b->setAttribute(ctx, storageName(ctx), storeFrom(ctx, initialHead(ctx), d, n)->asObject(ctx));
    return b;
}

} // namespace

[[noreturn]] void raiseBufferError(proto::ProtoContext* ctx, const std::string& msg) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const PO* cls = env ? env->getBuiltins()->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "BufferError"))
                        : nullptr;
    const PO* exc = (cls && cls != PROTO_NONE) ? env->callObject(cls, {PythonEnvironment::newStr(ctx, msg)}) : nullptr;
    if (exc && env) env->raiseException(exc);
    throw std::runtime_error(msg);
}

namespace {

// Publishes `derive(current store)` with a compare-and-swap, retrying on a
// conflict; derive returns nullptr after raising, which ends the mutation.
// derive runs no Python code: arguments are read before the loop. The
// export word is carried over from the current store: a change of size
// raises BufferError while exports are held, and advances the epoch of a
// watched bytearray -- checked against the store the swap replaces, so an
// export and a resize can never both succeed.
template <class Derive>
bool mutate(proto::ProtoContext* ctx, const PO* ba, Derive derive) {
    const proto::ProtoString* name = storageName(ctx);
    for (;;) {
        const PO* raw = ba->getOwnAttributeDirect(ctx, name);
        const PL* cur = raw ? raw->asList(ctx) : nullptr;
        if (!cur) return false;
        const PL* next = derive(cur);
        if (!next) return false;
        if (next == cur) return true;
        long long word = headWord(ctx, cur);
        if (storeSize(ctx, next) != storeSize(ctx, cur)) {
            if (word & kExportMask) {
                raiseBufferError(ctx, "Existing exports of data: object cannot be re-sized");
                return false;
            }
            if (word & kWatched) word += 1LL << kEpochShift;
        }
        if (headWord(ctx, next) != word) next = next->setAt(ctx, 0, ctx->fromInteger(word));
        if (ba->setAttributeIfEqual(ctx, name, raw, next->asObject(ctx))) return true;
    }
}

// Replaces the whole content with `derive(old content)` (for operations that
// move bytes); derive fills `out` from `in` and returns false after raising.
template <class Derive>
bool rewrite(proto::ProtoContext* ctx, const PO* ba, Derive derive) {
    return mutate(ctx, ba, [&](const PL* cur) -> const PL* {
        std::string in;
        storeRead(ctx, cur, in);
        std::string out;
        if (!derive(in, out)) return nullptr;
        return storeFrom(ctx, cur->getAt(ctx, 0), out.data(), out.size());
    });
}

std::string typeName(proto::ProtoContext* ctx, const PO* o) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    std::string name = "object";
    const PO* cls = env && o ? env->getType(ctx, o) : nullptr;
    const PO* nm = cls ? cls->getAttribute(ctx, env->getNameString()) : nullptr;
    if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, name);
    return name;
}

PythonEnvironment* envOf(proto::ProtoContext* ctx) { return PythonEnvironment::fromContext(ctx); }

void raiseValue(proto::ProtoContext* ctx, const std::string& msg) {
    if (PythonEnvironment* env = envOf(ctx)) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, msg));
}
void raiseType(proto::ProtoContext* ctx, const std::string& msg) {
    if (PythonEnvironment* env = envOf(ctx)) env->raiseTypeError(ctx, msg);
}
void raiseIndex(proto::ProtoContext* ctx, const std::string& msg) {
    if (PythonEnvironment* env = envOf(ctx)) env->raiseIndexError(ctx, msg);
}

// The bytearray a method acts on: `self` in a bound call, the first
// argument in an unbound one (`bytearray.append(ba, 1)`), which must be a
// bytearray: CPython's descriptor TypeError otherwise, so an unbound call
// can never write to a bytes object. `shift` counts the arguments it took.
const PO* receiver(proto::ProtoContext* ctx, const PO* self, const proto::ProtoArguments* args,
                   unsigned& shift, const char* method) {
    shift = 0;
    if (stateOf(ctx, self)) return self;
    if (args && args->getSize(ctx) >= 1) {
        const PO* first = args->getAt(ctx, 0);
        if (stateOf(ctx, first)) {
            shift = 1;
            return first;
        }
        const PythonEnvironment* env = envOf(ctx);
        if (!self || self == PROTO_NONE || self == bytearrayType(ctx) || (env && self == env->getNonePrototype())) {
            raiseType(ctx, std::string("descriptor '") + method + "' for 'bytearray' objects doesn't apply to a '"
                           + typeName(ctx, first) + "' object");
            return nullptr;
        }
    }
    raiseType(ctx, std::string("descriptor '") + method + "' for 'bytearray' objects doesn't apply to a '"
                   + typeName(ctx, self) + "' object");
    return nullptr;
}

size_t argc(proto::ProtoContext* ctx, const proto::ProtoArguments* args, unsigned shift) {
    const size_t n = args ? args->getSize(ctx) : 0;
    return n > shift ? n - shift : 0;
}
const PO* arg(proto::ProtoContext* ctx, const proto::ProtoArguments* args, unsigned shift, size_t i) {
    return args->getAt(ctx, static_cast<int>(shift + i));
}

// An index argument as an integer (int, bool or __index__); false with
// TypeError raised otherwise.
bool asIndex(proto::ProtoContext* ctx, const PO* v, long long& out, const char* what = nullptr) {
    if (v == PROTO_TRUE || v == PROTO_FALSE) { out = v == PROTO_TRUE ? 1 : 0; return true; }
    PythonEnvironment* env = envOf(ctx);
    if (v && v->isInteger(ctx)) {
        try {
            out = v->asLong(ctx);
        } catch (const proto::ProtoException& overflow) {
            PythonEnvironment::rethrowUnlessOverflow(ctx, overflow);
            if (env) env->raiseOverflowError(ctx, "cannot fit 'int' into an index-sized integer");
            return false;
        }
        return true;
    }
    if (env && isCellObject(v)) {
        const PO* m = env->getAttribute(ctx, v, PythonEnvironment::getInternedString(ctx, "__index__"), false);
        if (m && m != PROTO_NONE) {
            const PO* r = env->callObject(m, {});
            if (!r) return false;
            return asIndex(ctx, r, out, what);
        }
    }
    raiseType(ctx, what ? std::string(what)
                        : "'" + typeName(ctx, v) + "' object cannot be interpreted as an integer");
    return false;
}

// A byte value argument: an int (or __index__) in range(0, 256).
bool asByte(proto::ProtoContext* ctx, const PO* v, unsigned char& out) {
    long long n = 0;
    if (v == PROTO_TRUE || v == PROTO_FALSE) {
        n = v == PROTO_TRUE ? 1 : 0;
    } else if (v && v->isInteger(ctx)) {
        try {
            n = v->asLong(ctx);
        } catch (const proto::ProtoException& overflow) {
            PythonEnvironment::rethrowUnlessOverflow(ctx, overflow);
            n = -1;
        }
    } else {
        PythonEnvironment* env = envOf(ctx);
        const PO* m = (env && isCellObject(v)) ? env->getAttribute(ctx, v, PythonEnvironment::getInternedString(ctx, "__index__"), false) : nullptr;
        if (!m || m == PROTO_NONE) {
            raiseType(ctx, "'" + typeName(ctx, v) + "' object cannot be interpreted as an integer");
            return false;
        }
        const PO* r = env->callObject(m, {});
        if (!r) return false;
        return asByte(ctx, r, out);
    }
    if (n < 0 || n > 255) {
        raiseValue(ctx, "byte must be in range(0, 256)");
        return false;
    }
    out = static_cast<unsigned char>(n);
    return true;
}

bool isSlice(proto::ProtoContext* ctx, const PO* key) {
    PythonEnvironment* env = envOf(ctx);
    return env && env->getSliceType() && isCellObject(key) && key->isInstanceOf(ctx, env->getSliceType()) == PROTO_TRUE;
}

struct Slice { long long start, stop, step, count; };

// PySlice_Unpack + PySlice_AdjustIndices.
bool sliceIndices(proto::ProtoContext* ctx, const PO* key, long long n, Slice& s) {
    auto part = [&](const char* name, bool& given, long long& v) -> bool {
        const PO* p = key->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, name));
        given = p && p != PROTO_NONE;
        if (!given) return true;
        return asIndex(ctx, p, v, "slice indices must be integers or None or have an __index__ method");
    };
    bool hasStart = false, hasStop = false, hasStep = false;
    long long start = 0, stop = 0, step = 1;
    if (!part("step", hasStep, step) || !part("start", hasStart, start) || !part("stop", hasStop, stop)) return false;
    if (!hasStep) step = 1;
    if (step == 0) {
        raiseValue(ctx, "slice step cannot be zero");
        return false;
    }
    if (!hasStart) start = step < 0 ? n - 1 : 0;
    else if (start < 0) { start += n; if (start < 0) start = step < 0 ? -1 : 0; }
    else if (start >= n) start = step < 0 ? n - 1 : n;
    if (!hasStop) stop = step < 0 ? -1 : n;
    else if (stop < 0) { stop += n; if (stop < 0) stop = step < 0 ? -1 : 0; }
    else if (stop >= n) stop = step < 0 ? n - 1 : n;
    long long count = 0;
    if (step < 0) { if (stop < start) count = (start - stop - 1) / (-step) + 1; }
    else if (start < stop) count = (stop - start - 1) / step + 1;
    s = {start, stop, step, count};
    return true;
}

// The bytes of an iterable of ints; false after raising.
bool gatherInts(proto::ProtoContext* ctx, const PO* iterable, std::string& out) {
    PythonEnvironment* env = envOf(ctx);
    if (!env) return false;
    const PO* it = env->iter(iterable);
    if (!it) return false;
    PythonEnvironment::TransientPin pinIt(env, it);
    for (;;) {
        const PO* item = env->next(it);
        if (!item) break;
        unsigned char b = 0;
        if (!asByte(ctx, item, b)) return false;
        out += static_cast<char>(b);
    }
    return true;
}

// The content of `src` for extend / slice assignment / the constructor:
// a bytes-like object's bytes, or an iterable of ints. A str or an int is
// rejected with `what` (CPython's message differs per operation).
bool sourceBytes(proto::ProtoContext* ctx, const PO* src, std::string& out, const std::string& strMessage) {
    if (PythonEnvironment::bytesLikeContent(ctx, src, out)) return true;
    if (src && src->isString(ctx)) {
        raiseType(ctx, strMessage);
        return false;
    }
    PythonEnvironment* env = envOf(ctx);
    if (env && isCellObject(src) && !env->getAttribute(ctx, src, env->getIterString(), false)) {
        // Not iterable at all.
        raiseType(ctx, "can't extend bytearray with " + typeName(ctx, src));
        return false;
    }
    return gatherInts(ctx, src, out);
}

const PO* none() { return PROTO_NONE; }

// --- methods -----------------------------------------------------------------

#define BA_METHOD(name)                                                                                 \
    const PO* name(proto::ProtoContext* ctx, const PO* self, const proto::ParentLink* link,            \
                   const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs)

#define BA_RECEIVER(method)                                                 \
    (void)link; (void)kwargs;                                               \
    unsigned shift = 0;                                                     \
    const PO* ba = receiver(ctx, self, args, shift, method);                \
    if (!ba) return nullptr;

BA_METHOD(ba_len) {
    BA_RECEIVER("__len__")
    return ctx->fromInteger(static_cast<long long>(storeSize(ctx, stateOf(ctx, ba))));
}

BA_METHOD(ba_getitem) {
    BA_RECEIVER("__getitem__")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "expected 1 argument");
        return nullptr;
    }
    const PO* key = arg(ctx, args, shift, 0);
    const PL* st = stateOf(ctx, ba);
    const long long n = static_cast<long long>(storeSize(ctx, st));
    if (isSlice(ctx, key)) {
        Slice s{};
        if (!sliceIndices(ctx, key, n, s)) return nullptr;
        std::string out;
        if (s.step == 1) {
            storeRead(ctx, st, out, static_cast<size_t>(s.start), static_cast<size_t>(s.start + s.count));
        } else {
            for (long long i = 0, at = s.start; i < s.count; ++i, at += s.step)
                out += static_cast<char>(storeAt(ctx, st, static_cast<size_t>(at)));
        }
        return PythonEnvironment::newBytearray(ctx, out.data(), out.size());
    }
    long long i = 0;
    if (!asIndex(ctx, key, i, ("bytearray indices must be integers or slices, not " + typeName(ctx, key)).c_str()))
        return nullptr;
    if (i < 0) i += n;
    if (i < 0 || i >= n) {
        raiseIndex(ctx, "bytearray index out of range");
        return nullptr;
    }
    return ctx->fromInteger(storeAt(ctx, st, static_cast<size_t>(i)));
}

BA_METHOD(ba_setitem) {
    BA_RECEIVER("__setitem__")
    if (argc(ctx, args, shift) != 2) {
        raiseType(ctx, "expected 2 arguments");
        return nullptr;
    }
    const PO* key = arg(ctx, args, shift, 0);
    const PO* value = arg(ctx, args, shift, 1);
    if (!isSlice(ctx, key)) {
        long long i = 0;
        if (!asIndex(ctx, key, i, ("bytearray indices must be integers or slices, not " + typeName(ctx, key)).c_str()))
            return nullptr;
        unsigned char b = 0;
        if (!asByte(ctx, value, b)) return nullptr;
        const bool ok = mutate(ctx, ba, [&](const PL* cur) -> const PL* {
            const long long n = static_cast<long long>(storeSize(ctx, cur));
            long long at = i < 0 ? i + n : i;
            if (at < 0 || at >= n) {
                raiseIndex(ctx, "bytearray index out of range");
                return nullptr;
            }
            return storeSetByte(ctx, cur, static_cast<size_t>(at), b);
        });
        return ok ? none() : nullptr;
    }
    // A slice: the new bytes are read once, before the publish loop.
    std::string repl;
    if (value == nullptr || value == PROTO_NONE || (value && value->isInteger(ctx))) {
        raiseType(ctx, "can assign only bytes, buffers, or iterables of ints in range(0, 256)");
        return nullptr;
    }
    if (!sourceBytes(ctx, value, repl, "can assign only bytes, buffers, or iterables of ints in range(0, 256)"))
        return nullptr;
    const bool ok = rewrite(ctx, ba, [&](const std::string& in, std::string& out) {
        Slice s{};
        if (!sliceIndices(ctx, key, static_cast<long long>(in.size()), s)) return false;
        if (s.step == 1) {
            const size_t a = static_cast<size_t>(s.start);
            const size_t b = static_cast<size_t>(std::max(s.start, s.stop));
            out.assign(in, 0, a);
            out += repl;
            out.append(in, b, std::string::npos);
            return true;
        }
        if (static_cast<long long>(repl.size()) != s.count) {
            raiseValue(ctx, "attempt to assign bytes of size " + std::to_string(repl.size())
                            + " to extended slice of size " + std::to_string(s.count));
            return false;
        }
        out = in;
        for (long long k = 0, at = s.start; k < s.count; ++k, at += s.step) out[static_cast<size_t>(at)] = repl[static_cast<size_t>(k)];
        return true;
    });
    return ok ? none() : nullptr;
}

BA_METHOD(ba_delitem) {
    BA_RECEIVER("__delitem__")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "expected 1 argument");
        return nullptr;
    }
    const PO* key = arg(ctx, args, shift, 0);
    const bool slice = isSlice(ctx, key);
    long long i = 0;
    if (!slice && !asIndex(ctx, key, i, ("bytearray indices must be integers or slices, not " + typeName(ctx, key)).c_str()))
        return nullptr;
    const bool ok = rewrite(ctx, ba, [&](const std::string& in, std::string& out) {
        const long long n = static_cast<long long>(in.size());
        if (!slice) {
            long long at = i < 0 ? i + n : i;
            if (at < 0 || at >= n) {
                raiseIndex(ctx, "bytearray index out of range");
                return false;
            }
            out = in;
            out.erase(static_cast<size_t>(at), 1);
            return true;
        }
        Slice s{};
        if (!sliceIndices(ctx, key, n, s)) return false;
        std::vector<bool> drop(in.size(), false);
        for (long long k = 0, at = s.start; k < s.count; ++k, at += s.step) drop[static_cast<size_t>(at)] = true;
        out.reserve(in.size() - static_cast<size_t>(s.count));
        for (size_t k = 0; k < in.size(); ++k) if (!drop[k]) out += in[k];
        return true;
    });
    return ok ? none() : nullptr;
}

BA_METHOD(ba_append) {
    BA_RECEIVER("append")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "bytearray.append() takes exactly one argument (" + std::to_string(argc(ctx, args, shift)) + " given)");
        return nullptr;
    }
    unsigned char b = 0;
    if (!asByte(ctx, arg(ctx, args, shift, 0), b)) return nullptr;
    const char c = static_cast<char>(b);
    return mutate(ctx, ba, [&](const PL* cur) { return storeAppend(ctx, cur, &c, 1); }) ? none() : nullptr;
}

BA_METHOD(ba_extend) {
    BA_RECEIVER("extend")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "bytearray.extend() takes exactly one argument (" + std::to_string(argc(ctx, args, shift)) + " given)");
        return nullptr;
    }
    std::string add;
    const PO* src = arg(ctx, args, shift, 0);
    if (src && src->isString(ctx)) {
        raiseType(ctx, "expected iterable of integers; got: 'str'");
        return nullptr;
    }
    if (!sourceBytes(ctx, src, add, "expected iterable of integers; got: 'str'")) return nullptr;
    return mutate(ctx, ba, [&](const PL* cur) { return storeAppend(ctx, cur, add.data(), add.size()); }) ? none() : nullptr;
}

BA_METHOD(ba_iadd) {
    BA_RECEIVER("__iadd__")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "expected 1 argument");
        return nullptr;
    }
    const PO* other = arg(ctx, args, shift, 0);
    // CPython holds the operand's buffer while it resizes: a bytearray
    // extended by itself, or by a view of itself, has an export then.
    if (other == ba || (isMemoryview(ctx, other) && memoryviewExporter(ctx, other) == ba)) {
        raiseBufferError(ctx, "Existing exports of data: object cannot be re-sized");
        return nullptr;
    }
    std::string add;
    if (!PythonEnvironment::bytesLikeContent(ctx, other, add)) {
        raiseType(ctx, "can't concat " + typeName(ctx, other) + " to bytearray");
        return nullptr;
    }
    if (!mutate(ctx, ba, [&](const PL* cur) { return storeAppend(ctx, cur, add.data(), add.size()); })) return nullptr;
    return ba;
}

BA_METHOD(ba_add) {
    BA_RECEIVER("__add__")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "expected 1 argument");
        return nullptr;
    }
    const PO* other = arg(ctx, args, shift, 0);
    std::string out;
    storeRead(ctx, stateOf(ctx, ba), out);
    std::string add;
    if (!PythonEnvironment::bytesLikeContent(ctx, other, add)) {
        raiseType(ctx, "can't concat " + typeName(ctx, other) + " to bytearray");
        return nullptr;
    }
    out += add;
    return PythonEnvironment::newBytearray(ctx, out.data(), out.size());
}

// The repeat count of `*` and `*=`; false after raising.
bool repeatCount(proto::ProtoContext* ctx, const PO* v, long long& n) {
    if (!(v == PROTO_TRUE || v == PROTO_FALSE || (v && v->isInteger(ctx)))) {
        PythonEnvironment* env = envOf(ctx);
        const PO* m = (env && isCellObject(v)) ? env->getAttribute(ctx, v, PythonEnvironment::getInternedString(ctx, "__index__"), false) : nullptr;
        if (!m || m == PROTO_NONE) {
            raiseType(ctx, "can't multiply sequence by non-int of type '" + typeName(ctx, v) + "'");
            return false;
        }
    }
    if (!asIndex(ctx, v, n)) return false;
    if (n < 0) n = 0;
    return true;
}

BA_METHOD(ba_mul) {
    BA_RECEIVER("__mul__")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "expected 1 argument");
        return nullptr;
    }
    long long times = 0;
    if (!repeatCount(ctx, arg(ctx, args, shift, 0), times)) return nullptr;
    std::string in;
    storeRead(ctx, stateOf(ctx, ba), in);
    if (!in.empty() && static_cast<unsigned long long>(times) > (1ULL << 40) / in.size()) {
        if (PythonEnvironment* env = envOf(ctx)) env->raiseOverflowError(ctx, "repeated bytes are too long");
        return nullptr;
    }
    std::string out;
    out.reserve(in.size() * static_cast<size_t>(times));
    for (long long k = 0; k < times; ++k) out += in;
    return PythonEnvironment::newBytearray(ctx, out.data(), out.size());
}

BA_METHOD(ba_imul) {
    BA_RECEIVER("__imul__")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "expected 1 argument");
        return nullptr;
    }
    long long times = 0;
    if (!repeatCount(ctx, arg(ctx, args, shift, 0), times)) return nullptr;
    const bool ok = rewrite(ctx, ba, [&](const std::string& in, std::string& out) {
        if (!in.empty() && static_cast<unsigned long long>(times) > (1ULL << 40) / in.size()) {
            if (PythonEnvironment* env = envOf(ctx)) env->raiseOverflowError(ctx, "repeated bytes are too long");
            return false;
        }
        out.reserve(in.size() * static_cast<size_t>(times));
        for (long long k = 0; k < times; ++k) out += in;
        return true;
    });
    return ok ? ba : nullptr;
}

BA_METHOD(ba_insert) {
    BA_RECEIVER("insert")
    if (argc(ctx, args, shift) != 2) {
        raiseType(ctx, "insert expected 2 arguments, got " + std::to_string(argc(ctx, args, shift)));
        return nullptr;
    }
    long long i = 0;
    if (!asIndex(ctx, arg(ctx, args, shift, 0), i)) return nullptr;
    unsigned char b = 0;
    if (!asByte(ctx, arg(ctx, args, shift, 1), b)) return nullptr;
    const bool ok = rewrite(ctx, ba, [&](const std::string& in, std::string& out) {
        const long long n = static_cast<long long>(in.size());
        long long at = i < 0 ? i + n : i;
        if (at < 0) at = 0;
        if (at > n) at = n;
        out = in;
        out.insert(out.begin() + at, static_cast<char>(b));
        return true;
    });
    return ok ? none() : nullptr;
}

BA_METHOD(ba_pop) {
    BA_RECEIVER("pop")
    const size_t given = argc(ctx, args, shift);
    if (given > 1) {
        raiseType(ctx, "pop expected at most 1 argument, got " + std::to_string(given));
        return nullptr;
    }
    long long i = -1;
    if (given == 1 && !asIndex(ctx, arg(ctx, args, shift, 0), i)) return nullptr;
    unsigned char popped = 0;
    const bool ok = mutate(ctx, ba, [&](const PL* cur) -> const PL* {
        const long long n = static_cast<long long>(storeSize(ctx, cur));
        if (n == 0) {
            raiseIndex(ctx, "pop from empty bytearray");
            return nullptr;
        }
        long long at = i < 0 ? i + n : i;
        if (at < 0 || at >= n) {
            raiseIndex(ctx, "pop index out of range");
            return nullptr;
        }
        popped = storeAt(ctx, cur, static_cast<size_t>(at));
        if (at == n - 1) return storePopLast(ctx, cur);
        std::string in;
        storeRead(ctx, cur, in);
        in.erase(static_cast<size_t>(at), 1);
        return storeFrom(ctx, cur->getAt(ctx, 0), in.data(), in.size());
    });
    return ok ? ctx->fromInteger(popped) : nullptr;
}

BA_METHOD(ba_remove) {
    BA_RECEIVER("remove")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "bytearray.remove() takes exactly one argument (" + std::to_string(argc(ctx, args, shift)) + " given)");
        return nullptr;
    }
    unsigned char b = 0;
    if (!asByte(ctx, arg(ctx, args, shift, 0), b)) return nullptr;
    const bool ok = rewrite(ctx, ba, [&](const std::string& in, std::string& out) {
        const size_t at = in.find(static_cast<char>(b));
        if (at == std::string::npos) {
            raiseValue(ctx, "value not found in bytearray");
            return false;
        }
        out = in;
        out.erase(at, 1);
        return true;
    });
    return ok ? none() : nullptr;
}

BA_METHOD(ba_clear) {
    BA_RECEIVER("clear")
    return mutate(ctx, ba, [&](const PL* cur) { return ctx->newList()->appendLast(ctx, cur->getAt(ctx, 0)); }) ? none() : nullptr;
}

BA_METHOD(ba_reverse) {
    BA_RECEIVER("reverse")
    const bool ok = rewrite(ctx, ba, [&](const std::string& in, std::string& out) {
        out.assign(in.rbegin(), in.rend());
        return true;
    });
    return ok ? none() : nullptr;
}

BA_METHOD(ba_copy) {
    BA_RECEIVER("copy")
    std::string in;
    storeRead(ctx, stateOf(ctx, ba), in);
    return PythonEnvironment::newBytearray(ctx, in.data(), in.size());
}

BA_METHOD(ba_resize) {
    BA_RECEIVER("resize")
    if (argc(ctx, args, shift) != 1) {
        raiseType(ctx, "bytearray.resize() takes exactly one argument (" + std::to_string(argc(ctx, args, shift)) + " given)");
        return nullptr;
    }
    long long n = 0;
    if (!asIndex(ctx, arg(ctx, args, shift, 0), n)) return nullptr;
    if (n < 0) {
        raiseValue(ctx, "Can only resize to positive sizes, got " + std::to_string(n));
        return nullptr;
    }
    const bool ok = rewrite(ctx, ba, [&](const std::string& in, std::string& out) {
        out = in;
        out.resize(static_cast<size_t>(n), '\0');
        return true;
    });
    return ok ? none() : nullptr;
}

BA_METHOD(ba_alloc) {
    BA_RECEIVER("__alloc__")
    return ctx->fromInteger(static_cast<long long>(storeSize(ctx, stateOf(ctx, ba))) + 1);
}

BA_METHOD(ba_sizeof) {
    BA_RECEIVER("__sizeof__")
    return ctx->fromInteger(56 + static_cast<long long>(storeSize(ctx, stateOf(ctx, ba))) + 1);
}

BA_METHOD(ba_repr) {
    BA_RECEIVER("__repr__")
    std::string in;
    storeRead(ctx, stateOf(ctx, ba), in);
    // CPython's bytearray_repr: double quotes when the content has a single
    // quote and no double quote; a single quote is escaped either way.
    const char quote = (in.find('\'') != std::string::npos && in.find('"') == std::string::npos) ? '"' : '\'';
    std::string literal = "b";
    literal += quote;
    for (unsigned char c : in) {
        if (c == '\'' || c == '\\') { literal += '\\'; literal += static_cast<char>(c); }
        else if (c == '\t') literal += "\\t";
        else if (c == '\n') literal += "\\n";
        else if (c == '\r') literal += "\\r";
        else if (c < 0x20 || c >= 0x7f) {
            char hex[5];
            std::snprintf(hex, sizeof hex, "\\x%02x", c);
            literal += hex;
        } else literal += static_cast<char>(c);
    }
    literal += quote;
    return PythonEnvironment::newStr(ctx, typeName(ctx, ba) + "(" + literal + ")");
}

// __reduce_ex__(protocol), as CPython's bytearray_reduce_ex: below protocol
// 3 the content travels as a latin-1 str (picklable by every protocol),
// from 3 on as bytes; the state is the instance __dict__ of a subclass, or
// None.
const PO* reduceWith(proto::ProtoContext* ctx, const PO* ba, long long protocol) {
    PythonEnvironment* env = envOf(ctx);
    if (!env) return PROTO_NONE;
    std::string in;
    storeRead(ctx, stateOf(ctx, ba), in);
    const PO* cls = env->getType(ctx, ba);
    const PL* ctorArgs = ctx->newList();
    if (protocol < 3) {
        std::string latin1;
        for (unsigned char c : in) {
            if (c < 0x80) latin1 += static_cast<char>(c);
            else { latin1 += static_cast<char>(0xC0 | (c >> 6)); latin1 += static_cast<char>(0x80 | (c & 0x3F)); }
        }
        ctorArgs = ctorArgs->appendLast(ctx, PythonEnvironment::newStr(ctx, latin1))
                           ->appendLast(ctx, PythonEnvironment::getInternedString(ctx, "latin-1")->asObject(ctx));
    } else {
        ctorArgs = ctorArgs->appendLast(ctx, PythonEnvironment::newBytes(ctx, in.data(), in.size()));
    }
    const PO* state = PROTO_NONE;
    if (cls != bytearrayType(ctx)) {
        const PO* d = env->getAttribute(ctx, ba, PythonEnvironment::getInternedString(ctx, "__dict__"), false);
        if (d && d != PROTO_NONE) {
            const PO* len = env->callMethod(d, "__len__", {});
            if (len && len->isInteger(ctx) && len->asLong(ctx) > 0) state = d;
        }
    }
    const PL* out = ctx->newList()->appendLast(ctx, cls)
                       ->appendLast(ctx, ctx->newTupleFromList(ctorArgs)->asObject(ctx))
                       ->appendLast(ctx, state);
    return ctx->newTupleFromList(out)->asObject(ctx);
}

BA_METHOD(ba_reduce_ex) {
    BA_RECEIVER("__reduce_ex__")
    long long protocol = 0;
    if (argc(ctx, args, shift) >= 1 && !asIndex(ctx, arg(ctx, args, shift, 0), protocol)) return nullptr;
    return reduceWith(ctx, ba, protocol);
}

BA_METHOD(ba_reduce) {
    BA_RECEIVER("__reduce__")
    return reduceWith(ctx, ba, 0);
}

BA_METHOD(ba_buffer) {
    BA_RECEIVER("__buffer__")
    PythonEnvironment* env = envOf(ctx);
    const PO* mv = env ? env->getBuiltins()->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "memoryview")) : nullptr;
    return mv ? env->callObject(mv, {ba}) : PROTO_NONE;
}

BA_METHOD(ba_release_buffer) {
    BA_RECEIVER("__release_buffer__")
    if (argc(ctx, args, shift) == 1 && isMemoryview(ctx, arg(ctx, args, shift, 0))) {
        PythonEnvironment* env = envOf(ctx);
        if (env && !env->callMethod(arg(ctx, args, shift, 0), "release", {})) return nullptr;
    }
    return none();
}

// bytearray.fromhex(string): bytes.fromhex's bytes as a bytearray, or, for a
// subclass, cls(that bytearray), as CPython does.
BA_METHOD(ba_fromhex) {
    (void)link;
    PythonEnvironment* env = envOf(ctx);
    if (!env) return PROTO_NONE;
    const PO* cls = bytearrayType(ctx);
    unsigned first = 0;
    if (args && args->getSize(ctx) >= 1 && env->isActuallyAClass(ctx, args->getAt(ctx, 0))) {
        cls = args->getAt(ctx, 0);
        first = 1;
    } else if (self && self != PROTO_NONE && env->isActuallyAClass(ctx, self)) {
        cls = self;
    }
    if (argc(ctx, args, first) != 1) {
        raiseType(ctx, "fromhex() takes exactly one argument (" + std::to_string(argc(ctx, args, first)) + " given)");
        return nullptr;
    }
    // bytes' native fromhex (bytes.fromhex itself is a classmethod).
    const PO* fromhex = env->getBytesPrototype()->getOwnAttributeDirect(ctx,
        PythonEnvironment::getInternalString(ctx, "\001__native_fromhex__"));
    if (!fromhex) fromhex = env->getBytesPrototype()->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "fromhex"));
    if (!fromhex || !fromhex->asMethod(ctx)) return PROTO_NONE;
    const PL* fargs = ctx->newList()->appendLast(ctx, env->getBytesPrototype())->appendLast(ctx, arg(ctx, args, first, 0));
    const PO* bytesResult = fromhex->asMethod(ctx)(ctx, env->getBytesPrototype(), nullptr,
                                                    argsView(proto::ProtoListArguments(ctx, fargs)), kwargs);
    if (!bytesResult) return nullptr;
    std::string out;
    if (!PythonEnvironment::bytesLikeContent(ctx, bytesResult, out)) return bytesResult;
    const PO* result = PythonEnvironment::newBytearray(ctx, out.data(), out.size());
    if (cls == bytearrayType(ctx)) return result;
    return env->callObject(cls, {result});
}

// --- iterator -----------------------------------------------------------------

BA_METHOD(ba_iter) {
    BA_RECEIVER("__iter__")
    PythonEnvironment* env = envOf(ctx);
    const PO* itCls = env ? env->getBytearrayPrototype()->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "\001__bytearray_iterator__")) : nullptr;
    if (!itCls || itCls == PROTO_NONE) return PROTO_NONE;
    const PO* it = itCls->newChild(ctx, true);
    it->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "\001__it_seq__"), ba);
    it->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "\001__it_index__"), ctx->fromInteger(0));
    return it;
}

// Reads the bytearray at the iterator's index, as CPython's iterator does:
// it sees appends made while iterating and stops at the current end.
const PO* ba_iter_next(proto::ProtoContext* ctx, const PO* self, const proto::ParentLink*,
                       const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    const proto::ProtoString* idxName = PythonEnvironment::getInternalString(ctx, "\001__it_index__");
    const PO* seq = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "\001__it_seq__"));
    const PL* st = stateOf(ctx, seq);
    if (!st) return nullptr;
    for (;;) {
        const PO* idxObj = self->getOwnAttributeDirect(ctx, idxName);
        if (!idxObj || !idxObj->isInteger(ctx)) return nullptr;
        const long long idx = idxObj->asLong(ctx);
        st = stateOf(ctx, seq);
        if (idx < 0 || static_cast<size_t>(idx) >= storeSize(ctx, st)) return nullptr;
        const unsigned char b = storeAt(ctx, st, static_cast<size_t>(idx));
        if (self->setAttributeIfEqual(ctx, idxName, idxObj, ctx->fromInteger(idx + 1))) return ctx->fromInteger(b);
    }
}

const PO* ba_iter_self(proto::ProtoContext*, const PO* self, const proto::ParentLink*,
                       const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    return self;
}

// --- bytes' methods on bytearray ---------------------------------------------
//
// Each borrowed bytes method runs on a bytes snapshot of the receiver; a
// bytes result becomes a bytearray, as do the bytes in a list or tuple
// result (split, partition). The originals are C functions, the same in
// every environment, kept in a process-wide table that the wrappers index.

constexpr int kBorrowedMax = 128;
std::atomic<proto::ProtoMethod> g_borrowed[kBorrowedMax];
std::atomic<const char*> g_borrowedName[kBorrowedMax];
std::atomic<int> g_borrowedCount{0};

const PO* fromBytesResult(proto::ProtoContext* ctx, const PO* r) {
    PythonEnvironment* env = envOf(ctx);
    if (!env || !isCellObject(r)) return r;
    const PO* type = env->getType(ctx, r);
    auto isBytes = [&](const PO* o) { return isCellObject(o) && env->getType(ctx, o) == env->getBytesPrototype(); };
    auto convert = [&](const PO* o) -> const PO* {
        std::string buf;
        if (!PythonEnvironment::bytesLikeContent(ctx, o, buf)) return o;
        return PythonEnvironment::newBytearray(ctx, buf.data(), buf.size());
    };
    if (type == env->getBytesPrototype()) return convert(r);
    if (type != env->getListPrototype() && type != env->getTuplePrototype()) return r;
    const PL* items = nullptr;
    if (type == env->getTuplePrototype()) {
        TupleView t = PythonEnvironment::tupleView(ctx, r);
        if (t) items = t->asList(ctx);
    } else {
        items = listStorage(ctx, r);
        if (!items) {
            const PO* d = r->getAttribute(ctx, env->getDataString());
            items = d ? listStorage(ctx, d) : nullptr;
        }
    }
    if (!items) return r;
    bool any = false;
    for (size_t i = 0; i < items->getSize(ctx) && !any; ++i) any = isBytes(items->getAt(ctx, static_cast<int>(i)));
    if (!any) return r;
    PythonEnvironment::TransientPin pinR(env, r);
    const PL* out = ctx->newList();
    for (size_t i = 0; i < items->getSize(ctx); ++i) {
        PythonEnvironment::TransientPin pinOut(env, out->asObject(ctx));
        const PO* o = items->getAt(ctx, static_cast<int>(i));
        out = out->appendLast(ctx, isBytes(o) ? convert(o) : o);
    }
    if (type == env->getTuplePrototype()) return ctx->newTupleFromList(out)->asObject(ctx);
    return PythonEnvironment::wrapList(ctx, out);
}

template <int I>
const PO* borrowed(proto::ProtoContext* ctx, const PO* self, const proto::ParentLink* link,
                   const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs) {
    const proto::ProtoMethod original = g_borrowed[I].load(std::memory_order_acquire);
    const char* name = g_borrowedName[I].load(std::memory_order_acquire);
    if (!original) return PROTO_NONE;
    unsigned shift = 0;
    const PO* ba = receiver(ctx, self, args, shift, name ? name : "method");
    if (!ba) return nullptr;
    std::string content;
    storeRead(ctx, stateOf(ctx, ba), content);
    const PO* snapshot = PythonEnvironment::newBytes(ctx, content.data(), content.size());
    PythonEnvironment::TransientPin pinSnapshot(envOf(ctx), snapshot);
    const PL* rest = ctx->newList();
    const size_t n = args ? args->getSize(ctx) : 0;
    for (size_t i = shift; i < n; ++i) rest = rest->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
    const PO* r = original(ctx, snapshot, link, argsView(proto::ProtoListArguments(ctx, rest)), kwargs);
    return r ? fromBytesResult(ctx, r) : r;
}

template <int... I>
constexpr std::array<proto::ProtoMethod, sizeof...(I)> borrowedTable(std::integer_sequence<int, I...>) {
    return {{&borrowed<I>...}};
}
constexpr auto kBorrowed = borrowedTable(std::make_integer_sequence<int, kBorrowedMax>{});

// The wrapper for bytes method `m` named `name`: the same wrapper for the
// same method in every environment; nullptr when the table is full.
proto::ProtoMethod wrapBorrowed(proto::ProtoMethod m, const char* name) {
    const int n = std::min(g_borrowedCount.load(std::memory_order_acquire), kBorrowedMax);
    for (int i = 0; i < n; ++i)
        if (g_borrowed[i].load(std::memory_order_acquire) == m) return kBorrowed[i];
    const int i = g_borrowedCount.fetch_add(1, std::memory_order_acq_rel);
    if (i >= kBorrowedMax) return nullptr;
    g_borrowedName[i].store(name, std::memory_order_release);
    g_borrowed[i].store(m, std::memory_order_release);
    return kBorrowed[i];
}

// --- construction -------------------------------------------------------------

// The content bytearray(source, encoding, errors) describes; args[first..]
// are the three, an absent one None. False with an exception raised.
bool initialContent(proto::ProtoContext* ctx, const proto::ProtoArguments* args, unsigned first, std::string& out) {
    PythonEnvironment* env = envOf(ctx);
    auto at = [&](unsigned i) -> const PO* {
        if (!args || args->getSize(ctx) <= first + i) return nullptr;
        const PO* v = args->getAt(ctx, static_cast<int>(first + i));
        return v == PROTO_NONE ? nullptr : v;
    };
    const PO* src = at(0);
    const PO* encoding = at(1);
    const PO* errors = at(2);
    if (src && src->isString(ctx)) {
        if (!encoding) {
            raiseType(ctx, "string argument without an encoding");
            return false;
        }
        if (!env) return false;
        std::vector<const PO*> eargs{encoding};
        if (errors) eargs.push_back(errors);
        const PO* encoded = env->callMethod(src, "encode", eargs);
        if (!encoded) return false;
        return PythonEnvironment::bytesLikeContent(ctx, encoded, out);
    }
    if (encoding || errors) {
        raiseType(ctx, encoding ? "encoding without a string argument" : "errors without a string argument");
        return false;
    }
    if (!src) return true;
    if (PythonEnvironment::bytesLikeContent(ctx, src, out)) return true;
    // An int (or an object with __index__) makes a zero-filled buffer.
    const bool indexLike = src == PROTO_TRUE || src == PROTO_FALSE || src->isInteger(ctx)
        || (env && isCellObject(src) && env->getAttribute(ctx, src, PythonEnvironment::getInternedString(ctx, "__index__"), false));
    if (indexLike) {
        long long n = 0;
        if (!asIndex(ctx, src, n)) return false;
        if (n < 0) {
            raiseValue(ctx, "negative count");
            return false;
        }
        out.assign(static_cast<size_t>(n), '\0');
        return true;
    }
    if (env && isCellObject(src) && !env->getAttribute(ctx, src, env->getIterString(), false)
        && !env->getAttribute(ctx, src, PythonEnvironment::getInternedString(ctx, "__getitem__"), false)) {
        raiseType(ctx, "cannot convert '" + typeName(ctx, src) + "' object to bytearray");
        return false;
    }
    if (env && !isCellObject(src)) {
        raiseType(ctx, "cannot convert '" + typeName(ctx, src) + "' object to bytearray");
        return false;
    }
    return gatherInts(ctx, src, out);
}

// bytearray.__new__(cls, ...): a new, empty instance of cls; __init__ fills
// it, as in CPython.
const PO* ba_new_impl(proto::ProtoContext* ctx, const PO* self, const proto::ParentLink*,
                      const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    PythonEnvironment* env = envOf(ctx);
    const PO* cls = args && args->getSize(ctx) >= 1 ? args->getAt(ctx, 0) : self;
    if (!cls || !env || !env->isActuallyAClass(ctx, cls)) cls = bytearrayType(ctx);
    if (!cls) return PROTO_NONE;
    return makeInstance(ctx, cls, "", 0);
}

// bytearray.__init__(self, source=None, encoding=None, errors=None)
// replaces the content; withSignature<> moved keywords to their positions.
const PO* ba_init_impl(proto::ProtoContext* ctx, const PO* self, const proto::ParentLink*,
                       const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    unsigned shift = 0;
    const PO* ba = receiver(ctx, self, args, shift, "__init__");
    if (!ba) return nullptr;
    std::string content;
    if (!initialContent(ctx, args, shift, content)) return nullptr;
    const bool ok = mutate(ctx, ba, [&](const PL* cur) { return storeFrom(ctx, cur->getAt(ctx, 0), content.data(), content.size()); });
    return ok ? none() : nullptr;
}

const BuiltinSignature kSigNew{"bytearray", 1, {{"source", BuiltinParam::None, 0}, {"encoding", BuiltinParam::None, 0}, {"errors", BuiltinParam::None, 0}}, {}, true};
const BuiltinSignature kSigInit{"bytearray", 1, {{"source", BuiltinParam::None, 0}, {"encoding", BuiltinParam::None, 0}, {"errors", BuiltinParam::None, 0}}, {}};

} // namespace

// --- public helpers (PythonEnvironment) ------------------------------------

std::string PythonEnvironment::typeNameOf(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    return typeName(ctx, obj);
}

bool PythonEnvironment::isBytearray(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    return stateOf(ctx, obj) != nullptr;
}

bool PythonEnvironment::bytearrayContent(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::string& out) {
    const PL* st = stateOf(ctx, obj);
    if (!st) return false;
    out.clear();
    storeRead(ctx, st, out);
    return true;
}

bool PythonEnvironment::bytesLikeContent(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::string& out) {
    if (!obj || obj == PROTO_NONE) return false;
    if (obj->isByteBuffer(ctx)) {
        const proto::ProtoByteBuffer* bb = obj->asByteBuffer(ctx);
        out.assign(bb->getBuffer(ctx), bb->getSize(ctx));
        return true;
    }
    if (!isCellObject(obj) || obj->isString(ctx)) return false;
    if (bytearrayContent(ctx, obj, out)) return true;
    // A memoryview: the bytes it views, read from one snapshot.
    if (memoryviewContent(ctx, obj, out)) return true;
    PythonEnvironment* menv = fromContext(ctx);
    const PO* d = menv ? obj->getAttribute(ctx, menv->getDataString()) : nullptr;
    if (d && d != PROTO_NONE && d != obj) {
        if (d->isByteBuffer(ctx)) {
            const proto::ProtoByteBuffer* bb = d->asByteBuffer(ctx);
            out.assign(bb->getBuffer(ctx), bb->getSize(ctx));
            return true;
        }
        // A bytes object whose payload is a legacy ProtoString.
        PythonEnvironment* env = fromContext(ctx);
        if (d->isString(ctx) && env && env->getBytesPrototype()
            && obj->isInstanceOf(ctx, env->getBytesPrototype()) == PROTO_TRUE) {
            out.clear();
            d->asString(ctx)->toUTF8String(ctx, out);
            return true;
        }
    }
    return false;
}

bool PythonEnvironment::isBytesLike(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    std::string ignored;
    return bytesLikeContent(ctx, obj, ignored);
}

const proto::ProtoObject* PythonEnvironment::newBytearray(proto::ProtoContext* ctx, const char* data, size_t n) {
    const PO* cls = bytearrayType(ctx);
    return cls ? makeInstance(ctx, cls, data, n) : PROTO_NONE;
}

bool PythonEnvironment::bytearrayWrite(proto::ProtoContext* ctx, const proto::ProtoObject* obj, size_t at,
                                       const std::string& data) {
    // A memoryview writes into the bytearray it views.
    if (!stateOf(ctx, obj) && isMemoryview(ctx, obj)) return memoryviewWrite(ctx, obj, at, data);
    if (!stateOf(ctx, obj)) {
        raiseType(ctx, "a writable bytes-like object is required, not '" + typeName(ctx, obj) + "'");
        return false;
    }
    return mutate(ctx, obj, [&](const PL* cur) -> const PL* {
        if (at + data.size() > storeSize(ctx, cur)) {
            raiseIndex(ctx, "write outside the bytearray");
            return nullptr;
        }
        return data.empty() ? cur : storeOverwrite(ctx, cur, at, data);
    });
}

// --- buffer exports (MemoryviewType.cpp) ------------------------------------

namespace {

// Publishes a new export word computed from the current one, keeping the
// content; `change` returns false after raising.
template <class Change>
bool publishHead(proto::ProtoContext* ctx, const PO* ba, Change change, long long* epochOut = nullptr) {
    if (!stateOf(ctx, ba)) return false;
    const proto::ProtoString* name = storageName(ctx);
    for (;;) {
        const PO* raw = ba->getOwnAttributeDirect(ctx, name);
        const PL* cur = raw ? raw->asList(ctx) : nullptr;
        if (!cur) return false;
        const long long word = headWord(ctx, cur);
        long long next = word;
        if (!change(word, next)) return false;
        if (epochOut) *epochOut = next >> kEpochShift;
        if (next == word) return true;
        if (ba->setAttributeIfEqual(ctx, name, raw, cur->setAt(ctx, 0, ctx->fromInteger(next))->asObject(ctx))) return true;
    }
}

[[noreturn]] void raiseStaleView(proto::ProtoContext* ctx) {
    raiseBufferError(ctx, "memoryview: the underlying bytearray was resized after the view was made");
}

} // namespace

bool bytearrayWatch(proto::ProtoContext* ctx, const proto::ProtoObject* ba, long long& epoch) {
    return publishHead(ctx, ba, [](long long w, long long& n) { n = w | kWatched; return true; }, &epoch);
}

bool bytearrayHold(proto::ProtoContext* ctx, const proto::ProtoObject* ba, long long epoch, int delta) {
    return publishHead(ctx, ba, [&](long long w, long long& n) {
        if (delta > 0 && (w >> kEpochShift) != epoch) raiseStaleView(ctx);
        const long long count = (w & kExportMask) + delta;
        if (count < 0 || count > kExportMask) {
            raiseBufferError(ctx, "bytearray export count out of range");
            return false;
        }
        n = (w & ~kExportMask) | count;
        return true;
    });
}

bool bytearrayViewRead(proto::ProtoContext* ctx, const proto::ProtoObject* ba, long long epoch, std::string& out) {
    const PL* st = stateOf(ctx, ba);
    if (!st) return false;
    if ((headWord(ctx, st) >> kEpochShift) != epoch) raiseStaleView(ctx);
    out.clear();
    storeRead(ctx, st, out);
    return true;
}

bool bytearrayViewWrite(proto::ProtoContext* ctx, const proto::ProtoObject* ba, long long epoch,
                        const std::vector<std::pair<size_t, std::string>>& pieces) {
    return mutate(ctx, ba, [&](const PL* cur) -> const PL* {
        if ((headWord(ctx, cur) >> kEpochShift) != epoch) raiseStaleView(ctx);
        const size_t n = storeSize(ctx, cur);
        const PL* next = cur;
        for (const auto& p : pieces) {
            if (p.first + p.second.size() > n) {
                raiseIndex(ctx, "write outside the bytearray");
                return nullptr;
            }
            if (!p.second.empty()) next = storeOverwrite(ctx, next, p.first, p.second);
        }
        return next;
    });
}

// --- the type -------------------------------------------------------------------

const proto::ProtoObject* makeBytearrayType(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                            const proto::ProtoObject* typeProto,
                                            const proto::ProtoObject* bytesProto) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const PO* cls = ctx->newObject(true);
    if (objectProto) cls = cls->addParent(ctx, objectProto);
    // bytes' methods that bytearray does not define itself.
    static const char* const own[] = {
        "__new__", "__init__", "__hash__", "__repr__", "__str__", "__class__", "__name__", "__qualname__",
        "__mro__", "__bases__", "__base__", "__module__", "__doc__", "__dict__", "\001__data__", "__getnewargs__",
        "__bytes__", "\001__is_python_class__", "__len__", "__getitem__", "__setitem__", "__delitem__",
        "__iter__", "__add__", "__iadd__", "__imul__", "__reduce__", "__reduce_ex__", "fromhex",
        "maketrans", "__sizeof__", "__buffer__", "__release_buffer__"};
    struct Sink { const PO* cls; };
    Sink sink{cls};
    bytesProto->processOwnAttributes(ctx, &sink,
        [](proto::ProtoContext* c, void* userData, const proto::ProtoString* name, const proto::ProtoObject* value) {
            if (!name || !value) return;
            std::string n;
            name->toUTF8String(c, n);
            for (const char* k : own) if (n == k) return;
            Sink* s = static_cast<Sink*>(userData);
            if (const proto::ProtoMethod m = value->asMethod(c)) {
                // The name outlives the process: interned symbols are perennial.
                std::string* keep = new std::string(n);
                if (const proto::ProtoMethod w = wrapBorrowed(m, keep->c_str())) value = c->fromMethod(nullptr, w);
            }
            s->cls = s->cls->setAttribute(c, name, value);
        });
    cls = sink.cls;
    auto set = [&](const char* name, const PO* value) {
        cls = cls->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, name), value);
    };
    auto method = [&](const char* name, proto::ProtoMethod m) { set(name, ctx->fromMethod(nullptr, m)); };
    if (typeProto) set("__class__", typeProto);
    set("__name__", PythonEnvironment::getInternedString(ctx, "bytearray")->asObject(ctx));
    set("__qualname__", PythonEnvironment::getInternedString(ctx, "bytearray")->asObject(ctx));
    set("__module__", PythonEnvironment::getInternedString(ctx, "builtins")->asObject(ctx));
    set("__doc__", PythonEnvironment::newStr(ctx,
        "bytearray(iterable_of_ints) -> bytearray\n"
        "bytearray(string, encoding[, errors]) -> bytearray\n"
        "bytearray(bytes_or_buffer) -> mutable copy of bytes_or_buffer\n"
        "bytearray(int) -> bytes array of size given by the parameter initialized with null bytes\n"
        "bytearray() -> empty bytes array\n\n"
        "Construct a mutable bytearray object from:\n"
        "  - an iterable yielding integers in range(256)\n"
        "  - a text string encoded using the specified encoding\n"
        "  - a bytes or a buffer object\n"
        "  - any object implementing the buffer API.\n"
        "  - an integer"));
    method("__new__", withSignature<ba_new_impl, kSigNew>);
    method("__init__", withSignature<ba_init_impl, kSigInit>);
    set("__hash__", PROTO_NONE);
    method("__repr__", ba_repr);
    method("__str__", ba_repr);
    method("__len__", ba_len);
    method("__getitem__", ba_getitem);
    method("__setitem__", ba_setitem);
    method("__delitem__", ba_delitem);
    method("__iter__", ba_iter);
    method("__add__", ba_add);
    method("__iadd__", ba_iadd);
    method("__imul__", ba_imul);
    method("__mul__", ba_mul);
    method("__rmul__", ba_mul);
    method("append", ba_append);
    method("extend", ba_extend);
    method("insert", ba_insert);
    method("pop", ba_pop);
    method("remove", ba_remove);
    method("clear", ba_clear);
    method("reverse", ba_reverse);
    method("copy", ba_copy);
    method("resize", ba_resize);
    method("__alloc__", ba_alloc);
    method("__sizeof__", ba_sizeof);
    method("__reduce__", ba_reduce);
    method("__reduce_ex__", ba_reduce_ex);
    method("__buffer__", ba_buffer);
    method("__release_buffer__", ba_release_buffer);
    method("fromhex", ba_fromhex);
    // maketrans is bytes' own, returning bytes for bytearray too.
    const PO* maketrans = bytesProto->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "maketrans"));
    if (maketrans && maketrans != PROTO_NONE) set("maketrans", maketrans);
    if (objectProto) {
        set("__mro__", ctx->newTupleFromList(ctx->newList()->appendLast(ctx, cls)->appendLast(ctx, objectProto))->asObject(ctx));
        set("__bases__", ctx->newTupleFromList(ctx->newList()->appendLast(ctx, objectProto))->asObject(ctx));
        set("__base__", objectProto);
    }
    // The iterator type.
    const PO* it = ctx->newObject(false);
    if (objectProto) it = it->addParent(ctx, objectProto);
    if (typeProto) it = it->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), typeProto);
    it = it->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"),
                          PythonEnvironment::getInternedString(ctx, "bytearray_iterator")->asObject(ctx));
    if (env) {
        it = it->setAttribute(ctx, env->getNextString(), ctx->fromMethod(nullptr, ba_iter_next));
        it = it->setAttribute(ctx, env->getIterString(), ctx->fromMethod(nullptr, ba_iter_self));
    }
    if (objectProto)
        it = it->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__mro__"),
                              ctx->newTupleFromList(ctx->newList()->appendLast(ctx, it)->appendLast(ctx, objectProto))->asObject(ctx));
    cls = cls->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "\001__bytearray_iterator__"), it);
    if (env) env->setBytearrayPrototype(cls);
    return cls;
}

} // namespace protoPython
