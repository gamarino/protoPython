# Every set and frozenset operation keeps unequal elements that share a hash
# apart and compares them with __eq__.

import copy
import pickle


class K:
    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return 11

    def __eq__(self, other):
        return isinstance(other, K) and self.v == other.v

    def __repr__(self):
        return "K(%d)" % self.v

    def __reduce__(self):
        return (K, (self.v,))


def ks(*vs):
    return [K(v) for v in vs]


def vals(s):
    return sorted(e.v for e in s)


# add / len / in / iteration
s = set()
for e in ks(1, 2, 3, 4, 5):
    s.add(e)
assert len(s) == 5, len(s)
assert vals(s) == [1, 2, 3, 4, 5], vals(s)
assert all(K(v) in s for v in range(1, 6))
assert K(6) not in s
assert len(list(iter(s))) == 5
assert bool(s)

# discard / remove of a middle element of the bucket
s.discard(K(3))
assert vals(s) == [1, 2, 4, 5], vals(s)
s.discard(K(99))
assert len(s) == 4
s.remove(K(1))
assert vals(s) == [2, 4, 5]
try:
    s.remove(K(1))
except KeyError:
    pass
else:
    raise AssertionError("remove of an absent colliding element must raise KeyError")
assert len(s) == 3

# pop drains every element exactly once
p = set(ks(1, 2, 3))
popped = sorted(p.pop().v for _ in range(3))
assert popped == [1, 2, 3] and len(p) == 0
try:
    p.pop()
except KeyError:
    pass
else:
    raise AssertionError("pop from an empty set must raise KeyError")

# copy / clear
c = set(ks(1, 2, 3))
c2 = c.copy()
assert vals(c2) == [1, 2, 3]
c.clear()
assert len(c) == 0 and len(c2) == 3

# Set operations between colliding sets
x = set(ks(1, 2, 3))
y = set(ks(2, 3, 4))
assert vals(x | y) == [1, 2, 3, 4]
assert vals(x & y) == [2, 3]
assert vals(x - y) == [1]
assert vals(x ^ y) == [1, 4]
assert vals(x.union(ks(5))) == [1, 2, 3, 5]
assert vals(x.intersection(ks(1, 3, 9))) == [1, 3]
assert vals(x.difference(ks(1))) == [2, 3]
assert vals(x.symmetric_difference(ks(3, 7))) == [1, 2, 7]
assert x.issubset(ks(1, 2, 3, 4)) and not x.issubset(ks(1, 2))
assert x.issuperset(ks(1, 3)) and not x.issuperset(ks(1, 8))
assert x.isdisjoint(ks(7, 8)) and not x.isdisjoint(ks(3, 8))
assert set(ks(1, 2)) < x and x <= x and not x < x
assert x > set(ks(1)) and x >= set(ks(1, 2, 3))
assert not (set(ks(1, 4)) <= x)

# In-place forms
z = set(ks(1, 2))
z |= set(ks(2, 3))
assert vals(z) == [1, 2, 3]
z &= set(ks(2, 3, 4))
assert vals(z) == [2, 3]
z -= set(ks(3))
assert vals(z) == [2]
z ^= set(ks(2, 5))
assert vals(z) == [5]
z.update(ks(6, 7))
assert vals(z) == [5, 6, 7]
z.intersection_update(ks(6, 7, 8))
assert vals(z) == [6, 7]
z.difference_update(ks(6))
assert vals(z) == [7]
z.symmetric_difference_update(ks(7, 8))
assert vals(z) == [8]

# Equality and comparisons
assert set(ks(1, 2, 3)) == set(ks(3, 2, 1))
assert set(ks(1, 2, 3)) != set(ks(1, 2, 4))
assert set(ks(1, 2)) != set(ks(1, 2, 3))

# frozenset: equality and hash with colliding members
f1 = frozenset(ks(1, 2, 3))
f2 = frozenset(ks(3, 1, 2))
f3 = frozenset(ks(1, 2, 4))
assert len(f1) == 3
assert f1 == f2 and hash(f1) == hash(f2)
assert f1 != f3
assert f1 == set(ks(1, 2, 3))
assert len({f1, f2, f3}) == 2
assert vals(f1 | f3) == [1, 2, 3, 4]
assert type(f1 & f3) is frozenset and vals(f1 & f3) == [1, 2]
# A set is looked up as the equal frozenset.
assert set(ks(1, 2, 3)) in {f1}
holder = {f1, f3}
holder.discard(set(ks(1, 2, 4)))
assert holder == {f1}

# Frozensets that collide with each other (same hash, unequal) as elements.
class FK:
    def __init__(self, v):
        self.v = v
    def __hash__(self):
        return 0
    def __eq__(self, other):
        return isinstance(other, FK) and self.v == other.v
fa, fb = frozenset([FK(1)]), frozenset([FK(2)])
assert fa != fb
assert len({fa, fb}) == 2

# Colliding with built-in values: 11 hashes like K(*), but is not equal.
mixed = {11, K(1), K(2)}
assert len(mixed) == 3
assert 11 in mixed and 11.0 in mixed and K(2) in mixed and K(3) not in mixed
mixed.discard(K(1))
assert len(mixed) == 2 and 11 in mixed

# repr, copy, deepcopy and pickle keep every element
r = repr(set(ks(1, 2)))
assert "K(1)" in r and "K(2)" in r, r
assert vals(copy.copy(set(ks(1, 2, 3)))) == [1, 2, 3]
assert vals(copy.deepcopy(frozenset(ks(1, 2, 3)))) == [1, 2, 3]
assert vals(pickle.loads(pickle.dumps(set(ks(1, 2, 3))))) == [1, 2, 3]

# Dict views as set operands
dk = {K(1): 0, K(2): 0}.keys()
assert vals(set(ks(2, 3)) & dk) == [2]
assert vals(dk | set(ks(3))) == [1, 2, 3]

print("set_colliding_hash_ops: ok")
