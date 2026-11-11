// memoryview: a one-dimensional view of a bytes-like object's memory.
//
// A view is recognised by its type and keeps its state under internal
// (U+0001-prefixed) keys that Python code can neither read nor replace
// (PythonEnvironment::pythonAttributeKey): the viewed object, the byte offset
// of the first item, the byte step between items, the item count, the item
// format and size, whether it is read-only, the epoch of a viewed bytearray
// (BytearrayType.h) and its own state (live, held, released). Its public
// attributes (obj, nbytes, format, shape...) are read-only getset descriptors
// over that state.
//
// A view of a bytearray reads the bytearray's current content and writes
// through to it, so it shares the memory as CPython's does. Every read takes
// one version of the content and every write publishes one (with a
// compare-and-swap), so a view never sees a half-written update.
//
// Exports. CPython counts every view as an export of the bytearray, and a
// resize while one exists raises BufferError; the count drops when the view
// is released, which reference counting does at once for a temporary view.
// protoCore has no reference counts, so a view cannot tell when it dies:
// counting every view would make `bytes(memoryview(ba)[:n]); del ba[:n]`
// (asyncio's streams) fail. A view therefore holds an export (counted) while
// it is inside a `with` block -- until __exit__ or release() -- and an
// unheld view is invalidated by a resize instead: using it after one raises
// BufferError (docs/CPYTHON_CONFORMANCE.md).

#include <protoPython/MemoryviewType.h>
#include <protoPython/BuiltinSignature.h>
#include <protoPython/BytearrayType.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace protoPython {

namespace {

using PO = proto::ProtoObject;
using PL = proto::ProtoList;

bool isCellObject(const PO* o) { return o && (reinterpret_cast<uintptr_t>(o) & 0x3F) == 0; }

// View states.
constexpr long long kLive = 0;
constexpr long long kHeld = 1;
constexpr long long kReleased = 2;

#define MV_KEY(ctx, k) PythonEnvironment::getInternalString(ctx, "\001__mv_" k "__")

PythonEnvironment* envOf(proto::ProtoContext* ctx) { return PythonEnvironment::fromContext(ctx); }

[[noreturn]] void raiseNamed(proto::ProtoContext* ctx, const char* type, const std::string& msg) {
    PythonEnvironment* env = envOf(ctx);
    const PO* cls = env ? env->getBuiltins()->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, type)) : nullptr;
    const PO* exc = (cls && cls != PROTO_NONE) ? env->callObject(cls, {PythonEnvironment::newStr(ctx, msg)}) : nullptr;
    if (exc && env) env->raiseException(exc);
    throw std::runtime_error(msg);
}
[[noreturn]] void raiseType(proto::ProtoContext* ctx, const std::string& msg) { raiseNamed(ctx, "TypeError", msg); }
[[noreturn]] void raiseValue(proto::ProtoContext* ctx, const std::string& msg) { raiseNamed(ctx, "ValueError", msg); }
[[noreturn]] void raiseIndex(proto::ProtoContext* ctx, const std::string& msg) { raiseNamed(ctx, "IndexError", msg); }
[[noreturn]] void raiseNotImpl(proto::ProtoContext* ctx, const std::string& msg) { raiseNamed(ctx, "NotImplementedError", msg); }

const PO* mvType(proto::ProtoContext* ctx) {
    PythonEnvironment* env = envOf(ctx);
    return env ? env->getMemoryviewPrototype() : nullptr;
}

long long intAttr(proto::ProtoContext* ctx, const PO* o, const proto::ProtoString* k, long long dflt) {
    const PO* v = o->getOwnAttributeDirect(ctx, k);
    return v && v->isInteger(ctx) ? v->asLong(ctx) : dflt;
}

// --- item formats --------------------------------------------------------------

struct Format {
    char code = 'B';
    long long size = 1;
};

// The struct-module native formats of one item ('@' prefix allowed).
bool parseFormat(const std::string& text, Format& f) {
    std::string t = text;
    if (!t.empty() && t[0] == '@') t.erase(0, 1);
    if (t.size() != 1) return false;
    f.code = t[0];
    switch (f.code) {
        case 'B': case 'b': case 'c': case '?': f.size = 1; return true;
        case 'H': case 'h': case 'e': f.size = 2; return true;
        case 'I': case 'i': case 'f': f.size = 4; return true;
        case 'L': case 'l': case 'Q': case 'q': case 'N': case 'n': case 'P': case 'd': f.size = 8; return true;
        default: return false;
    }
}

bool isByteFormat(char c) { return c == 'B' || c == 'b' || c == 'c'; }

// IEEE 754 binary16 ('e'), as CPython's PyFloat_Pack2 / PyFloat_Unpack2:
// round half to even; a finite value beyond the range is an overflow.
bool packHalf(double x, uint16_t& out) {
    const uint16_t sign = std::signbit(x) ? 0x8000 : 0;
    if (std::isnan(x)) { out = sign | 0x7e00; return true; }
    if (std::isinf(x)) { out = sign | 0x7c00; return true; }
    double f = std::fabs(x);
    if (f == 0.0) { out = sign; return true; }
    int e = 0;
    f = std::frexp(f, &e);   // f in [0.5, 1)
    f *= 2.0;
    e -= 1;                  // f in [1, 2)
    if (e >= 16) return false;
    if (e < -14) {
        f = std::ldexp(f, 14 + e);
        e = 0;
    } else {
        e += 15;
        f -= 1.0;
    }
    f *= 1024.0;
    unsigned bits = static_cast<unsigned>(f);
    f -= bits;
    if (f > 0.5 || (f == 0.5 && (bits & 1))) {
        ++bits;
        if (bits == 1024) {
            bits = 0;
            ++e;
            if (e == 31) return false;
        }
    }
    out = static_cast<uint16_t>(sign | (e << 10) | bits);
    return true;
}

double unpackHalf(uint16_t h) {
    const int e = (h >> 10) & 0x1f;
    const unsigned m = h & 0x3ff;
    double x;
    if (e == 0x1f) x = m ? std::nan("") : INFINITY;
    else if (e == 0) x = std::ldexp(static_cast<double>(m), -24);
    else x = std::ldexp(static_cast<double>(m + 1024), e - 25);
    return (h & 0x8000) ? -x : x;
}

