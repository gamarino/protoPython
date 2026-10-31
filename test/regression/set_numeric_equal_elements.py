# 1, 1.0 and True are one set element (and 0, 0.0, False another), as in
# CPython: equal values with equal hashes collapse, and the first stored
# object stays.

s = {1, 1.0, True}
assert len(s) == 1, s
assert type(next(iter(s))) is int
assert 1 in s and 1.0 in s and True in s

z = {0.0, 0, False}
assert len(z) == 1 and type(next(iter(z))) is float

m = {1, 2.0, True, 2, 3}
assert len(m) == 3, m
assert m == {1, 2, 3}

f = frozenset([True, 1, 1.0])
assert len(f) == 1 and f == frozenset([1]) and hash(f) == hash(frozenset([1.0]))

big = 2 ** 70
assert len({big, float(big)}) == 1

s = {1, 2, 3}
s.discard(1.0)
s.remove(True if False else 2.0)
assert s == {3}

print("set_numeric_equal_elements: ok")
