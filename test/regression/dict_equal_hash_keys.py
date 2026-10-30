# Unequal keys that share a hash are distinct dict keys; equal keys with
# equal hashes are one key. A dict keyed by hash alone silently merged
# K(1) and K(2) below.

class K:
    def __init__(self, n):
        self.n = n
    def __hash__(self):
        return 42
    def __eq__(self, other):
        return isinstance(other, K) and other.n == self.n
    def __repr__(self):
        return "K(%d)" % self.n


class Same:
    """Equal to the string "x" and hashed like it."""
    def __hash__(self):
        return hash("x")
    def __eq__(self, other):
        return other == "x"


a, b = K(1), K(2)
d = {a: "a"}
assert b not in d
assert d.get(b, "missing") == "missing"
d[b] = "b"
assert len(d) == 2, len(d)
assert d[a] == "a" and d[b] == "b"
assert list(d.items()) == [(a, "a"), (b, "b")]
assert list(d.values()) == ["a", "b"]

# An equal but distinct key finds the entry and keeps the stored key object.
a2 = K(1)
d[a2] = "a2"
assert len(d) == 2 and d[a] == "a2"
assert next(iter(d)) is a

# Literal, comprehension, copy, update, |, ==.
lit = {K(1): 1, K(2): 2, K(3): 3}
assert len(lit) == 3 and lit[K(2)] == 2
comp = {K(i): i for i in range(5)}
assert len(comp) == 5 and all(comp[K(i)] == i for i in range(5))
c = comp.copy()
assert c == comp and len(c) == 5
u = {}
u.update(comp)
assert u == comp
assert len(comp | {K(9): 9}) == 6
assert {**comp, K(0): "x"}[K(0)] == "x"
assert dict(comp.items())[K(4)] == 4

# Removal keeps the colliding neighbours.
del d[a]
assert a not in d and b in d and d[b] == "b" and len(d) == 1
assert comp.pop(K(3)) == 3 and K(3) not in comp and len(comp) == 4
assert comp.setdefault(K(3), 33) == 33 and comp[K(3)] == 33
k, v = comp.popitem()
assert k == K(3) and v == 33 and len(comp) == 4
assert sorted(x.n for x in comp) == [0, 1, 2, 4]

class StrLike:
    """Hashed like "x" but unequal to it."""
    def __hash__(self):
        return hash("x")
    def __eq__(self, other):
        return False

# A colliding key equal to a str finds the str's entry, and an unequal one
# does not.
m = {"x": "str"}
assert m[Same()] == "str"
m[Same()] = "same"
assert len(m) == 1 and m["x"] == "same"
s7 = StrLike()
assert s7 not in m
m[s7] = "s7"
assert len(m) == 2 and m["x"] == "same" and m[s7] == "s7"
del m["x"]
assert list(m.items()) == [(s7, "s7")]

# __eq__ is consulted only for keys of equal hash.
calls = []
class Counted:
    def __init__(self, n, h):
        self.n, self.h = n, h
    def __hash__(self):
        return self.h
    def __eq__(self, other):
        calls.append(self.n)
        return isinstance(other, Counted) and other.n == self.n
e = {Counted(1, 1): 1, Counted(2, 2): 2}
calls.clear()
assert e[Counted(2, 2)] == 2
assert calls == [2], calls

print("dict_equal_hash_keys: ok")
