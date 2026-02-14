"""
binascii: hex and base64 encoding/decoding (RFC 4648).
"""

def _to_bytes(data):
    if data is None:
        return b''
    if hasattr(data, '__iter__') and not isinstance(data, (str, bytes)):
        return bytes(bytearray(data))
    if isinstance(data, bytes):
        return data
    if isinstance(data, str):
        return data.encode('utf-8')
    return bytes(data)

_HEX = b'0123456789abcdef'

def hexlify(data, sep=b'', bytes_per_sep=1):
    """Return hexadecimal representation of bytes."""
    data = _to_bytes(data)
    out = []
    for i, b in enumerate(data):
        if sep and i > 0 and bytes_per_sep > 0 and i % bytes_per_sep == 0:
            out.append(sep)
        out.append(_HEX[b >> 4])
        out.append(_HEX[b & 15])
    return bytes(out)

def unhexlify(hexstr):
    """Return bytes from hexadecimal string."""
    data = _to_bytes(hexstr)
    rev = {}
    for i, c in enumerate(_HEX):
        rev[c] = i
    out = []
    i = 0
    while i < len(data):
        if data[i] in rev:
            high = rev[data[i]]
            i += 1
            if i < len(data) and data[i] in rev:
                low = rev[data[i]]
                i += 1
                out.append((high << 4) | low)
            else:
                out.append(high)
                break
        else:
            i += 1
    return bytes(out)

_B64_ALPHABET = b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'

def b2a_base64(data, *, newline=True):
    """Encode bytes to base64."""
    data = _to_bytes(data)
    out = []
    for i in range(0, len(data), 3):
        chunk = data[i:i + 3]
        n = len(chunk)
        a, b = chunk[0], chunk[1] if n > 1 else 0
        c = chunk[2] if n > 2 else 0
        out.append(_B64_ALPHABET[a >> 2])
        out.append(_B64_ALPHABET[((a & 3) << 4) | (b >> 4)])
        out.append(_B64_ALPHABET[((b & 15) << 2) | (c >> 6)] if n >= 2 else ord(b'='))
        out.append(_B64_ALPHABET[c & 63] if n >= 3 else ord(b'='))
    result = bytes(out)
    if newline:
        result = result + b'\n'
    return result

def a2b_base64(data):
    """Decode base64 to bytes."""
    data = _to_bytes(data)
    rev = {}
    for i, c in enumerate(_B64_ALPHABET):
        rev[c] = i
    out = []
    buf = []
    for c in data:
        if c in rev:
            buf.append(rev[c])
        if len(buf) == 4:
            a, b, c, d = buf
            out.append((a << 2) | (b >> 4))
            out.append(((b & 15) << 4) | (c >> 2))
            out.append(((c & 3) << 6) | d)
            buf = []
    if len(buf) >= 2:
        a, b = buf[0], buf[1]
        out.append((a << 2) | (b >> 4))
        if len(buf) >= 3:
            out.append(((b & 15) << 4) | (buf[2] >> 2))
            if len(buf) == 4:
                out.append(((buf[2] & 3) << 6) | buf[3])
    return bytes(out)
