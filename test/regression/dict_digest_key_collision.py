# Dict keys whose key hash is a digest are confirmed by equality.
#
# A dict files an entry under its key's 64-bit key hash. An int that fits a
# SmallInteger is keyed by its tagged word, (n << 10) | 1, which identifies
# it. A str, bytes, non-integral float, tuple or user-hashed key is keyed by
# a digest, which does not: a str whose FNV-1a content hash equals the word
# of some int was the same dict entry as that int, and `d[s] = 1; d[n]`
# answered 1. Every dict path (literal, item access, update, comprehension,
# keyword arguments, module and class namespaces, exec locals) now confirms
# a hit by identity, then equality.
#
# The twins are found at run time: for a digest h with h & 0x3FF == 1, the
# int h >> 10 (sign-extended at 2**54) has key hash h.

import struct
import sys

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
    return h ^ 0x9E3779B97F4A7C15


def libstdcxx_double_hash(d):
    # protoCore keys a double by std::hash<double>; this is libstdc++'s
    # (_Hash_bytes over the 8 bytes, seed 0xc70f6907). On another standard
    # library the pair found here does not collide, and the checks below
    # still hold.
    mul = (0xC6A4A793 << 32) + 0x5BD1E995
    sm = lambda v: v ^ (v >> 47)
    data = struct.unpack("<Q", struct.pack("<d", d))[0]
    h = (0xC70F6907 ^ (8 * mul)) & MASK
    h ^= (sm((data * mul) & MASK) * mul) & MASK
    h = (h * mul) & MASK
    h = (sm(h) * mul) & MASK
    return sm(h)


class UserHashed:
    # A user __hash__ is a digest too: 1025 is the key word of the int 1.
    def __init__(self, tag):
        self.tag = tag

    def __hash__(self):
        return 1025

    def __eq__(self, other):
        return isinstance(other, UserHashed) and other.tag == self.tag


st, n_st = find(lambda i: "k" + str(i), hash)
by, n_by = find(lambda i: b"b" + str(i).encode(), fnv_bytes)
fl, n_fl = find(lambda i: i + 0.5, libstdcxx_double_hash)
tu, n_tu = find(lambda i: (i, "t"), hash)
uh, n_uh = UserHashed("u"), 1

PAIRS = [(st, n_st), (by, n_by), (fl, n_fl), (tu, n_tu), (uh, n_uh)]


