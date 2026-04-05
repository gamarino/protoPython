# Copyright 2007 Google, Inc. All Rights Reserved.
def abstractmethod(f):
    f.__isabstractmethod__ = True
    return f
_abc_invalidation_counter = 0
def get_cache_token(): return _abc_invalidation_counter
class ABCMeta(type):
    def __new__(mcls, name, bases, ns, **kwargs):
        try:
            if "__name__" not in ns: ns["__name__"] = name
        except KeyError: ns["__name__"] = name
        abstracts = set()
        for k in ns:
            v = ns[k]
            if getattr(v, "__isabstractmethod__", False): abstracts.add(k)
        for base in bases:
            for mname in getattr(base, "__abstractmethods__", ()):
                value = ns.get(mname, getattr(base, mname, None))
                if getattr(value, "__isabstractmethod__", False): abstracts.add(mname)
        cls = type.__new__(mcls, name, bases, ns)
        cls.__abstractmethods__ = frozenset(abstracts)
        cls._abc_registry = set(); cls._abc_cache = set(); cls._abc_negative_cache = set()
        cls._abc_negative_cache_version = _abc_invalidation_counter
        return cls
    def register(cls, subclass):
        if not isinstance(subclass, type): raise TypeError("classes only")
        cls._abc_registry.add(subclass)
        global _abc_invalidation_counter
        _abc_invalidation_counter += 1
        return subclass
    def __instancecheck__(cls, instance):
        subclass = instance.__class__
        if subclass in cls._abc_cache: return True
        return cls.__subclasscheck__(subclass)
    def __subclasscheck__(cls, subclass):
        if subclass in cls._abc_cache: return True
        if cls in getattr(subclass, '__mro__', ()):
            cls._abc_cache.add(subclass); return True
        for rcls in cls._abc_registry:
            if issubclass(subclass, rcls):
                cls._abc_cache.add(subclass); return True
        return False
class ABC(metaclass=ABCMeta): __slots__ = ()
def update_abstractmethods(cls): return cls
ABCMeta.__module__ = 'abc'
