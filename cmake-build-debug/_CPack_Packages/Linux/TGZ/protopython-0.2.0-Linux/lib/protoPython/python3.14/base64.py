# base64.py - Standard base64 encode/decode (RFC 4648).
# Accepts bytes-like input; returns bytes.

_B64_ALPHABET = b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'


def b64encode(s):
    """Encode bytes-like s to base64 bytes. Padding with '=' as needed."""
    if s is None:
        return b''
    if hasattr(s, '__iter__') and not isinstance(s, (str, bytes)):
        data = bytes(bytearray(s))
    else:
        data = s if isinstance(s, bytes) else s.encode('utf-8') if isinstance(s, str) else bytes(s)
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
    return bytes(out)


def b64decode(s):
    """Decode base64 bytes-like s to bytes. Ignores non-alphabet characters."""
    if s is None:
        return b''
    if hasattr(s, '__iter__') and not isinstance(s, (str, bytes)):
        data = bytes(bytearray(s))
    else:
        data = s if isinstance(s, bytes) else s.encode('ascii') if isinstance(s, str) else bytes(s)
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
