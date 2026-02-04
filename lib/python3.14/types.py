"""
Minimal types stub for protoPython.
FunctionType, ModuleType, SimpleNamespace; placeholder types for imports.
"""


class FunctionType:
    """Stub: placeholder for function type. Full impl requires code object binding."""
    pass


class ModuleType:
    """Minimal module type with __name__ and __dict__."""
    def __init__(self, name, doc=None):
        self.__name__ = name
        self.__doc__ = doc
        self.__dict__ = {}


class SimpleNamespace:
    """Stub: minimal namespace; attribute access via __dict__ or __init__."""
    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            setattr(self, k, v)
