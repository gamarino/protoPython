# dataclasses.py - Minimal stub; dataclass decorator placeholder.

def dataclass(cls=None, **kwargs):
    """Placeholder: returns the class unchanged (no field generation)."""
    if cls is None:
        def wrap(c):
            return c
        return wrap
    return cls
