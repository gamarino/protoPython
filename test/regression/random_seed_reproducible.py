# random.Random(seed) is reproducible.
#
# _random.Random was a stub that ignored the seed and read os.urandom() on
# every call, so two generators built with the same seed produced different
# sequences and random.seed(n) did not make the module sequence repeatable.

import random

# Two instances with the same seed agree, element for element.
a = random.Random(12345)
b = random.Random(12345)
seq_a = [a.random() for _ in range(20)]
seq_b = [b.random() for _ in range(20)]
assert seq_a == seq_b, "same seed produced different sequences"

# Different seeds disagree.
c = random.Random(54321)
assert [c.random() for _ in range(20)] != seq_a

# Every value is a float in [0, 1).
for v in seq_a:
    assert isinstance(v, float)
    assert 0.0 <= v < 1.0

# The sequence is not constant.
assert len(set(seq_a)) > 1

# The module-level seed() makes the shared generator reproducible.
random.seed(7)
x = [random.random() for _ in range(10)]
random.seed(7)
y = [random.random() for _ in range(10)]
assert x == y, "random.seed(7) did not reproduce the sequence"

# .seed() on an instance resets it.
d = random.Random(99)
first = [d.random() for _ in range(5)]
d.seed(99)
assert [d.random() for _ in range(5)] == first

# getrandbits is reproducible and respects its width.
e = random.Random(2024)
f = random.Random(2024)
for k in (1, 8, 32, 33, 64, 200):
    va = e.getrandbits(k)
    vb = f.getrandbits(k)
    assert va == vb, "getrandbits(%d) not reproducible" % k
    assert 0 <= va < (1 << k), "getrandbits(%d) out of range: %d" % (k, va)

# The derived helpers inherit reproducibility.
g = random.Random(31337)
h = random.Random(31337)
assert [g.randint(1, 1000) for _ in range(10)] == [h.randint(1, 1000) for _ in range(10)]

g = random.Random(31337)
h = random.Random(31337)
population = ["alpha", "beta", "gamma", "delta", "epsilon"]
assert [g.choice(population) for _ in range(10)] == [h.choice(population) for _ in range(10)]

g = random.Random(5)
h = random.Random(5)
la = list(range(10))
lb = list(range(10))
g.shuffle(la)
h.shuffle(lb)
assert la == lb, "shuffle is not reproducible"
assert sorted(la) == list(range(10)), "shuffle lost or duplicated elements"

# getstate/setstate round-trip: restoring the state replays the sequence.
r = random.Random(808)
state = r.getstate()
before = [r.random() for _ in range(5)]
r.setstate(state)
assert [r.random() for _ in range(5)] == before, "setstate did not restore the stream"

# The stream matches CPython's Mersenne Twister for an integer seed.
assert random.Random(42).random() == 0.6394267984578837

# An unseeded generator still differs between instances.
assert random.Random().random() != random.Random().random()

print("random_seed_reproducible: ok")
