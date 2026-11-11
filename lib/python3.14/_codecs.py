"""protoPython: the _codecs module, in Python.

CPython implements _codecs in C. Here the codec registry (register, lookup,
register_error, lookup_error) lives in this module, and the codec functions
the `encodings` package builds on (utf_8_*, latin_1_*, ascii_*, charmap_*)
use the interpreter's native str.encode and bytes.decode for the codecs it
implements natively.
"""

__all__ = [
    "register", "unregister", "lookup", "encode", "decode",
    "register_error", "lookup_error", "_forget_codec",
    "utf_8_encode", "utf_8_decode",
    "latin_1_encode", "latin_1_decode",
    "ascii_encode", "ascii_decode",
    "charmap_encode", "charmap_decode", "charmap_build",
    "utf_16_encode", "utf_16_decode", "utf_16_le_encode", "utf_16_le_decode",
    "utf_16_be_encode", "utf_16_be_decode", "utf_16_ex_decode",
    "utf_32_encode", "utf_32_decode", "utf_32_le_encode", "utf_32_le_decode",
    "utf_32_be_encode", "utf_32_be_decode", "utf_32_ex_decode",
    "utf_7_encode", "utf_7_decode",
    "raw_unicode_escape_encode", "raw_unicode_escape_decode",
    "unicode_escape_encode", "unicode_escape_decode",
    "strict_errors", "ignore_errors", "replace_errors",
    "xmlcharrefreplace_errors", "backslashreplace_errors", "namereplace_errors",
    "surrogateescape_errors", "surrogatepass_errors",
]

import sys as _sys
import _codecs_native

_search_path = []
_search_cache = {}
_error_registry = {}


# --- Registry ---------------------------------------------------------------

def _normalize(encoding):
    # As CPython's lookup: lower case, and every run of characters other
    # than letters, digits and '.' becomes one '_' (encodings.normalize_encoding).
    chars = []
    punct = False
    for c in encoding.lower():
        if c.isalnum() or c == '.':
            if punct and chars:
                chars.append('_')
            chars.append(c)
            punct = False
        else:
            punct = True
    return ''.join(chars)


def register(search_function):
    """Register a codec search function."""
    if not callable(search_function):
        raise TypeError("argument must be callable")
    _search_path.append(search_function)


def unregister(search_function):
    """Unregister a codec search function and clear the registry's cache."""
    if search_function in _search_path:
        _search_path.remove(search_function)
    _search_cache.clear()


def _forget_codec(encoding):
    _search_cache.pop(_normalize(encoding), None)


def lookup(encoding):
    """Look up a codec tuple in the codec registry and return a CodecInfo."""
    if not isinstance(encoding, str):
        raise TypeError("lookup() argument must be str, not %s" % type(encoding).__name__)
    norm = _normalize(encoding)
    result = _search_cache.get(norm)
    if result is not None:
        return result
    if not _search_path:
        import encodings  # noqa: F401  (registers its search function)
    for search_function in list(_search_path):
        result = search_function(norm)
        if result is None:
            continue
        if not isinstance(result, tuple) or len(result) != 4:
            raise TypeError("codec search functions must return 4-tuples")
        _search_cache[norm] = result
        return result
    raise LookupError("unknown encoding: %s" % encoding)


def encode(obj, encoding='utf-8', errors='strict'):
    """Encode obj using the codec registered for encoding."""
    result = lookup(encoding)[0](obj, errors)
    if not isinstance(result, tuple) or len(result) != 2:
        raise TypeError("encoder must return a tuple (object, integer)")
    return result[0]


def decode(obj, encoding='utf-8', errors='strict'):
    """Decode obj using the codec registered for encoding."""
    result = lookup(encoding)[1](obj, errors)
    if not isinstance(result, tuple) or len(result) != 2:
        raise TypeError("decoder must return a tuple (object,integer)")
    return result[0]


def register_error(errors, handler):
    """Register handler as the error handler named errors."""
    if not callable(handler):
        raise TypeError("handler must be callable")
    _error_registry[errors] = handler


