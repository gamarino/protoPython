// _codecs_native: the code-unit loops of the UTF-16, UTF-32 and charmap
// codecs, for lib/python3.14/_codecs.py.
//
// Each function converts from a start position up to the first position it
// cannot convert on its own (an error, or an incomplete unit at the end) and
// returns (result, stop). The Python side reports that position through the
// error-handler protocol and calls again after it, so the handlers see what
// CPython's codecs give them while clean input never leaves native code.

#include <protoPython/CodecsNative.h>
#include <protoPython/PythonEnvironment.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace protoPython {
namespace codecs_native {

namespace {

using PO = proto::ProtoObject;

[[noreturn]] void raiseType(proto::ProtoContext* ctx, const std::string& msg) {
    if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx)) env->raiseTypeError(ctx, msg);
    throw std::runtime_error(msg);
}

const PO* argAt(proto::ProtoContext* ctx, const proto::ProtoArguments* args, int i, int count, const char* fn) {
    if (!args || static_cast<int>(args->getSize(ctx)) != count)
        raiseType(ctx, std::string(fn) + "() takes exactly " + std::to_string(count) + " arguments");
    return args->getAt(ctx, i);
}

long long intArg(proto::ProtoContext* ctx, const PO* v, const char* fn) {
    if (v == PROTO_TRUE) return 1;
    if (v == PROTO_FALSE) return 0;
    if (!v || !v->isInteger(ctx)) raiseType(ctx, std::string(fn) + "(): an integer is required");
    return v->asLong(ctx);
}

// The bytes of a bytes-like argument: a view of a byte buffer when the
// object holds one (no copy), else a copy in `owned`.
struct Bytes {
    const char* data = nullptr;
    size_t size = 0;
    std::string owned;
};

void bytesArg(proto::ProtoContext* ctx, const PO* v, Bytes& out, const char* fn) {
    const PO* d = v;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (d && !d->isByteBuffer(ctx) && (reinterpret_cast<uintptr_t>(d) & 0x3F) == 0 && !d->isString(ctx) && env) {
        const PO* inner = d->getAttribute(ctx, env->getDataString());
        if (inner && inner->isByteBuffer(ctx) && env->getBytesPrototype()
            && d->isInstanceOf(ctx, env->getBytesPrototype()) == PROTO_TRUE)
            d = inner;
    }
    if (d && d->isByteBuffer(ctx)) {
        const proto::ProtoByteBuffer* bb = d->asByteBuffer(ctx);
        out.data = bb->getBuffer(ctx);
        out.size = bb->getSize(ctx);
        return;
    }
    if (!PythonEnvironment::bytesLikeContent(ctx, v, out.owned))
        raiseType(ctx, std::string(fn) + "(): a bytes-like object is required");
    out.data = out.owned.data();
    out.size = out.owned.size();
}

// The code points of a str (protoCore keeps strings as UTF-8, a lone
// surrogate as its three-byte form).
std::vector<uint32_t> codePoints(proto::ProtoContext* ctx, const PO* v, const char* fn) {
    if (!v || !v->isString(ctx)) raiseType(ctx, std::string(fn) + "(): argument must be str");
    std::string u8;
    v->asString(ctx)->toUTF8String(ctx, u8);
    std::vector<uint32_t> cps;
    cps.reserve(u8.size());
    for (size_t i = 0; i < u8.size();) {
        const unsigned char c = static_cast<unsigned char>(u8[i]);
        uint32_t cp;
        size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; len = 3; }
        else { cp = c & 0x07; len = 4; }
        for (size_t k = 1; k < len && i + k < u8.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(u8[i + k]) & 0x3F);
        cps.push_back(cp);
        i += len;
    }
    return cps;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void putUnit(std::string& out, uint32_t unit, int size, bool little) {
    for (int k = 0; k < size; ++k) {
        const int shift = little ? 8 * k : 8 * (size - 1 - k);
        out.push_back(static_cast<char>((unit >> shift) & 0xFF));
    }
}

uint32_t getUnit(const char* p, int size, bool little) {
    uint32_t u = 0;
    for (int k = 0; k < size; ++k) {
        const uint32_t b = static_cast<unsigned char>(p[k]);
        u |= little ? b << (8 * k) : b << (8 * (size - 1 - k));
    }
    return u;
}

const PO* pair(proto::ProtoContext* ctx, const PO* a, long long stop) {
    const proto::ProtoList* l = ctx->newList()->appendLast(ctx, a)->appendLast(ctx, ctx->fromInteger(stop));
    return ctx->newTupleFromList(l)->asObject(ctx);
}