def check_pair(k, n):
    what = (k, n)
    assert k != n, what

    # literal, getitem, in, get
    d = {k: "digest", n: "int"}
    assert len(d) == 2, (what, d)
    assert d[k] == "digest" and d[n] == "int", (what, d)
    assert k in d and n in d
    assert d.get(k) == "digest" and d.get(n) == "int"
    assert n not in {k: 1} and k not in {n: 1}
    assert {k: 1}.get(n, "miss") == "miss" and {n: 1}.get(k, "miss") == "miss"
    try:
        {k: 1}[n]
    except KeyError:
        pass
    else:
        raise AssertionError(("int found a digest key", what))
    try:
        {n: 1}[k]
    except KeyError:
        pass
    else:
        raise AssertionError(("digest key found an int", what))

    # setitem, in both orders; an equal key replaces only its own entry
    for first, second in ((k, n), (n, k)):
        d = {}
        d[first] = 1
        d[second] = 2
        assert len(d) == 2 and d[first] == 1 and d[second] == 2, (what, d)
        d[first] = 3
        assert len(d) == 2 and d[first] == 3 and d[second] == 2, (what, d)
        assert list(d) == [first, second] and list(d.values()) == [3, 2]

    # del, pop, popitem keep the colliding neighbour
    d = {k: "digest", n: "int"}
    del d[k]
    assert len(d) == 1 and k not in d and d[n] == "int", (what, d)
    d[k] = "back"
    assert d.pop(n) == "int" and n not in d and d[k] == "back", (what, d)
    assert d.pop(n, "gone") == "gone"
    d[n] = "int"
    assert d.popitem() == (n, "int") and d == {k: "back"}, (what, d)

    # setdefault
    d = {k: "digest"}
    assert d.setdefault(n, "int") == "int" and len(d) == 2, (what, d)
    assert d.setdefault(k, "other") == "digest"

    # update, from a dict, pairs and keywords-free mapping
    d = {k: "digest"}
    d.update({n: "int"})
    assert len(d) == 2 and d[k] == "digest" and d[n] == "int", (what, d)
    d = {n: "int"}
    d.update([(k, "digest")])
    assert len(d) == 2 and d[k] == "digest" and d[n] == "int", (what, d)
    merged = {**{k: "digest"}, **{n: "int"}}
    assert len(merged) == 2 and merged[k] == "digest", (what, merged)
    assert ({k: 1} | {n: 2}) == {k: 1, n: 2} and len({k: 1} | {n: 2}) == 2

    # comprehension and dict()
    comp = {x: i for i, x in enumerate((k, n))}
    assert len(comp) == 2 and comp[k] == 0 and comp[n] == 1, (what, comp)
    built = dict([(k, 0), (n, 1)])
    assert built == comp and len(built) == 2
    assert dict.fromkeys([k, n], 0) == {k: 0, n: 0} and len(dict.fromkeys([k, n])) == 2

    # equality, copy, keys views
    assert {k: 1} != {n: 1}
    c = {k: 1, n: 2}.copy()
    assert len(c) == 2 and c[k] == 1 and c[n] == 2
    assert set({k: 1, n: 2}.keys()) == {k, n}
    assert len({k, n}) == 2 and n not in {k} and k not in {n}


for k, n in PAIRS:
    check_pair(k, n)

# --- True / 1 / 1.0 are one key; an exact key in a bucket is found ---------
d = {uh: "user", 1: "one"}
assert len(d) == 2 and d[True] == "one" and d[1.0] == "one" and d[uh] == "user"
d[True] = "true"
assert len(d) == 2 and d[1] == "true" and list(d) == [uh, 1]
del d[1.0]
assert d == {uh: "user"} and 1 not in d and True not in d
d[1] = "one"
assert d.pop(True) == "one" and len(d) == 1

# --- keyword arguments --------------------------------------------------
def kw(**kwargs):
    return kwargs

got = kw(**{st: "s"})
assert got == {st: "s"} and n_st not in got and got.get(n_st) is None
got[n_st] = "i"
assert len(got) == 2 and got[st] == "s" and got[n_st] == "i"

def named(**kwargs):
    return kwargs[st]

assert named(**{st: 7}) == 7

# --- module namespace ---------------------------------------------------
# protoPython's module namespaces take str keys only (a documented
# deviation), so the int twin is probed, never stored.
g = globals()
g[st] = "global"
assert g[st] == "global" and eval(st) == "global"
assert n_st not in g and g.get(n_st, "miss") == "miss"
del g[st]
assert st not in g

mod = sys.modules[__name__]
setattr(mod, st, "attr")
assert vars(mod)[st] == "attr" and n_st not in vars(mod)

# --- exec locals --------------------------------------------------------
loc = {n_st: "int"}
exec(st + " = 5", {}, loc)
assert loc[st] == 5 and loc[n_st] == "int" and len(loc) == 2, loc

# --- class namespaces ---------------------------------------------------
class C:
    pass

setattr(C, st, "class attr")
assert C.__dict__[st] == "class attr" and n_st not in C.__dict__
assert getattr(C, st) == "class attr"

class Meta(type):
    @classmethod
    def __prepare__(mcs, name, bases):
        ns = {}
        ns[n_st] = "prepared int"
        return ns

class D(metaclass=Meta):
    pass

setattr(D, st, "on D")
assert getattr(D, st) == "on D"

print("dict_digest_key_collision: ok")
