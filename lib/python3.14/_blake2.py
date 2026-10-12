"""BLAKE2b and BLAKE2s for hashlib (see _hashobject)."""
from _hashobject import make_blake2 as _make_blake2

blake2b = _make_blake2("blake2b")
blake2s = _make_blake2("blake2s")

BLAKE2B_SALT_SIZE = blake2b.SALT_SIZE
BLAKE2B_PERSON_SIZE = blake2b.PERSON_SIZE
BLAKE2B_MAX_KEY_SIZE = blake2b.MAX_KEY_SIZE
BLAKE2B_MAX_DIGEST_SIZE = blake2b.MAX_DIGEST_SIZE
BLAKE2S_SALT_SIZE = blake2s.SALT_SIZE
BLAKE2S_PERSON_SIZE = blake2s.PERSON_SIZE
BLAKE2S_MAX_KEY_SIZE = blake2s.MAX_KEY_SIZE
BLAKE2S_MAX_DIGEST_SIZE = blake2s.MAX_DIGEST_SIZE
