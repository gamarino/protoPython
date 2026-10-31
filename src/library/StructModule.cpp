#include <protoPython/PythonEnvironment.h>
#include "protoPython/StructModule.h"
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

// Real `struct` module implementation. Replaces a stub that had
// `pack` returning b'', `unpack` returning [], `calcsize` returning 1
// — every caller (base64.a85encode, hashlib hex digest formatters,
// network byte-order code) was silently broken.
//
// Scope vs CPython:
// - Format chars: x b B h H i I l L q Q f d s c ?
// - Byte order: '<' (little-endian), '>' or '!' (big-endian),
//   '@' / '=' / no-prefix → native (we treat as little-endian
//   since x86_64 is the dominant target; users requiring strict
//   native semantics should specify explicitly).
// - Repeat counts on any format char.
// - `Struct(fmt).pack(...)` / `.unpack(...)` / `.size`.
// - `iter_unpack` returns a list of tuples (CPython returns an
//   iterator; a list is acceptable for small inputs which is the
//   only protoPython use case).
// - Padding char `x` skips a byte on pack (writes \0) and unpack.
//
// Out of scope: native-alignment padding for `@`/`=` (we always
// pack tightly), `n`/`N`/`P` (size_t-sized ints), `e` (half-float),
// `Pascal-style p`. None of these appear in the audit's test
// chain.

namespace protoPython {
namespace struct_module {

// ---- Format parser ---------------------------------------------------------

struct FmtItem {
    char code;
    int count;  // applied to char (e.g. 4i = repeat int four times)
    int size;   // bytes per element
};

static int element_size(char c) {
    switch (c) {
        case 'x': case 'b': case 'B': case 'c': case 's': case '?': return 1;
        case 'h': case 'H': return 2;
        case 'i': case 'I': case 'l': case 'L': case 'f': return 4;
        case 'q': case 'Q': case 'd': return 8;
        default: return -1;
    }
}

// Returns (byte_order, items). byte_order: '<' = little, '>' = big.
// Throws via env on invalid format.
// Raises struct.error(msg): _struct.error, an Exception subclass of its own
// as in CPython (it was an alias of ValueError, so `except ValueError`
// caught it and `except struct.error` caught every ValueError).
static void raise_struct_error(proto::ProtoContext* ctx, const std::string& msg) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return;
    const proto::ProtoObject* text = proto::ProtoString::fromUTF8String(ctx, msg.c_str())->asObject(ctx);
    const proto::ProtoObject* mod = env->resolveModule("_struct", ctx);
    const proto::ProtoObject* errCls = (mod && mod != PROTO_NONE)
        ? mod->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "error")) : nullptr;
    if (errCls && errCls != PROTO_NONE) {
        const proto::ProtoObject* exc = env->callObject(errCls, {text});
        if (exc && exc != PROTO_NONE) env->raiseException(exc);
    }
    env->raiseValueError(ctx, text);
}

static bool parse_format(proto::ProtoContext* ctx, const std::string& fmt,
                         char& byte_order, std::vector<FmtItem>& items) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    byte_order = '<';  // default
    size_t i = 0;
    if (!fmt.empty()) {
        char c0 = fmt[0];
        if (c0 == '<') { byte_order = '<'; i = 1; }
        else if (c0 == '>' || c0 == '!') { byte_order = '>'; i = 1; }
        else if (c0 == '@' || c0 == '=') { byte_order = '<'; i = 1; }
    }
    int pending_count = 0;
    bool has_count = false;
    while (i < fmt.size()) {
        char c = fmt[i++];
        if (c >= '0' && c <= '9') {
            pending_count = pending_count * 10 + (c - '0');
            has_count = true;
            continue;
        }
        if (c == ' ' || c == '\t') continue;
        int sz = element_size(c);
        if (sz < 0) {
            raise_struct_error(ctx, "bad char in struct format");
            return false;
        }
        int n = has_count ? pending_count : 1;
        // `s` (and `c` is single-byte) treats the count as the byte
        // length of one fixed string, not a repeat.
        if (c == 's') {
            FmtItem it{c, 1, n == 0 ? 0 : n};
            items.push_back(it);
        } else {
            FmtItem it{c, n, sz};
            items.push_back(it);
        }
        pending_count = 0;
        has_count = false;
    }
    return true;
}

