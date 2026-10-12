# hashlib digests against CPython (hashlib_digests.expected is its output):
# every guaranteed algorithm over empty, short, block-boundary, multi-block
# and binary inputs; incremental updates and copies; BLAKE2 parameters;
# SHAKE lengths; hashlib.new; hmac; file_digest.
import hashlib
import hmac
import io

def data_of(n, seed):
    return bytes((i * 131 + seed * 7 + (i >> 3)) & 0xFF for i in range(n))

lengths = [0, 1, 3, 55, 56, 57, 63, 64, 65, 111, 112, 113, 127, 128, 129, 200, 1000, 4097]
inputs = [b"", b"abc", b"\x00", b"\xff" * 64] + [data_of(n, n) for n in lengths]
algorithms = ["md5", "sha1", "sha224", "sha256", "sha384", "sha512",
              "sha3_224", "sha3_256", "sha3_384", "sha3_512", "blake2b", "blake2s"]

for name in algorithms:
    ctor = getattr(hashlib, name)
    for d in inputs:
        print(name, len(d), ctor(d).hexdigest())
    h = hashlib.new(name)
    print(name, "attrs", h.name, h.digest_size, h.block_size)
    # Incremental updates in uneven pieces equal one update; copies diverge.
    whole = data_of(777, 3)
    h = ctor()
    for i in range(0, len(whole), 61):
        h.update(whole[i:i + 61])
    c = h.copy()
    c.update(b"tail")
    print(name, "incremental", h.hexdigest() == ctor(whole).hexdigest(), c.hexdigest(), h.hexdigest())
    print(name, "digest bytes", h.digest() == bytes.fromhex(h.hexdigest()), len(h.digest()))

for shake in ("shake_128", "shake_256"):
    h = getattr(hashlib, shake)(b"The quick brown fox")
    print(shake, h.hexdigest(0), h.hexdigest(16), h.hexdigest(200)[-20:], len(h.digest(500)))

print("blake2b params", hashlib.blake2b(b"msg", digest_size=20, key=b"secret", salt=b"salty",
                                         person=b"me").hexdigest())
print("blake2s params", hashlib.blake2s(b"msg", digest_size=16, key=b"k" * 32, salt=b"s" * 8,
                                         person=b"p" * 8).hexdigest())
print("blake2b tree", hashlib.blake2b(b"x", fanout=2, depth=3, leaf_size=4096, node_offset=5,
                                       node_depth=1, inner_size=64, last_node=True).hexdigest())
print("blake2b key only", hashlib.blake2b(key=b"key").hexdigest())

print("buffers", hashlib.md5(bytearray(b"abc")).hexdigest(), hashlib.sha256(memoryview(b"abc")).hexdigest())
for bad in ("text", 5):
    try:
        hashlib.md5(bad)
    except TypeError:
        print("TypeError for", type(bad).__name__)
try:
    hashlib.new("nonesuch")
except ValueError as e:
    print("ValueError", e)

print("hmac md5", hmac.new(b"key", b"The quick brown fox", "md5").hexdigest())
print("hmac sha256", hmac.new(b"k" * 100, b"msg", hashlib.sha256).hexdigest())
print("hmac digest", hmac.digest(b"key", b"msg", "sha1").hex(), hmac.compare_digest(b"a", b"a"))
print("file_digest", hashlib.file_digest(io.BytesIO(data_of(5000, 1)), "sha256").hexdigest())
print(sorted(hashlib.algorithms_guaranteed))
