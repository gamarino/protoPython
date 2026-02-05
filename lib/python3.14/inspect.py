"""
Inspect module for protoPython.
isfunction, ismodule, getsourcefile, signature (from __code__ when available).
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


class Parameter:
    """Minimal parameter descriptor for inspect.signature."""

    POSITIONAL_ONLY = 0
    POSITIONAL_OR_KEYWORD = 1
    VAR_POSITIONAL = 2
    KEYWORD_ONLY = 3
    VAR_KEYWORD = 4

    def __init__(self, name, kind=1, default=None):
        self.name = name
        self.kind = kind
        self.default = default

    def __repr__(self):
        if self.default is not None:
            return '<Parameter %r default=%r>' % (self.name, self.default)
        return '<Parameter %r>' % (self.name,)


class Signature:
    """Minimal signature object with parameters list."""

    def __init__(self, parameters=None):
        self.parameters = dict(parameters) if parameters else {}

    def __repr__(self):
        params = ', '.join(p.name for p in self.parameters.values())
        return '<Signature (%s)>' % params


def signature(obj):
    """Return Signature built from __code__ when available. Raises ValueError otherwise."""
    if not callable(obj):
        raise ValueError('signature() requires a callable object')
    code = getattr(obj, '__code__', None)
    if code is None:
        raise ValueError("signature() not implemented for objects without __code__")
    params = []
    names = getattr(code, 'co_varnames', ())
    nargs = getattr(code, 'co_argcount', 0)
    nposonly = getattr(code, 'co_posonlyargcount', 0)
    nkwonly = getattr(code, 'co_kwonlyargcount', 0)
    defaults = getattr(obj, '__defaults__', None) or ()
    kwdefaults = getattr(obj, '__kwdefaults__', None) or {}
    ndefaults = len(defaults)
    # Positional params: names[0:nargs]; kw-only skip *args at index nargs when nkwonly > 0
    kw_start = nargs + (1 if nkwonly > 0 else 0)
    for i in range(nargs):
        if i >= len(names):
            break
        name = names[i]
        kind = Parameter.POSITIONAL_ONLY if i < nposonly else Parameter.POSITIONAL_OR_KEYWORD
        default = None
        if i >= nargs - ndefaults and ndefaults > 0:
            default = defaults[i - (nargs - ndefaults)]
        params.append((name, Parameter(name, kind=kind, default=default)))
    for j in range(nkwonly):
        idx = kw_start + j
        if idx >= len(names):
            break
        name = names[idx]
        default = kwdefaults.get(name)
        params.append((name, Parameter(name, kind=Parameter.KEYWORD_ONLY, default=default)))
    return Signature(parameters=params)