static int format_total_size(const std::vector<FmtItem>& items) {
    int total = 0;
    for (const auto& it : items) {
        if (it.code == 's') total += it.size;
        else total += it.count * it.size;
    }
    return total;
}

// ---- Endian-aware integer packing ------------------------------------------

static void pack_uint(char* dst, uint64_t v, int size, char order) {
    if (order == '<') {
        for (int i = 0; i < size; ++i) dst[i] = static_cast<char>((v >> (i * 8)) & 0xFF);
    } else {
        for (int i = 0; i < size; ++i) dst[size - 1 - i] = static_cast<char>((v >> (i * 8)) & 0xFF);
    }
}

static uint64_t unpack_uint(const char* src, int size, char order) {
    uint64_t v = 0;
    if (order == '<') {
        for (int i = 0; i < size; ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(src[i])) << (i * 8);
    } else {
        for (int i = 0; i < size; ++i) v |= static_cast<uint64_t>(static_cast<unsigned char>(src[size - 1 - i])) << (i * 8);
    }
    return v;
}

// ---- Helpers --------------------------------------------------------------

static const proto::ProtoObject* make_bytes_obj(proto::ProtoContext* ctx, const std::string& data) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    proto::ProtoObject* obj = const_cast<proto::ProtoObject*>(ctx->newObject(false));
    if (env && env->getBytesPrototype()) {
        obj = const_cast<proto::ProtoObject*>(obj->addParent(ctx, env->getBytesPrototype()));
        obj = const_cast<proto::ProtoObject*>(obj->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__class__"),
            env->getBytesPrototype()));
    }
    const proto::ProtoByteBuffer* bb = ctx->newByteBuffer(
        data.data(), static_cast<proto::proto_ulong>(data.size()));
    obj = const_cast<proto::ProtoObject*>(obj->setAttribute(ctx,
        env ? env->getDataString() : PythonEnvironment::getInternedString(ctx, "__data__"),
        bb->asObject(ctx)));
    return obj;
}

static bool extract_bytes(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::string& out) {
    if (!obj || obj == PROTO_NONE) return false;
    if (obj->isString(ctx)) {
        obj->asString(ctx)->toUTF8String(ctx, out);
        return true;
    }
    if (const proto::ProtoByteBuffer* bb = obj->asByteBuffer(ctx)) {
        proto::proto_ulong n = bb->getSize(ctx);
        out.assign(bb->getBuffer(ctx), n);
        return true;
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        const proto::ProtoObject* mv = obj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__mv_data__"));
        if (mv && mv != PROTO_NONE && mv != obj) return extract_bytes(ctx, mv, out);
        const proto::ProtoObject* data = obj->getAttribute(ctx, env->getDataString());
        if (data && data != PROTO_NONE && data != obj) {
            if (const proto::ProtoByteBuffer* bb = data->asByteBuffer(ctx)) {
                proto::proto_ulong n = bb->getSize(ctx);
                out.assign(bb->getBuffer(ctx), n);
                return true;
            }
            if (data->isString(ctx)) {
                data->asString(ctx)->toUTF8String(ctx, out);
                return true;
            }
        }
        // array.array and other objects with tobytes(), called bound.
        const proto::ProtoObject* tobytesM = env->getAttribute(ctx, obj,
            PythonEnvironment::getInternedString(ctx, "tobytes"), /*raiseError=*/false);
        const proto::ProtoObject* res = (tobytesM && tobytesM != PROTO_NONE)
            ? env->callObject(tobytesM, {}) : nullptr;
        if (res && res != obj) return extract_bytes(ctx, res, out);
    }
    return false;
}

// ---- Pack -----------------------------------------------------------------


