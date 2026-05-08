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
            if (env) env->raiseValueError(ctx,
                PythonEnvironment::getInternedString(ctx,
                    "bad char in struct format")->asObject(ctx));
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
        data.data(), static_cast<unsigned long>(data.size()));
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
        unsigned long n = bb->getSize(ctx);
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
                unsigned long n = bb->getSize(ctx);
                out.assign(bb->getBuffer(ctx), n);
                return true;
            }
            if (data->isString(ctx)) {
                data->asString(ctx)->toUTF8String(ctx, out);
                return true;
            }
        }
        const proto::ProtoObject* underData = obj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "_data"));
        if (underData && underData != PROTO_NONE && underData != obj) {
            return extract_bytes(ctx, underData, out);
        }
        const proto::ProtoObject* tobytesM = obj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "tobytes"));
        if (tobytesM && tobytesM->asMethod(ctx)) {
            const proto::ProtoObject* res = tobytesM->asMethod(ctx)(ctx,
                const_cast<proto::ProtoObject*>(obj),
                nullptr, ctx->newList(), nullptr);
            if (res && res != obj) return extract_bytes(ctx, res, out);
        }
    }
    return false;
}

// ---- Pack -----------------------------------------------------------------

static const proto::ProtoObject* pack_impl(proto::ProtoContext* ctx,
                                           const std::string& fmt,
                                           const proto::ProtoList* values,
                                           unsigned long values_off) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    char order;
    std::vector<FmtItem> items;
    if (!parse_format(ctx, fmt, order, items)) return nullptr;

    std::string out;
    out.reserve(format_total_size(items));
    unsigned long vi = values_off;
    unsigned long n_values = values ? values->getSize(ctx) : 0;

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
                    long long iv = v->isInteger(ctx) ? v->asLong(ctx) : 0;
                    pack_uint(buf, static_cast<uint64_t>(iv), it.size, order);
                    out.append(buf, it.size);
                    break;
                }
                case 'f': {
                    double dv = v->isInteger(ctx) ? static_cast<double>(v->asLong(ctx))
                              : v->isFloat(ctx)   ? v->asDouble(ctx)
                              : 0.0;
                    float fv = static_cast<float>(dv);
                    uint32_t bits;
                    std::memcpy(&bits, &fv, 4);
                    pack_uint(buf, bits, 4, order);
                    out.append(buf, 4);
                    break;
                }
                case 'd': {
                    double dv = v->isInteger(ctx) ? static_cast<double>(v->asLong(ctx))
                              : v->isFloat(ctx)   ? v->asDouble(ctx)
                              : 0.0;
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
                                             unsigned long offset) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    char order;
    std::vector<FmtItem> items;
    if (!parse_format(ctx, fmt, order, items)) return nullptr;

    int total = format_total_size(items);
    if (buf.size() < offset + static_cast<size_t>(total)) {
        if (env) env->raiseValueError(ctx,
            PythonEnvironment::getInternedString(ctx,
                "unpack requires a buffer of correct size")->asObject(ctx));
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
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
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
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PythonEnvironment::getInternedString(ctx, "")->asObject(ctx);
    std::string fmt;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), fmt)) return PROTO_NONE;
    return pack_impl(ctx, fmt, args, 1);
}

const proto::ProtoObject* py_unpack(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 2) return ctx->newList()->asObject(ctx);
    std::string fmt;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), fmt)) return ctx->newList()->asObject(ctx);
    std::string buf;
    if (!extract_bytes(ctx, args->getAt(ctx, 1), buf)) return ctx->newList()->asObject(ctx);
    return unpack_impl(ctx, fmt, buf, 0);
}

const proto::ProtoObject* py_unpack_from(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 2) return ctx->newList()->asObject(ctx);
    std::string fmt;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), fmt)) return ctx->newList()->asObject(ctx);
    std::string buf;
    if (!extract_bytes(ctx, args->getAt(ctx, 1), buf)) return ctx->newList()->asObject(ctx);
    long long offset = 0;
    if (args->getSize(ctx) >= 3) {
        const proto::ProtoObject* o = args->getAt(ctx, 2);
        if (o && o->isInteger(ctx)) offset = o->asLong(ctx);
    }
    return unpack_impl(ctx, fmt, buf, static_cast<unsigned long>(offset));
}

const proto::ProtoObject* py_iter_unpack(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 2) return ctx->newList()->asObject(ctx);
    std::string fmt;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), fmt)) return ctx->newList()->asObject(ctx);
    std::string buf;
    if (!extract_bytes(ctx, args->getAt(ctx, 1), buf)) return ctx->newList()->asObject(ctx);
    char order;
    std::vector<FmtItem> items;
    if (!parse_format(ctx, fmt, order, items)) return nullptr;
    int chunk = format_total_size(items);
    if (chunk <= 0) return ctx->newList()->asObject(ctx);
    const proto::ProtoList* result = ctx->newList();
    for (size_t off = 0; off + chunk <= buf.size(); off += chunk) {
        const proto::ProtoObject* tup = unpack_impl(ctx, fmt, buf, static_cast<unsigned long>(off));
        if (!tup) return nullptr;
        result = result->appendLast(ctx, tup);
    }
    return result->asObject(ctx);
}

const proto::ProtoObject* py_clearcache(proto::ProtoContext*,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

// ---- Struct class ---------------------------------------------------------

const proto::ProtoObject* py_struct_new(proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
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
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    const proto::ProtoObject* fmtObj = self->getAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "format"));
    std::string fmt;
    if (fmtObj && fmtObj->isString(ctx)) fmtObj->asString(ctx)->toUTF8String(ctx, fmt);
    return pack_impl(ctx, fmt, args, 0);
}

const proto::ProtoObject* py_struct_unpack(proto::ProtoContext* ctx,
    const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return ctx->newList()->asObject(ctx);
    const proto::ProtoObject* fmtObj = self->getAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "format"));
    std::string fmt;
    if (fmtObj && fmtObj->isString(ctx)) fmtObj->asString(ctx)->toUTF8String(ctx, fmt);
    std::string buf;
    if (!extract_bytes(ctx, args->getAt(ctx, 0), buf)) return ctx->newList()->asObject(ctx);
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
    // struct.error aliases ValueError so `except struct.error` catches
    // our ValueErrors. Real CPython makes it a subclass; for our use
    // it's enough that the exception type is in the catch chain.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "error"),
        env ? env->lookupName("ValueError") : PROTO_NONE);

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
