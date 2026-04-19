// BinasciiModule.cpp — Native implementation of Python's binascii module.
//
// Provides base64 and hex encoding/decoding functions used by base64.py,
// email, and similar standard library modules.

#include <protoCore.h>
#include <protoPython/PythonEnvironment.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>

namespace protoPython {
namespace binascii {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string obj_to_bytes(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return "";
    if (obj->isString(ctx)) {
        std::string s;
        obj->asString(ctx)->toUTF8String(ctx, s);
        return s;
    }
    // bytes/bytearray stored as string __data__
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        const proto::ProtoObject* data = obj->getAttribute(ctx, env->getDataString());
        if (data && data != PROTO_NONE && data->isString(ctx)) {
            std::string s;
            data->asString(ctx)->toUTF8String(ctx, s);
            return s;
        }
    }
    return "";
}

static const proto::ProtoObject* make_bytes(proto::ProtoContext* ctx, const std::string& data) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    proto::ProtoObject* obj = const_cast<proto::ProtoObject*>(ctx->newObject(false));
    if (env && env->getBytesPrototype()) {
        obj = const_cast<proto::ProtoObject*>(obj->addParent(ctx, env->getBytesPrototype()));
    }
    obj = const_cast<proto::ProtoObject*>(obj->setAttribute(ctx,
        env ? env->getDataString() : PythonEnvironment::getInternalString(ctx, "__data__"),
        PythonEnvironment::getInternedString(ctx, data.c_str())->asObject(ctx)));
    return obj;
}

// ---------------------------------------------------------------------------
// Base64 alphabet
// ---------------------------------------------------------------------------

static const char b64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -1; // padding
    return -2; // invalid
}

// b2a_base64(data, *, newline=True) → bytes
static const proto::ProtoObject* py_b2a_base64(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwargs) {

    if (!posArgs || posArgs->getSize(ctx) < 1) return make_bytes(ctx, "\n");
    std::string src = obj_to_bytes(ctx, posArgs->getAt(ctx, 0));

    bool newline = true;
    if (kwargs) {
        const proto::ProtoObject* nl = kwargs->getAt(ctx,
            PythonEnvironment::getInternedString(ctx, "newline")->getHash(ctx));
        if (nl && nl != PROTO_NONE) newline = (nl != PROTO_FALSE);
    }

    std::string out;
    size_t i = 0;
    while (i < src.size()) {
        unsigned char b0 = (unsigned char)src[i++];
        unsigned char b1 = i < src.size() ? (unsigned char)src[i++] : 0;
        unsigned char b2 = i < src.size() ? (unsigned char)src[i++] : 0;
        int trio = (b0 << 16) | (b1 << 8) | b2;
        out += b64_chars[(trio >> 18) & 0x3F];
        out += b64_chars[(trio >> 12) & 0x3F];
        out += (src.size() + (i > src.size() ? src.size() - i + 3 : 0) >= i - 2 ||
                i <= src.size() + 2) ? b64_chars[(trio >> 6) & 0x3F] : '=';
        out += (src.size() + (i > src.size() ? src.size() - i + 3 : 0) >= i - 1 ||
                i <= src.size() + 1) ? b64_chars[trio & 0x3F] : '=';
    }
    // Handle padding properly
    size_t rem = src.size() % 3;
    if (rem == 1 && !out.empty()) { out[out.size()-1] = '='; out[out.size()-2] = '='; }
    else if (rem == 2 && !out.empty()) { out[out.size()-1] = '='; }
    if (newline) out += '\n';
    return make_bytes(ctx, out);
}