const PO* bigUnsigned(proto::ProtoContext* ctx, unsigned long long v) {
    if (v <= static_cast<unsigned long long>(INT64_MAX >> 8)) return ctx->fromInteger(static_cast<long long>(v));
    PythonEnvironment* env = envOf(ctx);
    const PO* intType = env ? env->getIntPrototype() : nullptr;
    const std::string digits = std::to_string(v);
    return intType ? env->callObject(intType, {PythonEnvironment::newStr(ctx, digits)}) : ctx->fromInteger(0);
}

const PO* decodeItem(proto::ProtoContext* ctx, const Format& f, const char* p) {
    auto u = [&](int n) {
        unsigned long long v = 0;
        std::memcpy(&v, p, static_cast<size_t>(n));  // little-endian hosts (protoPython's targets)
        return v;
    };
    switch (f.code) {
        case 'B': return ctx->fromInteger(static_cast<unsigned char>(p[0]));
        case 'b': return ctx->fromInteger(static_cast<signed char>(p[0]));
        case 'c': return PythonEnvironment::newBytes(ctx, p, 1);
        case '?': return p[0] ? PROTO_TRUE : PROTO_FALSE;
        case 'H': return ctx->fromInteger(static_cast<long long>(static_cast<uint16_t>(u(2))));
        case 'h': return ctx->fromInteger(static_cast<int16_t>(u(2)));
        case 'I': return ctx->fromInteger(static_cast<long long>(static_cast<uint32_t>(u(4))));
        case 'i': return ctx->fromInteger(static_cast<int32_t>(u(4)));
        case 'L': case 'Q': case 'N': case 'P': return bigUnsigned(ctx, u(8));
        case 'l': case 'q': case 'n': {
            const long long v = static_cast<long long>(u(8));
            if (v >= -(INT64_MAX >> 8) && v <= (INT64_MAX >> 8)) return ctx->fromInteger(v);
            PythonEnvironment* env = envOf(ctx);
            return env->callObject(env->getIntPrototype(), {PythonEnvironment::newStr(ctx, std::to_string(v))});
        }
        case 'e': return ctx->fromDouble(unpackHalf(static_cast<uint16_t>(u(2))));
        case 'f': { float x; std::memcpy(&x, p, 4); return ctx->fromDouble(x); }
        case 'd': { double x; std::memcpy(&x, p, 8); return ctx->fromDouble(x); }
        default: raiseNotImpl(ctx, std::string("memoryview: format ") + f.code + " not supported");
    }
}

// The bytes of `value` stored as one item of format `f`.
std::string encodeItem(proto::ProtoContext* ctx, const Format& f, const PO* value) {
    const std::string fmtName(1, f.code);
    std::string out(static_cast<size_t>(f.size), '\0');
    if (f.code == 'c') {
        std::string b;
        if (!PythonEnvironment::bytesLikeContent(ctx, value, b) || b.size() != 1)
            raiseValue(ctx, "memoryview: invalid value for format 'c'");
        return b;
    }
    if (f.code == '?') {
        PythonEnvironment* env = envOf(ctx);
        out[0] = (env && env->isTrue(value)) ? 1 : 0;
        return out;
    }
    if (f.code == 'f' || f.code == 'd' || f.code == 'e') {
        double x = 0;
        if (value && value->isDouble(ctx)) x = value->asDouble(ctx);
        else if (value && value->isInteger(ctx)) x = static_cast<double>(value->asLong(ctx));
        else raiseType(ctx, "memoryview: invalid type for format '" + fmtName + "'");
        if (f.code == 'e') {
            uint16_t h = 0;
            if (!packHalf(x, h)) raiseValue(ctx, "memoryview: invalid value for format 'e'");
            std::memcpy(&out[0], &h, 2);
        } else if (f.code == 'f') { const float y = static_cast<float>(x); std::memcpy(&out[0], &y, 4); }
        else std::memcpy(&out[0], &x, 8);
        return out;
    }
    if (!(value == PROTO_TRUE || value == PROTO_FALSE || (value && value->isInteger(ctx))))
        raiseType(ctx, "memoryview: invalid type for format '" + fmtName + "'");
    const bool isSigned = f.code == 'b' || f.code == 'h' || f.code == 'i' || f.code == 'l' || f.code == 'q' || f.code == 'n';
    long long v = 0;
    try {
        v = value == PROTO_TRUE ? 1 : value == PROTO_FALSE ? 0 : value->asLong(ctx);
    } catch (const proto::ProtoException& overflow) {
        PythonEnvironment::rethrowUnlessOverflow(ctx, overflow);
        // Beyond long long: an unsigned 64-bit item holds up to 2**64-1.
        std::string hex;
        if (!isSigned && f.size == 8 && value->integerSign(ctx) > 0) {
            value->asIntegerString(ctx, 16)->toUTF8String(ctx, hex);
        }
        if (hex.empty() || hex.size() > 16)
            raiseValue(ctx, "memoryview: invalid value for format '" + fmtName + "'");
        const unsigned long long uv = std::stoull(hex, nullptr, 16);
        std::memcpy(&out[0], &uv, 8);
        return out;
    }
    if (f.size < 8) {
        const long long bits = f.size * 8;
        const long long lo = isSigned ? -(1LL << (bits - 1)) : 0;
        const long long hi = isSigned ? (1LL << (bits - 1)) - 1 : (1LL << bits) - 1;
        if (v < lo || v > hi) raiseValue(ctx, "memoryview: invalid value for format '" + fmtName + "'");
    } else if (!isSigned && v < 0) {
        raiseValue(ctx, "memoryview: invalid value for format '" + fmtName + "'");
    }
    std::memcpy(&out[0], &v, static_cast<size_t>(f.size));
    return out;
}

// --- the view's state -----------------------------------------------------------

