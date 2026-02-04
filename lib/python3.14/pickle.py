"""
Minimal pickle stub for protoPython.
dump, dumps, load, loads; full implementation requires serialization format.
"""

def dump(obj, file, protocol=None, *, fix_imports=True):
    """Stub: no-op. Full impl requires pickle serialization."""
    pass

def dumps(obj, protocol=None, *, fix_imports=True):
    """Stub: return empty bytes. Full impl requires pickle serialization."""
    return b''

def load(file, *, fix_imports=True, encoding="ASCII", errors="strict"):
    """Stub: return None. Full impl requires pickle deserialization."""
    return None

def loads(data, *, fix_imports=True, encoding="ASCII", errors="strict"):
    """Stub: return None. Full impl requires pickle deserialization."""
    return None