// The integer to pack for code `code` (`size` bytes) from `v`, two's
// complement in `out`.  As CPython: a bool packs as 0/1, any other value
// must be an int or have __index__ ("required argument is not an
// integer"), and it must fit the code's range ("'I' format requires
// 0 <= number <= 4294967295").  Values were truncated silently, so
// pack('!I', 2**32) gave b'\0\0\0\0' and base64's overflow checks never
// fired.
static bool pack_integer(proto::ProtoContext* ctx, const proto::ProtoObject* v, char code, int size, uint64_t& out) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (v == PROTO_TRUE || v == PROTO_FALSE) {
        out = v == PROTO_TRUE ? 1 : 0;
        return true;
    }
    if (!v || !v->isInteger(ctx)) {
        const proto::ProtoObject* idx = (env && v && v != PROTO_NONE && !v->isFloat(ctx))
            ? env->getAttribute(ctx, v, PythonEnvironment::getInternedString(ctx, "__index__"), false) : nullptr;
        const proto::ProtoObject* r = (idx && idx != PROTO_NONE) ? env->callObject(idx, {}) : nullptr;
        if (!r || !r->isInteger(ctx)) {
            raise_struct_error(ctx, "required argument is not an integer");
            return false;
        }
        v = r;
    }
    const bool isSigned = code >= 'a' && code <= 'z';
    const int bits = size * 8;
    auto rangeError = [&]() {
        std::string lo, hi;
        if (isSigned) {
            lo = "-" + std::to_string(1ULL << (bits - 1));
            hi = std::to_string((1ULL << (bits - 1)) - 1);
        } else {
            lo = "0";
            hi = std::to_string(bits == 64 ? ~0ULL : (1ULL << bits) - 1);
        }
        raise_struct_error(ctx, std::string("'") + code + "' format requires " + lo + " <= number <= " + hi);
        return false;
    };
    long long iv = 0;
    try {
        iv = v->asLong(ctx);
    } catch (const proto::ProtoException& overflow) {
        PythonEnvironment::rethrowUnlessOverflow(ctx, overflow);
        // Beyond long long: only an unsigned 64-bit code can hold it.
        if (isSigned || bits != 64 || v->integerSign(ctx) < 0) return rangeError();
        std::string hex;
        v->asIntegerString(ctx, 16)->toUTF8String(ctx, hex);
        if (hex.size() > 16) return rangeError();
        out = std::stoull(hex, nullptr, 16);
        return true;
    }
    if (isSigned) {
        if (bits < 64) {
            const long long lim = 1LL << (bits - 1);
            if (iv < -lim || iv > lim - 1) return rangeError();
        }
    } else {
        if (iv < 0) return rangeError();
        if (bits < 64 && static_cast<unsigned long long>(iv) > (1ULL << bits) - 1) return rangeError();
    }
    out = static_cast<uint64_t>(iv);
    return true;
}

// The float to pack for 'f' or 'd': an int or a float, else
// "required argument is not a float".
static bool pack_float(proto::ProtoContext* ctx, const proto::ProtoObject* v, double& out) {
    if (v && v->isFloat(ctx)) { out = v->asDouble(ctx); return true; }
    if (v == PROTO_TRUE || v == PROTO_FALSE) { out = v == PROTO_TRUE ? 1.0 : 0.0; return true; }
    if (v && v->isInteger(ctx)) {
        try {
            out = static_cast<double>(v->asLong(ctx));
        } catch (const proto::ProtoException& overflow) {
            PythonEnvironment::rethrowUnlessOverflow(ctx, overflow);
            std::string dec;
            v->asIntegerString(ctx, 10)->toUTF8String(ctx, dec);
            out = std::stod(dec);
        }
        return true;
    }
    if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx)) {
        const proto::ProtoObject* f = (v && v != PROTO_NONE)
            ? env->getAttribute(ctx, v, PythonEnvironment::getInternedString(ctx, "__float__"), false) : nullptr;
        const proto::ProtoObject* r = (f && f != PROTO_NONE) ? env->callObject(f, {}) : nullptr;
        if (r && r->isFloat(ctx)) { out = r->asDouble(ctx); return true; }
    }
    raise_struct_error(ctx, "required argument is not a float");
    return false;
}