#define CODEC_FN(name)                                                                              \
    const PO* name(proto::ProtoContext* ctx, const PO*, const proto::ParentLink*,                  \
                   const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)

// utf_encode(s, start, size, little) -> (bytes, stop): s[start:] as UTF-16
// (size 2) or UTF-32 (size 4) code units, up to the first surrogate.
CODEC_FN(py_utf_encode) {
    const std::vector<uint32_t> cps = codePoints(ctx, argAt(ctx, args, 0, 4, "utf_encode"), "utf_encode");
    long long i = intArg(ctx, args->getAt(ctx, 1), "utf_encode");
    const int size = static_cast<int>(intArg(ctx, args->getAt(ctx, 2), "utf_encode"));
    const bool little = intArg(ctx, args->getAt(ctx, 3), "utf_encode") != 0;
    if (size != 2 && size != 4) raiseType(ctx, "utf_encode(): size must be 2 or 4");
    const long long n = static_cast<long long>(cps.size());
    if (i < 0) i = 0;
    std::string out;
    out.reserve(static_cast<size_t>(n > i ? (n - i) * size : 0));
    for (; i < n; ++i) {
        const uint32_t cp = cps[static_cast<size_t>(i)];
        if (cp >= 0xD800 && cp <= 0xDFFF) break;
        if (size == 4 || cp < 0x10000) {
            putUnit(out, cp, size, little);
        } else {
            const uint32_t v = cp - 0x10000;
            putUnit(out, 0xD800 | (v >> 10), 2, little);
            putUnit(out, 0xDC00 | (v & 0x3FF), 2, little);
        }
    }
    return pair(ctx, PythonEnvironment::newBytes(ctx, out.data(), out.size()), i);
}

// utf16_decode(data, start, little) -> (str, stop): the units from `start`
// up to the first one that is not a valid character or pair, or that is
// incomplete at the end.
CODEC_FN(py_utf16_decode) {
    Bytes b;
    bytesArg(ctx, argAt(ctx, args, 0, 3, "utf16_decode"), b, "utf16_decode");
    long long i = intArg(ctx, args->getAt(ctx, 1), "utf16_decode");
    const bool little = intArg(ctx, args->getAt(ctx, 2), "utf16_decode") != 0;
    const long long n = static_cast<long long>(b.size);
    if (i < 0) i = 0;
    std::string out;
    out.reserve(static_cast<size_t>(n > i ? n - i : 0));
    while (n - i >= 2) {
        const uint32_t u = getUnit(b.data + i, 2, little);
        if (u < 0xD800 || u > 0xDFFF) {
            appendUtf8(out, u);
            i += 2;
            continue;
        }
        if (u >= 0xDC00 || n - i < 4) break;
        const uint32_t u2 = getUnit(b.data + i + 2, 2, little);
        if (u2 < 0xDC00 || u2 > 0xDFFF) break;
        appendUtf8(out, 0x10000 + ((u - 0xD800) << 10) + (u2 - 0xDC00));
        i += 4;
    }
    return pair(ctx, PythonEnvironment::newStr(ctx, out), i);
}

// utf32_decode(data, start, little) -> (str, stop).
CODEC_FN(py_utf32_decode) {
    Bytes b;
    bytesArg(ctx, argAt(ctx, args, 0, 3, "utf32_decode"), b, "utf32_decode");
    long long i = intArg(ctx, args->getAt(ctx, 1), "utf32_decode");
    const bool little = intArg(ctx, args->getAt(ctx, 2), "utf32_decode") != 0;
    const long long n = static_cast<long long>(b.size);
    if (i < 0) i = 0;
    std::string out;
    out.reserve(static_cast<size_t>(n > i ? n - i : 0));
    while (n - i >= 4) {
        const uint32_t cp = getUnit(b.data + i, 4, little);
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) break;
        appendUtf8(out, cp);
        i += 4;
    }
    return pair(ctx, PythonEnvironment::newStr(ctx, out), i);
}

// charmap_decode(data, start, table) -> (str, stop): bytes through a
// decoding table (a str), up to a byte the table leaves undefined (U+FFFE
// or past its end).
CODEC_FN(py_charmap_decode) {
    Bytes b;
    bytesArg(ctx, argAt(ctx, args, 0, 3, "charmap_decode"), b, "charmap_decode");
    long long i = intArg(ctx, args->getAt(ctx, 1), "charmap_decode");
    const std::vector<uint32_t> table = codePoints(ctx, args->getAt(ctx, 2), "charmap_decode");
    const long long n = static_cast<long long>(b.size);
    if (i < 0) i = 0;
    std::string out;
    out.reserve(static_cast<size_t>(n > i ? n - i : 0));
    for (; i < n; ++i) {
        const size_t byte = static_cast<unsigned char>(b.data[i]);
        if (byte >= table.size() || table[byte] == 0xFFFE) break;
        appendUtf8(out, table[byte]);
    }
    return pair(ctx, PythonEnvironment::newStr(ctx, out), i);
}

