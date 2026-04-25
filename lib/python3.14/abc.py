def abstractmethod(f):
    f.__isabstractmethod__ = True
    return f


def get_cache_token():
    return 0


class ABCMeta(type):
    def __new__(mcls, name, bases, ns, **kw):
        cls = type.__new__(mcls, name, bases, ns)
        # Populate __abstractmethods__ per CPython ABCMeta semantics:
        # collect names whose values declare __isabstractmethod__ across
        # this class's namespace and any abstract names inherited from
        # bases that this class did not override with a concrete impl.
        abstracts = set()
        # Inherited abstracts from bases (subtract concrete overrides).
        for base in bases:
            inherited = getattr(base, "__abstractmethods__", None) or ()
            for n in inherited:
                # Concrete override in cls drops the inherited abstract.
                v = ns.get(n) if hasattr(ns, "get") else None
                if v is None:
                    v = getattr(cls, n, None)
                if v is None or getattr(v, "__isabstractmethod__", False):
                    abstracts.add(n)
        # Direct abstracts declared in this class body.
        keys = getattr(ns, "__keys__", None) or list(ns)
        for n in keys:
            v = ns.get(n) if hasattr(ns, "get") else getattr(cls, n, None)
            if v is None:
                v = getattr(cls, n, None)
            if getattr(v, "__isabstractmethod__", False):
                abstracts.add(n)
        cls.__abstractmethods__ = frozenset(abstracts)
        return cls

    def register(cls, sub):
        return sub


class ABC(metaclass=ABCMeta):
    __slots__ = ()


def update_abstractmethods(c):
    return c