static const proto::ProtoObject* pack_impl(proto::ProtoContext* ctx,
                                           const std::string& fmt,
                                           const proto::ProtoArguments* values,
                                           proto::proto_ulong values_off) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    char order;
    std::vector<FmtItem> items;
    if (!parse_format(ctx, fmt, order, items)) return nullptr;

    std::string out;
    out.reserve(format_total_size(items));
    proto::proto_ulong vi = values_off;
    proto::proto_ulong n_values = values ? values->getSize(ctx) : 0;

    auto need_value = [&](const proto::ProtoObject*& dst) -> bool {
        if (vi >= n_values) {
            if (env) env->raiseTypeError(ctx, "struct.pack: not enough arguments");
            return false;
        }
        dst = values->getAt(ctx, static_cast<int>(vi++));
        return true;
    };

    for (const auto& it : items) {
        if (it.code == 'x') {
            for (int k = 0; k < it.count; ++k) out += '\0';
            continue;
        }
        if (it.code == 's') {
            const proto::ProtoObject* v = nullptr;
            if (!need_value(v)) return nullptr;
            std::string sv;
            extract_bytes(ctx, v, sv);
            if (sv.size() < static_cast<size_t>(it.size)) {
                out += sv;
                out.append(it.size - sv.size(), '\0');
            } else {
                out.append(sv.data(), it.size);
            }
            continue;
        }
        for (int k = 0; k < it.count; ++k) {
            const proto::ProtoObject* v = nullptr;
            if (!need_value(v)) return nullptr;
            char buf[8];
            switch (it.code) {
                case 'b': case 'B': case 'h': case 'H':
                case 'i': case 'I': case 'l': case 'L':
                case 'q': case 'Q': {
                    uint64_t uv = 0;
                    if (!pack_integer(ctx, v, it.code, it.size, uv)) return nullptr;
                    pack_uint(buf, uv, it.size, order);
                    out.append(buf, it.size);
                    break;
                }
                case 'f': {
                    double dv = 0.0;
                    if (!pack_float(ctx, v, dv)) return nullptr;
                    float fv = static_cast<float>(dv);
                    uint32_t bits;
                    std::memcpy(&bits, &fv, 4);
                    pack_uint(buf, bits, 4, order);
                    out.append(buf, 4);
                    break;
                }
                case 'd': {
                    double dv = 0.0;
                    if (!pack_float(ctx, v, dv)) return nullptr;
                    uint64_t bits;
                    std::memcpy(&bits, &dv, 8);
                    pack_uint(buf, bits, 8, order);
                    out.append(buf, 8);
                    break;
                }
                case 'c': {
                    std::string sv;
                    extract_bytes(ctx, v, sv);
                    out += (sv.empty() ? '\0' : sv[0]);
                    break;
                }
                case '?': {
                    bool tv = (v == PROTO_TRUE) || (v->isInteger(ctx) && v->asLong(ctx) != 0);
                    out += tv ? '\x01' : '\x00';
                    break;
                }
            }
        }
    }
    return make_bytes_obj(ctx, out);
}

// ---- Unpack ---------------------------------------------------------------

static const proto::ProtoObject* sign_extend(proto::ProtoContext* ctx,
                                             uint64_t v, int size, char code) {
    bool is_signed = (code == 'b' || code == 'h' || code == 'i' ||
                      code == 'l' || code == 'q');
    if (is_signed) {
        if (size < 8) {
            uint64_t sign_bit = 1ULL << (size * 8 - 1);
            if (v & sign_bit) {
                uint64_t mask = ~((1ULL << (size * 8)) - 1);
                v |= mask;
            }
        }
        return ctx->fromInteger(static_cast<long long>(v));
    }
    return ctx->fromInteger(static_cast<long long>(v));
}

static const proto::ProtoObject* unpack_impl(proto::ProtoContext* ctx,
                                             const std::string& fmt,
                                             const std::string& buf,
                                             proto::proto_ulong offset) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    char order;
    std::vector<FmtItem> items;
    if (!parse_format(ctx, fmt, order, items)) return nullptr;

    int total = format_total_size(items);
    if (buf.size() < offset + static_cast<size_t>(total)) {
        raise_struct_error(ctx, "unpack requires a buffer of " + std::to_string(total) + " bytes");
        return nullptr;
    }

    const proto::ProtoList* result = ctx->newList();
    size_t i = offset;
    for (const auto& it : items) {
        if (it.code == 'x') { i += it.count; continue; }
        if (it.code == 's') {
            std::string s(buf.data() + i, it.size);
            result = result->appendLast(ctx, make_bytes_obj(ctx, s));
            i += it.size;
            continue;
        }
        for (int k = 0; k < it.count; ++k) {
            switch (it.code) {
                case 'b': case 'B': case 'h': case 'H':
                case 'i': case 'I': case 'l': case 'L':
                case 'q': case 'Q': {
                    uint64_t v = unpack_uint(buf.data() + i, it.size, order);
                    result = result->appendLast(ctx, sign_extend(ctx, v, it.size, it.code));
                    break;
                }
                case 'f': {
                    uint32_t bits = static_cast<uint32_t>(unpack_uint(buf.data() + i, 4, order));
                    float fv;
                    std::memcpy(&fv, &bits, 4);
                    result = result->appendLast(ctx, ctx->fromDouble(static_cast<double>(fv)));
                    break;
                }
                case 'd': {
                    uint64_t bits = unpack_uint(buf.data() + i, 8, order);
                    double dv;
                    std::memcpy(&dv, &bits, 8);
                    result = result->appendLast(ctx, ctx->fromDouble(dv));
                    break;
                }
                case 'c': {
                    std::string s(buf.data() + i, 1);
                    result = result->appendLast(ctx, make_bytes_obj(ctx, s));
                    break;
                }
                case '?': {
                    result = result->appendLast(ctx,
                        buf[i] ? PROTO_TRUE : PROTO_FALSE);
                    break;
                }
            }
            i += it.size;
        }
    }
    // Return as tuple to match CPython.
    if (env) return env->newTuple(result);
    return result->asObject(ctx);
}

