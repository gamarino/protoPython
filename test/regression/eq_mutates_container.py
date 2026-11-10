# A user __eq__ (or __hash__) that replaces a set's or dict's storage during
# that container's own lookup. The lookup walked the old storage version held
# only by C++ locals while the collector could free it: a recycled cell was
# compared (AttributeError on a stray object) or the process crashed. Run with
# PROTOCORE_GC_CONTEXT_THRESHOLD=100 so a collection runs inside the __eq__.
#
# As in CPython, a lookup whose comparison changed the container restarts on
# the new storage; __eq__ mutates only on its first calls here, so the
# restarts end (CPython loops forever on an __eq__ that always mutates).
import sys

ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 else 150


class K:
    def __init__(self, t):
        self.t = t

    def __hash__(self):
        return 12345

    def __eq__(self, other):
        return isinstance(other, K) and other.t == self.t


def churn():
    return [[0] * 100 for _ in range(50)]


class Mutator(K):
    budget = 0
    target = None

    def __eq__(self, other):
        if Mutator.budget > 0:
            Mutator.budget -= 1
            c = Mutator.target
            if isinstance(c, set):
                c.clear()
                c.update(K(i) for i in range(40))
            else:
                c.clear()
                for i in range(40):
                    c[K(i)] = [i]
            churn()
        if not isinstance(other, K):
            return False
        # A recycled cell has no `t`: that is the use-after-free.
        return other.t == self.t

    __hash__ = K.__hash__


def arm(container, n=2):
    Mutator.target = container
    Mutator.budget = n


for r in range(ROUNDS):
    # sets: in, add, discard, remove
    s = {K(i) for i in range(8)}
    arm(s)
    assert Mutator(3) in s
    arm(s)
    s.add(Mutator(39))
    assert K(39) in s
    arm(s)
    s.discard(Mutator(4))
    assert K(4) not in s
    arm(s)
    s.remove(Mutator(5))
    assert K(5) not in s

    # dicts: get, in, getitem, setitem, delitem, pop, setdefault
    d = {K(i): [i] for i in range(8)}
    arm(d)
    assert d.get(Mutator(3)) == [3]
    arm(d)
    assert Mutator(6) in d
    arm(d)
    assert d[Mutator(7)] == [7]
    arm(d)
    d[Mutator(2)] = "two"
    assert d[K(2)] == "two" and len(d) == 40
    arm(d)
    del d[Mutator(9)]
    assert K(9) not in d
    arm(d)
    assert d.pop(Mutator(10)) == [10]
    arm(d)
    assert d.setdefault(Mutator(11), "x") == [11]

print("eq_mutates_container: ok")