struct View {
    const PO* data = nullptr;      // the object whose bytes are viewed
    const PO* exporter = nullptr;  // obj
    long long start = 0;           // byte offset of item 0
    long long step = 1;            // bytes between items
    long long len = 0;             // items
    Format format;
    std::string formatText = "B";
    bool readonly = true;
    long long epoch = -1;          // a viewed bytearray's epoch, else -1
};

bool loadView(proto::ProtoContext* ctx, const PO* mv, View& v, bool allowReleased = false) {
    if (!isMemoryview(ctx, mv)) return false;
    if (!allowReleased && intAttr(ctx, mv, MV_KEY(ctx, "state"), kLive) == kReleased)
        raiseValue(ctx, "operation forbidden on released memoryview object");
    v.data = mv->getOwnAttributeDirect(ctx, MV_KEY(ctx, "data"));
    v.exporter = mv->getOwnAttributeDirect(ctx, MV_KEY(ctx, "obj"));
    if (!v.data) return false;
    v.start = intAttr(ctx, mv, MV_KEY(ctx, "start"), 0);
    v.step = intAttr(ctx, mv, MV_KEY(ctx, "step"), 1);
    v.len = intAttr(ctx, mv, MV_KEY(ctx, "len"), 0);
    const PO* ft = mv->getOwnAttributeDirect(ctx, MV_KEY(ctx, "format"));
    if (ft && ft->isString(ctx)) {
        v.formatText.clear();
        ft->asString(ctx)->toUTF8String(ctx, v.formatText);
    }
    if (!parseFormat(v.formatText, v.format)) v.format = Format{};
    v.readonly = mv->getOwnAttributeDirect(ctx, MV_KEY(ctx, "readonly")) != PROTO_FALSE;
    v.epoch = intAttr(ctx, mv, MV_KEY(ctx, "epoch"), -1);
    return true;
}

const PO* storeView(proto::ProtoContext* ctx, const PO* cls, const View& v) {
    const PO* mv = cls->newChild(ctx, true);
    mv->setAttribute(ctx, MV_KEY(ctx, "data"), v.data);
    mv->setAttribute(ctx, MV_KEY(ctx, "obj"), v.exporter ? v.exporter : v.data);
    mv->setAttribute(ctx, MV_KEY(ctx, "start"), ctx->fromInteger(v.start));
    mv->setAttribute(ctx, MV_KEY(ctx, "step"), ctx->fromInteger(v.step));
    mv->setAttribute(ctx, MV_KEY(ctx, "len"), ctx->fromInteger(v.len));
    mv->setAttribute(ctx, MV_KEY(ctx, "format"), PythonEnvironment::newStr(ctx, v.formatText));
    mv->setAttribute(ctx, MV_KEY(ctx, "readonly"), v.readonly ? PROTO_TRUE : PROTO_FALSE);
    if (v.epoch >= 0) mv->setAttribute(ctx, MV_KEY(ctx, "epoch"), ctx->fromInteger(v.epoch));
    mv->setAttribute(ctx, MV_KEY(ctx, "state"), ctx->fromInteger(kLive));
    return mv;
}

bool contiguous(const View& v) { return v.step == v.format.size || v.len <= 1; }

// The viewed object's whole content, from one version of it.
void sourceBytes(proto::ProtoContext* ctx, const View& v, std::string& all) {
    if (v.epoch >= 0) {
        if (!bytearrayViewRead(ctx, v.data, v.epoch, all)) raiseValue(ctx, "memoryview: the viewed object is gone");
        return;
    }
    all.clear();
    if (!PythonEnvironment::bytesLikeContent(ctx, v.data, all) && v.data->isString(ctx))
        v.data->asString(ctx)->toUTF8String(ctx, all);
}

// The bytes [from, to) of the viewed object (clipped to its size), from one
// version of it, without copying the rest: an item read is O(itemsize).
void sourceRange(proto::ProtoContext* ctx, const View& v, long long from, long long to, std::string& out) {
    out.clear();
    if (from < 0) from = 0;
    if (to <= from) return;
    if (v.epoch >= 0) {
        if (!bytearrayViewReadRange(ctx, v.data, v.epoch, static_cast<size_t>(from), static_cast<size_t>(to), out))
            raiseValue(ctx, "memoryview: the viewed object is gone");
        return;
    }
    const PO* d = v.data;
    if (!d->isByteBuffer(ctx) && isCellObject(d) && !d->isString(ctx)) {
        PythonEnvironment* env = envOf(ctx);
        const PO* inner = env ? d->getAttribute(ctx, env->getDataString()) : nullptr;
        if (inner && inner->isByteBuffer(ctx)) d = inner;
    }
    if (d->isByteBuffer(ctx)) {
        const proto::ProtoByteBuffer* bb = d->asByteBuffer(ctx);
        const long long n = static_cast<long long>(bb->getSize(ctx));
        if (to > n) to = n;
        if (to > from) out.assign(bb->getBuffer(ctx) + from, static_cast<size_t>(to - from));
        return;
    }
    std::string all;
    sourceBytes(ctx, v, all);
    const long long n = static_cast<long long>(all.size());
    if (to > n) to = n;
    if (to > from) out.assign(all, static_cast<size_t>(from), static_cast<size_t>(to - from));
}

// The view's bytes, items in order.
void viewBytes(proto::ProtoContext* ctx, const View& v, std::string& out) {
    if (contiguous(v)) {
        sourceRange(ctx, v, v.start, v.start + v.len * v.format.size, out);
        return;
    }
    std::string all;
    sourceBytes(ctx, v, all);
    out.clear();
    const long long n = static_cast<long long>(all.size());
    out.reserve(static_cast<size_t>(v.len * v.format.size));
    for (long long i = 0; i < v.len; ++i) {
        const long long at = v.start + i * v.step;
        if (at < 0 || at + v.format.size > n) break;
        out.append(all, static_cast<size_t>(at), static_cast<size_t>(v.format.size));
    }
}

