"""
quopri: quoted-printable encoding and decoding (RFC 2045).
"""

def _hex(c):
    return '0123456789ABCDEF'[c >> 4] + '0123456789ABCDEF'[c & 15]

def encode(input, output, quotetabs=False):
    """Encode input (readable) to output (writable) in quoted-printable."""
    data = input.read() if hasattr(input, 'read') else bytes(input)
    if isinstance(data, str):
        data = data.encode('latin-1')
    line_len = 76
    line = []
    count = 0
    for b in data:
        if b == 32:  # space: encode at end of line or as _ in some modes
            line.append('_')
            count += 1
        elif b == 9:  # tab
            if quotetabs:
                line.append('=%02X' % b)
                count += 3
            else:
                line.append(chr(b))
                count += 1
        elif 33 <= b <= 126 and b not in (61, 95):  # printable except = and _
            line.append(chr(b))
            count += 1
        else:
            line.append('=' + _hex(b))
            count += 3
        if count >= line_len - 1:
            out = ''.join(line) + '=\n'
            if hasattr(output, 'write'):
                output.write(out)
            line = []
            count = 0
    if line:
        out = ''.join(line)
        if hasattr(output, 'write'):
            output.write(out)

def decode(input, output, header=False):
    """Decode quoted-printable from input to output."""
    data = input.read() if hasattr(input, 'read') else bytes(input)
    if isinstance(data, str):
        data = data.encode('latin-1')
    out = []
    i = 0
    while i < len(data):
        if i + 2 <= len(data) and data[i:i+2] == b'=\n':
            i += 2
            continue
        if i + 2 <= len(data) and data[i] == ord('='):
            try:
                out.append(int(bytes([data[i+1], data[i+2]]).decode('ascii'), 16))
                i += 3
                continue
            except (ValueError, IndexError):
                pass
        if header and data[i] == ord('_'):
            out.append(ord(' '))
        else:
            out.append(data[i])
        i += 1
    result = bytes(out)
    if hasattr(output, 'write'):
        output.write(result)
    return result
