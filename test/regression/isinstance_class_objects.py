"""A class is an instance of its metaclass only: isinstance(str, str) is False."""
import abc
import enum
import typing


class Meta(type):
    pass


class WithMeta(metaclass=Meta):
    pass


class Plain:
    pass


class Color(enum.Enum):
    RED = 1


for cls in (str, int, float, list, dict, bytes, tuple, set, Plain, WithMeta):
    assert not isinstance(cls, cls), cls
assert isinstance(str, type) and isinstance(Plain, type) and isinstance(type, type)
assert isinstance(Plain, object) and isinstance(str, object)
assert isinstance(WithMeta, Meta) and not isinstance(Plain, Meta)
assert isinstance(Color, enum.EnumType) and isinstance(Color.RED, Color)
assert isinstance(3, int) and isinstance("a", str) and not isinstance(3, str)
assert isinstance(Plain(), Plain) and isinstance(WithMeta(), WithMeta)


class Base(abc.ABC):
    pass


Base.register(int)
assert isinstance(3, Base) and not isinstance(int, Base)
assert typing._type_convert(str) is str

print("isinstance class objects OK")