// Writes item-aligned bytes `data` at byte offset `at` of the view.
void viewWrite(proto::ProtoContext* ctx, const View& v, size_t at, const std::string& data) {
    if (v.readonly) raiseType(ctx, "cannot modify read-only memory");
    const long long total = v.len * v.format.size;
    if (static_cast<long long>(at + data.size()) > total) raiseIndex(ctx, "write outside the memoryview");
    std::vector<std::pair<size_t, std::string>> pieces;
    if (contiguous(v)) {
        pieces.emplace_back(static_cast<size_t>(v.start) + at, data);
    } else {
        for (size_t k = 0; k < data.size();) {
            const long long item = static_cast<long long>((at + k) / static_cast<size_t>(v.format.size));
            const size_t within = (at + k) % static_cast<size_t>(v.format.size);
            const size_t take = std::min(data.size() - k, static_cast<size_t>(v.format.size) - within);
            pieces.emplace_back(static_cast<size_t>(v.start + item * v.step) + within, data.substr(k, take));
            k += take;
        }
    }
    if (v.epoch >= 0) {
        if (!bytearrayViewWrite(ctx, v.data, v.epoch, pieces)) raiseValue(ctx, "memoryview: write failed");
        return;
    }
    raiseType(ctx, "cannot modify read-only memory");
}

// --- methods -----------------------------------------------------------------

#define MV_METHOD(name)                                                                              \
    const PO* name(proto::ProtoContext* ctx, const PO* self, const proto::ParentLink* link,         \
                   const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs)

// The view a method acts on (bound: self; unbound: the first argument) and
// how many arguments that took.
const PO* receiver(proto::ProtoContext* ctx, const PO* self, const proto::ProtoArguments* args, unsigned& shift,
                   const char* method) {
    shift = 0;
    if (isMemoryview(ctx, self)) return self;
    if (args && args->getSize(ctx) >= 1 && isMemoryview(ctx, args->getAt(ctx, 0))) {
        shift = 1;
        return args->getAt(ctx, 0);
    }
    raiseType(ctx, std::string("descriptor '") + method + "' for 'memoryview' objects doesn't apply to a '"
                   + PythonEnvironment::typeNameOf(ctx, self) + "' object");
}

size_t argc(proto::ProtoContext* ctx, const proto::ProtoArguments* args, unsigned shift) {
    const size_t n = args ? args->getSize(ctx) : 0;
    return n > shift ? n - shift : 0;
}
const PO* arg(proto::ProtoContext* ctx, const proto::ProtoArguments* args, unsigned shift, size_t i) {
    return args->getAt(ctx, static_cast<int>(shift + i));
}

#define MV_RECEIVER(method, view)                                 \
    (void)link; (void)kwargs;                                     \
    unsigned shift = 0;                                           \
    const PO* mv = receiver(ctx, self, args, shift, method);      \
    View view;                                                    \
    loadView(ctx, mv, view);

// memoryview(object): a view of a bytes-like object.
MV_METHOD(mv_new_impl) {
    (void)link; (void)kwargs;
    PythonEnvironment* env = envOf(ctx);
    const size_t n = args ? args->getSize(ctx) : 0;
    if (!env || n < 2) raiseType(ctx, "memoryview() missing required argument 'object' (pos 1)");
    const PO* cls = args->getAt(ctx, 0);
    const PO* obj = args->getAt(ctx, 1);
    if (!env->isActuallyAClass(ctx, cls)) cls = mvType(ctx);
    (void)self;
    View v;
    if (isMemoryview(ctx, obj)) {
        loadView(ctx, obj, v);
        return storeView(ctx, cls, v);
    }
    if (obj && obj->isString(ctx)) raiseType(ctx, "memoryview: a bytes-like object is required, not 'str'");
    long long epoch = 0;
    size_t size = 0;
    if (bytearrayWatch(ctx, obj, epoch, &size)) {
        v.data = obj;
        v.exporter = obj;
        v.len = static_cast<long long>(size);
        v.readonly = false;
        v.epoch = epoch;
        return storeView(ctx, cls, v);
    }
    std::string content;
    const PO* bytesType = env->getBytesPrototype();
    if (isCellObject(obj) && bytesType && obj->isInstanceOf(ctx, bytesType) == PROTO_TRUE && PythonEnvironment::bytesLikeContent(ctx, obj, content)) {
        v.data = obj;
        v.exporter = obj;
        v.len = static_cast<long long>(content.size());
        return storeView(ctx, cls, v);
    }
    if (obj && obj->isByteBuffer(ctx)) {
        v.data = obj;
        v.exporter = obj;
        v.len = static_cast<long long>(obj->asByteBuffer(ctx)->getSize(ctx));
        return storeView(ctx, cls, v);
    }
    if (isCellObject(obj)) {
        // array.array: a read-only view of a copy of its items (protoPython's
        // arrays export no shared memory), with the array's typecode.
        const PO* tc = env->getAttribute(ctx, obj, PythonEnvironment::getInternedString(ctx, "typecode"), false);
        const PO* tobytes = env->getAttribute(ctx, obj, PythonEnvironment::getInternedString(ctx, "tobytes"), false);
        if (tc && tc != PROTO_NONE && tc->isString(ctx) && tobytes && tobytes != PROTO_NONE) {
            const PO* copy = env->callObject(tobytes, {});
            if (!copy) return nullptr;
            std::string code;
            tc->asString(ctx)->toUTF8String(ctx, code);
            if (code == "u" || code == "w") code = "I";
            Format f;
            if (!parseFormat(code, f)) raiseNotImpl(ctx, "memoryview: array typecode '" + code + "' not supported");
            if (!PythonEnvironment::bytesLikeContent(ctx, copy, content)) raiseType(ctx, "memoryview: a bytes-like object is required");
            v.data = copy;
            v.exporter = obj;
            v.format = f;
            v.formatText = code;
            v.step = f.size;
            v.len = static_cast<long long>(content.size()) / f.size;
            return storeView(ctx, cls, v);
        }
        // PEP 688: an object that defines __buffer__.
        const PO* buffer = env->getAttribute(ctx, obj, PythonEnvironment::getInternedString(ctx, "__buffer__"), false);
        if (buffer && buffer != PROTO_NONE) {
            const PO* r = env->callObject(buffer, {ctx->fromInteger(0)});
            if (!r) return nullptr;
            if (!isMemoryview(ctx, r)) raiseType(ctx, "__buffer__ returned non-memoryview object");
            loadView(ctx, r, v);
            // The view remembers the export: its release() hands `r` back
            // through obj.__release_buffer__, as CPython's does.
            const PO* mv = storeView(ctx, cls, v);
            mv->setAttribute(ctx, MV_KEY(ctx, "pyexporter"), obj);
            mv->setAttribute(ctx, MV_KEY(ctx, "pyview"), r);
            return mv;
        }
    }
    raiseType(ctx, "memoryview: a bytes-like object is required, not '" + PythonEnvironment::typeNameOf(ctx, obj) + "'");
}

