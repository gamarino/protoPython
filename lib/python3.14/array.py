"""The array module: compact sequences of basic values (CPython's API).

An array holds values of one C type, given by its typecode, and converts
them to and from machine bytes with the struct module.  This replaces a stub
that kept raw bytes, so every typecode wider than one byte read back wrong
(array('i', ...).tolist() listed bytes) and fromfile, tofile, append,
byteswap and the sequence operations were missing.
"""

import struct as _struct
import sys as _sys

__all__ = ['ArrayType', 'array', 'typecodes']

typecodes = 'bBuwhHiIlLqQfd'

# Item sizes as CPython has them on this platform: C long is 8 bytes on
# 64-bit Linux and macOS and 4 on Windows; wchar_t ('u') is 2 bytes on
# Windows and 4 elsewhere.
_LONG_SIZE = 4 if _sys.platform == 'win32' else _struct.calcsize('l')
_WCHAR_SIZE = 2 if _sys.platform == 'win32' else 4

# typecode -> (struct code, item size)
_FORMATS = {
    'b': ('b', 1), 'B': ('B', 1),
    'u': ('H' if _WCHAR_SIZE == 2 else 'I', _WCHAR_SIZE),
    'w': ('I', 4),
    'h': ('h', 2), 'H': ('H', 2),
    'i': ('i', 4), 'I': ('I', 4),
    'l': ('q' if _LONG_SIZE == 8 else 'i', _LONG_SIZE),
    'L': ('Q' if _LONG_SIZE == 8 else 'I', _LONG_SIZE),
    'q': ('q', 8), 'Q': ('Q', 8),
    'f': ('f', 4), 'd': ('d', 8),
}

_RANGES = {}
for _code, (_fmt, _size) in _FORMATS.items():
    if _code in 'fduw':
        continue
    if _fmt.isupper():
        _RANGES[_code] = (0, (1 << (8 * _size)) - 1)
    else:
        _RANGES[_code] = (-(1 << (8 * _size - 1)), (1 << (8 * _size - 1)) - 1)
del _code, _fmt, _size


def _bad_typecode():
    return ValueError("bad typecode (must be b, B, u, w, h, H, i, I, l, L, "
                      "q, Q, f or d)")


