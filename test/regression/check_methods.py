# _collections_abc._check_methods follows CPython: True when every method is
# bound in a class of C.__mro__, NotImplemented when one is missing or None.
import abc
from _collections_abc import _check_methods


class C:
    def __iter__(self):
        return iter(())


class D:
    pass


class N:
    __iter__ = None


class E(C):
    pass


class F(C):
    __iter__ = None


assert _check_methods(C, "__iter__") is True
assert _check_methods(E, "__iter__") is True
assert _check_methods(D, "__iter__") is NotImplemented
assert _check_methods(N, "__iter__") is NotImplemented
assert _check_methods(F, "__iter__") is NotImplemented
assert _check_methods(D, "keys") is NotImplemented
assert _check_methods(D, "__init__") is True
assert _check_methods(C, "__iter__", "__next__") is NotImplemented


class Frobbable(abc.ABC):
    @classmethod
    def __subclasshook__(cls, X):
        if cls is Frobbable:
            return _check_methods(X, "frob")
        return NotImplemented


class G:
    def frob(self):
        pass


class H:
    frob = None


assert issubclass(G, Frobbable)
assert isinstance(G(), Frobbable)
assert not issubclass(D, Frobbable)
assert not issubclass(H, Frobbable)
print("check_methods OK")
