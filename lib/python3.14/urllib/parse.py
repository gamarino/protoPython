"""
urllib.parse: quote, unquote, urljoin (percent-encoding per RFC 3986).
"""

_ALWAYS_SAFE = frozenset(b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-~')
_HEX = '0123456789ABCDEF'

def quote(string, safe='/', encoding=None, errors=None):
    """Percent-encode string for use in a URL."""
    if isinstance(string, str):
        string = string.encode('utf-8')
    if not string:
        return ''
    out = []
    for b in string:
        if b in _ALWAYS_SAFE or chr(b) in safe:
            out.append(chr(b))
        else:
            out.append('%')
            out.append(_HEX[b >> 4])
            out.append(_HEX[b & 15])
    return ''.join(out)

def unquote(string, encoding='utf-8', errors='replace'):
    """Percent-decode a URL component."""
    if not string:
        return ''
    if isinstance(string, bytes):
        string = string.decode('latin-1')
    out = []
    i = 0
    while i < len(string):
        if string[i] == '%' and i + 2 < len(string):
            try:
                out.append(chr(int(string[i+1:i+3], 16)))
                i += 3
                continue
            except ValueError:
                pass
        out.append(string[i])
        i += 1
    return ''.join(out)

def urljoin(base, url, allow_fragments=True):
    """Join base and url; minimal implementation."""
    if not base:
        return url
    if not url:
        return base
    if url.startswith('//') or url.startswith('http:') or url.startswith('https:'):
        return url
    if url.startswith('/'):
        base2 = base.split('?')[0]
        if '//' in base2:
            idx = base2.index('//') + 2
            rest = base2[idx:]
            if '/' in rest:
                base2 = base2[:idx] + rest[:rest.index('/') + 1]
            return base2 + url[1:]
        return url
    if not base.endswith('/'):
        base = base + '/'
    return base + url
