# Comparison operators find rich comparison methods through the full MRO of
# the operand types, honour NotImplemented and give a subclass right operand
# priority, as CPython's PyObject_RichCompare does.

class EqBase:
    def __eq__(self, other):
        return True


class EqSub(EqBase):
    pass


assert EqSub() == EqSub()
assert not (EqSub() != EqSub())


class OrderBase:
    def __init__(self, v):
        self.v = v

    def __lt__(self, other):
        return self.v < other.v

    def __le__(self, other):
        return self.v <= other.v


class OrderMid(OrderBase):
    pass


class OrderLeaf(OrderMid):
    pass


assert OrderLeaf(1) < OrderLeaf(2)
assert not (OrderLeaf(2) < OrderLeaf(1))
assert OrderLeaf(2) <= OrderLeaf(2)
# a > b falls back to the reflected b.__lt__(a).
assert OrderLeaf(3) > OrderLeaf(2)
assert OrderLeaf(3) >= OrderLeaf(3)
assert sorted([OrderLeaf(3), OrderLeaf(1), OrderLeaf(2)], key=None)[0].v == 1
assert [x.v for x in sorted([OrderLeaf(3), OrderLeaf(1), OrderLeaf(2)])] == [1, 2, 3]
assert min(OrderLeaf(5), OrderLeaf(4)).v == 4


# Rich results are returned unchanged.
class RichBase:
    def __lt__(self, other):
        return "lt"

    def __gt__(self, other):
        return "gt"


class RichSub(RichBase):
    pass


assert (RichSub() < RichSub()) == "lt"
# The right operand's type is a subclass of the left operand's type: its
# reflected method runs first, even when it is inherited.
assert (RichBase() < RichSub()) == "gt"
assert (RichSub() < RichBase()) == "lt"


# Reflected method inherited by an unrelated right operand.
class Plain:
    pass


class ReflBase:
    def __gt__(self, other):
        return "reflected-gt"


class ReflSub(ReflBase):
    pass


assert (Plain() < ReflSub()) == "reflected-gt"


# NotImplemented from an inherited method: == falls back to identity and an
# ordering raises TypeError.
class NIBase:
    def __eq__(self, other):
        return NotImplemented

    def __lt__(self, other):
        return NotImplemented


class NISub(NIBase):
    pass


a = NISub()
assert a == a
assert not (NISub() == NISub())
assert NISub() != NISub()
try:
    NISub() < NISub()
except TypeError as exc:
    assert "'<' not supported between instances of 'NISub' and 'NISub'" in str(exc), exc
else:
    raise AssertionError("expected TypeError")


# A class without comparison methods keeps the default behaviour.
class NoCmp:
    pass


class NoCmpSub(NoCmp):
    pass


n = NoCmpSub()
assert n == n
assert NoCmpSub() != NoCmpSub()
try:
    NoCmpSub() < NoCmpSub()
except TypeError:
    pass
else:
    raise AssertionError("expected TypeError")


# Special methods are looked up on the type, not on the instance.
class InstanceAttr:
    pass


ia = InstanceAttr()
ia.__eq__ = lambda other: True
assert not (ia == InstanceAttr())


# A metaclass comparison method applies to its classes, not to their
# instances.
class Meta(type):
    def __lt__(cls, other):
        return "meta-lt"


class WithMeta(metaclass=Meta):
    pass


assert (WithMeta < int) == "meta-lt"
try:
    WithMeta() < WithMeta()
except TypeError:
    pass
else:
    raise AssertionError("expected TypeError")


# __ne__ defined on a base answers != directly; otherwise != inverts __eq__.
class NeBase:
    def __ne__(self, other):
        return "ne"


class NeSub(NeBase):
    pass


assert (NeSub() != NeSub()) == "ne"


# Inherited methods of built-in base classes.
class IntSub(int):
    pass


class StrSub(str):
    pass


assert IntSub(3) == 3 and IntSub(3) < IntSub(4)
assert StrSub("a") < StrSub("b") and StrSub("a") == "a"
assert [1, 2] == [1, 2] and (1, 2) < (1, 3)


# functools.total_ordering on a base class.
import functools


@functools.total_ordering
class Version:
    def __init__(self, n):
        self.n = n

    def __eq__(self, other):
        return self.n == other.n

    def __lt__(self, other):
        return self.n < other.n


class Release(Version):
    pass


assert Release(1) < Release(2) and Release(2) >= Release(1)
assert Release(2) > Release(1) and Release(1) <= Release(1)
assert Release(1) == Release(1)

print("compare_inherited_dunders: ok")