// charmap_encode(s, start, table) -> (bytes, stop): characters through the
// inverse of a decoding table (as charmap_build builds it: a character the
// table holds twice maps to its last byte), up to one it does not hold.
CODEC_FN(py_charmap_encode) {
    const std::vector<uint32_t> cps = codePoints(ctx, argAt(ctx, args, 0, 3, "charmap_encode"), "charmap_encode");
    long long i = intArg(ctx, args->getAt(ctx, 1), "charmap_encode");
    const std::vector<uint32_t> table = codePoints(ctx, args->getAt(ctx, 2), "charmap_encode");
    if (table.size() > 256) raiseType(ctx, "charmap_encode(): the table maps at most 256 bytes");
    std::unordered_map<uint32_t, unsigned char> inverse;
    inverse.reserve(table.size());
    for (size_t k = 0; k < table.size(); ++k) inverse[table[k]] = static_cast<unsigned char>(k);
    const long long n = static_cast<long long>(cps.size());
    if (i < 0) i = 0;
    std::string out;
    out.reserve(static_cast<size_t>(n > i ? n - i : 0));
    for (; i < n; ++i) {
        auto it = inverse.find(cps[static_cast<size_t>(i)]);
        if (it == inverse.end()) break;
        out.push_back(static_cast<char>(it->second));
    }
    return pair(ctx, PythonEnvironment::newBytes(ctx, out.data(), out.size()), i);
}

// charmap_encode_dict(s, start, mapping) -> (bytes, stop): characters
// through a dict encoding map (codecs.make_encoding_map's: code point to a
// byte value or a bytes object), up to one the map does not hold or maps
// to None. `mapping` is an exact dict; the caller handles the rest.
CODEC_FN(py_charmap_encode_dict) {
    const std::vector<uint32_t> cps = codePoints(ctx, argAt(ctx, args, 0, 3, "charmap_encode_dict"), "charmap_encode_dict");
    long long i = intArg(ctx, args->getAt(ctx, 1), "charmap_encode_dict");
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const PO* mapping = args->getAt(ctx, 2);
    const PO* data = env && mapping ? mapping->getAttribute(ctx, env->getDataString()) : nullptr;
    const proto::ProtoSparseList* items = data ? data->asSparseList(ctx) : nullptr;
    if (!items) raiseType(ctx, "charmap_encode_dict(): mapping must be a dict");
    const long long n = static_cast<long long>(cps.size());
    if (i < 0) i = 0;
    std::string out;
    out.reserve(static_cast<size_t>(n > i ? n - i : 0));
    std::string bytesValue;
    for (; i < n; ++i) {
        const PO* key = ctx->fromInteger(static_cast<long long>(cps[static_cast<size_t>(i)]));
        const PO* v = PythonEnvironment::dictDataGet(ctx, items, key);
        if (!v || v == PROTO_NONE) break;
        if (v->isInteger(ctx)) {
            const long long b = v->asLong(ctx);
            if (b < 0 || b > 255) break;  // the caller raises CPython's TypeError
            out.push_back(static_cast<char>(b));
        } else if (PythonEnvironment::bytesLikeContent(ctx, v, bytesValue)) {
            out += bytesValue;
        } else {
            break;
        }
    }
    return pair(ctx, PythonEnvironment::newBytes(ctx, out.data(), out.size()), i);
}

// UTF-7 (RFC 2152), as CPython's encoder: direct characters as themselves,
// everything else in base64 shift sequences of UTF-16 units.
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool utf7Direct(uint32_t cp) {
    if (cp == 0 || cp >= 128) return false;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) return true;
    static const char extra[] = "'(),-./:?!\"#$%&*;<=>@[]^_`{|} \t\r\n";
    for (const char* p = extra; *p; ++p)
        if (static_cast<uint32_t>(static_cast<unsigned char>(*p)) == cp) return true;
    return false;
}

