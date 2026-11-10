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
    "raw_unicode_escape_encode", "raw_unicode_escape_decode",
    "unicode_escape_encode", "unicode_escape_decode",
    "strict_errors", "ignore_errors", "replace_errors",
    "xmlcharrefreplace_errors", "backslashreplace_errors", "namereplace_errors",
]

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
        data = exc.object[exc.start:exc.end]
        if all(b >= 128 for b in data[:4]):
            consumed = 0
            out = []
            for b in data[:4]:
                if b < 128:
                    break
                out.append(chr(0xDC00 + b))
                consumed += 1
            return (''.join(out), exc.start + consumed)
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


for _name, _handler in (('strict', strict_errors), ('ignore', ignore_errors),
                        ('replace', replace_errors),
                        ('xmlcharrefreplace', xmlcharrefreplace_errors),
                        ('backslashreplace', backslashreplace_errors),
                        ('namereplace', namereplace_errors),
                        ('surrogateescape', surrogateescape_errors)):
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


def charmap_build(decoding_table):
    return {ord(c): i for i, c in enumerate(decoding_table)}


def charmap_decode(data, errors=None, mapping=None):
    data = bytes(data)
    if mapping is None:
        return latin_1_decode(data, errors)
    handler = None
    out = []
    i = 0
    while i < len(data):
        b = data[i]
        if isinstance(mapping, str):
            c = mapping[b] if b < len(mapping) else '￾'
        else:
            c = mapping.get(b)
            if isinstance(c, int):
                c = chr(c)
        if c is None or c == '￾':
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
    handler = None
    out = bytearray()
    i = 0
    while i < len(s):
        b = mapping.get(ord(s[i]))
        if b is None:
            if handler is None:
                handler = lookup_error(_errors(errors))
            rep, i = handler(UnicodeEncodeError('charmap', s, i, i + 1,
                                                'character maps to <undefined>'))
            out += rep.encode('ascii') if isinstance(rep, str) else rep
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
