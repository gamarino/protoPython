"""
uu: uuencode and uudecode (pure Python).
Each 3 bytes become 4 printable characters (0x20-0x5f); line length 45.
"""

def _b64chars():
    return bytes(range(0x20, 0x60))

def encode(in_file, out_file, name='-', mode=0o666):
    """Uuencode in_file to out_file. name and mode written in header."""
    data = in_file.read() if hasattr(in_file, 'read') else bytes(in_file)
    chars = _b64chars()
    out = []
    out.append(('begin %o %s\n' % (mode, name)).encode('ascii'))
    line_len = 45
    for i in range(0, len(data), line_len):
        chunk = data[i:i + line_len]
        n = len(chunk)
        out.append(bytes([chars[n]]))
        for j in range(0, n, 3):
            a, b = chunk[j], chunk[j + 1] if j + 1 < n else 0
            c = chunk[j + 2] if j + 2 < n else 0
            out.append(bytes([
                chars[a >> 2],
                chars[((a & 3) << 4) | (b >> 4)],
                chars[((b & 15) << 2) | (c >> 6)] if j + 1 < n else ord(b' '),
                chars[c & 63] if j + 2 < n else ord(b' ')
            ]))
        out.append(b'\n')
    out.append(b' \nend\n')
    result = b''.join(out)
    if hasattr(out_file, 'write'):
        out_file.write(result)
    return result

def decode(in_file, out_file=None, mode=None, quiet=False):
    """Uudecode in_file; write to out_file if given, else return bytes."""
    data = in_file.read() if hasattr(in_file, 'read') else bytes(in_file)
    if isinstance(data, str):
        data = data.encode('latin-1')
    lines = data.split(b'\n')
    out = []
    started = False
    for line in lines:
        if not started:
            if line.startswith(b'begin '):
                started = True
            continue
        if line.startswith(b'end') or line.strip() == b'end':
            break
        if len(line) < 1:
            continue
        n = line[0] - 0x20
        if n <= 0:
            continue
        chars = _b64chars()
        rev = {}
        for i, c in enumerate(chars):
            rev[c] = i
        i = 1
        while n > 0 and i + 3 <= len(line):
            a, b, c, d = rev.get(line[i], 0), rev.get(line[i+1], 0), rev.get(line[i+2], 0), rev.get(line[i+3], 0)
            out.append((a << 2) | (b >> 4))
            n -= 1
            if n > 0:
                out.append(((b & 15) << 4) | (c >> 2))
                n -= 1
            if n > 0:
                out.append(((c & 3) << 6) | d)
                n -= 1
            i += 4
    result = bytes(out)
    if out_file is not None and hasattr(out_file, 'write'):
        out_file.write(result)
        return None
    return result
