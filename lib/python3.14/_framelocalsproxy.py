"""protoPython: frame.f_locals for function frames (PEP 667).

CPython 3.13+ answers frame.f_locals of a function frame with a
FrameLocalsProxy, a mapping that reads the frame's variables as they are
now and writes through to them.  The interpreter's frame getter builds one
of these over two private hooks:

  sys._protopy_frame_locals(frame)        a dict snapshot of the variables
  sys._protopy_frame_set_local(frame, k, v)
                                          a store into the variable (its
                                          fast slot, its cell, or the frame)

A function whose variables live in fast slots has no room for a name it
does not define: storing one raises ValueError (CPython keeps such names in
an extra dict of the frame).
"""

import sys

_snapshot = sys._protopy_frame_locals
_set_local = sys._protopy_frame_set_local

__all__ = ["FrameLocalsProxy"]


class FrameLocalsProxy:
    __slots__ = ("_frame", "_last")

    def __init__(self, frame):
        self._frame = frame
        self._last = {}
        self._dict()

    def _dict(self):
        # None: the function returned and its fast slots are gone; its
        # variables are the values last read (or stored) through this proxy.
        d = _snapshot(self._frame)
        if d is None:
            return self._last
        self._last = d
        return d

    def __getitem__(self, key):
        return self._dict()[key]

    def __setitem__(self, key, value):
        if not isinstance(key, str):
            raise TypeError("frame locals keys must be str, not %s" % type(key).__name__)
        if _snapshot(self._frame) is None:
            self._last[key] = value
            return
        if not _set_local(self._frame, key, value):
            raise ValueError("cannot add the variable %r to the locals of a "
                             "function frame (protoPython)" % key)

    def __delitem__(self, key):
        if key in self._dict():
            raise ValueError("cannot remove variables from FrameLocalsProxy")
        raise KeyError(key)

    def __contains__(self, key):
        return key in self._dict()

    def __iter__(self):
        return iter(list(self._dict()))

    def __reversed__(self):
        return reversed(list(self._dict()))

    def __len__(self):
        return len(self._dict())

    def keys(self):
        return self._dict().keys()

    def values(self):
        return self._dict().values()

    def items(self):
        return self._dict().items()

    def get(self, key, default=None):
        return self._dict().get(key, default)

    def setdefault(self, key, default=None):
        d = self._dict()
        if key in d:
            return d[key]
        self[key] = default
        return default

    def pop(self, key, *default):
        if key in self._dict():
            raise ValueError("cannot remove variables from FrameLocalsProxy")
        if default:
            return default[0]
        raise KeyError(key)

    def update(self, other=(), /, **kwargs):
        if hasattr(other, "keys"):
            for k in other.keys():
                self[k] = other[k]
        else:
            for k, v in other:
                self[k] = v
        for k, v in kwargs.items():
            self[k] = v

    def copy(self):
        return self._dict()

    def __eq__(self, other):
        if isinstance(other, FrameLocalsProxy):
            other = other._dict()
        return self._dict() == other

    def __ne__(self, other):
        return not self == other

    def __or__(self, other):
        d = self._dict()
        d.update(other)
        return d

    def __ror__(self, other):
        d = dict(other)
        d.update(self._dict())
        return d

    def __ior__(self, other):
        self.update(other)
        return self

    def __repr__(self):
        return repr(self._dict())

    __hash__ = None


FrameLocalsProxy.__module__ = "builtins"

try:
    import _collections_abc
    _collections_abc.Mapping.register(FrameLocalsProxy)
    del _collections_abc
except ImportError:
    pass