// ---- Module-level entry points --------------------------------------------

const proto::ProtoObject* py_calcsize(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    if (!args || args->getSize(ctx) < 1) return ctx->fromInteger(0);
    std::string fmt;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), fmt)) return ctx->fromInteger(0);
    char order;
    std::vector<FmtItem> items;
    if (!parse_format(ctx, fmt, order, items)) return nullptr;
    return ctx->fromInteger(format_total_size(items));
}

const proto::ProtoObject* py_pack(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    if (!args || args->getSize(ctx) < 1) return PythonEnvironment::getInternedString(ctx, "")->asObject(ctx);
    std::string fmt;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), fmt)) return PROTO_NONE;
    return pack_impl(ctx, fmt, args, 1);
}

const proto::ProtoObject* py_unpack(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    if (!args || args->getSize(ctx) < 2) return PythonEnvironment::wrapList(ctx, ctx->newList());
    std::string fmt;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), fmt)) return PythonEnvironment::wrapList(ctx, ctx->newList());
    std::string buf;
    if (!extract_bytes(ctx, args->getAt(ctx, 1), buf)) return PythonEnvironment::wrapList(ctx, ctx->newList());
    return unpack_impl(ctx, fmt, buf, 0);
}

const proto::ProtoObject* py_unpack_from(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    if (!args || args->getSize(ctx) < 2) return PythonEnvironment::wrapList(ctx, ctx->newList());
    std::string fmt;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), fmt)) return PythonEnvironment::wrapList(ctx, ctx->newList());
    std::string buf;
    if (!extract_bytes(ctx, args->getAt(ctx, 1), buf)) return PythonEnvironment::wrapList(ctx, ctx->newList());
    long long offset = 0;
    if (args->getSize(ctx) >= 3) {
        const proto::ProtoObject* o = args->getAt(ctx, 2);
        if (o && o->isInteger(ctx)) offset = o->asLong(ctx);
    }
    return unpack_impl(ctx, fmt, buf, static_cast<proto::proto_ulong>(offset));
}

const proto::ProtoObject* py_iter_unpack(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    if (!args || args->getSize(ctx) < 2) return PythonEnvironment::wrapList(ctx, ctx->newList());
    std::string fmt;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), fmt)) return PythonEnvironment::wrapList(ctx, ctx->newList());
    std::string buf;
    if (!extract_bytes(ctx, args->getAt(ctx, 1), buf)) return PythonEnvironment::wrapList(ctx, ctx->newList());
    char order;
    std::vector<FmtItem> items;
    if (!parse_format(ctx, fmt, order, items)) return nullptr;
    int chunk = format_total_size(items);
    if (chunk <= 0) return PythonEnvironment::wrapList(ctx, ctx->newList());
    const proto::ProtoList* result = ctx->newList();
    for (size_t off = 0; off + chunk <= buf.size(); off += chunk) {
        const proto::ProtoObject* tup = unpack_impl(ctx, fmt, buf, static_cast<proto::proto_ulong>(off));
        if (!tup) return nullptr;
        result = result->appendLast(ctx, tup);
    }
    return PythonEnvironment::wrapList(ctx, result);
}

const proto::ProtoObject* py_clearcache(proto::ProtoContext*,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    return PROTO_NONE;
}

// ---- Struct class ---------------------------------------------------------

