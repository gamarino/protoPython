# One dict and set index rule for every key type, derived from hash(): a key
# whose __hash__ returns hash(x) and that equals x finds x, in both orders.
# A user __hash__ result was used raw while an int was indexed by its tagged
# word, so W() (hash(W()) == hash(5), W() == 5) never found the key 5.


class W:
    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return hash(self.v)

    def __eq__(self, other):
        return other == self.v if not isinstance(other, W) else other.v == self.v


for x in (5, -1, 0, 2**53 - 1, 2**70, -(2**60), 1.5, float("inf"), "abc", b"xy", (1, "a"),
          frozenset({1, 2}), None, True):
    w = W(x)
    assert hash(w) == hash(x), (x, hash(w), hash(x))
    d = {x: "x"}
    assert d[w] == "x" and w in d, x
    d2 = {w: "w"}
    assert d2[x] == "w" and x in d2, x
    assert w in {x} and x in {w}, x
    d[w] = "replaced"
    assert len(d) == 1 and d[x] == "replaced", (x, d)
    assert d.pop(w) == "replaced" and not d, x

# __hash__ returning a large int is reduced as hash() reduces it.
class Big:
    def __hash__(self):
        return 2**80

    def __eq__(self, other):
        return other == 2**80


assert hash(Big()) == hash(2**80)
assert {2**80: 1}[Big()] == 1

# Equal tuples with equal-hashing elements hash alike.
assert hash((W(5), "a")) == hash((5, "a"))
assert {(5, "a"): 1}[(W(5), "a")] == 1

# -1, whose hash is -2, and -2 stay two keys.
d = {-1: "m1", -2: "m2"}
assert len(d) == 2 and d[-1] == "m1" and d[-2] == "m2"
del d[-2]
assert d == {-1: "m1"}

print("dict_user_hash_index: ok")
