# operator.eq, ne, lt, le, gt and ge behave as the comparison operators for
# every operand type, instead of converting both operands to C++ integers.
import math
import operator


class F(float):
    pass


nan = F("nan")
assert operator.eq(nan, nan) is False
assert operator.ne(nan, nan) is True
assert operator.eq(nan, 1.0) is False
assert operator.lt(F(1.5), 2) is True
assert operator.ge(F(2.5), F(2.5)) is True

assert operator.eq(1.5, 1.5) and not operator.eq(1.5, 2.5)
assert operator.lt(1.25, 1.5) and operator.gt(2.5, 2.25)
assert operator.eq("abc", "abc") and operator.lt("abc", "abd")
assert operator.ne([1, 2], [1, 3]) and operator.le((1, 2), (1, 2))
assert operator.eq(2**70, 2**70) and operator.lt(2**70, 2**71)
assert operator.eq(None, None) and operator.ne(None, 0)
assert not operator.eq(float("nan"), float("nan"))


class Rich:
    def __eq__(self, other):
        return "rich-eq"

    def __lt__(self, other):
        return "rich-lt"


# Rich results are returned unchanged, as `a == b` does.
assert operator.eq(Rich(), 1) == "rich-eq"
assert operator.lt(Rich(), 1) == "rich-lt"

try:
    operator.lt(1, "a")
except TypeError:
    pass
else:
    raise AssertionError("expected TypeError")

assert sorted([3.5, 1.25, 2.0], key=None) == [1.25, 2.0, 3.5]
assert math.isnan(nan)

print("operator_rich_compare: ok")
