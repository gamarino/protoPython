"""
ctypes stub for protoPython.
Full implementation requires FFI (dlopen/dlsym) and platform shared libraries.
CDLL raises NotImplementedError; c_* types and byref/string_at return placeholders.
"""

def CDLL(name, *args, **kwargs):
    """Stub: raises NotImplementedError. Full impl requires dlopen/dlsym."""
    raise NotImplementedError("ctypes.CDLL requires native FFI (dlopen/dlsym); not implemented in protoPython")

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
    """Stub: raises NotImplementedError. Full impl requires FFI."""
    raise NotImplementedError("ctypes.byref requires native FFI; not implemented in protoPython")

def create_string_buffer(init=None, size=None):
    """Stub: return empty bytes or placeholder."""
    return b''

def string_at(ptr, size=-1):
    """Stub: return empty bytes."""
    return b''
