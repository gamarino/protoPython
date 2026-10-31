# Unequal set elements that share a hash stay distinct, and membership
# compares them with __eq__. A set keyed by hash alone kept one element per
# hash: {a, b} below had length 1 and K(3) was "in" it without __eq__ ever
# running.

eq_calls = []


class K:
    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return 7

    def __eq__(self, other):
        eq_calls.append((self.v, getattr(other, "v", other)))
        return isinstance(other, K) and self.v == other.v

    def __repr__(self):
        return "K(%d)" % self.v


a, b = K(1), K(2)
s = {a, b}
assert len(s) == 2, len(s)
assert a in s and b in s

del eq_calls[:]
assert K(3) not in s
assert eq_calls, "membership of an unequal colliding element must call __eq__"

# An equal but distinct element is found, and the stored object stays.
assert K(1) in s
s.add(K(1))
assert len(s) == 2
assert any(e is a for e in s)

# The dict was already correct; set and dict agree.
d = {a: 1, b: 2}
assert len(d) == 2
assert set(d) == s

# frozenset, set() and comprehensions take the same path as the literal.
assert len(frozenset([a, b])) == 2
assert len(set([a, b, K(1), K(2)])) == 2
assert len({K(i) for i in range(5)}) == 5

print("set_colliding_hash: ok")
