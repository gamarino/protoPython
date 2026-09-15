# Containers compare elements with `x is e or x == e` (CPython's
# PyObject_RichCompareBool), so an object is found in a container even when it
# does not compare equal to itself, as NaN does.
import collections

nan = float("nan")
assert not (nan == nan)
assert nan != nan

# Membership.
assert nan in [nan]
assert nan in (nan,)
assert nan in {nan: 1}
assert nan in {nan}
assert nan in frozenset([nan])
assert nan in collections.deque([nan])
assert nan in (x for x in [nan])
assert nan not in [float("nan")]


class Seq:
    def __init__(self, items):
        self.items = items

    def __iter__(self):
        return iter(self.items)


assert nan in Seq([nan])


class L(list):
    pass


class T(tuple):
    pass


assert nan in L([nan])
assert nan in T((nan,))

# The unbound and bound __contains__ follow the same rule, and match equal
# values that are distinct objects.
assert [nan].__contains__(nan)
assert list.__contains__([nan], nan)
assert (nan,).__contains__(nan)
a = 2.5
b = float("2.5")
assert a is not b
assert [a].__contains__(b)
assert (a,).__contains__(b)
assert [(1, 2)].__contains__((1, 2))

# index, count and remove.
assert [nan].index(nan) == 0
assert (nan,).index(nan) == 0
assert [nan, nan].count(nan) == 2
assert (nan, nan).count(nan) == 2
assert [nan, float("nan")].count(nan) == 1
items = [nan]
items.remove(nan)
assert items == []
d = collections.deque([nan])
d.remove(nan)
assert len(d) == 0

# Element-wise equality of containers holding the same NaN object.
assert [nan] == [nan]
assert (nan,) == (nan,)
assert T((nan,)) == T((nan,))
assert L([nan]) == L([nan])
assert {1: nan} == {1: nan}
assert [nan] != [float("nan")]

print("nan_containment: ok")