def lookup_error(errors):
    """Return the error handler registered under the name errors."""
    try:
        return _error_registry[errors]
    except KeyError:
        raise LookupError("unknown error handler name '%s'" % errors) from None


# --- Built-in error handlers -------------------------------------------------

def _check_exc(exc):
    if not isinstance(exc, UnicodeError):
        raise TypeError("don't know how to handle %s in error callback" % type(exc).__name__)


def strict_errors(exc):
    if isinstance(exc, BaseException):
        raise exc
    raise TypeError("codec must pass exception instance")


def ignore_errors(exc):
    _check_exc(exc)
    return ('', exc.end)


def replace_errors(exc):
    _check_exc(exc)
    if isinstance(exc, UnicodeEncodeError):
        return ('?' * (exc.end - exc.start), exc.end)
    if isinstance(exc, UnicodeDecodeError):
        return ('�', exc.end)
    return ('�' * (exc.end - exc.start), exc.end)


def xmlcharrefreplace_errors(exc):
    if not isinstance(exc, UnicodeEncodeError):
        _check_exc(exc)
        raise TypeError("don't know how to handle %s in error callback" % type(exc).__name__)
    return (''.join('&#%d;' % ord(c) for c in exc.object[exc.start:exc.end]), exc.end)


def backslashreplace_errors(exc):
    _check_exc(exc)
    if isinstance(exc, UnicodeDecodeError):
        return (''.join('\\x%02x' % b for b in exc.object[exc.start:exc.end]), exc.end)
    out = []
    for c in exc.object[exc.start:exc.end]:
        n = ord(c)
        if n < 0x100:
            out.append('\\x%02x' % n)
        elif n < 0x10000:
            out.append('\\u%04x' % n)
        else:
            out.append('\\U%08x' % n)
    return (''.join(out), exc.end)


def namereplace_errors(exc):
    if not isinstance(exc, UnicodeEncodeError):
        _check_exc(exc)
        raise TypeError("don't know how to handle %s in error callback" % type(exc).__name__)
    import unicodedata
    out = []
    for c in exc.object[exc.start:exc.end]:
        try:
            out.append('\\N{%s}' % unicodedata.name(c))
        except ValueError:
            n = ord(c)
            out.append('\\x%02x' % n if n < 0x100 else
                       '\\u%04x' % n if n < 0x10000 else '\\U%08x' % n)
    return (''.join(out), exc.end)


def surrogateescape_errors(exc):
    if isinstance(exc, UnicodeDecodeError):
        # As CPython: up to four bytes of the range, stopping at the first
        # ASCII byte; an error that starts with an ASCII byte is re-raised.
        out = []
        for b in exc.object[exc.start:exc.end][:4]:
            if b < 128:
                break
            out.append(chr(0xDC00 + b))
        if out:
            return (''.join(out), exc.start + len(out))
        raise exc
    elif isinstance(exc, UnicodeEncodeError):
        out = bytearray()
        for c in exc.object[exc.start:exc.end]:
            n = ord(c)
            if not 0xDC80 <= n <= 0xDCFF:
                raise exc
            out.append(n - 0xDC00)
        return (bytes(out), exc.end)
    raise exc if isinstance(exc, UnicodeError) else TypeError(
        "don't know how to handle %s in error callback" % type(exc).__name__)


def _surrogatepass_unit(encoding):
    # (unit size, byte order) of the UTF codec an exception names; None for
    # a codec surrogatepass does not apply to.
    e = encoding.lower().replace('_', '-')
    if e in ('utf-8', 'utf8'):
        return (1, None)
    for prefix, size in (('utf-16', 2), ('utf-32', 4)):
        if e == prefix:
            return (size, 'little' if _sys.byteorder == 'little' else 'big')
        if e == prefix + '-le':
            return (size, 'little')
        if e == prefix + '-be':
            return (size, 'big')
    return None


