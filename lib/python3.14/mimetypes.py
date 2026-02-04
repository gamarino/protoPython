"""
Mimetypes: guess_type, guess_extension from suffix map.
"""

_SUFFIX_TYPE_MAP = {
    '.py': 'text/x-python',
    '.txt': 'text/plain',
    '.html': 'text/html',
    '.htm': 'text/html',
    '.css': 'text/css',
    '.js': 'application/javascript',
    '.json': 'application/json',
    '.xml': 'application/xml',
    '.csv': 'text/csv',
    '.md': 'text/markdown',
    '.rst': 'text/x-rst',
    '.sh': 'application/x-sh',
    '.bat': 'application/x-bat',
    '.pdf': 'application/pdf',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.jpeg': 'image/jpeg',
    '.gif': 'image/gif',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon',
    '.zip': 'application/zip',
    '.tar': 'application/x-tar',
    '.gz': 'application/gzip',
}

_TYPE_SUFFIX_MAP = {}
for suf, mime in _SUFFIX_TYPE_MAP.items():
    if mime not in _TYPE_SUFFIX_MAP:
        _TYPE_SUFFIX_MAP[mime] = []
    _TYPE_SUFFIX_MAP[mime].append(suf)


def guess_type(url, strict=True):
    """Return (type, encoding) based on url suffix. encoding is None."""
    if not url or not hasattr(url, 'split'):
        return (None, None)
    s = str(url)
    idx = s.rfind('.')
    if idx < 0:
        return (None, None)
    suffix = s[idx:].lower()
    return (_SUFFIX_TYPE_MAP.get(suffix), None)


def guess_extension(type, strict=True):
    """Return first suffix for the given MIME type."""
    if not type:
        return None
    exts = _TYPE_SUFFIX_MAP.get(type)
    return exts[0] if exts else None


def guess_all_extensions(type, strict=True):
    """Return all suffixes for the given MIME type."""
    if not type:
        return []
    return list(_TYPE_SUFFIX_MAP.get(type, []))
