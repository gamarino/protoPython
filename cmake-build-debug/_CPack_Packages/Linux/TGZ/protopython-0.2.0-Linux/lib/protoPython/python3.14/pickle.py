"""
Minimal pickle for protoPython.
loads/dumps for int, float, str, bytes, list, dict (minimal format).
Full protocol not implemented.
"""

def _dumps_one(obj):
    """Serialize a single object to bytes (minimal format)."""
    if obj is None:
        return b'N'
    if isinstance(obj, bool):
        return b'T' if obj else b'F'
    if isinstance(obj, int):
        return b'I' + str(obj).encode('utf-8') + b'\n'
    if isinstance(obj, float):
        return b'G' + str(obj).encode('utf-8') + b'\n'
    if isinstance(obj, bytes):
        return b'B' + str(len(obj)).encode('utf-8') + b':' + obj + b'\n'
    if isinstance(obj, str):
        u = obj.encode('utf-8')
        return b'S' + str(len(u)).encode('utf-8') + b':' + u + b'\n'
    if isinstance(obj, list):
        out = b'L' + str(len(obj)).encode('utf-8') + b'\n'
        for x in obj:
            out += _dumps_one(x)
        return out + b'E\n'
    if isinstance(obj, dict):
        out = b'D' + str(len(obj)).encode('utf-8') + b'\n'
        for k, v in obj.items():
            if not isinstance(k, str):
                raise TypeError("pickle minimal: dict keys must be str")
            out += _dumps_one(k) + _dumps_one(v)
        return out + b'E\n'
    raise TypeError("pickle minimal: unsupported type %s" % type(obj).__name__)

def _loads_one(data, pos):
    """Deserialize one object; returns (object, new_pos)."""
    if pos[0] >= len(data):
        return None, pos[0]
    t = chr(data[pos[0]])
    pos[0] += 1
    if t == 'N':
        return None, pos[0]
    if t == 'T':
        return True, pos[0]
    if t == 'F':
        return False, pos[0]
    if t == 'I':
        end = data.find(b'\n', pos[0])
        s = data[pos[0]:end].decode('utf-8')
        pos[0] = end + 1
        return int(s), pos[0]
    if t == 'G':
        end = data.find(b'\n', pos[0])
        s = data[pos[0]:end].decode('utf-8')
        pos[0] = end + 1
        return float(s), pos[0]
    if t == 'B':
        colon = data.find(b':', pos[0])
        n = int(data[pos[0]:colon].decode('utf-8'))
        pos[0] = colon + 1
        b = data[pos[0]:pos[0] + n]
        pos[0] += n + 1  # skip \n
        return b, pos[0]
    if t == 'S':
        colon = data.find(b':', pos[0])
        n = int(data[pos[0]:colon].decode('utf-8'))
        pos[0] = colon + 1
        u = data[pos[0]:pos[0] + n].decode('utf-8')
        pos[0] += n + 1
        return u, pos[0]
    if t == 'L':
        end = data.find(b'\n', pos[0])
        n = int(data[pos[0]:end].decode('utf-8'))
        pos[0] = end + 1
        out = []
        for _ in range(n):
            v, pos[0] = _loads_one(data, pos)
            out.append(v)
        if pos[0] < len(data) and data[pos[0]:pos[0]+2] == b'E\n':
            pos[0] += 2
        return out, pos[0]
    if t == 'D':
        end = data.find(b'\n', pos[0])
        n = int(data[pos[0]:end].decode('utf-8'))
        pos[0] = end + 1
        out = {}
        for _ in range(n):
            k, pos[0] = _loads_one(data, pos)
            v, pos[0] = _loads_one(data, pos)
            out[k] = v
        if pos[0] < len(data) and data[pos[0]:pos[0]+2] == b'E\n':
            pos[0] += 2
        return out, pos[0]
    return None, pos[0]

def dump(obj, file, protocol=None, *, fix_imports=True):
    """Write serialized obj to file (must have write method)."""
    file.write(dumps(obj, protocol, fix_imports=fix_imports))

def dumps(obj, protocol=None, *, fix_imports=True):
    """Serialize obj to bytes. Supports int, float, str, bytes, list, dict (str keys)."""
    return _dumps_one(obj)

def load(file, *, fix_imports=True, encoding="ASCII", errors="strict"):
    """Deserialize one object from file (must have read method)."""
    data = file.read() if hasattr(file, 'read') else b''
    if isinstance(data, str):
        data = data.encode(encoding or 'utf-8')
    return loads(data, fix_imports=fix_imports, encoding=encoding, errors=errors)

def loads(data, *, fix_imports=True, encoding="ASCII", errors="strict"):
    """Deserialize from bytes. Supports int, float, str, bytes, list, dict."""
    if isinstance(data, str):
        data = data.encode(encoding or 'utf-8')
    pos = [0]
    obj, _ = _loads_one(data, pos)
    return obj
