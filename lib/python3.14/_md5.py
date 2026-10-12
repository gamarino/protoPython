"""MD5 for hashlib (see _hashobject)."""
from _hashobject import make as _make

md5 = _make("md5", 16, 64)
