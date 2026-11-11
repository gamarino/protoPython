# Tuples whose hashes collide are distinct dict keys and set elements.
#
# A dict or set treated a tuple's hash as identifying the tuple, so two
# unequal tuples with one hash became one key without __eq__ ever running.
# The old element mix, h = (h * 1000003) ^ eh, also cancelled on nested
# tuples: (1, (2, 3)) and (2, (1, 3)) hashed alike, so
# {(1, (2, 3)): 'x', (2, (1, 3)): 'y'} had one key. A combined 64-bit hash
# of several values cannot identify them, so a tuple hit is now confirmed by
# equality, and the mix is CPython's xxHash round (protoCore 3.5.1's
# ProtoTuple::getHash).

a, b = (1, (2, 3)), (2, (1, 3))
assert a != b

# --- dict ---------------------------------------------------------------
d = {}
d[a] = "x"
d[b] = "y"
assert len(d) == 2, (len(d), d)
assert d[a] == "x" and d[b] == "y", d
assert a in d and b in d
assert (3, (1, 2)) not in d
assert d.get((3, (2, 1)), "missing") == "missing"
assert list(d.items()) == [(a, "x"), (b, "y")]

lit = {(1, (2, 3)): "x", (2, (1, 3)): "y"}
assert len(lit) == 2 and lit == d
comp = {(i, (j, k)): (i, j, k) for i in range(4) for j in range(4) for k in range(4)}
assert len(comp) == 64, len(comp)
assert all(comp[(i, (j, k))] == (i, j, k) for i in range(4) for j in range(4) for k in range(4))

# Equal but distinct tuples find the entry; the stored key object stays.
a2 = tuple([1, tuple([2, 3])])
assert a2 is not a and a2 == a
d[a2] = "x2"
assert len(d) == 2 and d[a] == "x2" and next(iter(d)) is a

# Lookups after deletion keep the colliding neighbour.
del d[a]
assert len(d) == 1 and a not in d and b in d and d[b] == "y"
try:
    d[a]
except KeyError:
    pass
else:
    raise AssertionError("deleted key still found")
d[a] = "again"
assert len(d) == 2 and d[a] == "again" and d[b] == "y"
assert d.pop(b) == "y" and b not in d and d[a] == "again" and len(d) == 1
assert d.setdefault(b, "z") == "z" and d[b] == "z" and len(d) == 2
del d[b]
del d[a]
assert len(d) == 0 and a not in d and b not in d

# Removal from a dict filled through a literal and a comprehension.
del comp[(1, (2, 3))]
assert (1, (2, 3)) not in comp and comp[(2, (1, 3))] == (2, 1, 3) and len(comp) == 63
assert comp.pop((2, (1, 3))) == (2, 1, 3) and (2, (1, 3)) not in comp and len(comp) == 62

# A tuple key and the equal number-mixed tuple are one key (1 == 1.0 == True).
m = {(1, (2, 3)): "int"}
m[(1.0, (2, True + 2))] = "mixed"
assert len(m) == 1 and m[(1, (2, 3))] == "mixed"

# Tuple subclasses key like tuples.
class T(tuple):
    pass

assert {T(a): 1, T(b): 2} == {a: 1, b: 2}
assert len({T(a): 1, b: 2}) == 2

# --- set ----------------------------------------------------------------
s = {a, b}
assert len(s) == 2, (len(s), s)
assert a in s and b in s and (3, (1, 2)) not in s
assert a not in {b} and b not in {a}
s.discard(a)
assert s == {b} and a not in s
s.add(a)
assert len(s) == 2
s.remove(b)
assert s == {a} and b not in s
assert len({(i, (j, k)) for i in range(4) for j in range(4) for k in range(4)}) == 64
assert {a} | {b} == {a, b} and len({a} | {b}) == 2
assert {a, b} - {a} == {b} and {a, b} & {b} == {b}
assert {a} ^ {b} == {a, b}
assert not {a}.issubset({b}) and {a}.isdisjoint({b})

# --- frozenset ----------------------------------------------------------
fs = frozenset([a, b])
assert len(fs) == 2 and a in fs and b in fs
assert frozenset([a]) != frozenset([b])
assert len({frozenset([a]), frozenset([b])}) == 2
assert len({frozenset([a, b]), frozenset([b, a])}) == 1
assert {frozenset([a]): 1, frozenset([b]): 2}[frozenset([b])] == 2

# --- the mix ------------------------------------------------------------
# Equal tuples hash alike; the nested pair no longer cancels.
assert hash(a) == hash(a2)
assert hash(a) != hash(b)
assert hash((1, (2, 3))) == hash((1.0, (2.0, 3)))

# --- real 64-bit collisions -----------------------------------------------
# Two tuples of ints with one hash under the new mix, solved for outside
# protoPython. Before the fix they were one dict key.
c1, c2 = (0, 0), (45, -5777169575480160)
assert hash(c1) == hash(c2), (hash(c1), hash(c2))
dc = {c1: "a", c2: "b"}
assert len(dc) == 2 and dc[c1] == "a" and dc[c2] == "b", dc
assert c2 not in {c1: 1} and c1 not in {c2: 1}
del dc[c1]
assert c1 not in dc and dc[c2] == "b"
sc = {c1, c2}
assert len(sc) == 2 and c2 not in {c1} and c1 not in {c2}
assert len(frozenset([c1, c2])) == 2

# A key whose hash is a digest (str, bytes, tuple) can equal an int's key
# hash, the SmallInteger tagged word (n << 10) | 1. Find such pairs.
MASK = (1 << 64) - 1

def int_twin(h):
    h &= MASK
    if h & 0x3FF != 1:
        return None
    n = h >> 10
    return n - (1 << 54) if n >= 1 << 53 else n

def find(make, h):
    i = 0
    while True:
        k = make(i)
        n = int_twin(h(k))
        if n is not None:
            return k, n
        i += 1

def fnv_bytes(b):
    h = 14695981039346656037
    for c in b:
        h = ((h ^ c) * 1099511628211) & MASK
    return h ^ 0x9e3779b97f4a7c15

t, n = find(lambda i: (i, "t"), hash)
assert t != n
dt = {t: "tuple", n: "int"}
assert len(dt) == 2 and dt[t] == "tuple" and dt[n] == "int", dt
assert len({t, n}) == 2 and n not in {t} and t not in {n}

# Sets confirm every single-element hit, so str and bytes elements whose
# digest equals an int's key hash stay distinct (dicts likewise; see
# dict_digest_key_collision.py).
st, n = find(lambda i: "k" + str(i), hash)
assert len({st, n}) == 2 and n not in {st} and st not in {n}
s2 = {st}
s2.add(n)
assert len(s2) == 2
s2.discard(n)
assert s2 == {st}
by, n = find(lambda i: b"b" + str(i).encode(), fnv_bytes)
assert len({by, n}) == 2 and n not in {by} and by not in {n}
assert len(frozenset([by, n])) == 2

print("tuple_key_hash: ok")
