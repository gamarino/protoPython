def abstractmethod(f): f.__isabstractmethod__=True; return f
_abc_invalidation_counter = 0
def get_cache_token(): return _abc_invalidation_counter
class ABCMeta(type):
    def __new__(mcls, name, bases, ns, **kwargs):
        if "__name__" not in ns: ns["__name__"] = name
        return type.__new__(mcls, name, bases, ns)
    def register(cls, sub): return sub
class ABC(metaclass=ABCMeta): __slots__ = ()
def update_abstractmethods(c): return c
ABCMeta.__module__ = 'abc'
