# hashlib.py - Pure-Python MD5, SHA1, SHA256 (no native hashing required).

def _to_bytes(data):
    """Convert data to bytes for hashing."""
    if isinstance(data, bytes):
        return data
    if isinstance(data, bytearray):
        return bytes(data)
    if isinstance(data, str):
        return data.encode('utf-8')
    raise TypeError("Unsupported type: %s" % type(data).__name__)


def _rotr32(x, n):
    """Right rotate 32-bit value."""
    x = x & 0xffffffff
    return ((x >> n) | (x << (32 - n))) & 0xffffffff


def _rotl32(x, n):
    """Left rotate 32-bit value."""
    x = x & 0xffffffff
    return ((x << n) | (x >> (32 - n))) & 0xffffffff


def _add32(*args):
    """Add 32-bit values with wraparound."""
    return sum(args) & 0xffffffff


# --- MD5 (RFC 1321) ---

_MD5_K = [
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
]

_MD5_S = [
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
]


class _MD5Hash:
    """Pure-Python MD5 hash object."""
    digest_size = 16
    block_size = 64

    def __init__(self, data=b''):
        self._buf = []
        self._count = 0
        self._h = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]
        if data:
            self.update(data)

    def update(self, data):
        data = _to_bytes(data)
        self._buf.append(data)
        self._count += len(data)
        return self

    def digest(self):
        data = b''.join(self._buf)
        data = bytearray(data)
        orig_len = len(data) * 8
        data.append(0x80)
        while (len(data) % 64) != 56:
            data.append(0)
        for i in range(8):
            data.append((orig_len >> (i * 8)) & 0xff)
        h = list(self._h)
        for chunk_start in range(0, len(data), 64):
            block = data[chunk_start:chunk_start + 64]
            X = []
            for i in range(16):
                X.append(
                    block[i * 4] | (block[i * 4 + 1] << 8) |
                    (block[i * 4 + 2] << 16) | (block[i * 4 + 3] << 24)
                )
            A, B, C, D = h[0], h[1], h[2], h[3]
            for i in range(64):
                if i < 16:
                    f = (B & C) | ((~B) & D)
                    g = i
                elif i < 32:
                    f = (D & B) | ((~D) & C)
                    g = (5 * i + 1) % 16
                elif i < 48:
                    f = B ^ C ^ D
                    g = (3 * i + 5) % 16
                else:
                    f = C ^ (B | (~D))
                    g = (7 * i) % 16
                f = _add32(f, A, _MD5_K[i], X[g])
                A = D
                D = C
                C = B
                B = _add32(B, _rotl32(f, _MD5_S[i]))
            h[0] = _add32(h[0], A)
            h[1] = _add32(h[1], B)
            h[2] = _add32(h[2], C)
            h[3] = _add32(h[3], D)
        out = []
        for x in h:
            for j in range(4):
                out.append((x >> (j * 8)) & 0xff)
        return bytes(out)

    def hexdigest(self):
        return ''.join('%02x' % b for b in self.digest())

    def copy(self):
        other = _MD5Hash()
        other._buf = list(self._buf)
        other._count = self._count
        other._h = list(self._h)
        return other


# --- SHA1 (FIPS 180-1) ---

class _SHA1Hash:
    """Pure-Python SHA1 hash object."""
    digest_size = 20
    block_size = 64

    def __init__(self, data=b''):
        self._buf = []
        self._count = 0
        self._h = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0]
        if data:
            self.update(data)

    def update(self, data):
        data = _to_bytes(data)
        self._buf.append(data)
        self._count += len(data)
        return self

    def digest(self):
        data = b''.join(self._buf)
        data = bytearray(data)
        orig_len = len(data) * 8
        data.append(0x80)
        while (len(data) % 64) != 56:
            data.append(0)
        for i in range(8):
            data.append((orig_len >> (56 - i * 8)) & 0xff)
        h = list(self._h)
        for chunk_start in range(0, len(data), 64):
            block = data[chunk_start:chunk_start + 64]
            W = []
            for i in range(16):
                W.append(
                    (block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
                    (block[i * 4 + 2] << 8) | block[i * 4 + 3]
                )
            for i in range(16, 80):
                w = W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16]
                W.append(_rotl32(w, 1))
            A, B, C, D, E = h[0], h[1], h[2], h[3], h[4]
            for i in range(80):
                if i < 20:
                    f = (B & C) | ((~B) & D)
                    k = 0x5a827999
                elif i < 40:
                    f = B ^ C ^ D
                    k = 0x6ed9eba1
                elif i < 60:
                    f = (B & C) | (B & D) | (C & D)
                    k = 0x8f1bbcdc
                else:
                    f = B ^ C ^ D
                    k = 0xca62c1d6
                temp = _add32(_rotl32(A, 5), f, E, k, W[i])
                E = D
                D = C
                C = _rotl32(B, 30)
                B = A
                A = temp
            h[0] = _add32(h[0], A)
            h[1] = _add32(h[1], B)
            h[2] = _add32(h[2], C)
            h[3] = _add32(h[3], D)
            h[4] = _add32(h[4], E)
        out = []
        for x in h:
            for j in range(4):
                out.append((x >> (24 - j * 8)) & 0xff)
        return bytes(out)

    def hexdigest(self):
        return ''.join('%02x' % b for b in self.digest())

    def copy(self):
        other = _SHA1Hash()
        other._buf = list(self._buf)
        other._count = self._count
        other._h = list(self._h)
        return other


