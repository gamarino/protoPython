"""
Minimal ctypes stub for protoPython.
CDLL, c_int, c_double, etc.; full implementation requires FFI and platform shared libraries.
"""

def CDLL(name, *args, **kwargs):
    """Stub: return None. Full impl requires dlopen/dlsym."""
    return None

def c_int(val=0):
    """Stub: return value as-is or 0."""
    return val

def c_double(val=0.0):
    """Stub: return value as-is or 0.0."""
    return val

def c_long(val=0):
    """Stub: return value as-is or 0."""
    return val

def c_void_p(val=None):
    """Stub: return None."""
    return None

def byref(obj):
    """Stub: return None."""
    return None

def create_string_buffer(init=None, size=None):
    """Stub: return empty bytes or placeholder."""
    return b''

def string_at(ptr, size=-1):
    """Stub: return empty bytes."""
    return b''
