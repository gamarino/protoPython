"""Orderings nobody implements raise TypeError, also inside containers and sorted()."""
from collections import deque


def raises(fn):
    try:
        fn()
    except TypeError:
        return True
    return False


assert [1].__lt__((1,)) is NotImplemented and (1,).__gt__([1]) is NotImplemented
assert list.__lt__([1], 5) is NotImplemented
assert raises(lambda: [1] < (1,)) and raises(lambda: (1,) >= [1])
assert raises(lambda: (1, 'a') < (1, 2)) and raises(lambda: [1, 'a'] <= [1, 2])
assert raises(lambda: {1} < [1]) and raises(lambda: deque([1]) < [1])
assert raises(lambda: sorted([3, 'a'])) and raises(lambda: max([1, 'a']))
assert raises(lambda: [3, 'a'].sort()) and raises(lambda: sorted([(1, 2), (1, 'a')]))
try:
    [1, 'a'] <= [1, 2]
except TypeError as exc:
    assert "'<='" in str(exc), str(exc)
items = [2, 'a', 1]
assert raises(items.sort) and len(items) == 3


class V:
    def __init__(self, v):
        self.v = v

    def __lt__(self, other):
        return self.v < other.v

    def __eq__(self, other):
        return self.v == other.v


# Orderable elements still compare, including ones with their own __lt__.
assert [V(1)] < [V(2)] and not ([V(2)] < [V(1)])
assert (1, V(1)) < (1, V(2))
assert [1, 2] < [1, 3] and (1, 2) < (1, 2, 0) and [2] > [1, 9] and (1, 'a') < (1, 'b')
assert [1.5] < [2] and (True,) < (2,) and [1] <= [1] and (2,) >= (1, 5)
assert sorted([[2], [1, 5], [1]]) == [[1], [1, 5], [2]]
assert sorted([(1, 'b'), (1, 'a')]) == [(1, 'a'), (1, 'b')]
assert not ([1] == (1,)) and [1] != (1,)
print("ordering type errors OK")
