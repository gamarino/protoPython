"""
tokenize: generate_tokens, tokenize, untokenize.
Uses native _tokenize_source when available (builtins._tokenize_source).
"""

import collections

# Token type constants (matches Python's token module)
ENDMARKER = 0
NAME = 1
NUMBER = 2
STRING = 3
NEWLINE = 4
NL = 61
COMMENT = 60
INDENT = 5
DEDENT = 6
OP = 54

tok_name = {
    ENDMARKER: 'ENDMARKER',
    NAME: 'NAME',
    NUMBER: 'NUMBER',
    STRING: 'STRING',
    NEWLINE: 'NEWLINE',
    NL: 'NL',
    COMMENT: 'COMMENT',
    INDENT: 'INDENT',
    DEDENT: 'DEDENT',
    OP: 'OP',
}

EXACT_TOKEN_TYPES = {}


class TokenInfo(collections.namedtuple('TokenInfo', 'type string start end line')):
    def __repr__(self):
        annotated_type = '%d (%s)' % (self.type, tok_name.get(self.type, str(self.type)))
        return ('TokenInfo(type=%s, string=%r, start=%r, end=%r, line=%r)' %
                self._replace(type=annotated_type))

    @property
    def exact_type(self):
        if self.type == OP and self.string in EXACT_TOKEN_TYPES:
            return EXACT_TOKEN_TYPES[self.string]
        return self.type

def _get_tokenize_source():
    try:
        b = __builtins__
        if hasattr(b, 'get') and callable(b.get):
            return b.get('_tokenize_source')
        return getattr(b, '_tokenize_source', None)
    except Exception:
        return None

_tokenize_source = _get_tokenize_source()

def generate_tokens(readline):
    """Yield tokens (type, string, start, end, line). Uses native tokenizer if available."""
    if _tokenize_source is None:
        return
        yield
    lines = []
    while True:
        line = readline()
        if not line:
            break
        lines.append(line)
    source = ''.join(lines) if lines and isinstance(lines[0], str) else (b''.join(lines) if lines else '')
    if isinstance(source, bytes):
        source = source.decode('utf-8', errors='replace')
    for item in _tokenize_source(source):
        try:
            typ, val = item[0], item[1]
        except (IndexError, TypeError):
            continue
        yield (typ, val, (0, 0), (0, 0), '')

def tokenize(readline):
    """Alias for generate_tokens for compatibility."""
    return generate_tokens(readline)

def untokenize(iterable):
    """Reconstruct source from token strings (second element of each token tuple)."""
    out = []
    for item in iterable:
        if len(item) >= 2:
            v = item[1]
            out.append(v if isinstance(v, (str, bytes)) else str(v))
    if not out:
        return b''
    return b''.join(out) if isinstance(out[0], bytes) else ''.join(out)


def open(filename):
    """Open a file in read mode using the encoding detected by detect_encoding."""
    import builtins
    return builtins.open(filename, encoding='utf-8', errors='replace')
