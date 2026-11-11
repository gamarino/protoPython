# hash() of numbers is CPython's numeric hash: the value modulo the Mersenne
# prime 2**61 - 1, with the sign of the number, -1 reading -2. A large int
# hashed by a digest of its hex digits and a non-integral float by its bit
# pattern; equal numbers of any type hash alike, and dicts and sets index keys
# by these values. A __hash__ returning True gives hash() 1.
P = 2**61 - 1
# The expected values are CPython's on a 64-bit build.

ints = {
    0: 0, 1: 1, -1: -2, -2: -2, 2**55: 2**55, 2**61 - 2: 2**61 - 2,
    2**61 - 1: 0, 2**61: 1, 2**62: 2, 2**63 - 1: 3, -2**63: -4, 2**63: 4,
    2**64 + 5: 13, 2**100: 549755813888, -2**100: -549755813888,
    10**30: 1000000000000000000000000000000 % P,
    -(10**30) - 7: -((10**30 + 7) % P),
}
for n, h in ints.items():
    assert hash(n) == h, (n, hash(n), h)

floats = {
    0.5: 1152921504606846976, -0.5: -1152921504606846976,
    1.5: 1152921504606846977, 0.1: 230584300921369408,
    3.14159: 326484311674566659, 1e300: 1224995262755759164,
    1e-300: 482449582752280463, -2.75: -1729382256910270466,
    float("inf"): 314159, float("-inf"): -314159,
    2.0**70: hash(2**70), 1e22: hash(10**22),
}
for f, h in floats.items():
    assert hash(f) == h, (f, hash(f), h)

# Equal numbers are one key.
d = {2**61: "a", 2**100: "b", 0.5: "c"}
assert d[1 + 2**61 - 1] == "a" and d[2.0**100] == "b" and d[1 / 2] == "c"
assert {1, 2**61, 1.0} == {1, 2**61}
assert len({2**61, 1}) == 2   # one hash, unequal: two elements


class T:
    def __hash__(self):
        return True


class Big:
    def __hash__(self):
        return 2**100


class Word:
    def __hash__(self):
        return 2**62   # fits a 64-bit word: hash() returns it as is


class Bad:
    def __hash__(self):
        return "x"


assert hash(T()) == 1 and type(hash(T())) is int
assert hash(Big()) == 549755813888
assert hash(Word()) == 2**62
assert {Word(): 1} and Word() not in {2**62: 1}   # one hash, unequal
try:
    hash(Bad())
except TypeError:
    pass
else:
    raise AssertionError("a str from __hash__ did not raise TypeError")
assert {T(): 1}  # hashable through the same value

print("numeric_hash_cpython: ok")
