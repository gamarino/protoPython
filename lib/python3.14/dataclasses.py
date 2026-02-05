# dataclasses.py - Minimal dataclass decorator with __init__ from annotations.

def dataclass(cls=None, **kwargs):
    """Minimal dataclass: adds __init__ from __annotations__ with default values from class."""
    def wrap(c):
        ann = getattr(c, '__annotations__', None)
        if not ann:
            return c
        names = list(ann.keys())
        def _default_for(name):
            v = getattr(c, name, None)
            if callable(v) and not isinstance(v, type):
                return None
            return v
        def __init__(self, *args, **kw):
            for i, n in enumerate(names):
                if i < len(args):
                    val = args[i]
                elif n in kw:
                    val = kw[n]
                else:
                    val = kw.get(n, _default_for(n))
                setattr(self, n, val)
        c.__init__ = __init__
        return c
    if cls is None:
        return wrap
    return wrap(cls)
