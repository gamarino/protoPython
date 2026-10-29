# __name__, __qualname__, __mro__, __bases__ and __base__ are attributes of
# classes (type's descriptors in CPython), not of their instances: reading
# them on an instance, on None or on an int raises AttributeError, and
# hasattr answers False.  An object that owns one (a function, a module, an
# instance that assigned it) still reads its own.
import types

SLOTS = ("__name__", "__qualname__", "__mro__", "__bases__", "__base__")


class C:
    pass


class Sub(C):
    pass


# object() instances are not covered: internal prototypes are plain objects.
for obj in (C(), Sub(), None, 1, 2.5, "s", [], (), {}, True, b"x"):
    for name in SLOTS:
        assert not hasattr(obj, name), (obj, name)
        try:
            getattr(obj, name)
        except AttributeError:
            pass
        else:
            raise AssertionError("%r.%s did not raise" % (obj, name))

try:
    C().__name__
except AttributeError as e:
    assert "__name__" in str(e), str(e)
else:
    raise AssertionError("C().__name__ did not raise")

# Classes keep them.
assert C.__name__ == "C" and C.__qualname__ == "C"
assert C.__mro__ == (C, object) and C.__bases__ == (object,) and C.__base__ is object
assert int.__name__ == "int" and type(None).__name__ == "NoneType"
assert Sub.__mro__ == (Sub, C, object)

# Objects that own them.
def f():
    pass


assert f.__name__ == "f" and f.__qualname__ == "f"
assert types.__name__ == "types"
o = C()
o.__name__ = "mine"
assert o.__name__ == "mine"


# A class __getattr__ is still consulted for them.
class Dyn:
    def __getattr__(self, name):
        return "dyn:" + name


assert Dyn().__name__ == "dyn:__name__"
assert C().__module__ == __name__ and C().__class__ is C
print("type_only_attributes: ok")