const proto::ProtoObject* py_struct_new(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* cls = args->getAt(ctx, 0);
    proto::ProtoObject* inst = const_cast<proto::ProtoObject*>(cls->newChild(ctx, true));
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx,
        env ? env->getClassString() : PythonEnvironment::getInternedString(ctx, "__class__"), cls));
    if (args->getSize(ctx) >= 2) {
        std::string fmt;
        extract_bytes(ctx, args->getAt(ctx, 1), fmt);
        inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "format"),
            PythonEnvironment::getInternedString(ctx, fmt.c_str())->asObject(ctx)));
        char order;
        std::vector<FmtItem> items;
        if (parse_format(ctx, fmt, order, items)) {
            inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx,
                PythonEnvironment::getInternedString(ctx, "size"),
                ctx->fromInteger(format_total_size(items))));
        }
    }
    return inst;
}

const proto::ProtoObject* py_struct_pack(proto::ProtoContext* ctx,
    const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    const proto::ProtoObject* fmtObj = self->getAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "format"));
    std::string fmt;
    if (fmtObj && fmtObj->isString(ctx)) fmtObj->asString(ctx)->toUTF8String(ctx, fmt);
    return pack_impl(ctx, fmt, args, 0);
}

const proto::ProtoObject* py_struct_unpack(proto::ProtoContext* ctx,
    const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) {
    if (!args || args->getSize(ctx) < 1) return PythonEnvironment::wrapList(ctx, ctx->newList());
    const proto::ProtoObject* fmtObj = self->getAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "format"));
    std::string fmt;
    if (fmtObj && fmtObj->isString(ctx)) fmtObj->asString(ctx)->toUTF8String(ctx, fmt);
    std::string buf;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), buf)) return PythonEnvironment::wrapList(ctx, ctx->newList());
    return unpack_impl(ctx, fmt, buf, 0);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    proto::ProtoObject* mod = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "calcsize"),
        ctx->fromMethod(mod, py_calcsize));
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pack"),
        ctx->fromMethod(mod, py_pack));
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "unpack"),
        ctx->fromMethod(mod, py_unpack));
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "iter_unpack"),
        ctx->fromMethod(mod, py_iter_unpack));
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pack_into"),
        ctx->fromMethod(mod, py_pack));
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "unpack_from"),
        ctx->fromMethod(mod, py_unpack_from));
    // struct.error: class error(Exception), __module__ 'struct', as in CPython.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* errorCls = PROTO_NONE;
    if (env) {
        const proto::ProtoObject* typeCls = env->lookupName("type");
        const proto::ProtoObject* excCls = env->lookupName("Exception");
        const proto::ProtoObject* dictCls = env->lookupName("dict");
        if (typeCls && excCls && dictCls) {
            const proto::ProtoObject* bases = env->newTuple(ctx->newList()->appendLast(ctx, excCls));
            const proto::ProtoObject* ns = env->callObject(dictCls, {});
            errorCls = env->callObject(typeCls,
                {PythonEnvironment::getInternedString(ctx, "error")->asObject(ctx), bases, ns});
            if (errorCls && errorCls != PROTO_NONE)
                env->setAttribute(ctx, errorCls, PythonEnvironment::getInternedString(ctx, "__module__"),
                                  PythonEnvironment::getInternedString(ctx, "struct")->asObject(ctx));
        }
        if (!errorCls || errorCls == PROTO_NONE) errorCls = env->lookupName("ValueError");
    }
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "error"), errorCls);

    // Struct class: real impl with format-parsing __new__ that
    // pre-computes .size, plus pack/unpack methods that read .format.
    proto::ProtoObject* structClass = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    if (env && env->getTypePrototype()) {
        structClass = const_cast<proto::ProtoObject*>(structClass->addParent(ctx, env->getTypePrototype()));
    }
    structClass->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__name__"),
        PythonEnvironment::getInternedString(ctx, "Struct")->asObject(ctx));
    structClass->setAttribute(ctx,
        env ? env->getNewString() : PythonEnvironment::getInternedString(ctx, "__new__"),
        ctx->fromMethod(nullptr, py_struct_new));
    structClass->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pack"),
        ctx->fromMethod(nullptr, py_struct_pack));
    structClass->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "unpack"),
        ctx->fromMethod(nullptr, py_struct_unpack));
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Struct"), structClass);
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_clearcache"),
        ctx->fromMethod(mod, py_clearcache));

    return mod;
}

} // namespace struct_module
} // namespace protoPython