def surrogatepass_errors(exc):
    """Encode or decode lone surrogates as UTF-8/16/32 code units."""
    if isinstance(exc, UnicodeEncodeError):
        unit = _surrogatepass_unit(exc.encoding)
        if unit is None:
            raise exc
        out = bytearray()
        for c in exc.object[exc.start:exc.end]:
            n = ord(c)
            if not 0xD800 <= n <= 0xDFFF:
                raise exc
            if unit[0] == 1:
                out += bytes((0xE0 | (n >> 12), 0x80 | ((n >> 6) & 0x3F), 0x80 | (n & 0x3F)))
            else:
                out += n.to_bytes(unit[0], unit[1])
        return (bytes(out), exc.end)
    if isinstance(exc, UnicodeDecodeError):
        unit = _surrogatepass_unit(exc.encoding)
        if unit is None:
            raise exc
        data = exc.object
        p = exc.start
        if unit[0] == 1:
            b = data[p:p + 3]
            if (len(b) == 3 and b[0] & 0xF0 == 0xE0 and b[1] & 0xC0 == 0x80
                    and b[2] & 0xC0 == 0x80):
                n = ((b[0] & 0x0F) << 12) | ((b[1] & 0x3F) << 6) | (b[2] & 0x3F)
                if 0xD800 <= n <= 0xDFFF:
                    return (chr(n), p + 3)
            raise exc
        b = data[p:p + unit[0]]
        if len(b) == unit[0]:
            n = int.from_bytes(b, unit[1])
            if 0xD800 <= n <= 0xDFFF:
                return (chr(n), p + unit[0])
        raise exc
    raise exc if isinstance(exc, UnicodeError) else TypeError(
        "don't know how to handle %s in error callback" % type(exc).__name__)


for _name, _handler in (('strict', strict_errors), ('ignore', ignore_errors),
                        ('replace', replace_errors),
                        ('xmlcharrefreplace', xmlcharrefreplace_errors),
                        ('backslashreplace', backslashreplace_errors),
                        ('namereplace', namereplace_errors),
                        ('surrogateescape', surrogateescape_errors),
                        ('surrogatepass', surrogatepass_errors)):
    _error_registry[_name] = _handler
del _name, _handler


# --- Codec functions ----------------------------------------------------------

def _errors(errors):
    return 'strict' if errors is None else errors


def utf_8_encode(s, errors=None):
    return (s.encode('utf-8', _errors(errors)), len(s))


def _utf8_incomplete_tail(data):
    # The number of bytes at the end of data that start a UTF-8 sequence
    # without completing it (a decoder that is not final keeps them).
    n = len(data)
    i = n - 1
    while i >= 0 and n - i <= 3 and 0x80 <= data[i] <= 0xBF:
        i -= 1
    if i < 0:
        return 0
    lead = data[i]
    if 0xC2 <= lead <= 0xDF:
        need = 2
    elif 0xE0 <= lead <= 0xEF:
        need = 3
    elif 0xF0 <= lead <= 0xF4:
        need = 4
    else:
        return 0
    have = n - i
    return have if have < need else 0


def utf_8_decode(data, errors=None, final=False):
    data = bytes(data)
    end = len(data) if final else len(data) - _utf8_incomplete_tail(data)
    return (data[:end].decode('utf-8', _errors(errors)), end)


def latin_1_encode(s, errors=None):
    return (s.encode('latin-1', _errors(errors)), len(s))


def latin_1_decode(data, errors=None, final=False):
    data = bytes(data)
    return (data.decode('latin-1', _errors(errors)), len(data))


def ascii_encode(s, errors=None):
    return (s.encode('ascii', _errors(errors)), len(s))


def ascii_decode(data, errors=None, final=False):
    data = bytes(data)
    return (data.decode('ascii', _errors(errors)), len(data))


def _escape_code_point(cp):
    if cp < 0x100:
        return b'\\x%02x' % cp
    if cp < 0x10000:
        return b'\\u%04x' % cp
    return b'\\U%08x' % cp


