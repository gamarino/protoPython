"""
Minimal plistlib stub for protoPython.
load, loads, dump, dumps are stubs; full implementation requires plist parser/serializer.
"""

def load(fp, *, fmt=None, dict_type=dict):
    """Stub: return empty dict. Full impl requires plist parser."""
    return dict_type()

def loads(data, *, fmt=None, dict_type=dict):
    """Stub: return empty dict. Full impl requires plist parser."""
    return dict_type()

def dump(value, fp, *, fmt=None, sort_keys=True, skipkeys=False):
    """Stub: no-op. Full impl requires plist serializer."""
    pass

def dumps(value, *, fmt=None, sort_keys=True, skipkeys=False):
    """Stub: return empty bytes. Full impl requires plist serializer."""
    return b''
