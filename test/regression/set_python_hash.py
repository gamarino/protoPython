"""Sets key their elements like dict keys: numeric equivalence, __hash__, tuples, unhashables."""


class P:
    def __init__(self, x):
        self.x = x

    def __hash__(self):
        return hash(self.x)

    def __eq__(self, other):
        return isinstance(other, P) and other.x == self.x


class MyInt(int):
    pass


class MyTuple(tuple):
    pass


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


# __hash__ is honoured.
assert len({P(3), P(3)}) == 1
assert P(3) in {P(3)}
assert P(4) not in {P(3)}
s = {P(1), 2}
s.remove(P(1))
s.discard(2.0)
assert s == set()

# 1, 1.0 and True are one element; the first one stored is kept.
assert len({1, 1.0, True}) == 1
assert repr({1, 1.0}) == "{1}"
assert 1.0 in {1} and True in {1} and False in {0} and 2.0 ** 70 in {2 ** 70}
assert {1, 2} == {2.0, 1.0}
assert frozenset([1]) == frozenset([1.0])
assert hash(frozenset([1])) == hash(frozenset([1.0]))
assert len({1}.union([1.0, True, 2])) == 2

# Set algebra uses the same keys.
assert {P(1), P(2)} & {P(2)} == {P(2)}
assert {P(1), P(2)} - {P(2)} == {P(1)}
assert {P(1), P(2)} ^ {P(2), P(3)} == {P(1), P(3)}
assert {P(1)} <= {P(1), P(2)} and {P(1)} < {P(1), P(2)}
assert {1, 2} >= {2.0} and not {1} > {1.0}
assert {P(1), P(2)}.issuperset([P(1)])
assert not {P(1)}.isdisjoint([P(1)])
u = {1}
u.update([1.0, True, P(5), P(5)])
u |= {P(5)}
assert len(u) == 2
v = {P(1), P(2)}
v &= {P(1)}
v -= {P(9)}
assert v == {P(1)}
assert type(frozenset([1]) | {2}) is frozenset and type({2} | frozenset([1])) is set

# Frozensets and tuples as elements and keys.
assert len({frozenset([1]), frozenset([2])}) == 2
assert frozenset([1, 2]) in {frozenset([2, 1])}
assert {frozenset([1]): "a", frozenset([2]): "b"}[frozenset([2.0])] == "b"
assert {1} in {frozenset([1])}
assert hash((1, 2)) == hash((1.0, 2))
assert len({(1, 2), (1.0, 2)}) == 1
assert {(1, (2, True)): "y"}[(1, (2, 1))] == "y"
assert {(P(1), 2): "z"}[(P(1), 2)] == "z"

# Subclasses of builtins that keep the builtin __hash__ are keyed by value.
assert {MyInt(3): "i"}[3] == "i" and {3: "i"}[MyInt(3)] == "i"
assert {MyTuple((1, 2)): "t"}[(1, 2)] == "t"
assert MyInt(3) in {3}

# Unhashable elements raise TypeError.
assert raises(TypeError, lambda: {[1]})
assert raises(TypeError, lambda: set().add([1]))
assert raises(TypeError, lambda: [1] in {1})
assert raises(TypeError, lambda: {([1],)})
assert raises(TypeError, lambda: set([{}]))
assert raises(TypeError, lambda: frozenset([[1]]))
assert raises(TypeError, lambda: {x for x in [[1]]})

print("set python hash OK")