def raw_unicode_escape_encode(s, errors=None):
    # Code points below 256 are one byte; others \uXXXX or \UXXXXXXXX.
    out = bytearray()
    for c in s:
        cp = ord(c)
        if cp < 0x100:
            out.append(cp)
        elif cp < 0x10000:
            out += b'\\u%04x' % cp
        else:
            out += b'\\U%08x' % cp
    return (bytes(out), len(s))


def unicode_escape_encode(s, errors=None):
    # As a str literal's body, without quotes.
    out = bytearray()
    for c in s:
        cp = ord(c)
        if c == '\\':
            out += b'\\\\'
        elif c == '\t':
            out += b'\\t'
        elif c == '\n':
            out += b'\\n'
        elif c == '\r':
            out += b'\\r'
        elif 0x20 <= cp < 0x7f:
            out.append(cp)
        else:
            out += _escape_code_point(cp)
    return (bytes(out), len(s))


_HEX = b'0123456789abcdefABCDEF'


def _escape_decode(data, errors, raw, codec):
    data = bytes(data)
    handler = None
    out = []
    i = 0
    n = len(data)

    def fail(start, end, reason):
        nonlocal handler
        if handler is None:
            handler = lookup_error(_errors(errors))
        rep, pos = handler(UnicodeDecodeError(codec, data, start, end, reason))
        out.append(rep)
        return pos

    while i < n:
        b = data[i]
        if b != 0x5c:  # backslash
            out.append(chr(b))
            i += 1
            continue
        if raw:
            # Only \u and \U escape, after an odd run of backslashes.
            j = i
            while j < n and data[j] == 0x5c:
                j += 1
            run = j - i
            if run % 2 == 0 or j >= n or data[j] not in b'uU':
                out.append('\\' * run)
                i = j
                continue
            out.append('\\' * (run - 1))
            i = j - 1
        start = i
        if i + 1 >= n:
            i = fail(start, n, '\\ at end of string')
            continue
        e = chr(data[i + 1])
        i += 2
        simple = {'\n': '', '\\': '\\', "'": "'", '"': '"', 'a': '\a', 'b': '\b',
                  'f': '\f', 'n': '\n', 'r': '\r', 't': '\t', 'v': '\v'}
        if not raw and e in simple:
            out.append(simple[e])
        elif not raw and e in '01234567':
            j = i - 1
            k = j
            while k < n and k < j + 3 and chr(data[k]) in '01234567':
                k += 1
            out.append(chr(int(data[j:k], 8)))
            i = k
        elif e in 'xuU' and not (raw and e == 'x'):
            width = {'x': 2, 'u': 4, 'U': 8}[e]
            k = i
            while k < n and k < i + width and data[k] in _HEX:
                k += 1
            if k - i < width:
                i = fail(start, k, 'truncated \\%sXX escape' % e if e == 'x' else
                         'truncated \\%s%s escape' % (e, 'X' * width))
                continue
            cp = int(data[i:k], 16)
            if cp > 0x10ffff:
                i = fail(start, k, 'illegal Unicode character')
                continue
            out.append(chr(cp))
            i = k
        elif not raw and e == 'N':
            close = data.find(b'}', i)
            if i >= n or data[i] != 0x7b or close < 0:
                i = fail(start, n if close < 0 else close + 1, 'malformed \\N character escape')
                continue
            import unicodedata
            try:
                out.append(unicodedata.lookup(data[i + 1:close].decode('ascii')))
            except (KeyError, UnicodeDecodeError):
                i = fail(start, close + 1, 'unknown Unicode character name')
                continue
            i = close + 1
        else:
            out.append('\\' + e)
    return (''.join(out), n)


def raw_unicode_escape_decode(data, errors=None, final=True):
    return _escape_decode(data, errors, True, 'rawunicodeescape')


def unicode_escape_decode(data, errors=None, final=True):
    return _escape_decode(data, errors, False, 'unicodeescape')


# --- UTF-16 and UTF-32 ---------------------------------------------------------
#
# Written in Python, with CPython's error positions and reasons; an error
# handler sees the same UnicodeError CPython's would.

