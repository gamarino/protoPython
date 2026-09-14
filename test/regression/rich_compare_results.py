# Regression test: comparison operators return the rich comparison dunder's
# result as is.
#
# compareObjects coerced any non-bool result of __eq__/__lt__/... to True or
# False, so `X() == 1` gave True where CPython gives the returned object; only
# containers truth-test element comparisons.  An ordering where the dunders
# of both operands answered NotImplemented fell back to an arbitrary order
# instead of raising TypeError.


class X:
    def __eq__(self, other):
        return "yes"

    def __lt__(self, other):
        return [1]

    __hash__ = object.__hash__


assert (X() == 1) == "yes"
assert (1 == X()) == "yes"
assert (X() < 1) == [1]
assert (1 > X()) == [1]
assert [X()] == [1]          # containers truth-test element comparisons
assert X() in [1]
assert (X() != 1) is False   # object.__ne__ negates a truthy __eq__
if not (X() == 1):
    raise AssertionError("a truthy comparison result must pass an if")


class N:
    def __ne__(self, other):
        return 0


result = N() != 1
assert result == 0 and type(result) is int, result


class Both:
    def __eq__(self, other):
        return "eq"

    def __ne__(self, other):
        return "ne"


assert (Both() != 1) == "ne"


class Z:
    def __eq__(self, other):
        return NotImplemented


z = Z()
assert (Z() == Z()) is False
assert (z == z) is True
assert (Z() != Z()) is True


class A:
    __lt__ = lambda self, other: NotImplemented


def raises_type_error(label, fn, message=None):
    try:
        fn()
    except TypeError as exc:
        if message is not None:
            assert str(exc) == message, "%s: %r" % (label, str(exc))
    else:
        raise AssertionError(label + " must raise TypeError")


raises_type_error("A() < 1", lambda: A() < 1,
                  "'<' not supported between instances of 'A' and 'int'")
raises_type_error("1 > A()", lambda: 1 > A(),
                  "'>' not supported between instances of 'int' and 'A'")
raises_type_error("A() < A()", lambda: A() < A())
assert (A() == 1) is False and (A() != 1) is True

# Built-in orderings are unaffected.
assert (1 < 2) is True and ("a" < "b") is True and ((1, 2) < (1, 3)) is True
assert ([1, 2] < [1, 3]) is True and (1.5 > 1) is True and (True < 2) is True
assert sorted([3, 1, 2]) == [1, 2, 3]

print("rich compare results OK")