const BuiltinSignature kSigNew{"memoryview", 1, {{"object", BuiltinParam::Required, 0}}, {}};

MV_METHOD(mv_len) {
    MV_RECEIVER("__len__", v)
    return ctx->fromInteger(v.len);
}

MV_METHOD(mv_tobytes) {
    MV_RECEIVER("tobytes", v)
    std::string out;
    viewBytes(ctx, v, out);
    return PythonEnvironment::newBytes(ctx, out.data(), out.size());
}

const PO* listOf(proto::ProtoContext* ctx, const View& v) {
    std::string bytes;
    viewBytes(ctx, v, bytes);
    const PL* items = ctx->newList();
    for (size_t at = 0; at + static_cast<size_t>(v.format.size) <= bytes.size(); at += static_cast<size_t>(v.format.size))
        items = items->appendLast(ctx, decodeItem(ctx, v.format, bytes.data() + at));
    return PythonEnvironment::wrapList(ctx, items);
}

MV_METHOD(mv_tolist) {
    MV_RECEIVER("tolist", v)
    return listOf(ctx, v);
}

MV_METHOD(mv_hex) {
    MV_RECEIVER("hex", v)
    PythonEnvironment* env = envOf(ctx);
    std::string out;
    viewBytes(ctx, v, out);
    const PO* b = PythonEnvironment::newBytes(ctx, out.data(), out.size());
    PythonEnvironment::TransientPin pin(env, b);
    // bytes.hex on the snapshot, with the same arguments.
    const PO* hex = env->getBytesPrototype()->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hex"));
    if (!hex || !hex->asMethod(ctx)) return PROTO_NONE;
    const PL* rest = ctx->newList();
    for (size_t i = shift; i < (args ? args->getSize(ctx) : 0); ++i) rest = rest->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
    return hex->asMethod(ctx)(ctx, b, nullptr, argsView(proto::ProtoListArguments(ctx, rest)), kwargs);
}

// The index of item `key` (an int or __index__), bounds-checked.
long long itemIndex(proto::ProtoContext* ctx, const View& v, const PO* key) {
    long long i = 0;
    if (key == PROTO_TRUE || key == PROTO_FALSE) i = key == PROTO_TRUE ? 1 : 0;
    else if (key && key->isInteger(ctx)) i = key->asLong(ctx);
    else {
        // Any object with __index__, as CPython's PyNumber_AsSsize_t.
        PythonEnvironment* env = envOf(ctx);
        const PO* idx = (env && key && !key->isDouble(ctx) && !key->isString(ctx))
            ? env->getAttribute(ctx, env->getType(ctx, key), PythonEnvironment::getInternedString(ctx, "__index__"), false)
            : nullptr;
        if (!idx || idx == PROTO_NONE) raiseType(ctx, "memoryview: invalid slice key");
        const PO* r = env->callMethod(key, "__index__", {});
        if (!r) throw std::runtime_error("memoryview: __index__ raised");
        if (!r->isInteger(ctx)) raiseType(ctx, "__index__ returned non-int");
        i = r->asLong(ctx);
    }
    if (i < 0) i += v.len;
    if (i < 0 || i >= v.len) raiseIndex(ctx, "index out of bounds on dimension 1");
    return i;
}

bool isSlice(proto::ProtoContext* ctx, const PO* key) {
    PythonEnvironment* env = envOf(ctx);
    return env && env->getSliceType() && isCellObject(key) && key->isInstanceOf(ctx, env->getSliceType()) == PROTO_TRUE;
}

// slice.indices(len) for the view: start, step and count.
void sliceOf(proto::ProtoContext* ctx, const View& v, const PO* key, long long& start, long long& step, long long& count) {
    PythonEnvironment* env = envOf(ctx);
    const PO* r = env->callMethod(key, "indices", {ctx->fromInteger(v.len)});
    if (!r) throw std::runtime_error("slice.indices failed");
    TupleView t = PythonEnvironment::tupleView(ctx, r);
    if (!t || t->getSize(ctx) != 3) raiseType(ctx, "slice.indices() returned a non-triple");
    start = t->getAt(ctx, 0)->asLong(ctx);
    const long long stop = t->getAt(ctx, 1)->asLong(ctx);
    step = t->getAt(ctx, 2)->asLong(ctx);
    count = 0;
    if (step < 0) { if (stop < start) count = (start - stop - 1) / (-step) + 1; }
    else if (start < stop) count = (stop - start - 1) / step + 1;
}

MV_METHOD(mv_getitem) {
    MV_RECEIVER("__getitem__", v)
    if (argc(ctx, args, shift) != 1) raiseType(ctx, "expected 1 argument");
    const PO* key = arg(ctx, args, shift, 0);
    PythonEnvironment* env = envOf(ctx);
    if (env && key == env->getEllipsisPrototype()) return mv;
    if (isSlice(ctx, key)) {
        long long start = 0, step = 1, count = 0;
        sliceOf(ctx, v, key, start, step, count);
        View s = v;
        s.start = v.start + start * v.step;
        s.step = v.step * step;
        s.len = count;
        return storeView(ctx, env->getType(ctx, mv), s);
    }
    const long long i = itemIndex(ctx, v, key);
    const long long at = v.start + i * v.step;
    std::string item;
    if (at >= 0) sourceRange(ctx, v, at, at + v.format.size, item);
    if (at < 0 || static_cast<long long>(item.size()) != v.format.size) raiseIndex(ctx, "index out of bounds on dimension 1");
    return decodeItem(ctx, v.format, item.data());
}