// a2b_base64(data) → bytes
static const proto::ProtoObject* py_a2b_base64(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {

    if (!posArgs || posArgs->getSize(ctx) < 1) return make_bytes(ctx, "");
    std::string src = obj_to_bytes(ctx, posArgs->getAt(ctx, 0));

    std::string filtered;
    for (char c : src) {
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') filtered += c;
    }

    std::string out;
    for (size_t i = 0; i + 3 < filtered.size(); i += 4) {
        int v0 = b64_decode_char(filtered[i]);
        int v1 = b64_decode_char(filtered[i+1]);
        int v2 = b64_decode_char(filtered[i+2]);
        int v3 = b64_decode_char(filtered[i+3]);
        if (v0 < 0 || v1 < 0) break;
        out += (char)((v0 << 2) | (v1 >> 4));
        if (v2 >= 0) out += (char)(((v1 & 0xF) << 4) | (v2 >> 2));
        if (v3 >= 0) out += (char)(((v2 & 0x3) << 6) | v3);
    }
    return make_bytes(ctx, out);
}

// hexlify(data) → bytes (lowercase hex)
static const proto::ProtoObject* py_hexlify(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {

    if (!posArgs || posArgs->getSize(ctx) < 1) return make_bytes(ctx, "");
    std::string src = obj_to_bytes(ctx, posArgs->getAt(ctx, 0));
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(src.size() * 2);
    for (unsigned char c : src) {
        out += hex[c >> 4];
        out += hex[c & 0xF];
    }
    return make_bytes(ctx, out);
}

// unhexlify(hexstr) → bytes
static const proto::ProtoObject* py_unhexlify(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {

    if (!posArgs || posArgs->getSize(ctx) < 1) return make_bytes(ctx, "");
    std::string src = obj_to_bytes(ctx, posArgs->getAt(ctx, 0));
    std::string out;
    for (size_t i = 0; i + 1 < src.size(); i += 2) {
        char hi = src[i], lo = src[i+1];
        int h = (hi >= '0' && hi <= '9') ? hi - '0' :
                (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 :
                (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : 0;
        int l = (lo >= '0' && lo <= '9') ? lo - '0' :
                (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 :
                (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : 0;
        out += (char)((h << 4) | l);
    }
    return make_bytes(ctx, out);
}

// crc32(data, crc=0) → int
static const proto::ProtoObject* py_crc32(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {

    if (!posArgs || posArgs->getSize(ctx) < 1) return ctx->fromInteger(0);
    std::string src = obj_to_bytes(ctx, posArgs->getAt(ctx, 0));
    uint32_t crc = 0xFFFFFFFFu;
    if (posArgs->getSize(ctx) >= 2) {
        const proto::ProtoObject* crcObj = posArgs->getAt(ctx, 1);
        if (crcObj && crcObj->isInteger(ctx)) crc = (uint32_t)crcObj->asLong(ctx) ^ 0xFFFFFFFFu;
    }
    static const uint32_t table[256] = {
        0,          0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
        0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91B, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBE, 0xE7B82D09, 0x90BF1D3C
        // (abbreviated — use standard CRC-32 polynomial 0xEDB88320)
    };
    // Simplified CRC-32 using Sarwate algorithm
    static bool init = false;
    static uint32_t crc_table[256];
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            crc_table[i] = c;
        }
        init = true;
    }
    for (unsigned char ch : src) crc = crc_table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
    return ctx->fromInteger((long long)(crc ^ 0xFFFFFFFFu));
}

// b2a_hex (alias for hexlify)
// a2b_hex (alias for unhexlify)

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    proto::ProtoObject* mod = const_cast<proto::ProtoObject*>(ctx->newObject(false));
    mod = const_cast<proto::ProtoObject*>(mod->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "b2a_base64"),
        ctx->fromMethod(mod, py_b2a_base64)));
    mod = const_cast<proto::ProtoObject*>(mod->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "a2b_base64"),
        ctx->fromMethod(mod, py_a2b_base64)));
    mod = const_cast<proto::ProtoObject*>(mod->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "hexlify"),
        ctx->fromMethod(mod, py_hexlify)));
    mod = const_cast<proto::ProtoObject*>(mod->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "b2a_hex"),
        ctx->fromMethod(mod, py_hexlify)));
    mod = const_cast<proto::ProtoObject*>(mod->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "unhexlify"),
        ctx->fromMethod(mod, py_unhexlify)));
    mod = const_cast<proto::ProtoObject*>(mod->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "a2b_hex"),
        ctx->fromMethod(mod, py_unhexlify)));
    mod = const_cast<proto::ProtoObject*>(mod->setAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "crc32"),
        ctx->fromMethod(mod, py_crc32)));
    return mod;
}

} // namespace binascii
} // namespace protoPython
