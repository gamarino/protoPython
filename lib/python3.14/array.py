"""Minimal array module stub for protoPython."""

typecodes = 'bBuhHiIlLqQfd'

_ITEMSIZES = {
    'b': 1, 'B': 1, 'u': 2,
    'h': 2, 'H': 2,
    'i': 4, 'I': 4, 'l': 4, 'L': 4, 'f': 4,
    'q': 8, 'Q': 8, 'd': 8,
}


class array:
    """array(typecode[, initializer]) -> array"""

    typecodes = typecodes

    def __init__(self, typecode, initializer=b''):
        if typecode not in typecodes:
            raise ValueError(
                "bad typecode (must be b, B, u, h, H, i, I, l, L, q, Q, f or d)"
            )
        self.typecode = typecode
        self.itemsize = _ITEMSIZES.get(typecode, 1)
        # Do not call bytes() — it is broken for bytes input in protoPython.
        # Store the raw bytes-like object directly; memoryview will extract it.
        if isinstance(initializer, (bytes, bytearray)):
            self._data = initializer
        elif hasattr(initializer, '__iter__'):
            try:
                self._data = b''.join(bytes([x]) for x in initializer)
            except (TypeError, ValueError):
                self._data = b''
        else:
            self._data = b''

    def tobytes(self):
        return self._data

    def tolist(self):
        return list(self._data)

    def __len__(self):
        return len(self._data)

    def __iter__(self):
        return iter(self._data)

    def __bytes__(self):
        return self._data

    def __getitem__(self, idx):
        return self._data[idx]

    def __repr__(self):
        return f"array('{self.typecode}', {list(self._data)!r})"

    def __buffer__(self, flags):
        return memoryview(self._data).__buffer__(flags)
