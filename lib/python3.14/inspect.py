"""
Minimal inspect stub for protoPython.
isfunction, ismodule, getsourcefile, signature; no real introspection.
"""


def isfunction(obj):
    """Return True if obj has __code__ attribute (callable with code object)."""
    return hasattr(obj, '__code__') and callable(obj)


def ismodule(obj):
    """Return True if obj has __name__ and __dict__ (module-like)."""
    return hasattr(obj, '__name__') and hasattr(obj, '__dict__')


def getsourcefile(obj):
    """Stub: return None. Full impl requires code object / file path."""
    return None


def signature(obj):
    """Stub: raise. Full impl requires parameter introspection."""
    raise ValueError("signature() not implemented in protoPython inspect stub")
