# Regression test: __init_subclass__ runs exactly once, with the class
# keywords, for every way of creating a class.
#
# The hook was called only by the class-statement path (OP_BUILD_CLASS), so
# type(name, bases, ns), types.new_class(), a direct metaclass call and
# type.__new__ never ran it.  CPython runs it at the end of type.__new__
# with the keywords passed to type.__new__.
import types

seen = []


class Base:
    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__()
        seen.append((cls.__name__, kwargs))


def check(label, expected):
    got = list(seen)
    del seen[:]
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


type("X", (Base,), {})
check("type() three-argument form", [("X", {})])

type("X2", (Base,), {}, flag=1)
check("type() with class keywords", [("X2", {"flag": 1})])

types.new_class("Y", (Base,), {"flag": 2})
check("types.new_class()", [("Y", {"flag": 2})])


class Z(Base, flag=3):
    pass


check("class statement", [("Z", {"flag": 3})])


class Meta(type):
    def __new__(mcls, name, bases, ns, **kwargs):
        return super().__new__(mcls, name, bases, ns, **kwargs)

    def __init__(cls, name, bases, ns, **kwargs):
        super().__init__(name, bases, ns, **kwargs)


class W(Base, metaclass=Meta, flag=4):
    pass


check("metaclass calling super().__new__", [("W", {"flag": 4})])

Meta("V", (Base,), {}, flag=5)
check("direct metaclass call", [("V", {"flag": 5})])

type.__new__(Meta, "U", (Base,), {}, flag=6)
check("type.__new__ called directly", [("U", {"flag": 6})])


class NotAClass(type):
    def __new__(mcls, name, bases, ns):
        return 42


class Skipped(Base, metaclass=NotAClass):
    pass


check("metaclass that never calls type.__new__", [])
assert Skipped == 42


# A hook that instantiates the new class, whose __init__ uses zero-arg
# super(), for a class defined inside a function.
class Registry:
    instances = []

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        Registry.instances.append(cls())


class Payload:
    def __init__(self):
        self.ready = True


def make():
    class Local(Registry, Payload):
        def __init__(self):
            super().__init__()
    return Local


Local = make()
assert [type(obj) for obj in Registry.instances] == [Local], Registry.instances
assert Registry.instances[0].ready

print("init subclass dynamic OK")