def _native_order():
    return -1 if _sys.byteorder == 'little' else 1


def _utf_encode(s, errors, size, order, name):
    # order: -1 little, 1 big. Surrogates are errors (surrogatepass passes
    # them), reported one character at a time as CPython's encoder does.
    # The clean runs between them are encoded natively.
    endian = 'little' if order < 0 else 'big'
    little = order < 0
    s = str(s)
    out = []
    handler = None
    i = 0
    n = len(s)
    while i < n:
        chunk, i = _codecs_native.utf_encode(s, i, size, little)
        out.append(chunk)
        if i >= n:
            break
        j = i + 1
        if handler is None:
            handler = lookup_error(_errors(errors))
        rep, pos = handler(UnicodeEncodeError(name, s, i, j, 'surrogates not allowed'))
        if isinstance(rep, str):
            for c in rep:
                if 0xD800 <= ord(c) <= 0xDFFF:
                    raise UnicodeEncodeError(name, s, i, j, 'surrogates not allowed')
                out.append(_utf_unit_bytes(ord(c), size, endian))
        else:
            rep = bytes(rep)
            if len(rep) % size:
                raise UnicodeEncodeError(name, s, i, j, 'surrogates not allowed')
            out.append(rep)
        if pos < 0:
            pos += n
        if not 0 <= pos <= n:
            raise IndexError('position %d from error handler out of bounds' % pos)
        i = pos
    return b''.join(out)


def _utf_unit_bytes(cp, size, endian):
    if size == 4 or cp < 0x10000:
        return cp.to_bytes(size, endian)
    cp -= 0x10000
    return (0xD800 | (cp >> 10)).to_bytes(2, endian) + (0xDC00 | (cp & 0x3FF)).to_bytes(2, endian)


def _bom_order(data, size):
    # -1 / 1 for a little / big endian BOM at the start, 0 for none.
    if size == 2:
        if data[:2] == b'\xff\xfe':
            return -1
        if data[:2] == b'\xfe\xff':
            return 1
    else:
        if data[:4] == b'\xff\xfe\x00\x00':
            return -1
        if data[:4] == b'\x00\x00\xfe\xff':
            return 1
    return 0


def _utf16_decode(data, errors, order, final, name, i=0):
    # Returns (text, consumed) from data[i:] in byte order `order` (-1 / 1).
    endian = 'little' if order < 0 else 'big'
    out = []
    handler = None
    n = len(data)

    def fail(start, end, reason):
        nonlocal handler
        if handler is None:
            handler = lookup_error(_errors(errors))
        rep, pos = handler(UnicodeDecodeError(name, data, start, end, reason))
        out.append(rep)
        if pos < 0:
            pos += n
        if not 0 <= pos <= n:
            raise IndexError('position %d from error handler out of bounds' % pos)
        return pos

    little = order < 0
    while i < n:
        # Valid units natively; the loop below only sees an error or an
        # incomplete unit at the end.
        chunk, i = _codecs_native.utf16_decode(data, i, little)
        out.append(chunk)
        if i >= n:
            break
        if n - i < 2:
            if not final:
                break
            i = fail(i, n, 'truncated data')
            continue
        u = int.from_bytes(data[i:i + 2], endian)
        if u < 0xD800 or u > 0xDFFF:
            out.append(chr(u))
            i += 2
            continue
        if u >= 0xDC00:
            i = fail(i, i + 2, 'illegal encoding')
            continue
        if n - i < 4:
            if not final:
                break
            i = fail(i, n, 'unexpected end of data')
            continue
        u2 = int.from_bytes(data[i + 2:i + 4], endian)
        if 0xDC00 <= u2 <= 0xDFFF:
            out.append(chr(0x10000 + ((u - 0xD800) << 10) + (u2 - 0xDC00)))
            i += 4
            continue
        i = fail(i, i + 2, 'illegal UTF-16 surrogate')
    return (''.join(out), i)


