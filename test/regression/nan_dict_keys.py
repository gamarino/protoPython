# NaN hashes by identity (CPython 3.10+): distinct NaN objects are distinct
# dict and set keys, and the same NaN object is found again.
nan1 = float("nan")
nan2 = float("nan")
assert nan1 is not nan2
assert hash(nan1) == hash(nan1)

# Set literals, set() and frozenset().
assert len({nan1, nan2}) == 2
assert len({nan1, nan1}) == 1
assert len({float("nan"), float("nan")}) == 2
assert len(set([nan1, nan2, nan1])) == 2
assert len(frozenset([nan1, nan2, nan1])) == 2
s = set()
s.add(nan1)
s.add(nan2)
s.add(nan1)
assert len(s) == 2
assert nan1 in {nan1}
assert nan2 not in {nan1}
s.remove(nan2)
assert len(s) == 1 and nan1 in s and nan2 not in s

# Dict literals, lookups and dict.fromkeys().
d = {nan1: 1, nan2: 2}
assert len(d) == 2
assert d[nan1] == 1 and d[nan2] == 2
assert len({nan1: 1, nan1: 2}) == 1
assert nan1 in {nan1: 0}
assert nan2 not in {nan1: 0}
assert len(dict.fromkeys([nan1, nan2])) == 2
del d[nan1]
assert nan1 not in d and d[nan2] == 2

# Tuples are keyed by their elements.
assert len({(nan1,), (nan2,)}) == 2
assert len({(nan1,), (nan1,)}) == 1


# Instances of a float subclass holding NaN.
class F(float):
    pass


x = F("nan")
y = F("nan")
assert len({x, y}) == 2
assert len({x, x}) == 1
assert hash(x) == hash(x)

# Other floats still key by value.
assert len({1.5, float("1.5")}) == 1
assert len({float("inf"), float("inf")}) == 1
assert len({1.0, 1}) == 1

print("nan_dict_keys: ok")
