# Regression test: the _typing stub's Generic composes with ABC metaclasses.
#
# typing.py declares `class Protocol(Generic, metaclass=_ProtocolMeta)` with
# _ProtocolMeta derived from ABCMeta. The _typing stub gave Generic a private
# metaclass, which makes that declaration a metaclass conflict (CPython raises
# the same TypeError for that stub), so `import typing` stopped there. In
# CPython Generic is a plain class subscripted through __class_getitem__.
import abc
from _typing import Generic, TypeVar

T = TypeVar("T")

assert type(Generic) is type, "Generic must not carry a private metaclass"


class _ProtocolMeta(abc.ABCMeta):
    pass


class Protocol(Generic, metaclass=_ProtocolMeta):
    pass


class Plain(Generic):
    pass


alias = Generic[T]

assert type(Protocol) is _ProtocolMeta
assert issubclass(Protocol, Generic)
assert issubclass(Plain, Generic) and type(Plain) is type
assert alias.__origin__ is Generic and alias.__args__ == (T,)

print("typing stub generic OK")