class array:
    """array(typecode[, initializer]) -> array

    Return a new array whose items are restricted by typecode, and
    initialized from the optional initializer value, which must be a bytes
    or bytearray object, a Unicode string, or iterable over elements of the
    appropriate type."""

    __hash__ = None

    def __init__(self, typecode, initializer=None):
        if not isinstance(typecode, str) or len(typecode) != 1:
            raise TypeError("array() argument 1 must be a unicode character, not %s"
                            % type(typecode).__name__)
        if typecode not in _FORMATS:
            raise _bad_typecode()
        self._typecode = typecode
        self._fmt = '=' + _FORMATS[typecode][0]
        self._items = []
        if initializer is None:
            return
        if isinstance(initializer, str):
            if typecode not in 'uw':
                raise TypeError("cannot use a str to initialize an array with "
                                "typecode '%s'" % typecode)
            self.fromunicode(initializer)
        elif isinstance(initializer, (bytes, bytearray)):
            self.frombytes(initializer)
        elif isinstance(initializer, array):
            if initializer._typecode in 'uw' and typecode not in 'uw':
                raise TypeError("cannot use a unicode array to initialize an "
                                "array with typecode '%s'" % typecode)
            self._items = [self._check(v) for v in initializer._items]
        else:
            self.extend(initializer)

    @property
    def typecode(self):
        """the typecode character used to create the array"""
        return self._typecode

    @property
    def itemsize(self):
        """the size, in bytes, of one array item"""
        return _FORMATS[self._typecode][1]

    # -- item conversion ---------------------------------------------------

    def _check(self, value):
        code = self._typecode
        if code in 'uw':
            if not isinstance(value, str) or len(value) != 1:
                raise TypeError("array item must be a unicode character, not %s"
                                % type(value).__name__)
            return value
        if code in 'fd':
            if isinstance(value, (int, float)):
                return float(value)
            if hasattr(value, '__float__') or hasattr(value, '__index__'):
                return float(value)
            raise TypeError("must be real number, not %s" % type(value).__name__)
        if isinstance(value, float) or not hasattr(value, '__index__'):
            raise TypeError("'%s' object cannot be interpreted as an integer"
                            % type(value).__name__)
        value = value.__index__()
        lo, hi = _RANGES[code]
        if not lo <= value <= hi:
            if code in 'bB':
                raise OverflowError("%s integer is %s than %s" % (
                    "unsigned byte" if code == 'B' else "signed char",
                    "less" if value < lo else "greater",
                    "minimum" if value < lo else "maximum"))
            raise OverflowError("Python int too large to convert to C %s"
                                % ("unsigned long" if code.isupper() else "long"))
        return value

    def _pack(self, value):
        if self._typecode in 'uw':
            return _struct.pack(self._fmt, ord(value))
        return _struct.pack(self._fmt, value)

    def _unpack_all(self, data):
        size = self.itemsize
        n = len(data) // size
        values = list(_struct.unpack('=%d%s' % (n, self._fmt[1:]), data[:n * size]))
        if self._typecode in 'uw':
            values = [chr(v) for v in values]
        return values

    def _same_kind(self, other, what):
        if not isinstance(other, array):
            raise TypeError("can only %s array (not \"%s\") to array"
                            % (what, type(other).__name__))
        if other._typecode != self._typecode:
            raise TypeError("bad argument type for built-in operation")

    def _new(self, items):
        result = array(self._typecode)
        result._items = items
        return result

    # -- building ------------------------------------------------------------

    def append(self, v):
        """Append new value v to the end of the array."""
        self._items.append(self._check(v))

    def extend(self, iterable):
        """Append items to the end of the array."""
        if isinstance(iterable, array):
            if iterable._typecode != self._typecode:
                raise TypeError("can only extend with array of same kind")
            self._items.extend(iterable._items)
            return
        for v in iterable:
            self._items.append(self._check(v))

    def insert(self, i, v):
        """Insert a new item v into the array before position i."""
        self._items.insert(i, self._check(v))

    def frombytes(self, buffer):
        """Appends items from the bytes-like object, interpreting it as an
        array of machine values."""
        data = bytes(memoryview(buffer).cast('B')) if not isinstance(buffer, (bytes, bytearray)) else bytes(buffer)
        if len(data) % self.itemsize:
            raise ValueError("bytes length not a multiple of item size")
        self._items.extend(self._unpack_all(data))

    def fromfile(self, f, n):
        """Read n objects from the file object f and append them to the end
        of the array."""
        if not isinstance(n, int):
            raise TypeError("'%s' object cannot be interpreted as an integer"
                            % type(n).__name__)
        if n < 0:
            raise ValueError("negative count")
        nbytes = n * self.itemsize
        data = f.read(nbytes)
        if not isinstance(data, (bytes, bytearray)):
            raise TypeError("read() didn't return bytes")
        whole = len(data) - len(data) % self.itemsize
        self.frombytes(data[:whole])
        if len(data) != nbytes:
            raise EOFError("read() didn't return enough bytes")

    def fromlist(self, list):
        """Append items to array from list."""
        if not isinstance(list, type([])):
            raise TypeError("arg must be list")
        checked = [self._check(v) for v in list]
        self._items.extend(checked)

    def fromunicode(self, ustr):
        """Extends this array with data from the unicode string ustr."""
        if self._typecode not in 'uw':
            raise ValueError("fromunicode() may only be called on unicode "
                             "type arrays ('u' or 'w')")
        if not isinstance(ustr, str):
            raise TypeError("fromunicode() argument must be str, not %s"
                            % type(ustr).__name__)
        self._items.extend(ustr)

    # -- export ----------------------------------------------------------------

    def tobytes(self):
        """Convert the array to an array of machine values and return the
        bytes representation."""
        if self._typecode in 'uw':
            values = [ord(c) for c in self._items]
        else:
            values = self._items
        return _struct.pack('=%d%s' % (len(values), self._fmt[1:]), *values)

    def tofile(self, f):
        """Write all items (as machine values) to the file object f."""
        f.write(self.tobytes())

    def tolist(self):
        """Convert array to an ordinary list with the same items."""
        return list(self._items)

    def tounicode(self):
        """Extends this array with data from the unicode string ustr."""
        if self._typecode not in 'uw':
            raise ValueError("tounicode() may only be called on unicode type "
                             "arrays ('u' or 'w')")
        return ''.join(self._items)

    def buffer_info(self):
        """Return a tuple (address, length) giving the current memory
        address and the length in items of the buffer."""
        return (id(self), len(self._items))

    def __buffer__(self, flags):
        return memoryview(self.tobytes()).cast(self._fmt[1:] if self._typecode not in 'uw' else 'B')

    def __reduce_ex__(self, protocol):
        return (array, (self._typecode, list(self._items)), getattr(self, '__dict__', None))

    def __copy__(self):
        return self._new(list(self._items))

    def __deepcopy__(self, memo):
        return self._new(list(self._items))

    # -- editing -------------------------------------------------------------

    def byteswap(self):
        """Byteswap all items of the array."""
        if self.itemsize == 1:
            return
        size = self.itemsize
        data = self.tobytes()
        swapped = b''.join(data[i:i + size][::-1] for i in range(0, len(data), size))
        self._items = self._unpack_all(swapped)

    def count(self, v):
        """Return number of occurrences of v in the array."""
        return sum(1 for x in self._items if x == v)

    def index(self, v, start=0, stop=_sys.maxsize):
        """Return index of first occurrence of v in the array."""
        n = len(self._items)
        if start < 0:
            start = max(start + n, 0)
        if stop < 0:
            stop += n
        for i in range(start, min(stop, n)):
            if self._items[i] == v:
                return i
        raise ValueError("array.index(x): x not in array")

    def pop(self, i=-1):
        """Return the i-th element and delete it from the array."""
        if not self._items:
            raise IndexError("pop from empty array")
        try:
            return self._items.pop(i)
        except IndexError:
            raise IndexError("pop index out of range") from None

    def remove(self, v):
        """Remove the first occurrence of v in the array."""
        for i, x in enumerate(self._items):
            if x == v:
                del self._items[i]
                return
        raise ValueError("array.remove(x): x not in array")

    def reverse(self):
        """Reverse the order of the items in the array."""
        self._items.reverse()

    # -- sequence protocol -----------------------------------------------------

    def __len__(self):
        return len(self._items)

    def __iter__(self):
        return iter(list(self._items))

    def __contains__(self, v):
        return v in self._items

    def __getitem__(self, index):
        if isinstance(index, slice):
            return self._new(self._items[index])
        try:
            return self._items[index]
        except IndexError:
            raise IndexError("array index out of range") from None

    def __setitem__(self, index, value):
        if isinstance(index, slice):
            if not isinstance(value, array):
                raise TypeError("can only assign array (not \"%s\") to array slice"
                                % type(value).__name__)
            if value._typecode != self._typecode:
                raise TypeError("bad argument type for built-in operation")
            self._items[index] = list(value._items)
            return
        try:
            self._items[index] = self._check(value)
        except IndexError:
            raise IndexError("array assignment index out of range") from None

    def __delitem__(self, index):
        try:
            del self._items[index]
        except IndexError:
            raise IndexError("array assignment index out of range") from None

    def __add__(self, other):
        self._same_kind(other, "append")
        return self._new(self._items + other._items)

    def __iadd__(self, other):
        self._same_kind(other, "extend")
        self._items.extend(other._items)
        return self

    def __mul__(self, n):
        if not hasattr(n, '__index__'):
            return NotImplemented
        return self._new(self._items * n.__index__())

    __rmul__ = __mul__

    def __imul__(self, n):
        if not hasattr(n, '__index__'):
            return NotImplemented
        self._items *= n.__index__()
        return self

    def _cmp_items(self, other):
        if not isinstance(other, array):
            return NotImplemented
        return other._items

    def __eq__(self, other):
        items = self._cmp_items(other)
        return items if items is NotImplemented else self._items == items

    def __ne__(self, other):
        items = self._cmp_items(other)
        return items if items is NotImplemented else self._items != items

    def __lt__(self, other):
        items = self._cmp_items(other)
        return items if items is NotImplemented else self._items < items

    def __le__(self, other):
        items = self._cmp_items(other)
        return items if items is NotImplemented else self._items <= items

    def __gt__(self, other):
        items = self._cmp_items(other)
        return items if items is NotImplemented else self._items > items

    def __ge__(self, other):
        items = self._cmp_items(other)
        return items if items is NotImplemented else self._items >= items

    def __repr__(self):
        name = type(self).__name__
        if not self._items:
            return "%s('%s')" % (name, self._typecode)
        if self._typecode in 'uw':
            return "%s('%s', %r)" % (name, self._typecode, ''.join(self._items))
        return "%s('%s', %r)" % (name, self._typecode, self._items)

    def __class_getitem__(cls, item):
        import types
        return types.GenericAlias(cls, item)


ArrayType = array
