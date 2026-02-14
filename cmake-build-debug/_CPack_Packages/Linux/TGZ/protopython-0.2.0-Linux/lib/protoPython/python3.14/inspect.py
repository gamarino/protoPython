# inspect.py - Stub for protoPython

def getsource(obj):
    """Stub for getting source code of an object."""
    return "# Source not available in protoPython yet"

def isfunction(obj):
    return hasattr(obj, "__code__")

def ismodule(obj):
    return hasattr(obj, "__name__") and not isfunction(obj)
