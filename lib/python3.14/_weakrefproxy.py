"""protoPython: weakref.proxy, in Python.

CPython implements ProxyType and CallableProxyType in C (Objects/
weakrefobject.c).  Here a proxy holds a weakref.ref to its referent and
forwards attribute access and every protocol slot CPython's proxy forwards
(numbers, sequences, mappings, iteration, comparison, str/repr/bool/bytes)
to the referent; it raises ReferenceError once the referent is gone.
weakref installs these in _weakref, replacing the native names.
"""

from _weakref import ref

__all__ = ["proxy", "ProxyType", "CallableProxyType"]

_getattribute = object.__getattribute__


def _referent(p):
    o = _getattribute(p, "_wr")()
    if o is None:
        raise ReferenceError("weakly-referenced object no longer exists")
    return o


def _unwrap(o):
    return _referent(o) if isinstance(o, (ProxyType, CallableProxyType)) else o


class ProxyType:
    __slots__ = ("_wr", "__weakref__")

    def __new__(cls, *args, **kwargs):
        raise TypeError("cannot create 'weakref.%s' instances" % cls.__name__)

    def __getattribute__(self, name):
        return getattr(_referent(self), name)

    def __setattr__(self, name, value):
        setattr(_referent(self), name, value)

    def __delattr__(self, name):
        delattr(_referent(self), name)

    def __repr__(self):
        o = _getattribute(self, "_wr")()
        if o is None:
            return "<weakproxy at %#x; dead>" % id(self)
        return "<weakproxy at %#x; to '%s' at %#x>" % (
            id(self), type(o).__qualname__, id(o))

    def __str__(self):
        return str(_referent(self))

    def __bytes__(self):
        return bytes(_referent(self))

    def __bool__(self):
        return bool(_referent(self))

    def __len__(self):
        return len(_referent(self))

    def __iter__(self):
        return iter(_referent(self))

    def __next__(self):
        return next(_referent(self))

    def __reversed__(self):
        return reversed(_referent(self))

    def __contains__(self, item):
        return item in _referent(self)

    def __getitem__(self, key):
        return _referent(self)[key]

    def __setitem__(self, key, value):
        _referent(self)[key] = value

    def __delitem__(self, key):
        del _referent(self)[key]

    def __index__(self):
        return _referent(self).__index__()

    def __int__(self):
        return int(_referent(self))

    def __float__(self):
        return float(_referent(self))

    def __neg__(self):
        return -_referent(self)

    def __pos__(self):
        return +_referent(self)

    def __abs__(self):
        return abs(_referent(self))

    def __invert__(self):
        return ~_referent(self)

    __hash__ = None


def _install_binary(name, op):
    def forward(self, other):
        return op(_referent(self), _unwrap(other))

    def reflected(self, other):
        return op(_unwrap(other), _referent(self))

    def inplace(self, other):
        return getattr(__import__("operator"), "__i%s__" % name)(_referent(self), _unwrap(other))

    forward.__name__ = "__%s__" % name
    reflected.__name__ = "__r%s__" % name
    inplace.__name__ = "__i%s__" % name
    setattr(ProxyType, forward.__name__, forward)
    setattr(ProxyType, reflected.__name__, reflected)
    setattr(ProxyType, inplace.__name__, inplace)


def _install_compare(name, op):
    def compare(self, other):
        return op(_referent(self), _unwrap(other))

    compare.__name__ = "__%s__" % name
    setattr(ProxyType, compare.__name__, compare)


def _setup():
    import operator
    for name in ("add", "sub", "mul", "matmul", "truediv", "floordiv", "mod",
                 "pow", "lshift", "rshift", "and", "or", "xor"):
        _install_binary(name, getattr(operator, "__%s__" % name))
    for name in ("lt", "le", "eq", "ne", "gt", "ge"):
        _install_compare(name, getattr(operator, "__%s__" % name))
    ProxyType.__divmod__ = lambda self, other: divmod(_referent(self), _unwrap(other))
    ProxyType.__rdivmod__ = lambda self, other: divmod(_unwrap(other), _referent(self))


_setup()
del _setup


class CallableProxyType(ProxyType):
    __slots__ = ()

    def __call__(self, *args, **kwargs):
        return _referent(self)(*args, **kwargs)


ProxyType.__module__ = CallableProxyType.__module__ = "weakref"
ProxyType.__name__ = ProxyType.__qualname__ = "ProxyType"
CallableProxyType.__name__ = CallableProxyType.__qualname__ = "CallableProxyType"


def proxy(obj, callback=None, /):
    """proxy(object[, callback]) -- create a proxy object that weakly
    references 'object'.  'callback', if given, is called with a reference
    to the proxy when 'object' is about to be finalized."""
    p = object.__new__(CallableProxyType if callable(obj) else ProxyType)
    if callback is None:
        wr = ref(obj)
    else:
        wr = ref(obj, lambda _wr: callback(p))
    object.__setattr__(p, "_wr", wr)
    return p
