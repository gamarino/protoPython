// HashcoreModule.cpp — native message digests for hashlib (_hashcore).
//
// hashlib finds its algorithms in _md5, _sha1, _sha2, _sha3 and _blake2, which in
// CPython are C extensions.  protoPython had none of them, so hashlib.md5,
// sha1, sha256, ... did not exist.  This module holds the compression
// functions; the hash objects themselves are Python classes
// (lib/python3.14/_hashobject.py).
//
// A digest's running state is an immutable value: a bytes object.
//
//   new(name)                -> state
//   update(state, data)      -> new state      (state is not modified)
//   digest(state, n=0)       -> digest bytes   (n: output length, SHAKE only)
//   blake2_new(...)          -> state          (BLAKE2 with its parameter block)
//
// so `copy()` is sharing a value, and no native object needs GC support.
// The state is a memcpy of a plain struct: it is only meaningful inside this
// process and is never exposed as a stable format.

#include <protoCore.h>
#include <protoPython/PythonEnvironment.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace protoPython {
namespace hashcore {

namespace {

// ---------------------------------------------------------------------------
// Bytes in and out
// ---------------------------------------------------------------------------

// The Python wrapper passes only real bytes objects: its __data__ is a
// ProtoByteBuffer (see Compiler::compileConstant, ConstType::Bytes).
bool bytesOf(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::string& out) {
    if (!obj || obj == PROTO_NONE) return false;
    const proto::ProtoByteBuffer* bb = obj->asByteBuffer(ctx);
    if (!bb) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        const proto::ProtoObject* data = obj->getAttribute(ctx,
            env ? env->getDataString() : PythonEnvironment::getInternalString(ctx, "__data__"));
        if (data && data != PROTO_NONE) bb = data->asByteBuffer(ctx);
    }
    if (!bb) return false;
    const proto::proto_ulong n = bb->getSize(ctx);
    out.resize(n);
    const char* raw = bb->getBuffer(ctx);
    if (raw) {
        if (n) std::memcpy(&out[0], raw, n);
    } else {
        for (proto::proto_ulong i = 0; i < n; ++i)
            out[i] = static_cast<char>(bb->getAt(ctx, static_cast<int>(i)));
    }
    return true;
}

const proto::ProtoObject* makeBytes(proto::ProtoContext* ctx, const void* data, size_t n) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* obj = ctx->newObject(false);
    if (env && env->getBytesPrototype()) {
        obj = obj->addParent(ctx, env->getBytesPrototype());
        obj = obj->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"),
            env->getBytesPrototype());
    }
    const proto::ProtoByteBuffer* bb = ctx->newByteBuffer(
        static_cast<const char*>(data), static_cast<proto::proto_ulong>(n));
    return obj->setAttribute(ctx,
        env ? env->getDataString() : PythonEnvironment::getInternalString(ctx, "__data__"),
        bb->asObject(ctx));
}

inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
inline uint64_t rotl64(uint64_t x, int n) { return n ? (x << n) | (x >> (64 - n)) : x; }

inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
inline uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
inline uint64_t le64(const uint8_t* p) { return uint64_t(le32(p + 4)) << 32 | le32(p); }

// ---------------------------------------------------------------------------
// Merkle-Damgard digests: MD5, SHA-1, SHA-224/256, SHA-384/512
// ---------------------------------------------------------------------------

enum class Algo : uint8_t { MD5, SHA1, SHA224, SHA256, SHA384, SHA512 };

struct MDState {
    uint64_t h[8];        // chaining value (32-bit algorithms use the low words)
    uint64_t bytes;       // message length so far (2^64 bytes is never reached)
    uint8_t buf[128];     // pending partial block
    uint32_t used;        // bytes in buf
    uint8_t algo;
};

size_t blockSize(Algo a) { return (a == Algo::SHA384 || a == Algo::SHA512) ? 128 : 64; }

size_t digestSize(Algo a) {
    switch (a) {
        case Algo::MD5: return 16;
        case Algo::SHA1: return 20;
        case Algo::SHA224: return 28;
        case Algo::SHA256: return 32;
        case Algo::SHA384: return 48;
        case Algo::SHA512: return 64;
    }
    return 0;
}

