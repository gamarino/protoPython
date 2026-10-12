"""SHA-3 and SHAKE for hashlib (see _hashobject)."""
from _hashobject import make as _make

sha3_224 = _make("sha3_224", 28, 144)
sha3_256 = _make("sha3_256", 32, 136)
sha3_384 = _make("sha3_384", 48, 104)
sha3_512 = _make("sha3_512", 64, 72)
shake_128 = _make("shake_128", 0, 168, shake=True)
shake_256 = _make("shake_256", 0, 136, shake=True)