def _utf32_decode(data, errors, order, final, name, i=0):
    endian = 'little' if order < 0 else 'big'
    out = []
    handler = None
    n = len(data)

    def fail(start, end, reason):
        nonlocal handler
        if handler is None:
            handler = lookup_error(_errors(errors))
        rep, pos = handler(UnicodeDecodeError(name, data, start, end, reason))
        out.append(rep)
        if pos < 0:
            pos += n
        if not 0 <= pos <= n:
            raise IndexError('position %d from error handler out of bounds' % pos)
        return pos

    little = order < 0
    while i < n:
        chunk, i = _codecs_native.utf32_decode(data, i, little)
        out.append(chunk)
        if i >= n:
            break
        if n - i < 4:
            if not final:
                break
            i = fail(i, n, 'truncated data')
            continue
        cp = int.from_bytes(data[i:i + 4], endian)
        if cp > 0x10FFFF:
            i = fail(i, i + 4, 'code point not in range(0x110000)')
            continue
        if 0xD800 <= cp <= 0xDFFF:
            i = fail(i, i + 4, 'code point in surrogate code point range(0xd800, 0xe000)')
            continue
        out.append(chr(cp))
        i += 4
    return (''.join(out), i)


def _utf_ex_decode(data, errors, byteorder, final, size):
    # (text, consumed, byteorder): a BOM decides the order when byteorder is 0.
    data = bytes(data)
    start = 0
    if byteorder == 0:
        if len(data) < size and not final and (
                (size == 2 and data in (b'\xff', b'\xfe')) or
                (size == 4 and (b'\xff\xfe\x00\x00'.startswith(data) or
                                b'\x00\x00\xfe\xff'.startswith(data)))):
            return ('', 0, 0)
        bom = _bom_order(data, size)
        if bom:
            byteorder = bom
            start = size
    order = byteorder if byteorder else _native_order()
    suffix = '-le' if order < 0 else '-be'
    name = ('utf-16' if size == 2 else 'utf-32') + suffix
    decode = _utf16_decode if size == 2 else _utf32_decode
    text, consumed = decode(data, errors, order, final, name, start)
    return (text, consumed, byteorder)


def utf_16_encode(s, errors=None, byteorder=0):
    if byteorder == 0:
        order = _native_order()
        bom = b'\xff\xfe' if order < 0 else b'\xfe\xff'
        return (bom + _utf_encode(s, errors, 2, order, 'utf-16'), len(s))
    return (_utf_encode(s, errors, 2, -1 if byteorder < 0 else 1, 'utf-16'), len(s))


def utf_16_le_encode(s, errors=None):
    return (_utf_encode(s, errors, 2, -1, 'utf-16-le'), len(s))


def utf_16_be_encode(s, errors=None):
    return (_utf_encode(s, errors, 2, 1, 'utf-16-be'), len(s))


def utf_16_decode(data, errors=None, final=False):
    text, consumed, _ = _utf_ex_decode(data, errors, 0, final, 2)
    return (text, consumed)


def utf_16_le_decode(data, errors=None, final=False):
    return _utf16_decode(bytes(data), errors, -1, final, 'utf-16-le')


def utf_16_be_decode(data, errors=None, final=False):
    return _utf16_decode(bytes(data), errors, 1, final, 'utf-16-be')


def utf_16_ex_decode(data, errors=None, byteorder=0, final=False):
    return _utf_ex_decode(data, errors, byteorder, final, 2)


def utf_32_encode(s, errors=None, byteorder=0):
    if byteorder == 0:
        order = _native_order()
        bom = b'\xff\xfe\x00\x00' if order < 0 else b'\x00\x00\xfe\xff'
        return (bom + _utf_encode(s, errors, 4, order, 'utf-32'), len(s))
    return (_utf_encode(s, errors, 4, -1 if byteorder < 0 else 1, 'utf-32'), len(s))


def utf_32_le_encode(s, errors=None):
    return (_utf_encode(s, errors, 4, -1, 'utf-32-le'), len(s))


def utf_32_be_encode(s, errors=None):
    return (_utf_encode(s, errors, 4, 1, 'utf-32-be'), len(s))


