"""
Minimal inspect stub for protoPython.
isfunction, ismodule, getsourcefile, signature; no real introspection.
"""


def isfunction(obj):
    """Return True if obj is callable and has __code__ or __call__ (method-like)."""
    if not callable(obj):
        return False
    return hasattr(obj, '__code__') or hasattr(obj, '__call__')


def ismodule(obj):
    """Return True if obj has __name__ and is module-like."""
    return hasattr(obj, '__name__') and getattr(obj, '__name__', None) is not None


def getsourcefile(obj):
    """Return __file__ if present, else None."""
    return getattr(obj, '__file__', None)


def signature(obj):
    """Stub: raise. Full impl requires parameter introspection."""
    raise ValueError("signature() not implemented in protoPython inspect stub")
