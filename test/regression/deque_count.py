# collections.deque.count(x) counts the elements equal to x.
from collections import deque

d = deque([1, 2, 1, 3, 1])
assert d.count(1) == 3
assert d.count(2) == 1
assert d.count(9) == 0
assert deque().count(None) == 0
assert deque(["a", "b", "a"]).count("a") == 2
assert deque([(1, 2), (1, 2), [1, 2]]).count((1, 2)) == 2
# Identity first, as for lists: NaN is found by identity.
nan = float("nan")
assert deque([nan, 1.0]).count(nan) == 1
assert deque([1, 1.0, True]).count(1) == 3


class Eq:
    def __eq__(self, other):
        return isinstance(other, int) and other % 2 == 0


assert deque([2, 3, 4, 6]).count(Eq()) == 3
assert deque.count(deque([5, 5]), 5) == 2

try:
    d.count()
except TypeError:
    pass
else:
    raise AssertionError("expected TypeError")

print("deque_count: ok")
