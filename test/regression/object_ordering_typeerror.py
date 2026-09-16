# Ordering comparisons on plain objects raise TypeError.
#
# `object()` defines no ordering dunder, so CPython raises
# "'<' not supported between instances of 'object' and 'object'".
# protoPython used to fall through to a pointer comparison and answer True or
# False, which made `object() < object()` return an arbitrary boolean.


def expect_type_error(fn, op):
    try:
        result = fn()
    except TypeError as exc:
        message = str(exc)
        assert "not supported between instances of" in message, message
        assert "'object'" in message, message
        return
    raise AssertionError("%s did not raise TypeError, returned %r" % (op, result))


a = object()
b = object()

expect_type_error(lambda: a < b, "object() < object()")
expect_type_error(lambda: a <= b, "object() <= object()")
expect_type_error(lambda: a > b, "object() > object()")
expect_type_error(lambda: a >= b, "object() >= object()")

# Equality and identity still work and never raise.
assert (a == b) is False
assert (a == a) is True
assert (a != b) is True
assert a is a

# Sorting a list of plain objects raises for the same reason.
try:
    sorted([object(), object()])
    raise AssertionError("sorted() over plain objects did not raise TypeError")
except TypeError:
    pass


# A user class without ordering dunders keeps raising, with its own name.
class Plain:
    pass


try:
    Plain() < Plain()
    raise AssertionError("Plain() < Plain() did not raise TypeError")
except TypeError as exc:
    assert "'Plain'" in str(exc), str(exc)


# A class that defines the dunders still orders normally.
class Ordered:
    def __init__(self, v):
        self.v = v

    def __lt__(self, other):
        return self.v < other.v


assert Ordered(1) < Ordered(2)
assert not (Ordered(2) < Ordered(1))

# Types that do order natively are unaffected.
assert 1 < 2
assert "a" < "b"
assert (1, 2) < (1, 3)
assert [1, 2] < [1, 3]
assert 1.5 < 2.5

print("object_ordering_typeerror: ok")
