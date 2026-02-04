"""
Minimal urllib.parse stub for protoPython.
quote, unquote, urljoin return stub values; full implementation requires URL encoding.
"""

def quote(string, safe='/', encoding=None, errors=None):
    """Stub: return string unchanged. Full impl requires percent-encoding."""
    return string

def unquote(string, encoding='utf-8', errors='replace'):
    """Stub: return string unchanged. Full impl requires percent-decoding."""
    return string

def urljoin(base, url, allow_fragments=True):
    """Stub: return url unchanged. Full impl requires base URL resolution."""
    return url
