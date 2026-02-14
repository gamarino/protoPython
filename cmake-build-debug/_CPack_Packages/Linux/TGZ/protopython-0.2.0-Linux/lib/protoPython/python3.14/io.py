# io.py - Python wrapper for _io. StringIO, TextIOWrapper. See STUBS.md.
import _io

open = _io.open


class TextIOWrapper:
    """Minimal TextIOWrapper: wraps a binary buffer for text read/write with encoding."""

    def __init__(self, buffer, encoding='utf-8', errors='strict'):
        self._buffer = buffer
        self._encoding = encoding
        self._errors = errors

    def read(self, size=-1):
        data = self._buffer.read(size) if hasattr(self._buffer, 'read') else b''
        if isinstance(data, bytes):
            return data.decode(self._encoding, self._errors)
        return data if isinstance(data, str) else str(data)

    def write(self, s):
        if isinstance(s, str):
            data = s.encode(self._encoding, self._errors)
        else:
            data = s
        return self._buffer.write(data) if hasattr(self._buffer, 'write') else 0

    def close(self):
        if hasattr(self._buffer, 'close'):
            self._buffer.close()


class BytesIO:
    """Minimal BytesIO: in-memory buffer with read, write, getvalue."""

    def __init__(self, initial_bytes=b""):
        self._value = initial_bytes if isinstance(initial_bytes, bytes) else b""

    def getvalue(self):
        return self._value

    def read(self, size=-1):
        if size < 0:
            result = self._value
            self._value = b""
            return result
        result = self._value[:size]
        self._value = self._value[size:]
        return result

    def write(self, b):
        if isinstance(b, bytes):
            self._value += b
            return len(b)
        return 0


class StringIO:
    """Minimal StringIO: getvalue, read, write. Full implementation would extend buffer semantics."""

    def __init__(self, initial_value="", newline="\n"):
        self._value = initial_value if isinstance(initial_value, str) else ""

    def getvalue(self):
        return self._value

    def read(self, size=-1):
        return self._value

    def write(self, s):
        if isinstance(s, str):
            self._value += s
        return len(s) if s else 0