void md5Block(uint64_t* hs, const uint8_t* p) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t M[16];
    for (int i = 0; i < 16; ++i) M[i] = le32(p + 4 * i);
    uint32_t a = uint32_t(hs[0]), b = uint32_t(hs[1]), c = uint32_t(hs[2]), d = uint32_t(hs[3]);
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7 * i) & 15; }
        const uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rotl32(a + f + K[i] + M[g], S[i]);
        a = tmp;
    }
    hs[0] = uint32_t(hs[0] + a); hs[1] = uint32_t(hs[1] + b);
    hs[2] = uint32_t(hs[2] + c); hs[3] = uint32_t(hs[3] + d);
}

void sha1Block(uint64_t* hs, const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = be32(p + 4 * i);
    for (int i = 16; i < 80; ++i) w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = uint32_t(hs[0]), b = uint32_t(hs[1]), c = uint32_t(hs[2]),
             d = uint32_t(hs[3]), e = uint32_t(hs[4]);
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        const uint32_t t = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotl32(b, 30); b = a; a = t;
    }
    hs[0] = uint32_t(hs[0] + a); hs[1] = uint32_t(hs[1] + b); hs[2] = uint32_t(hs[2] + c);
    hs[3] = uint32_t(hs[3] + d); hs[4] = uint32_t(hs[4] + e);
}

void sha256Block(uint64_t* hs, const uint8_t* p) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = be32(p + 4 * i);
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t v[8];
    for (int i = 0; i < 8; ++i) v[i] = uint32_t(hs[i]);
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr32(v[4], 6) ^ rotr32(v[4], 11) ^ rotr32(v[4], 25);
        const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const uint32_t t1 = v[7] + S1 + ch + K[i] + w[i];
        const uint32_t S0 = rotr32(v[0], 2) ^ rotr32(v[0], 13) ^ rotr32(v[0], 22);
        const uint32_t mj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const uint32_t t2 = S0 + mj;
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; ++i) hs[i] = uint32_t(hs[i] + v[i]);
}