MV_METHOD(mv_setitem) {
    MV_RECEIVER("__setitem__", v)
    if (argc(ctx, args, shift) != 2) raiseType(ctx, "expected 2 arguments");
    const PO* key = arg(ctx, args, shift, 0);
    const PO* value = arg(ctx, args, shift, 1);
    if (v.readonly) raiseType(ctx, "cannot modify read-only memory");
    if (isSlice(ctx, key)) {
        long long start = 0, step = 1, count = 0;
        sliceOf(ctx, v, key, start, step, count);
        std::string repl;
        if (isMemoryview(ctx, value)) {
            View src;
            loadView(ctx, value, src);
            if (src.format.code != v.format.code || src.len != count)
                raiseValue(ctx, "memoryview assignment: lvalue and rvalue have different structures");
            viewBytes(ctx, src, repl);
        } else if (!PythonEnvironment::bytesLikeContent(ctx, value, repl)) {
            raiseType(ctx, "a bytes-like object is required, not '" + PythonEnvironment::typeNameOf(ctx, value) + "'");
        }
        if (static_cast<long long>(repl.size()) != count * v.format.size)
            raiseValue(ctx, "memoryview assignment: lvalue and rvalue have different structures");
        View s = v;
        s.start = v.start + start * v.step;
        s.step = v.step * step;
        s.len = count;
        viewWrite(ctx, s, 0, repl);
        return PROTO_NONE;
    }
    const long long i = itemIndex(ctx, v, key);
    const std::string item = encodeItem(ctx, v.format, value);
    viewWrite(ctx, v, static_cast<size_t>(i * v.format.size), item);
    return PROTO_NONE;
}

// release(): ends the view (and the export it holds), idempotent.
const PO* releaseView(proto::ProtoContext* ctx, const PO* mv) {
    const proto::ProtoString* key = MV_KEY(ctx, "state");
    for (;;) {
        const PO* st = mv->getOwnAttributeDirect(ctx, key);
        const long long state = st && st->isInteger(ctx) ? st->asLong(ctx) : kLive;
        if (state == kReleased) return PROTO_NONE;
        if (!mv->setAttributeIfEqual(ctx, key, st, ctx->fromInteger(kReleased))) continue;
        if (state == kHeld) {
            View v;
            loadView(ctx, mv, v, /*allowReleased=*/true);
            bytearrayHold(ctx, v.data, v.epoch, -1);
        }
        // A view of an object with a Python __buffer__ gives the export back
        // to it (PEP 688), once: only the thread that released it gets here.
        const PO* exporter = mv->getOwnAttributeDirect(ctx, MV_KEY(ctx, "pyexporter"));
        const PO* inner = mv->getOwnAttributeDirect(ctx, MV_KEY(ctx, "pyview"));
        if (exporter && inner) {
            PythonEnvironment* env = envOf(ctx);
            if (env && !env->callMethod(exporter, "__release_buffer__", {inner})) return nullptr;
        }
        return PROTO_NONE;
    }
}

MV_METHOD(mv_release) {
    (void)link; (void)kwargs;
    unsigned shift = 0;
    const PO* mv = receiver(ctx, self, args, shift, "release");
    return releaseView(ctx, mv);
}

// __enter__: a view of a bytearray holds an export until __exit__.
MV_METHOD(mv_enter) {
    MV_RECEIVER("__enter__", v)
    if (v.epoch < 0) return mv;
    const proto::ProtoString* key = MV_KEY(ctx, "state");
    const PO* st = mv->getOwnAttributeDirect(ctx, key);
    if (!st || !st->isInteger(ctx) || st->asLong(ctx) != kLive) return mv;
    bytearrayHold(ctx, v.data, v.epoch, +1);
    if (!mv->setAttributeIfEqual(ctx, key, st, ctx->fromInteger(kHeld))) {
        // Another thread entered or released it first.
        bytearrayHold(ctx, v.data, v.epoch, -1);
    }
    return mv;
}

MV_METHOD(mv_exit) {
    (void)link; (void)kwargs;
    unsigned shift = 0;
    const PO* mv = receiver(ctx, self, args, shift, "__exit__");
    releaseView(ctx, mv);
    return PROTO_FALSE;
}

MV_METHOD(mv_cast) {
    MV_RECEIVER("cast", v)
    const size_t n = argc(ctx, args, shift);
    if (n < 1 || n > 2) raiseType(ctx, "cast() takes 1 or 2 arguments");
    const PO* fmtObj = arg(ctx, args, shift, 0);
    if (!fmtObj || !fmtObj->isString(ctx)) raiseType(ctx, "memoryview: format argument must be a string");
    std::string text;
    fmtObj->asString(ctx)->toUTF8String(ctx, text);
    Format f;
    if (!parseFormat(text, f))
        raiseValue(ctx, "memoryview: destination format must be a native single character format prefixed with an optional '@'");
    if (!contiguous(v)) raiseType(ctx, "memoryview: casts are restricted to C-contiguous views");
    if (!isByteFormat(v.format.code) && !isByteFormat(f.code))
        raiseType(ctx, "memoryview: cannot cast between two non-byte formats");
    const long long nbytes = v.len * v.format.size;
    if (nbytes % f.size != 0) raiseType(ctx, "memoryview: length is not a multiple of itemsize");
    if (n == 2) {
        PythonEnvironment* env = envOf(ctx);
        const PO* shape = arg(ctx, args, shift, 1);
        const PO* len = env->callMethod(shape, "__len__", {});
        if (!len) return nullptr;
        if (len->asLong(ctx) != 1) raiseNotImpl(ctx, "memoryview: only one-dimensional shapes are supported");
        const PO* dim = env->getItem(shape, ctx->fromInteger(0), ctx);
        if (!dim || !dim->isInteger(ctx) || dim->asLong(ctx) * f.size != nbytes)
            raiseType(ctx, "memoryview: product(shape) * itemsize != buffer size");
    }
    View c = v;
    c.format = f;
    c.formatText = text;
    c.step = f.size;
    c.len = nbytes / f.size;
    return storeView(ctx, envOf(ctx)->getType(ctx, mv), c);
}

