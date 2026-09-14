# Regression test: `import typing` completes and generic classes work.
#
# It needed four interpreter fixes besides the _typing stub:
# - `type.__qualname__` (and __doc__ on type or a metaclass) returned the
#   getset descriptor itself: a data descriptor on the metatype did not take
#   precedence for classes. typing formats `origin.__qualname__`.
# - repr() of a getset descriptor raised "'str' object is not callable"
#   (its __repr__ was a placeholder string).
# - tuple.index / tuple.count returned None / 0 on a raw tuple such as *args.
# - class creation ignored PEP 560 __mro_entries__, so `class B(Generic[T])`
#   used the alias object itself as a base.
# An exception raised by __init_subclass__ also bypassed try/except.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


check("type.__qualname__", type.__qualname__, "type")


class Meta(type):
    """Meta doc."""


check("metaclass __doc__", Meta.__doc__, "Meta doc.")
descr_repr = repr(type.__dict__["__qualname__"])
check("getset repr", descr_repr.startswith("<attribute '__qualname__' of"), True)


def args_tuple(*args):
    return args


check("tuple.index on *args", args_tuple(1, 2, 3).index(2), 1)
check("tuple.count on *args", args_tuple(1, 2, 2).count(2), 2)


class Real:
    pass


class Stand:
    def __mro_entries__(self, bases):
        return (Real,)


class Built(Stand()):
    pass


check("__mro_entries__ base", Built.__bases__, (Real,))
check("__orig_bases__ recorded", type(Built.__orig_bases__[0]).__name__, "Stand")


class Picky:
    def __init_subclass__(cls, **kwargs):
        raise ValueError("no subclasses")


try:
    class Child(Picky):
        pass
except ValueError:
    caught = True
else:
    caught = False
check("__init_subclass__ exception is catchable", caught, True)

import typing
from typing import Generic, Protocol, TypeVar

T = TypeVar("T")


class Box(Generic[T]):
    def __init__(self, value):
        self.value = value


check("Generic subclass bases", Box.__bases__, (Generic,))
check("Generic subclass parameters", Box.__parameters__, (T,))
check("Box[int] origin", Box[int].__origin__, Box)
check("instance", Box(3).value, 3)


class SupportsClose(Protocol):
    def close(self) -> None: ...


check("Protocol class", SupportsClose._is_protocol, True)
check("typing.List[int]", typing.List[int].__args__, (int,))
check("typing.Type alias", typing.Type[int].__origin__, type)
check("Optional", typing.Optional[int], typing.Union[int, None])

print("typing import OK")
