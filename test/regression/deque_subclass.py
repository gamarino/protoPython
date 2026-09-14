# Regression test: deque subclasses work, and deque.__init__ takes
# (iterable, maxlen).
#
# The native deque methods took the deque only from `self`. Reached through
# the type (iter() and reversed() on a subclass instance, deque.append(d, x),
# instance creation calling __init__) they receive it as the first argument,
# so iterating a subclass raised "iter() returned non-iterator of type
# 'NoneType'". deque.__init__ was NotImplemented, so super().__init__(it,
# maxlen) did nothing, and maxlen was not implemented.
from collections import deque


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


class D(deque):
    def peek(self):
        return self[-1]


d = D([1, 2])
d.append(3)
d.appendleft(0)
check("subclass", (list(d), d.peek(), len(d), isinstance(d, deque), type(d).__name__),
      ([0, 1, 2, 3], 3, 4, True, "D"))
check("for loop over a subclass", [x for x in d], [0, 1, 2, 3])
check("reversed subclass", list(reversed(d)), [3, 2, 1, 0])
check("repr of a subclass", repr(D([1])), "D([1])")


class B(deque):
    def __init__(self, it, maxlen=None):
        super().__init__(it, maxlen)
        self.extra = True


b = B(range(5), 3)
check("super().__init__(it, maxlen)", (list(b), b.maxlen, b.extra), ([2, 3, 4], 3, True))
b.append(5)
check("bounded append", list(b), [3, 4, 5])
b.appendleft(9)
check("bounded appendleft", list(b), [9, 3, 4])

check("maxlen keyword", (list(deque([1, 2, 3], maxlen=2)), deque([1, 2, 3], maxlen=2).maxlen), ([2, 3], 2))
check("maxlen default", deque([1]).maxlen, None)
check("repr with maxlen", repr(deque([1, 2], maxlen=2)), "deque([1, 2], maxlen=2)")
bounded = deque(maxlen=2)
bounded.extend([1, 2, 3])
check("bounded extend", list(bounded), [2, 3])
bounded.extendleft([7, 8])
check("bounded extendleft", list(bounded), [8, 7])
check("maxlen=0 keeps nothing", list(deque([1, 2], maxlen=0)), [])

x = deque([1, 2])
x.__init__([7, 8])
check("re-init replaces the contents", list(x), [7, 8])
check("deque(generator)", list(deque(i for i in range(3))), [0, 1, 2])
q = deque([1])
deque.append(q, 5)
check("unbound methods", (list(deque.__iter__(q)), deque.__len__(q)), ([1, 5], 2))


class Ignores(deque):
    def __init__(self, it):
        pass


check("__new__ ignores the arguments", len(Ignores([1, 2])), 0)
check("popleft", deque("ab").popleft(), "a")

try:
    deque([], maxlen=-1)
except ValueError:
    pass
else:
    raise AssertionError("a negative maxlen was accepted")

print("deque subclass OK")