MV_METHOD(mv_toreadonly) {
    MV_RECEIVER("toreadonly", v)
    v.readonly = true;
    return storeView(ctx, envOf(ctx)->getType(ctx, mv), v);
}

MV_METHOD(mv_eq) {
    (void)link; (void)kwargs;
    unsigned shift = 0;
    const PO* mv = receiver(ctx, self, args, shift, "__eq__");
    PythonEnvironment* env = envOf(ctx);
    if (argc(ctx, args, shift) != 1) raiseType(ctx, "expected 1 argument");
    const PO* other = arg(ctx, args, shift, 0);
    // A released view compares equal to itself only, as CPython's
    // memory_richcompare: it never raises.
    if (other == mv) return PROTO_TRUE;
    auto released = [&](const PO* m) { return intAttr(ctx, m, MV_KEY(ctx, "state"), kLive) == kReleased; };
    if (released(mv) || (isMemoryview(ctx, other) && released(other))) return PROTO_FALSE;
    View v;
    loadView(ctx, mv, v);
    if (isMemoryview(ctx, other)) {
        View o;
        loadView(ctx, other, o);
        if (o.len != v.len) return PROTO_FALSE;
        if (o.format.code == v.format.code) {
            std::string a, b;
            viewBytes(ctx, v, a);
            viewBytes(ctx, o, b);
            return a == b ? PROTO_TRUE : PROTO_FALSE;
        }
        const PO* r = env->callMethod(listOf(ctx, v), "__eq__", {listOf(ctx, o)});
        return r ? r : nullptr;
    }
    std::string b;
    if (!PythonEnvironment::bytesLikeContent(ctx, other, b)) return env->getNotImplementedPrototype();
    if (!isByteFormat(v.format.code)) return PROTO_FALSE;
    std::string a;
    viewBytes(ctx, v, a);
    return a == b ? PROTO_TRUE : PROTO_FALSE;
}

MV_METHOD(mv_ne) {
    const PO* r = mv_eq(ctx, self, link, args, kwargs);
    if (r == PROTO_TRUE) return PROTO_FALSE;
    if (r == PROTO_FALSE) return PROTO_TRUE;
    return r;
}

MV_METHOD(mv_hash) {
    MV_RECEIVER("__hash__", v)
    if (!v.readonly) raiseValue(ctx, "cannot hash writable memoryview object");
    if (!isByteFormat(v.format.code)) raiseValue(ctx, "memoryview: hashing is restricted to formats 'B', 'b' or 'c'");
    // A read-only view of a bytearray hashes as its exporter does, as
    // CPython's memory_hash: the bytearray is unhashable.
    if (v.epoch >= 0) raiseType(ctx, "unhashable type: 'bytearray'");
    PythonEnvironment* env = envOf(ctx);
    std::string a;
    viewBytes(ctx, v, a);
    const PO* hashFn = env->getBuiltins()->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "hash"));
    return env->callObject(hashFn, {PythonEnvironment::newBytes(ctx, a.data(), a.size())});
}

MV_METHOD(mv_iter) {
    MV_RECEIVER("__iter__", v)
    PythonEnvironment* env = envOf(ctx);
    return env->iter(listOf(ctx, v));
}

MV_METHOD(mv_count) {
    MV_RECEIVER("count", v)
    if (argc(ctx, args, shift) != 1) raiseType(ctx, "count() takes exactly one argument");
    return envOf(ctx)->callMethod(listOf(ctx, v), "count", {arg(ctx, args, shift, 0)});
}

MV_METHOD(mv_index) {
    MV_RECEIVER("index", v)
    const size_t n = argc(ctx, args, shift);
    if (n < 1 || n > 3) raiseType(ctx, "index() takes 1 to 3 arguments");
    std::vector<const PO*> a;
    for (size_t i = 0; i < n; ++i) a.push_back(arg(ctx, args, shift, i));
    PythonEnvironment* env = envOf(ctx);
    const PO* lst = listOf(ctx, v);
    const PO* contains = env->callMethod(lst, "__contains__", {a[0]});
    if (contains == PROTO_FALSE) raiseValue(ctx, "memoryview.index(x): x not found");
    return env->callMethod(lst, "index", a);
}

// __reduce_ex__ / __reduce__: a view is not picklable (nor copyable), as
// in CPython; the default object reduction produced an unloadable payload.
MV_METHOD(mv_reduce) {
    (void)link; (void)kwargs; (void)self; (void)args;
    raiseType(ctx, "cannot pickle memoryview objects");
}

MV_METHOD(mv_repr) {
    (void)link; (void)kwargs;
    unsigned shift = 0;
    const PO* mv = receiver(ctx, self, args, shift, "__repr__");
    const bool released = intAttr(ctx, mv, MV_KEY(ctx, "state"), kLive) == kReleased;
    char buf[64];
    std::snprintf(buf, sizeof buf, "<%smemory at %p>", released ? "released " : "", static_cast<const void*>(mv));
    return PythonEnvironment::newStr(ctx, buf);
}

// __buffer__(flags): a new view that holds an export until
// __release_buffer__ (or its release()), as CPython's buffer protocol does.
MV_METHOD(mv_buffer) {
    MV_RECEIVER("__buffer__", v)
    const PO* view = storeView(ctx, mvType(ctx), v);
    return mv_enter(ctx, view, nullptr, nullptr, nullptr);
}

MV_METHOD(mv_release_buffer) {
    (void)link; (void)kwargs;
    unsigned shift = 0;
    const PO* mv = receiver(ctx, self, args, shift, "__release_buffer__");
    if (argc(ctx, args, shift) != 1) raiseType(ctx, "__release_buffer__() takes exactly one argument");
    const PO* owner = mv->getOwnAttributeDirect(ctx, MV_KEY(ctx, "data"));
    return memoryviewReleaseBuffer(ctx, owner, arg(ctx, args, shift, 0));
}

// --- read-only attributes --------------------------------------------------------

