"""deques compare element-wise with each other, are unhashable, and maxlen is read-only."""
from collections import deque


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


assert deque([1, 2]) == deque([1, 2]) and not (deque([1]) != deque([1]))
assert deque([1]) != deque([2]) and deque() == deque()
assert not (deque([1]) == [1]) and deque([1]) != [1]
assert deque([1]) < deque([2]) and deque([1, 2]) >= deque([1])
assert deque([1]) <= deque([1]) and deque([2]) > deque([1, 5])


class D(deque):
    pass


class P:
    def __init__(self, x):
        self.x = x

    def __eq__(self, other):
        return isinstance(other, P) and other.x == self.x

    __hash__ = None


assert D([1]) == deque([1]) and deque([1], maxlen=3) == deque([1])
assert deque([P(1)]) == deque([P(1)]) and deque([P(1)]) != deque([P(2)])
assert raises(TypeError, lambda: hash(deque()))

d = deque([1], maxlen=2)


def set_maxlen():
    d.maxlen = 5


assert raises(AttributeError, set_maxlen)
assert d.maxlen == 2 and deque().maxlen is None and deque(maxlen=0).maxlen == 0
d.extend([2, 3])
assert list(d) == [2, 3]

print("deque compare OK")
