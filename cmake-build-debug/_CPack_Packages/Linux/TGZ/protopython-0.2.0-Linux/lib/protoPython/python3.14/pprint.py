"""
pprint: recursive pretty-print and pformat.
"""

def _safe_key(key):
    try:
        return (0, str(type(key).__name__), key)
    except Exception:
        return (1, str(type(key).__name__), id(key))

def _pformat(obj, stream, indent, width, depth, compact, current_depth):
    if depth is not None and current_depth > depth:
        stream.append(repr(obj))
        return
    if isinstance(obj, dict):
        if not obj:
            stream.append('{}')
            return
        stream.append('{')
        items = sorted(obj.items(), key=_safe_key)
        for i, (k, v) in enumerate(items):
            if i > 0:
                stream.append(', ')
            stream.append('\n')
            stream.append(' ' * (indent * (current_depth + 1)))
            _pformat(k, stream, indent, width, depth, compact, current_depth + 1)
            stream.append(': ')
            _pformat(v, stream, indent, width, depth, compact, current_depth + 1)
        stream.append('\n')
        stream.append(' ' * (indent * current_depth))
        stream.append('}')
    elif isinstance(obj, (list, tuple)):
        bracket = '[' if isinstance(obj, list) else '('
        bracket_close = ']' if isinstance(obj, list) else ')'
        if not obj:
            stream.append(bracket + bracket_close)
            return
        stream.append(bracket)
        for i, item in enumerate(obj):
            if i > 0:
                stream.append(', ')
            stream.append('\n')
            stream.append(' ' * (indent * (current_depth + 1)))
            _pformat(item, stream, indent, width, depth, compact, current_depth + 1)
        stream.append('\n')
        stream.append(' ' * (indent * current_depth))
        stream.append(bracket_close)
    else:
        stream.append(repr(obj))

def pformat(object, indent=1, width=80, depth=None, *, compact=False):
    """Return a formatted string representation of object."""
    stream = []
    _pformat(object, stream, indent, width, depth, compact, 0)
    return ''.join(stream)

def pprint(object, stream=None, indent=1, width=80, depth=None, *, compact=False):
    """Pretty-print object to stream (default sys.stdout)."""
    s = pformat(object, indent=indent, width=width, depth=depth, compact=compact)
    if stream is None:
        try:
            import sys
            stream = sys.stdout
        except Exception:
            return
    if hasattr(stream, 'write'):
        stream.write(s)
        if not s.endswith('\n'):
            stream.write('\n')

class PrettyPrinter:
    """Pretty printer with configurable indent, width, depth, stream."""
    def __init__(self, indent=1, width=80, depth=None, stream=None, *, compact=False):
        self.indent = indent
        self.width = width
        self.depth = depth
        self.stream = stream
        self.compact = compact

    def pprint(self, object):
        """Pretty-print object to self.stream."""
        pprint(object, stream=self.stream, indent=self.indent, width=self.width,
               depth=self.depth, compact=self.compact)

    def pformat(self, object):
        """Return formatted string for object."""
        return pformat(object, indent=self.indent, width=self.width,
                       depth=self.depth, compact=self.compact)
