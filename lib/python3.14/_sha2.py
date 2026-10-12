"""SHA-224, SHA-256, SHA-384 and SHA-512 for hashlib (see _hashobject)."""
from _hashobject import make as _make

sha224 = _make("sha224", 28, 64)
sha256 = _make("sha256", 32, 64)
sha384 = _make("sha384", 48, 128)
sha512 = _make("sha512", 64, 128)
