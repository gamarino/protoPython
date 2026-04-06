def abstractmethod(f):
    f.__isabstractmethod__=True
    return f
def get_cache_token(): return 0
class ABCMeta(type):
    def __new__(mcls, name, bases, ns, **kw):
        return type.__new__(mcls, name, bases, ns)
    def register(cls, sub): return sub
class ABC(metaclass=ABCMeta): __slots__ = ()
def update_abstractmethods(c): return c