const PO* tupleOf(proto::ProtoContext* ctx, std::initializer_list<long long> items) {
    const PL* l = ctx->newList();
    for (long long x : items) l = l->appendLast(ctx, ctx->fromInteger(x));
    return ctx->newTupleFromList(l)->asObject(ctx);
}

#define MV_GETTER(name, expr)                                    \
    MV_METHOD(name) {                                            \
        MV_RECEIVER(#name, v)                                    \
        return (expr);                                           \
    }

MV_GETTER(get_obj, v.exporter ? v.exporter : v.data)
MV_GETTER(get_nbytes, ctx->fromInteger(v.len * v.format.size))
MV_GETTER(get_readonly, v.readonly ? PROTO_TRUE : PROTO_FALSE)
MV_GETTER(get_itemsize, ctx->fromInteger(v.format.size))
MV_GETTER(get_format, PythonEnvironment::newStr(ctx, v.formatText))
MV_GETTER(get_ndim, ctx->fromInteger(1))
MV_GETTER(get_shape, tupleOf(ctx, {v.len}))
MV_GETTER(get_strides, tupleOf(ctx, {v.step}))
MV_GETTER(get_suboffsets, ctx->newTupleFromList(ctx->newList())->asObject(ctx))
MV_GETTER(get_contiguous, contiguous(v) ? PROTO_TRUE : PROTO_FALSE)

} // namespace

// --- public helpers ------------------------------------------------------------

bool isMemoryview(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    const PO* t = mvType(ctx);
    return t && isCellObject(obj) && obj != t && obj->isInstanceOf(ctx, t) == PROTO_TRUE;
}

bool memoryviewContent(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::string& out) {
    View v;
    if (!loadView(ctx, obj, v)) return false;
    viewBytes(ctx, v, out);
    return true;
}

bool memoryviewWrite(proto::ProtoContext* ctx, const proto::ProtoObject* obj, size_t at, const std::string& data) {
    View v;
    if (!loadView(ctx, obj, v)) return false;
    viewWrite(ctx, v, at, data);
    return true;
}

const proto::ProtoObject* memoryviewReleaseBuffer(proto::ProtoContext* ctx, const proto::ProtoObject* owner,
                                                  const proto::ProtoObject* view) {
    if (!isMemoryview(ctx, view)) raiseType(ctx, "expected a memoryview object");
    if (intAttr(ctx, view, MV_KEY(ctx, "state"), kLive) == kReleased)
        raiseValue(ctx, "memoryview's buffer has already been released");
    if (view->getOwnAttributeDirect(ctx, MV_KEY(ctx, "data")) != owner)
        raiseValue(ctx, "memoryview's buffer is not this object");
    return releaseView(ctx, view);
}

const proto::ProtoObject* memoryviewExporter(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!isMemoryview(ctx, obj)) return nullptr;
    return obj->getOwnAttributeDirect(ctx, MV_KEY(ctx, "obj"));
}

const proto::ProtoObject* makeMemoryviewType(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                             const proto::ProtoObject* typeProto) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const PO* cls = ctx->newObject(true);
    if (objectProto) cls = cls->addParent(ctx, objectProto);
    auto set = [&](const char* name, const PO* value) {
        cls = cls->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, name), value);
    };
    auto method = [&](const char* name, proto::ProtoMethod m) { set(name, ctx->fromMethod(nullptr, m)); };
    if (typeProto) set("__class__", typeProto);
    set("__name__", PythonEnvironment::getInternedString(ctx, "memoryview")->asObject(ctx));
    set("__qualname__", PythonEnvironment::getInternedString(ctx, "memoryview")->asObject(ctx));
    set("__module__", PythonEnvironment::getInternedString(ctx, "builtins")->asObject(ctx));
    set("__doc__", PythonEnvironment::newStr(ctx, "Create a new memoryview object which references the given object."));
    method("__new__", withSignature<mv_new_impl, kSigNew>);
    method("__len__", mv_len);
    method("__getitem__", mv_getitem);
    method("__setitem__", mv_setitem);
    method("__iter__", mv_iter);
    method("__eq__", mv_eq);
    method("__ne__", mv_ne);
    method("__hash__", mv_hash);
    method("__repr__", mv_repr);
    method("__enter__", mv_enter);
    method("__exit__", mv_exit);
    method("__buffer__", mv_buffer);
    method("__reduce_ex__", mv_reduce);
    method("__reduce__", mv_reduce);
    method("__release_buffer__", mv_release_buffer);
    method("tobytes", mv_tobytes);
    method("tolist", mv_tolist);
    method("hex", mv_hex);
    method("release", mv_release);
    method("cast", mv_cast);
    method("toreadonly", mv_toreadonly);
    method("count", mv_count);
    method("index", mv_index);
    if (env && env->getGetSetDescriptorPrototype()) {
        const PO* gsd = env->getGetSetDescriptorPrototype();
        struct { const char* name; proto::ProtoMethod fget; } getters[] = {
            {"obj", get_obj}, {"nbytes", get_nbytes}, {"readonly", get_readonly}, {"itemsize", get_itemsize},
            {"format", get_format}, {"ndim", get_ndim}, {"shape", get_shape}, {"strides", get_strides},
            {"suboffsets", get_suboffsets}, {"c_contiguous", get_contiguous}, {"f_contiguous", get_contiguous},
            {"contiguous", get_contiguous},
        };
        for (const auto& g : getters) {
            const PO* d = gsd->newChild(ctx, true);
            d->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), gsd);
            d->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "fget"), ctx->fromMethod(nullptr, g.fget));
            d->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"),
                            PythonEnvironment::getInternedString(ctx, g.name)->asObject(ctx));
            d->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__objclass__"), cls);
            set(g.name, d);
        }
    }
    if (objectProto) {
        set("__mro__", ctx->newTupleFromList(ctx->newList()->appendLast(ctx, cls)->appendLast(ctx, objectProto))->asObject(ctx));
        set("__bases__", ctx->newTupleFromList(ctx->newList()->appendLast(ctx, objectProto))->asObject(ctx));
        set("__base__", objectProto);
    }
    if (env) env->setMemoryviewPrototype(cls);
    return cls;
}

} // namespace protoPython
