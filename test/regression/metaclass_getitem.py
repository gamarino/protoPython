"""cls[X] uses the metaclass __getitem__ before cls.__class_getitem__, as CPython."""
import enum


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


class Meta(type):
    def __getitem__(cls, key):
        return (cls.__name__, key)


class WithMeta(metaclass=Meta):
    def __class_getitem__(cls, key):
        return "class_getitem"


class OnlyClassGetItem:
    def __class_getitem__(cls, key):
        return ("cgi", key)


class Color(enum.Enum):
    RED = 1
    BLUE = 2


assert WithMeta["k"] == ("WithMeta", "k")
assert OnlyClassGetItem[int] == ("cgi", int)
assert Color["BLUE"] is Color.BLUE
assert raises(KeyError, lambda: Color["GREEN"])
assert list[int] == list[int] and dict[str, int].__args__ == (str, int)

print("metaclass getitem OK")
