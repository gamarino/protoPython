"""Hash objects for hashlib's _md5, _sha1, _sha2, _sha3 and _blake2.

The digests are computed by the native _hashcore module, which keeps a
digest's running state as an immutable bytes value (see
src/library/HashcoreModule.cpp): update() replaces the state, copy() shares
it.
"""

import _hashcore


def _as_bytes(data):
    """The octets of a bytes-like object; TypeError for str and non-buffers."""
    if isinstance(data, bytes):
        return data
    if isinstance(data, str):
        raise TypeError("Strings must be encoded before hashing")
    if isinstance(data, (bytearray, memoryview)):
        return bytes(data)
    if hasattr(data, "tobytes"):
        return bytes(data.tobytes())
    raise TypeError("object supporting the buffer API required")


def _initial(data, string):
    if string is not None:
        if data is not None:
            raise TypeError("'data' and 'string' are mutually exclusive "
                            "and support for 'string' keyword parameter "
                            "is slated for removal in a future version.")
        return string
    return data


class _Hash:
    """A message digest: update() with bytes, then digest() / hexdigest()."""

    __slots__ = ("_state", "name", "digest_size", "block_size")

    def __init__(self, name, digest_size, block_size, state):
        self.name = name
        self.digest_size = digest_size
        self.block_size = block_size
        self._state = state

    def update(self, data, /):
        """Update this hash object's state with the provided bytes."""
        self._state = _hashcore.update(self._state, _as_bytes(data))

    def digest(self):
        """Return the digest value as a bytes object."""
        return _hashcore.digest(self._state)

    def hexdigest(self):
        """Return the digest value as a string of hexadecimal digits."""
        return self.digest().hex()

    def copy(self):
        """Return a copy of the hash object."""
        return _Hash(self.name, self.digest_size, self.block_size, self._state)

    def __repr__(self):
        return f"<{self.name} _hashlib.HASH object @ {hex(id(self))}>"


class _Shake(_Hash):
    """A SHAKE extendable-output function: digest(length) chooses the size."""

    __slots__ = ()

    def digest(self, length):
        """Return the digest value as a bytes object."""
        if length < 0:
            raise ValueError("negative digest length")
        return _hashcore.digest(self._state, length)

    def hexdigest(self, length):
        """Return the digest value as a string of hexadecimal digits."""
        return self.digest(length).hex()

    def copy(self):
        return _Shake(self.name, self.digest_size, self.block_size, self._state)


def make(name, digest_size, block_size, shake=False):
    """A constructor `name(data=b'', *, usedforsecurity=True, string=None)`."""

    def constructor(data=None, *, usedforsecurity=True, string=None):
        cls = _Shake if shake else _Hash
        h = cls(name, digest_size, block_size, _hashcore.new(name))
        data = _initial(data, string)
        if data is not None:
            h.update(data)
        return h

    constructor.__name__ = constructor.__qualname__ = name
    constructor.__doc__ = f"Return a new {name} hash object; optionally initialized with a string."
    return constructor


_BLAKE2 = {
    # name: (max digest size, block size, max key, salt size, person size)
    "blake2b": (64, 128, 64, 16, 16),
    "blake2s": (32, 64, 32, 8, 8),
}


def make_blake2(name):
    max_digest, block_size, max_key, salt_size, person_size = _BLAKE2[name]
    max_offset = (1 << 64) - 1 if name == "blake2b" else (1 << 48) - 1

    def constructor(data=None, *, digest_size=max_digest, key=b"", salt=b"",
                    person=b"", fanout=1, depth=1, leaf_size=0, node_offset=0,
                    node_depth=0, inner_size=0, last_node=False,
                    usedforsecurity=True, string=None):
        key, salt, person = _as_bytes(key), _as_bytes(salt), _as_bytes(person)
        if not 1 <= digest_size <= max_digest:
            raise ValueError(f"digest_size for {name} must be between 1 and {max_digest} bytes, here it is {digest_size}")
        if len(key) > max_key:
            raise ValueError(f"maximum key length is {max_key} bytes")
        if len(salt) > salt_size:
            raise ValueError(f"maximum salt length is {salt_size} bytes")
        if len(person) > person_size:
            raise ValueError(f"maximum person length is {person_size} bytes")
        if not 0 <= fanout <= 255:
            raise ValueError("fanout must be between 0 and 255")
        if not 1 <= depth <= 255:
            raise ValueError("depth must be between 1 and 255")
        if not 0 <= leaf_size <= 0xFFFFFFFF:
            raise OverflowError("leaf_size is too large")
        if not 0 <= node_offset <= max_offset:
            raise OverflowError("node_offset is too large")
        if not 0 <= node_depth <= 255:
            raise ValueError("node_depth must be between 0 and 255")
        if not 0 <= inner_size <= max_digest:
            raise ValueError(f"inner_size must be between 0 and is {max_digest}")
        state = _hashcore.blake2_new(name == "blake2b", digest_size, key, salt, person,
                                     fanout, depth, leaf_size, node_offset, node_depth,
                                     inner_size, bool(last_node))
        h = _Hash(name, digest_size, block_size, state)
        data = _initial(data, string)
        if data is not None:
            h.update(data)
        return h

    constructor.__name__ = constructor.__qualname__ = name
    constructor.SALT_SIZE = salt_size
    constructor.PERSON_SIZE = person_size
    constructor.MAX_KEY_SIZE = max_key
    constructor.MAX_DIGEST_SIZE = max_digest
    return constructor