def utf_32_decode(data, errors=None, final=False):
    text, consumed, _ = _utf_ex_decode(data, errors, 0, final, 4)
    return (text, consumed)


def utf_32_le_decode(data, errors=None, final=False):
    return _utf32_decode(bytes(data), errors, -1, final, 'utf-32-le')


def utf_32_be_decode(data, errors=None, final=False):
    return _utf32_decode(bytes(data), errors, 1, final, 'utf-32-be')


def utf_32_ex_decode(data, errors=None, byteorder=0, final=False):
    return _utf_ex_decode(data, errors, byteorder, final, 4)


# --- UTF-7 (RFC 2152), CPython's encoder and decoder ----------------------------

_B64 = b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
_B64_VALUE = {c: i for i, c in enumerate(_B64)}


def utf_7_encode(s, errors=None):
    # UTF-7 encodes every code point (lone surrogates included), so the
    # encoder never reports an error: it is native whole (CodecsNative.cpp).
    return (_codecs_native.utf7_encode(str(s)), len(s))


def utf_7_decode(data, errors=None, final=False):
    data = bytes(data)
    # Well-formed input decodes natively; the loop below handles errors
    # (through the handler protocol) and a shift sequence still open.
    text = _codecs_native.utf7_decode(data)
    if text is not None:
        return (text, len(data))
    n = len(data)
    out = []
    handler = None
    in_shift = False
    bits = 0
    buf = 0
    surrogate = 0
    shift_out_start = 0
    start = 0
    i = 0

    def fail(s0, e0, reason):
        nonlocal handler
        if handler is None:
            handler = lookup_error(_errors(errors))
        rep, pos = handler(UnicodeDecodeError('utf7', data, s0, e0, reason))
        out.append(rep)
        if pos < 0:
            pos += n
        if not 0 <= pos <= n:
            raise IndexError('position %d from error handler out of bounds' % pos)
        return pos

    while True:
        while i < n:
            ch = data[i]
            if in_shift:
                if ch in _B64_VALUE:
                    buf = (buf << 6) | _B64_VALUE[ch]
                    bits += 6
                    i += 1
                    if bits >= 16:
                        unit = buf >> (bits - 16)
                        bits -= 16
                        buf &= (1 << bits) - 1
                        if surrogate:
                            if 0xDC00 <= unit <= 0xDFFF:
                                out.append(chr(0x10000 + ((surrogate - 0xD800) << 10) + (unit - 0xDC00)))
                                surrogate = 0
                                continue
                            out.append(chr(surrogate))
                            surrogate = 0
                        if 0xD800 <= unit <= 0xDBFF:
                            surrogate = unit
                        else:
                            out.append(chr(unit))
                    continue
                in_shift = False
                if bits >= 6:
                    i += 1
                    i = fail(start, i, 'partial character in shift sequence')
                    continue
                if bits > 0 and buf != 0:
                    i += 1
                    i = fail(start, i, 'non-zero padding bits in shift sequence')
                    continue
                if surrogate and ch <= 127 and ch != 0x2B:
                    out.append(chr(surrogate))
                surrogate = 0
                if ch == 0x2D:
                    i += 1
            elif ch == 0x2B:
                start = i
                i += 1
                if i < n and data[i] == 0x2D:
                    i += 1
                    out.append('+')
                elif i < n and data[i] not in _B64_VALUE:
                    i += 1
                    i = fail(start, i, 'ill-formed sequence')
                else:
                    in_shift = True
                    surrogate = 0
                    shift_out_start = len(out)
                    bits = 0
                    buf = 0
            elif ch <= 127:
                i += 1
                out.append(chr(ch))
            else:
                start = i
                i += 1
                i = fail(start, i, 'unexpected special character')
        if in_shift and final:
            in_shift = False
            if surrogate or bits >= 6 or (bits > 0 and buf != 0):
                i = fail(start, n, 'unterminated shift sequence')
                if i < n:
                    continue
        break
    if in_shift and not final:
        del out[shift_out_start:]
        return (''.join(out), start)
    return (''.join(out), i)


