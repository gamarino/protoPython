# struct.py - Minimal pack/unpack for common formats (b, h, i, l, q, f, d, s, ?).
# Byte order: little-endian. f/d use minimal IEEE 754; no native dependency.

def _float_to_bytes(x):
    """Crude float to 4 bytes (IEEE 754 binary32 little-endian)."""
    x = float(x)
    if x != x:
        return bytes([0xff, 0xff, 0xff, 0xff])
    import math
    if x == 0:
        return bytes(4)
    sign = 0x80 if x < 0 else 0
    x = abs(x)
    m, e = math.frexp(x)
    e += 127
    m = int((m * 2 - 1) * (1 << 23)) & ((1 << 23) - 1)
    if e <= 0:
        e = 0
        m = 0
    elif e >= 255:
        e = 255
        m = 0
    bits = (sign << 24) | (e << 23) | m
    return bytes([bits & 0xff, (bits >> 8) & 0xff, (bits >> 16) & 0xff, (bits >> 24) & 0xff])


def _double_to_bytes(x):
    """Crude double to 8 bytes (IEEE 754 binary64 little-endian)."""
    x = float(x)
    if x != x:
        return bytes(8)
    if x == 0:
        return bytes(8)
    import math
    sign = (1 << 63) if x < 0 else 0
    x = abs(x)
    m, e = math.frexp(x)
    e += 1022
    m = int((m * 2 - 1) * (1 << 52)) & ((1 << 52) - 1)
    if e <= 0:
        e = 0
        m = 0
    elif e >= 2047:
        e = 2047
        m = 0
    bits = sign | (e << 52) | m
    return bytes([bits & 0xff, (bits >> 8) & 0xff, (bits >> 16) & 0xff, (bits >> 24) & 0xff,
                  (bits >> 32) & 0xff, (bits >> 40) & 0xff, (bits >> 48) & 0xff, (bits >> 56) & 0xff])


def _bytes_to_float(b):
    """4 bytes to float (IEEE 754 binary32)."""
    if len(b) < 4:
        return 0.0
    bits = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)
    if bits == 0:
        return 0.0
    sign = -1 if (bits >> 31) & 1 else 1
    e = ((bits >> 23) & 0xff) - 127
    m = (bits & ((1 << 23) - 1)) | (1 << 23)
    return sign * (m / (1 << 23)) * (2 ** e)


def _bytes_to_double(b):
    """8 bytes to double (IEEE 754 binary64)."""
    if len(b) < 8:
        return 0.0
    lo = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)
    hi = b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24)
    bits = lo | (hi << 32)
    if bits == 0:
        return 0.0
    sign = -1 if (bits >> 63) & 1 else 1
    e = ((bits >> 52) & 0x7ff) - 1023
    m = (bits & ((1 << 52) - 1)) | (1 << 52)
    return sign * (m / (1 << 52)) * (2 ** e)


def _pack_simple(fmt, values):
    out = []
    vi = 0
    i = 0
    while i < len(fmt):
        c = fmt[i]
        if c in '<@=':
            i += 1
            continue
        if c in 'bBhHiIlLqQfds?':
            if c == 'b' or c == 'B':
                v = int(values[vi]) & 0xff
                out.append(bytes([v]))
            elif c == 'h' or c == 'H':
                v = int(values[vi]) & 0xffff
                out.append(bytes([v & 0xff, (v >> 8) & 0xff]))
            elif c == 'i' or c == 'I' or c == 'l' or c == 'L':
                v = int(values[vi]) & 0xffffffff
                out.append(bytes([v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff]))
            elif c == 'q' or c == 'Q':
                v = int(values[vi]) & 0xffffffffffffffff
                out.append(bytes([
                    v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff,
                    (v >> 32) & 0xff, (v >> 40) & 0xff, (v >> 48) & 0xff, (v >> 56) & 0xff
                ]))
            elif c == 'f':
                out.append(_float_to_bytes(float(values[vi])))
            elif c == 'd':
                out.append(_double_to_bytes(float(values[vi])))
            elif c == 's':
                s = values[vi]
                if isinstance(s, str):
                    s = s.encode('utf-8')
                out.append(bytes(s)[:1] if len(fmt) > i + 1 and fmt[i + 1].isdigit() else bytes(s))
            elif c == '?':
                out.append(bytes([1 if values[vi] else 0]))
            vi += 1
        i += 1
    return b''.join(out)


def pack(fmt, *values):
    """Pack values into bytes according to format. Minimal support: b, h, i, l, q, f, d, s, ? (little-endian)."""
    try:
        return _pack_simple(fmt, values)
    except (IndexError, TypeError, ValueError):
        return b''


def unpack(fmt, data):
    """Unpack bytes into tuple. Minimal support: b, h, i, l, q, f, d, s, ? (little-endian)."""
    if not data:
        return ()
    if hasattr(data, '__iter__') and not isinstance(data, (str, bytes)):
        data = bytes(bytearray(data))
    elif not isinstance(data, bytes):
        data = bytes(data)
    result = []
    pos = 0
    i = 0
    while i < len(fmt) and pos < len(data):
        c = fmt[i]
        if c in '<@=':
            i += 1
            continue
        if c == 'b':
            result.append(data[pos] if data[pos] < 128 else data[pos] - 256)
            pos += 1
        elif c == 'B':
            result.append(data[pos])
            pos += 1
        elif c in 'hH':
            if pos + 2 > len(data):
                break
            v = data[pos] | (data[pos + 1] << 8)
            result.append(v - 65536 if c == 'h' and v >= 32768 else v)
            pos += 2
        elif c in 'iIlL':
            if pos + 4 > len(data):
                break
            v = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24)
            result.append(v)
            pos += 4
        elif c in 'qQ':
            if pos + 8 > len(data):
                break
            v = (data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24) |
                 (data[pos + 4] << 32) | (data[pos + 5] << 40) | (data[pos + 6] << 48) | (data[pos + 7] << 56))
            result.append(v)
            pos += 8
        elif c == 'f':
            result.append(_bytes_to_float(data[pos:pos + 4]))
            pos += 4
        elif c == 'd':
            result.append(_bytes_to_double(data[pos:pos + 8]))
            pos += 8
        elif c == 's':
            n = 1
            result.append(data[pos:pos + n])
            pos += n
        elif c == '?':
            result.append(bool(data[pos]))
            pos += 1
        i += 1
    return tuple(result)