// utf7_encode(s) -> bytes.
CODEC_FN(py_utf7_encode) {
    const std::vector<uint32_t> cps = codePoints(ctx, argAt(ctx, args, 0, 1, "utf7_encode"), "utf7_encode");
    std::string out;
    bool inShift = false;
    int bits = 0;
    uint64_t buf = 0;
    auto put = [&](uint32_t value) {
        bits += 16;
        buf = (buf << 16) | value;
        while (bits >= 6) {
            out.push_back(kB64[(buf >> (bits - 6)) & 0x3F]);
            bits -= 6;
        }
        buf &= (1ULL << bits) - 1;
    };
    for (uint32_t cp : cps) {
        if (inShift) {
            if (utf7Direct(cp)) {
                if (bits) {
                    out.push_back(kB64[(buf << (6 - bits)) & 0x3F]);
                    buf = 0;
                    bits = 0;
                }
                inShift = false;
                if (b64Value(static_cast<unsigned char>(cp)) >= 0 || cp == '-') out.push_back('-');
                out.push_back(static_cast<char>(cp));
                continue;
            }
        } else {
            if (cp == '+') { out += "+-"; continue; }
            if (utf7Direct(cp)) { out.push_back(static_cast<char>(cp)); continue; }
            out.push_back('+');
            inShift = true;
        }
        if (cp >= 0x10000) {
            const uint32_t v = cp - 0x10000;
            put(0xD800 | (v >> 10));
            put(0xDC00 | (v & 0x3FF));
        } else {
            put(cp);
        }
    }
    if (bits) out.push_back(kB64[(buf << (6 - bits)) & 0x3F]);
    if (inShift) out.push_back('-');
    return PythonEnvironment::newBytes(ctx, out.data(), out.size());
}

// utf7_decode(data) -> str, or None when the input holds anything the
// error-handler protocol must see, or ends inside a shift sequence: the
// Python decoder then runs on it.
CODEC_FN(py_utf7_decode) {
    Bytes b;
    bytesArg(ctx, argAt(ctx, args, 0, 1, "utf7_decode"), b, "utf7_decode");
    std::string out;
    out.reserve(b.size);
    bool inShift = false;
    int bits = 0;
    uint64_t buf = 0;
    uint32_t surrogate = 0;
    for (size_t i = 0; i < b.size;) {
        const unsigned char ch = static_cast<unsigned char>(b.data[i]);
        if (inShift) {
            const int v = b64Value(ch);
            if (v >= 0) {
                buf = (buf << 6) | static_cast<uint64_t>(v);
                bits += 6;
                ++i;
                if (bits >= 16) {
                    const uint32_t unit = static_cast<uint32_t>(buf >> (bits - 16));
                    bits -= 16;
                    buf &= (1ULL << bits) - 1;
                    if (surrogate) {
                        if (unit >= 0xDC00 && unit <= 0xDFFF) {
                            appendUtf8(out, 0x10000 + ((surrogate - 0xD800) << 10) + (unit - 0xDC00));
                            surrogate = 0;
                            continue;
                        }
                        appendUtf8(out, surrogate);
                        surrogate = 0;
                    }
                    if (unit >= 0xD800 && unit <= 0xDBFF) surrogate = unit;
                    else appendUtf8(out, unit);
                }
                continue;
            }
            inShift = false;
            if (bits >= 6 || (bits > 0 && buf != 0)) return PROTO_NONE;
            if (surrogate && ch <= 127 && ch != '+') appendUtf8(out, surrogate);
            surrogate = 0;
            if (ch == '-') ++i;
        } else if (ch == '+') {
            ++i;
            if (i < b.size && b.data[i] == '-') { ++i; out.push_back('+'); }
            else if (i < b.size && b64Value(static_cast<unsigned char>(b.data[i])) < 0) return PROTO_NONE;
            else { inShift = true; surrogate = 0; bits = 0; buf = 0; }
        } else if (ch <= 127) {
            out.push_back(static_cast<char>(ch));
            ++i;
        } else {
            return PROTO_NONE;
        }
    }
    if (inShift) return PROTO_NONE;
    return PythonEnvironment::newStr(ctx, out);
}

} // namespace

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    proto::ProtoObject* mod = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    auto fn = [&](const char* name, proto::ProtoMethod m) {
        mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, name), ctx->fromMethod(mod, m));
    };
    fn("utf_encode", py_utf_encode);
    fn("utf16_decode", py_utf16_decode);
    fn("utf32_decode", py_utf32_decode);
    fn("charmap_decode", py_charmap_decode);
    fn("charmap_encode", py_charmap_encode);
    fn("charmap_encode_dict", py_charmap_encode_dict);
    fn("utf7_encode", py_utf7_encode);
    fn("utf7_decode", py_utf7_decode);
    mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__name__"),
                      PythonEnvironment::getInternedString(ctx, "_codecs_native")->asObject(ctx));
    return mod;
}

} // namespace codecs_native
} // namespace protoPython