# --- SHA256 (FIPS 180-2) ---

_SHA256_K = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]


def _sha256_ch(x, y, z):
    return (x & y) ^ ((~x) & z)


def _sha256_maj(x, y, z):
    return (x & y) ^ (x & z) ^ (y & z)


def _sha256_sigma0(x):
    return _rotr32(x, 2) ^ _rotr32(x, 13) ^ _rotr32(x, 22)


def _sha256_sigma1(x):
    return _rotr32(x, 6) ^ _rotr32(x, 11) ^ _rotr32(x, 25)


def _sha256_gamma0(x):
    return _rotr32(x, 7) ^ _rotr32(x, 18) ^ (x >> 3)


def _sha256_gamma1(x):
    return _rotr32(x, 17) ^ _rotr32(x, 19) ^ (x >> 10)


class _SHA256Hash:
    """Pure-Python SHA256 hash object."""
    digest_size = 32
    block_size = 64

    def __init__(self, data=b''):
        self._buf = []
        self._count = 0
        self._h = [
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
        ]
        if data:
            self.update(data)

    def update(self, data):
        data = _to_bytes(data)
        self._buf.append(data)
        self._count += len(data)
        return self

    def digest(self):
        data = b''.join(self._buf)
        data = bytearray(data)
        orig_len = len(data) * 8
        data.append(0x80)
        while (len(data) % 64) != 56:
            data.append(0)
        for i in range(8):
            data.append((orig_len >> (56 - i * 8)) & 0xff)
        h = list(self._h)
        for chunk_start in range(0, len(data), 64):
            block = data[chunk_start:chunk_start + 64]
            W = []
            for i in range(16):
                W.append(
                    (block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
                    (block[i * 4 + 2] << 8) | block[i * 4 + 3]
                )
            for i in range(16, 64):
                w = _add32(
                    _sha256_gamma1(W[i - 2]),
                    W[i - 7],
                    _sha256_gamma0(W[i - 15]),
                    W[i - 16]
                )
                W.append(w)
            a, b, c, d, e, f, g, h0 = h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]
            for i in range(64):
                t1 = _add32(
                    h0,
                    _sha256_sigma1(e),
                    _sha256_ch(e, f, g),
                    _SHA256_K[i],
                    W[i]
                )
                t2 = _add32(_sha256_sigma0(a), _sha256_maj(a, b, c))
                h0 = g
                g = f
                f = e
                e = _add32(d, t1)
                d = c
                c = b
                b = a
                a = _add32(t1, t2)
            h[0] = _add32(h[0], a)
            h[1] = _add32(h[1], b)
            h[2] = _add32(h[2], c)
            h[3] = _add32(h[3], d)
            h[4] = _add32(h[4], e)
            h[5] = _add32(h[5], f)
            h[6] = _add32(h[6], g)
            h[7] = _add32(h[7], h0)
        out = []
        for x in h:
            for j in range(4):
                out.append((x >> (24 - j * 8)) & 0xff)
        return bytes(out)

    def hexdigest(self):
        return ''.join('%02x' % b for b in self.digest())

    def copy(self):
        other = _SHA256Hash()
        other._buf = list(self._buf)
        other._count = self._count
        other._h = list(self._h)
        return other


# --- Public API ---

def md5(data=b'', **kwargs):
    """Return an MD5 hash object. Pure-Python implementation."""
    return _MD5Hash(data)


def sha1(data=b'', **kwargs):
    """Return a SHA1 hash object. Pure-Python implementation."""
    return _SHA1Hash(data)


def sha256(data=b'', **kwargs):
    """Return a SHA256 hash object. Pure-Python implementation."""
    return _SHA256Hash(data)
