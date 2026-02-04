"""
Minimal types stub for protoPython.
FunctionType, ModuleType, SimpleNamespace; placeholder types for imports.
"""


class FunctionType:
    """Stub: placeholder for function type. Full impl requires code object binding."""
    pass


class ModuleType:
    """Stub: placeholder for module type. Full impl requires module instance."""
    pass


class SimpleNamespace:
    """Stub: minimal namespace; attribute access via __dict__ or __init__."""
    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            setattr(self, k, v)
