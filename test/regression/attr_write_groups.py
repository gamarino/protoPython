# Attribute write groups: a run of `self.a = ...; self.b = ...` statements is
# published as one new version of the object (Compiler::tryCompileAttrGroup,
# "Attribute write groups" in ExecutionEngine.cpp).  The program's meaning
# must not change: every expected value below is what CPython prints, and
# ctest runs this file twice, with the groups on and with
# PROTOPY_ATTR_GROUPS=off (the per-write path).
#
# The cases cover what the compiler groups (parameters, constants, displays)
# and every reason the engine must decline a run and write per write: a
# __setattr__ override (own or inherited, including one that observes the
# earlier writes), a property or another data descriptor (own or inherited),
# __slots__, a class or a module as the receiver, and a deleted parameter.


class Point:
    def __init__(self, x, y, *rest, label="p", **extra):
        self.x = x
        self.y = y
        self.rest = rest
        self.label = label
        self.extra = extra
        self.items = []
        self.table = {"k": 1, 2: x}
        self.pair = (x, -1, +2.5)
        self.nothing = None


p = Point(1, 2, 3, label="q", z=4)
assert (p.x, p.y, p.rest, p.label, p.extra) == (1, 2, (3,), "q", {"z": 4})
assert p.items == [] and p.table == {"k": 1, 2: 1} and p.pair == (1, -1, 2.5)
assert p.nothing is None
assert Point(5, 6).items is not Point(5, 6).items, "each run builds its own list"


class Repeats:
    def set(self, a):
        self.v = a
        self.w = 2
        self.v = 3
        return self


assert Repeats().set(1).v == 3 and Repeats().set(1).w == 2


# The first value of a run may be anything; it runs before the run starts.
class Counter:
    created = 0

    def __init__(self, start):
        self.n = Counter.bump()
        self.start = start
        self.seen = []


def _bump():
    Counter.created += 1
    return Counter.created


Counter.bump = staticmethod(_bump)
c1, c2 = Counter(10), Counter(20)
assert (c1.n, c2.n, c2.start) == (1, 2, 20)


# A later value that reads an attribute, calls or looks up a global ends the
# run; the statements still run in order.
LIMIT = 9


class Mixed:
    def __init__(self, a):
        self.a = a
        self.b = self.a + 1
        self.c = len([a])
        self.d = LIMIT
        self.e = a


m = Mixed(4)
assert (m.a, m.b, m.c, m.d, m.e) == (4, 5, 1, 9, 4)


# --- reasons to decline: every write must take the per-write path ---------

class Times10:
    def __setattr__(self, name, value):
        object.__setattr__(self, name, value * 10)

    def __init__(self, a):
        self.a = a
        self.b = 2


class InheritsTimes10(Times10):
    def __init__(self, a):
        self.a = a
        self.b = 2


assert (Times10(1).a, Times10(1).b) == (10, 20)
assert (InheritsTimes10(1).a, InheritsTimes10(1).b) == (10, 20)


class Observer:
    """__setattr__ that reads what the earlier writes of the run stored."""

    def __setattr__(self, name, value):
        if name == "b":
            object.__setattr__(self, "seen", self.a)
        object.__setattr__(self, name, value)

    def __init__(self):
        self.a = 1
        self.b = 2


assert Observer().seen == 1


class WithProperty:
    @property
    def p(self):
        return self._p

    @p.setter
    def p(self, v):
        self._p = v + 100

    def __init__(self, a):
        self.q = a
        self.p = a
        self.r = 3


class InheritsProperty(WithProperty):
    def __init__(self, a):
        self.q = a
        self.p = a
        self.r = 3


assert WithProperty(1).p == 101 and InheritsProperty(1).p == 101


class Doubling:
    def __set_name__(self, owner, name):
        self.store = "_" + name

    def __get__(self, obj, objtype=None):
        return self if obj is None else getattr(obj, self.store)

    def __set__(self, obj, value):
        setattr(obj, self.store, value * 2)


class UsesDescriptor:
    d = Doubling()

    def __init__(self, a):
        self.c = a
        self.d = a


assert UsesDescriptor(5).d == 10 and UsesDescriptor(5).c == 5


class Slotted:
    __slots__ = ("a", "b")

    def __init__(self, a):
        self.a = a
        self.b = 2


s = Slotted(1)
assert (s.a, s.b) == (1, 2)


class SlotViolation:
    __slots__ = ("a",)

    def fill(self, x):
        self.a = x
        self.zz = 2


sv = SlotViolation()
try:
    sv.fill(7)
    raise AssertionError("writing a name outside __slots__ did not raise")
except AttributeError:
    pass
assert sv.a == 7, "the write before the failing one was lost"


class Defaults:
    count = 0

    def __init__(self, c):
        self.count = c
        self.other = 1


assert Defaults(5).count == 5 and Defaults.count == 0


class Deleting:
    def set(self, a, b):
        del b
        self.a = a
        self.b = b


d = Deleting()
try:
    d.set(1, 2)
    raise AssertionError("reading a deleted parameter did not raise")
except UnboundLocalError:
    pass
assert d.a == 1, "the write before the failing read was lost"


class Plain:
    pass


def set_on(target, v):
    target.a = v
    target.b = v
    target.c = v


set_on(Plain, 4)
assert (Plain.a, Plain.b, Plain.c) == (4, 4, 4)

import types

mod = types.ModuleType("grouped_module")


def set_module(target):
    target.a = 1
    target.b = 2


set_module(mod)
assert (mod.a, mod.b) == (1, 2)

print("attr_write_groups: ok")
