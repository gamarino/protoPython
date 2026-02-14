"""
Minimal ast stub for protoPython.
parse, dump, literal_eval; full implementation requires parser and AST construction.
"""

def parse(source, filename='<unknown>', mode='exec', *, type_comments=False, feature_version=None):
    """Stub: return None. Full impl requires parser and AST construction."""
    return None

def dump(node, annotate_fields=True, include_attributes=False, *, indent=None):
    """Stub: return empty string. Full impl requires AST serialization."""
    return ''

def literal_eval(node_or_string):
    """Safely evaluate a string containing a Python literal. Supports int, float, str,
    bytes, list, tuple, dict, set, None, True, False."""
    if isinstance(node_or_string, str):
        s = node_or_string.strip()
        if not s:
            raise ValueError("malformed node or string")
        if s == 'None':
            return None
        if s == 'True':
            return True
        if s == 'False':
            return False
        try:
            return _parse_literal(s)
        except Exception:
            raise ValueError("malformed node or string: %r" % node_or_string)
    return None


def _parse_literal(s):
    """Parse a literal from string s. Returns the value or raises."""
    s = s.strip()
    if s == 'None':
        return None
    if s == 'True':
        return True
    if s == 'False':
        return False
    if s.startswith('"') or s.startswith("'"):
        return _parse_str(s)
    if s.startswith('b"') or s.startswith("b'"):
        return _parse_bytes(s)
    if s.startswith('['):
        return _parse_list(s)
    if s.startswith('('):
        return _parse_tuple(s)
    if s.startswith('{'):
        return _parse_dict_or_set(s)
    if s[0].isdigit() or (len(s) > 1 and s[0] in '+-' and s[1].isdigit()):
        return _parse_number(s)
    raise ValueError("malformed literal")


def _parse_str(s):
    q = s[0]
    i = 1
    out = []
    while i < len(s):
        c = s[i]
        if c == '\\':
            i += 1
            if i < len(s):
                out.append(s[i])
            i += 1
            continue
        if c == q:
            return ''.join(out)
        out.append(c)
        i += 1
    raise ValueError("unterminated string")


def _parse_bytes(s):
    q = s[2]
    i = 3
    out = []
    while i < len(s):
        c = s[i]
        if c == '\\':
            i += 1
            if i < len(s):
                out.append(ord(s[i]))
            i += 1
            continue
        if c == q:
            return bytes(out)
        out.append(ord(c))
        i += 1
    raise ValueError("unterminated bytes")


def _parse_number(s):
    if '.' in s or 'e' in s or 'E' in s:
        return float(s)
    return int(s)


def _parse_list(s):
    if s == '[]':
        return []
    inner = s[1:-1]
    items = _split_top_level(inner, ',')
    return [_parse_literal(x.strip()) for x in items]


def _parse_tuple(s):
    if s == '()':
        return ()
    inner = s[1:-1]
    items = _split_top_level(inner, ',')
    return tuple(_parse_literal(x.strip()) for x in items)


def _parse_dict_or_set(s):
    if s == '{}':
        return {}
    inner = s[1:-1].strip()
    if not inner:
        return {}
    items = _split_top_level(inner, ',')
    first = items[0].strip()
    if ':' in first:
        d = {}
        for part in items:
            idx = part.find(':')
            if idx >= 0:
                k = _parse_literal(part[:idx].strip())
                v = _parse_literal(part[idx+1:].strip())
                d[k] = v
        return d
    return set(_parse_literal(x.strip()) for x in items)


def _parse_set(s):
    if s == '{}':
        return set()
    inner = s[1:-1]
    items = _split_top_level(inner, ',')
    return set(_parse_literal(x.strip()) for x in items)


def _split_top_level(s, sep):
    """Split s by sep, not inside brackets/quotes."""
    result = []
    current = []
    depth = 0
    in_str = None
    i = 0
    while i < len(s):
        c = s[i]
        if in_str:
            if c == in_str and (i == 0 or s[i-1] != '\\'):
                in_str = None
            current.append(c)
            i += 1
            continue
        if c in '"\'':
            in_str = c
            current.append(c)
            i += 1
            continue
        if c in '([{':
            depth += 1
            current.append(c)
            i += 1
            continue
        if c in ')]}':
            depth -= 1
            current.append(c)
            i += 1
            continue
        if depth == 0 and c == sep:
            result.append(''.join(current))
            current = []
            i += 1
            continue
        current.append(c)
        i += 1
    result.append(''.join(current))
    return result

def walk(node):
    """Stub: yield nothing. Full impl requires AST traversal."""
    return
    yield  # make this a generator

class NodeVisitor:
    """Stub class for AST visitor. Full impl requires visit/generic_visit dispatch."""
    def visit(self, node):
        """Stub: no-op."""
        pass
    def generic_visit(self, node):
        """Stub: no-op."""
        pass
