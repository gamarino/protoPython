"""
Minimal inspect stub for protoPython.
isfunction, ismodule, getsourcefile, signature; no real introspection.
"""


def isfunction(obj):
    """Stub: return False. Full impl requires function type check."""
    return False


def ismodule(obj):
    """Stub: return False. Full impl requires module type check."""
    return False


def getsourcefile(obj):
    """Stub: return None. Full impl requires code object / file path."""
    return None


def signature(obj):
    """Stub: raise. Full impl requires parameter introspection."""
    raise ValueError("signature() not implemented in protoPython inspect stub")
