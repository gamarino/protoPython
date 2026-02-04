# io.py - Python wrapper for _io. StringIO: minimal getvalue/read/write. See STUBS.md.
import _io

open = _io.open


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
