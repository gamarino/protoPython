"""Unhashable dict keys raise TypeError, like CPython; hashable keys are unaffected."""


def raises(exc, fn):
    try:
        fn()
    except exc as e:
        return str(e)
    return None


class P:
    def __init__(self, x):
        self.x = x

    def __hash__(self):
        return hash(self.x)

    def __eq__(self, other):
        return isinstance(other, P) and other.x == self.x


d = {1: "a"}


def store():
    d[[1]] = 2


def delete():
    del d[[1]]


assert raises(TypeError, store) == "unhashable type: 'list'"
assert raises(TypeError, lambda: d.__setitem__([1], 2))
assert raises(TypeError, lambda: d[[1]])
assert raises(TypeError, lambda: [1] in d)
assert raises(TypeError, lambda: d.get({}))
assert raises(TypeError, lambda: d.setdefault(set(), 0))
assert raises(TypeError, lambda: d.pop([1], None))
assert raises(TypeError, delete)
assert raises(TypeError, lambda: {[1]: 2})
assert raises(TypeError, lambda: {k: 0 for k in [[1]]})
assert d == {1: "a"}

# Hashable keys, including ones that are not str or int, go by their hash.
e = {(1, 2): 1, frozenset([1]): 2, None: 3, P(4): 4, 2.5: 5}
assert (1.0, 2) in e and frozenset([1.0]) in e and None in e and P(4) in e
assert 2.5 in e and (3, 4) not in e and P(5) not in e
assert e[(1, 2)] == 1 and e.get(P(4)) == 4

print("dict unhashable keys OK")