class _EncodingMap(dict):
    """charmap_build's result: the inverse of a decoding table, as a dict
    (as CPython's EncodingMap answers lookups), remembering the table so
    charmap_encode converts natively."""
    __slots__ = ('_table',)


def charmap_build(decoding_table):
    m = _EncodingMap({ord(c): i for i, c in enumerate(decoding_table)})
    m._table = decoding_table if isinstance(decoding_table, str) and len(decoding_table) <= 256 else None
    return m


def charmap_decode(data, errors=None, mapping=None):
    data = bytes(data)
    if mapping is None:
        return latin_1_decode(data, errors)
    handler = None
    out = []
    i = 0
    n = len(data)
    table = mapping if type(mapping) is str else None
    while i < n:
        if table is not None:
            chunk, i = _codecs_native.charmap_decode(data, i, table)
            out.append(chunk)
            if i >= n:
                break
        b = data[i]
        if isinstance(mapping, str):
            c = mapping[b] if b < len(mapping) else '\ufffe'
        else:
            c = mapping.get(b)
            if isinstance(c, int):
                c = chr(c)
        if c is None or c == '\ufffe':
            if handler is None:
                handler = lookup_error(_errors(errors))
            rep, i = handler(UnicodeDecodeError('charmap', data, i, i + 1,
                                                'character maps to <undefined>'))
            out.append(rep)
            continue
        out.append(c)
        i += 1
    return (''.join(out), len(data))


def charmap_encode(s, errors=None, mapping=None):
    if mapping is None:
        return latin_1_encode(s, errors)
    s = str(s)
    handler = None
    out = bytearray()
    i = 0
    n = len(s)
    table = getattr(mapping, '_table', None) if type(mapping) is _EncodingMap else None
    while i < n:
        if table is not None:
            chunk, i = _codecs_native.charmap_encode(s, i, table)
            out += chunk
            if i >= n:
                break
        b = mapping.get(ord(s[i]))
        if b is None:
            # A run of unmappable characters is one error, as CPython's
            # charmap encoder reports it.
            j = i + 1
            while j < n and mapping.get(ord(s[j])) is None:
                j += 1
            if handler is None:
                handler = lookup_error(_errors(errors))
            rep, pos = handler(UnicodeEncodeError('charmap', s, i, j,
                                                  'character maps to <undefined>'))
            if isinstance(rep, str):
                for c in rep:
                    rb = mapping.get(ord(c))
                    if rb is None:
                        raise UnicodeEncodeError('charmap', s, i, j, 'character maps to <undefined>')
                    if isinstance(rb, int):
                        out.append(rb)
                    else:
                        out += rb
            else:
                out += rep
            if pos < 0:
                pos += n
            if not 0 <= pos <= n:
                raise IndexError('position %d from error handler out of bounds' % pos)
            i = pos
            continue
        if isinstance(b, int):
            out.append(b)
        else:
            out += b
        i += 1
    return (bytes(out), len(s))


class _BuiltinFunction:
    """A callable that, like CPython's C functions, does not bind as a
    method: encodings stores codec functions as class attributes
    (`_buffer_decode = codecs.utf_8_decode`) and calls them unbound."""

    __slots__ = ('_function', '__name__', '__qualname__', '__doc__', '__module__')

    def __init__(self, function):
        self._function = function
        self.__name__ = function.__name__
        self.__qualname__ = function.__qualname__
        self.__doc__ = function.__doc__
        self.__module__ = '_codecs'

    def __call__(self, *args, **kwargs):
        return self._function(*args, **kwargs)

    def __repr__(self):
        return '<built-in function %s>' % self.__name__

    def __reduce__(self):
        # Pickled by reference, as CPython pickles a C function.
        return self.__qualname__

    def __reduce_ex__(self, protocol):
        return self.__qualname__


for _name in __all__:
    globals()[_name] = _BuiltinFunction(globals()[_name])
del _name
