"""
Minimal mimetypes stub for protoPython.
guess_type, guess_extension return stub values; full implementation requires MIME database.
"""

def guess_type(url, strict=True):
    """Stub: return (None, None). Full impl requires MIME types database."""
    return (None, None)

def guess_extension(type, strict=True):
    """Stub: return None. Full impl requires MIME types database."""
    return None

def guess_all_extensions(type, strict=True):
    """Stub: return empty list."""
    return []
