"""SHA-1 for hashlib (see _hashobject)."""
from _hashobject import make as _make

sha1 = _make("sha1", 20, 64)