void sha512Block(uint64_t* hs, const uint8_t* p) {
    static const uint64_t K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
    uint64_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = be64(p + 8 * i);
    for (int i = 16; i < 80; ++i) {
        const uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        const uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t v[8];
    for (int i = 0; i < 8; ++i) v[i] = hs[i];
    for (int i = 0; i < 80; ++i) {
        const uint64_t S1 = rotr64(v[4], 14) ^ rotr64(v[4], 18) ^ rotr64(v[4], 41);
        const uint64_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const uint64_t t1 = v[7] + S1 + ch + K[i] + w[i];
        const uint64_t S0 = rotr64(v[0], 28) ^ rotr64(v[0], 34) ^ rotr64(v[0], 39);
        const uint64_t mj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const uint64_t t2 = S0 + mj;
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; ++i) hs[i] += v[i];
}

void mdInit(MDState& s, Algo a) {
    std::memset(&s, 0, sizeof s);
    s.algo = static_cast<uint8_t>(a);
    static const uint64_t md5IV[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    static const uint64_t sha1IV[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    static const uint64_t sha224IV[8] = {0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939,
                                         0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4};
    static const uint64_t sha256IV[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    static const uint64_t sha384IV[8] = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL};
    static const uint64_t sha512IV[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    const uint64_t* iv = nullptr;
    size_t n = 8;
    switch (a) {
        case Algo::MD5: iv = md5IV; n = 4; break;
        case Algo::SHA1: iv = sha1IV; n = 5; break;
        case Algo::SHA224: iv = sha224IV; break;
        case Algo::SHA256: iv = sha256IV; break;
        case Algo::SHA384: iv = sha384IV; break;
        case Algo::SHA512: iv = sha512IV; break;
    }
    for (size_t i = 0; i < n; ++i) s.h[i] = iv[i];
}

void mdCompress(MDState& s, const uint8_t* block) {
    switch (static_cast<Algo>(s.algo)) {
        case Algo::MD5: md5Block(s.h, block); break;
        case Algo::SHA1: sha1Block(s.h, block); break;
        case Algo::SHA224:
        case Algo::SHA256: sha256Block(s.h, block); break;
        case Algo::SHA384:
        case Algo::SHA512: sha512Block(s.h, block); break;
    }
}

void mdUpdate(MDState& s, const uint8_t* p, size_t n) {
    const size_t bs = blockSize(static_cast<Algo>(s.algo));
    s.bytes += n;
    if (s.used) {
        const size_t take = std::min(n, bs - s.used);
        std::memcpy(s.buf + s.used, p, take);
        s.used += static_cast<uint32_t>(take);
        p += take;
        n -= take;
        if (s.used < bs) return;
        mdCompress(s, s.buf);
        s.used = 0;
    }
    for (; n >= bs; p += bs, n -= bs) mdCompress(s, p);
    if (n) {
        std::memcpy(s.buf, p, n);
        s.used = static_cast<uint32_t>(n);
    }
}

std::string mdDigest(MDState s) {
    const Algo a = static_cast<Algo>(s.algo);
    const size_t bs = blockSize(a);
    const size_t lenBytes = bs == 128 ? 16 : 8;
    const uint64_t bits = s.bytes * 8;
    uint8_t pad[256] = {0x80};
    const size_t padLen = ((s.used + 1 + lenBytes + bs - 1) / bs) * bs - s.used - lenBytes;
    uint8_t len[16] = {0};
    if (a == Algo::MD5) {
        for (int i = 0; i < 8; ++i) len[i] = uint8_t(bits >> (8 * i));
    } else {
        for (int i = 0; i < 8; ++i) len[lenBytes - 1 - i] = uint8_t(bits >> (8 * i));
    }
    // mdUpdate also advances the length counter, which is no longer read.
    mdUpdate(s, pad, padLen);
    mdUpdate(s, len, lenBytes);

    std::string out;
    const size_t ds = digestSize(a);
    if (a == Algo::MD5) {
        for (int i = 0; i < 4; ++i)
            for (int b = 0; b < 4; ++b) out.push_back(char(uint32_t(s.h[i]) >> (8 * b)));
    } else if (bs == 64) {
        for (int i = 0; i < 8; ++i)
            for (int b = 3; b >= 0; --b) out.push_back(char(uint32_t(s.h[i]) >> (8 * b)));
    } else {
        for (int i = 0; i < 8; ++i)
            for (int b = 7; b >= 0; --b) out.push_back(char(s.h[i] >> (8 * b)));
    }
    out.resize(ds);
    return out;
}

// ---------------------------------------------------------------------------
// SHA-3 and SHAKE (Keccak-f[1600], FIPS 202)
// ---------------------------------------------------------------------------

struct KeccakState {
    uint64_t a[25];
    uint8_t buf[168];   // pending partial block (rate <= 168)
    uint32_t used;
    uint32_t rate;      // bytes
    uint8_t suffix;     // 0x06 for SHA-3, 0x1f for SHAKE
    uint8_t outLen;     // fixed digest length; 0 for SHAKE
};

void keccakF(uint64_t* a) {
    static const uint64_t RC[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
        0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};
    static const int R[25] = {0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
                              25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14};
    for (int round = 0; round < 24; ++round) {
        uint64_t c[5], d[5], b[25];
        for (int x = 0; x < 5; ++x) c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
        for (int x = 0; x < 5; ++x) d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
        for (int i = 0; i < 25; ++i) a[i] ^= d[i % 5];
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 5; ++y)
                b[y + 5 * ((2 * x + 3 * y) % 5)] = rotl64(a[x + 5 * y], R[x + 5 * y]);
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 5; ++y)
                a[x + 5 * y] = b[x + 5 * y] ^ (~b[(x + 1) % 5 + 5 * y] & b[(x + 2) % 5 + 5 * y]);
        a[0] ^= RC[round];
    }
}

void keccakAbsorbBlock(KeccakState& s, const uint8_t* p) {
    for (uint32_t i = 0; i < s.rate / 8; ++i) s.a[i] ^= le64(p + 8 * i);
    keccakF(s.a);
}

void keccakUpdate(KeccakState& s, const uint8_t* p, size_t n) {
    if (s.used) {
        const size_t take = std::min(n, size_t(s.rate - s.used));
        std::memcpy(s.buf + s.used, p, take);
        s.used += static_cast<uint32_t>(take);
        p += take;
        n -= take;
        if (s.used < s.rate) return;
        keccakAbsorbBlock(s, s.buf);
        s.used = 0;
    }
    for (; n >= s.rate; p += s.rate, n -= s.rate) keccakAbsorbBlock(s, p);
    if (n) {
        std::memcpy(s.buf, p, n);
        s.used = static_cast<uint32_t>(n);
    }
}

std::string keccakDigest(KeccakState s, size_t outLen) {
    std::memset(s.buf + s.used, 0, s.rate - s.used);
    s.buf[s.used] ^= s.suffix;
    s.buf[s.rate - 1] ^= 0x80;
    keccakAbsorbBlock(s, s.buf);
    std::string out;
    out.reserve(outLen);
    while (true) {
        for (uint32_t i = 0; i < s.rate && out.size() < outLen; ++i)
            out.push_back(char(s.a[i / 8] >> (8 * (i % 8))));
        if (out.size() >= outLen) break;
        keccakF(s.a);
    }
    return out;
}


// ---------------------------------------------------------------------------
// BLAKE2b and BLAKE2s (RFC 7693), with the parameter block CPython exposes
// ---------------------------------------------------------------------------

struct Blake2State {
    uint64_t h[8];
    uint64_t t[2];        // byte counter
    uint8_t buf[128];     // pending block: always kept until more data arrives
    uint32_t used;
    uint8_t isB;          // 1 = BLAKE2b (64-bit words), 0 = BLAKE2s
    uint8_t outLen;
    uint8_t lastNode;
};

const uint64_t kBlake2bIV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
const uint32_t kBlake2sIV[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
const uint8_t kSigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};

void blake2bCompress(Blake2State& s, const uint8_t* block, bool last) {
    uint64_t m[16], v[16];
    for (int i = 0; i < 16; ++i) m[i] = le64(block + 8 * i);
    for (int i = 0; i < 8; ++i) { v[i] = s.h[i]; v[i + 8] = kBlake2bIV[i]; }
    v[12] ^= s.t[0];
    v[13] ^= s.t[1];
    if (last) {
        v[14] = ~v[14];
        if (s.lastNode) v[15] = ~v[15];
    }
    auto G = [&](int a, int b, int c, int d, uint64_t x, uint64_t y) {
        v[a] = v[a] + v[b] + x; v[d] = rotr64(v[d] ^ v[a], 32);
        v[c] = v[c] + v[d];     v[b] = rotr64(v[b] ^ v[c], 24);
        v[a] = v[a] + v[b] + y; v[d] = rotr64(v[d] ^ v[a], 16);
        v[c] = v[c] + v[d];     v[b] = rotr64(v[b] ^ v[c], 63);
    };
    for (int r = 0; r < 12; ++r) {
        const uint8_t* sg = kSigma[r];
        G(0, 4, 8, 12, m[sg[0]], m[sg[1]]);   G(1, 5, 9, 13, m[sg[2]], m[sg[3]]);
        G(2, 6, 10, 14, m[sg[4]], m[sg[5]]);  G(3, 7, 11, 15, m[sg[6]], m[sg[7]]);
        G(0, 5, 10, 15, m[sg[8]], m[sg[9]]);  G(1, 6, 11, 12, m[sg[10]], m[sg[11]]);
        G(2, 7, 8, 13, m[sg[12]], m[sg[13]]); G(3, 4, 9, 14, m[sg[14]], m[sg[15]]);
    }
    for (int i = 0; i < 8; ++i) s.h[i] ^= v[i] ^ v[i + 8];
}

void blake2sCompress(Blake2State& s, const uint8_t* block, bool last) {
    uint32_t m[16], v[16];
    for (int i = 0; i < 16; ++i) m[i] = le32(block + 4 * i);
    for (int i = 0; i < 8; ++i) { v[i] = uint32_t(s.h[i]); v[i + 8] = kBlake2sIV[i]; }
    v[12] ^= uint32_t(s.t[0]);
    v[13] ^= uint32_t(s.t[0] >> 32);
    if (last) {
        v[14] = ~v[14];
        if (s.lastNode) v[15] = ~v[15];
    }
    auto G = [&](int a, int b, int c, int d, uint32_t x, uint32_t y) {
        v[a] = v[a] + v[b] + x; v[d] = rotr32(v[d] ^ v[a], 16);
        v[c] = v[c] + v[d];     v[b] = rotr32(v[b] ^ v[c], 12);
        v[a] = v[a] + v[b] + y; v[d] = rotr32(v[d] ^ v[a], 8);
        v[c] = v[c] + v[d];     v[b] = rotr32(v[b] ^ v[c], 7);
    };
    for (int r = 0; r < 10; ++r) {
        const uint8_t* sg = kSigma[r];
        G(0, 4, 8, 12, m[sg[0]], m[sg[1]]);   G(1, 5, 9, 13, m[sg[2]], m[sg[3]]);
        G(2, 6, 10, 14, m[sg[4]], m[sg[5]]);  G(3, 7, 11, 15, m[sg[6]], m[sg[7]]);
        G(0, 5, 10, 15, m[sg[8]], m[sg[9]]);  G(1, 6, 11, 12, m[sg[10]], m[sg[11]]);
        G(2, 7, 8, 13, m[sg[12]], m[sg[13]]); G(3, 4, 9, 14, m[sg[14]], m[sg[15]]);
    }
    for (int i = 0; i < 8; ++i) s.h[i] = uint32_t(s.h[i]) ^ v[i] ^ v[i + 8];
}

size_t blake2Block(const Blake2State& s) { return s.isB ? 128 : 64; }

void blake2Count(Blake2State& s, size_t n) {
    s.t[0] += n;
    if (s.t[0] < n) ++s.t[1];
}

void blake2Update(Blake2State& s, const uint8_t* p, size_t n) {
    const size_t bs = blake2Block(s);
    while (n) {
        if (s.used == bs) {     // a full block is pending and more data follows
            blake2Count(s, bs);
            if (s.isB) blake2bCompress(s, s.buf, false);
            else blake2sCompress(s, s.buf, false);
            s.used = 0;
        }
        const size_t take = std::min(n, bs - s.used);
        std::memcpy(s.buf + s.used, p, take);
        s.used += static_cast<uint32_t>(take);
        p += take;
        n -= take;
    }
}

std::string blake2Digest(Blake2State s) {
    const size_t bs = blake2Block(s);
    blake2Count(s, s.used);
    std::memset(s.buf + s.used, 0, bs - s.used);
    if (s.isB) blake2bCompress(s, s.buf, true);
    else blake2sCompress(s, s.buf, true);
    std::string out;
    for (size_t i = 0; out.size() < s.outLen; ++i) {
        const int wordBytes = s.isB ? 8 : 4;
        for (int b = 0; b < wordBytes && out.size() < s.outLen; ++b)
            out.push_back(char(s.h[i] >> (8 * b)));
    }
    return out;
}

// Parameters were validated by the Python wrapper (_hashobject.py).
Blake2State blake2Init(bool isB, size_t outLen, const std::string& key, const std::string& salt,
                       const std::string& person, unsigned fanout, unsigned depth, uint32_t leafSize,
                       uint64_t nodeOffset, unsigned nodeDepth, unsigned innerSize, bool lastNode) {
    Blake2State s;
    std::memset(&s, 0, sizeof s);
    s.isB = isB ? 1 : 0;
    s.outLen = static_cast<uint8_t>(outLen);
    s.lastNode = lastNode ? 1 : 0;
    uint8_t param[64] = {0};
    param[0] = static_cast<uint8_t>(outLen);
    param[1] = static_cast<uint8_t>(key.size());
    param[2] = static_cast<uint8_t>(fanout);
    param[3] = static_cast<uint8_t>(depth);
    for (int i = 0; i < 4; ++i) param[4 + i] = uint8_t(leafSize >> (8 * i));
    if (isB) {
        for (int i = 0; i < 8; ++i) param[8 + i] = uint8_t(nodeOffset >> (8 * i));
        param[16] = static_cast<uint8_t>(nodeDepth);
        param[17] = static_cast<uint8_t>(innerSize);
        std::memcpy(param + 32, salt.data(), std::min<size_t>(salt.size(), 16));
        std::memcpy(param + 48, person.data(), std::min<size_t>(person.size(), 16));
        for (int i = 0; i < 8; ++i) s.h[i] = kBlake2bIV[i] ^ le64(param + 8 * i);
    } else {
        for (int i = 0; i < 6; ++i) param[8 + i] = uint8_t(nodeOffset >> (8 * i));
        param[14] = static_cast<uint8_t>(nodeDepth);
        param[15] = static_cast<uint8_t>(innerSize);
        std::memcpy(param + 16, salt.data(), std::min<size_t>(salt.size(), 8));
        std::memcpy(param + 24, person.data(), std::min<size_t>(person.size(), 8));
        for (int i = 0; i < 8; ++i) s.h[i] = kBlake2sIV[i] ^ le32(param + 4 * i);
    }
    if (!key.empty()) {
        uint8_t block[128] = {0};
        std::memcpy(block, key.data(), key.size());
        blake2Update(s, block, blake2Block(s));
    }
    return s;
}

// ---------------------------------------------------------------------------
// Module functions
// ---------------------------------------------------------------------------

// State layout: one tag byte (0 = Merkle-Damgard, 1 = Keccak, 2 = BLAKE2) + the struct.
bool algoByName(const std::string& name, Algo& a) {
    if (name == "md5") a = Algo::MD5;
    else if (name == "sha1") a = Algo::SHA1;
    else if (name == "sha224") a = Algo::SHA224;
    else if (name == "sha256") a = Algo::SHA256;
    else if (name == "sha384") a = Algo::SHA384;
    else if (name == "sha512") a = Algo::SHA512;
    else return false;
    return true;
}

bool keccakByName(const std::string& name, KeccakState& s) {
    std::memset(&s, 0, sizeof s);
    s.suffix = 0x06;
    if (name == "sha3_224") { s.rate = 144; s.outLen = 28; }
    else if (name == "sha3_256") { s.rate = 136; s.outLen = 32; }
    else if (name == "sha3_384") { s.rate = 104; s.outLen = 48; }
    else if (name == "sha3_512") { s.rate = 72; s.outLen = 64; }
    else if (name == "shake_128") { s.rate = 168; s.suffix = 0x1f; }
    else if (name == "shake_256") { s.rate = 136; s.suffix = 0x1f; }
    else return false;
    return true;
}

std::string argString(proto::ProtoContext* ctx, const proto::ProtoArguments* args, int i) {
    std::string s;
    if (!args || static_cast<int>(args->getSize(ctx)) <= i) return s;
    const proto::ProtoObject* o = args->getAt(ctx, i);
    if (o && o->isString(ctx)) o->asString(ctx)->toUTF8String(ctx, s);
    return s;
}

const proto::ProtoObject* fail(proto::ProtoContext* ctx, const std::string& msg) {
    if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx)) env->raiseValueError(ctx,
        ctx->fromUTF8String(msg.c_str()));
    return nullptr;
}

