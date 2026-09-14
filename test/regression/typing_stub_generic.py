# Regression test: the _typing stub's Generic composes with ABC metaclasses
# and defers to typing like CPython's C implementation.
#
# typing.py declares `class Protocol(Generic, metaclass=_ProtocolMeta)` with
# _ProtocolMeta derived from ABCMeta. The stub gave Generic a private
# metaclass, which made that declaration a metaclass conflict, so
# `import typing` stopped there. In CPython Generic is a plain class whose
# __class_getitem__ / __init_subclass__ call typing._generic_class_getitem /
# typing._generic_init_subclass.
import abc
import typing
from _typing import Generic, TypeVar

T = TypeVar("T")

assert type(Generic) is type, "Generic must not carry a private metaclass"


class _ProtocolMeta(abc.ABCMeta):
    pass


class Protocol(Generic, metaclass=_ProtocolMeta):
    pass


alias = Generic[T]

assert type(Protocol) is _ProtocolMeta
assert issubclass(Protocol, Generic)
assert isinstance(alias, typing._GenericAlias), type(alias)
assert alias.__origin__ is Generic and alias.__args__ == (T,)

try:
    class Plain(Generic):
        pass
except TypeError:
    pass
else:
    raise AssertionError("subclassing plain Generic must raise TypeError, as in CPython")

print("typing stub generic OK")