template <typename S>
std::string pack(uint8_t tag, const S& s) {
    std::string out(1 + sizeof(S), '\0');
    out[0] = static_cast<char>(tag);
    std::memcpy(&out[1], &s, sizeof(S));
    return out;
}

template <typename S>
bool unpack(const std::string& raw, uint8_t tag, S& s) {
    if (raw.size() != 1 + sizeof(S) || static_cast<uint8_t>(raw[0]) != tag) return false;
    std::memcpy(&s, raw.data() + 1, sizeof(S));
    return true;
}

const proto::ProtoObject* py_new(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                 const proto::ParentLink*, const proto::ProtoArguments* args,
                                 const proto::ProtoKeywordArguments*) {
    const std::string name = argString(ctx, args, 0);
    Algo a;
    if (algoByName(name, a)) {
        MDState s;
        mdInit(s, a);
        const std::string raw = pack(0, s);
        return makeBytes(ctx, raw.data(), raw.size());
    }
    KeccakState k;
    if (keccakByName(name, k)) {
        const std::string raw = pack(1, k);
        return makeBytes(ctx, raw.data(), raw.size());
    }
    return fail(ctx, "unsupported hash type " + name);
}

const proto::ProtoObject* py_update(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                    const proto::ParentLink*, const proto::ProtoArguments* args,
                                    const proto::ProtoKeywordArguments*) {
    std::string raw, data;
    if (!args || args->getSize(ctx) < 2 || !bytesOf(ctx, args->getAt(ctx, 0), raw)
        || !bytesOf(ctx, args->getAt(ctx, 1), data))
        return fail(ctx, "_hashcore.update(state, data) needs two bytes objects");
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
    MDState s;
    if (unpack(raw, 0, s)) {
        mdUpdate(s, p, data.size());
        const std::string out = pack(0, s);
        return makeBytes(ctx, out.data(), out.size());
    }
    KeccakState k;
    if (unpack(raw, 1, k)) {
        keccakUpdate(k, p, data.size());
        const std::string out = pack(1, k);
        return makeBytes(ctx, out.data(), out.size());
    }
    Blake2State b;
    if (unpack(raw, 2, b)) {
        blake2Update(b, p, data.size());
        const std::string out = pack(2, b);
        return makeBytes(ctx, out.data(), out.size());
    }
    return fail(ctx, "invalid hash state");
}

const proto::ProtoObject* py_digest(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                    const proto::ParentLink*, const proto::ProtoArguments* args,
                                    const proto::ProtoKeywordArguments*) {
    std::string raw;
    if (!args || args->getSize(ctx) < 1 || !bytesOf(ctx, args->getAt(ctx, 0), raw))
        return fail(ctx, "_hashcore.digest(state[, length]) needs a bytes state");
    MDState s;
    if (unpack(raw, 0, s)) {
        const std::string out = mdDigest(s);
        return makeBytes(ctx, out.data(), out.size());
    }
    KeccakState k;
    if (unpack(raw, 1, k)) {
        long long n = k.outLen;
        if (args->getSize(ctx) > 1 && args->getAt(ctx, 1)->isInteger(ctx))
            n = args->getAt(ctx, 1)->asLong(ctx);
        if (n < 0) return fail(ctx, "length must be non-negative");
        const std::string out = keccakDigest(k, static_cast<size_t>(n));
        return makeBytes(ctx, out.data(), out.size());
    }
    Blake2State b;
    if (unpack(raw, 2, b)) {
        const std::string out = blake2Digest(b);
        return makeBytes(ctx, out.data(), out.size());
    }
    return fail(ctx, "invalid hash state");
}

// blake2_new(is_b, digest_size, key, salt, person, fanout, depth, leaf_size,
//            node_offset, node_depth, inner_size, last_node) -> state
const proto::ProtoObject* py_blake2_new(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                        const proto::ParentLink*, const proto::ProtoArguments* args,
                                        const proto::ProtoKeywordArguments*) {
    if (!args || args->getSize(ctx) < 12) return fail(ctx, "_hashcore.blake2_new needs 12 arguments");
    auto num = [&](int i) -> long long {
        const proto::ProtoObject* o = args->getAt(ctx, i);
        if (o == PROTO_TRUE) return 1;
        if (o == PROTO_FALSE || !o->isInteger(ctx)) return 0;
        return o->asLong(ctx);
    };
    std::string key, salt, person;
    if (!bytesOf(ctx, args->getAt(ctx, 2), key) || !bytesOf(ctx, args->getAt(ctx, 3), salt)
        || !bytesOf(ctx, args->getAt(ctx, 4), person))
        return fail(ctx, "_hashcore.blake2_new: key, salt and person must be bytes");
    const Blake2State s = blake2Init(num(0) != 0, static_cast<size_t>(num(1)), key, salt, person,
        static_cast<unsigned>(num(5)), static_cast<unsigned>(num(6)), static_cast<uint32_t>(num(7)),
        static_cast<uint64_t>(num(8)), static_cast<unsigned>(num(9)), static_cast<unsigned>(num(10)),
        num(11) != 0);
    const std::string raw = pack(2, s);
    return makeBytes(ctx, raw.data(), raw.size());
}

} // namespace

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "new"),
        ctx->fromMethod(nullptr, py_new));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "update"),
        ctx->fromMethod(nullptr, py_update));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "digest"),
        ctx->fromMethod(nullptr, py_digest));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "blake2_new"),
        ctx->fromMethod(nullptr, py_blake2_new));
    return mod;
}

} // namespace hashcore
} // namespace protoPython
