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
# The twins are found at run time. A dict files a key under the index of its
# Python hash h (hash(key)): the tagged word (h << 10) | 1 when h fits a
# SmallInteger, else h itself (64-bit). An int n other than -1 is filed
# under its own word, so for a digest h outside the SmallInteger range with
# h & 0x3FF == 1, the int h >> 10 (sign-extended at 2**54) shares its index.
# Each pair is checked against that model before use; a generator that finds
# no twin within the bound is skipped with a message, never silently.

import sys

MASK = (1 << 64) - 1
SMALL_MIN, SMALL_MAX = -(1 << 53), (1 << 53) - 1
BOUND = 200000


def index_of(h):
    if SMALL_MIN <= h <= SMALL_MAX:
        return ((h << 10) | 1) & MASK
    return h & MASK


def int_twin(h):
    if SMALL_MIN <= h <= SMALL_MAX:
        return None
    hu = h & MASK
    if hu & 0x3FF != 1:
        return None
    n = hu >> 10
    return n - (1 << 54) if n >= 1 << 53 else n


def find(what, make):
    for i in range(BOUND):
        k = make(i)
        n = int_twin(hash(k))
        if n is not None:
            # The pair really collides: one index, two unequal keys.
            assert index_of(hash(k)) == index_of(hash(n)), (what, k, n)
            assert k != n, (what, k, n)
            return k, n
    print("dict_digest_key_collision: no %s twin within %d candidates; skipped"
          % (what, BOUND))
    return None


class UserHashed:
    # A user __hash__ equal to hash(1): the int 1 shares its index.
    def __init__(self, tag):
        self.tag = tag

    def __hash__(self):
        return hash(1)

    def __eq__(self, other):
        return isinstance(other, UserHashed) and other.tag == self.tag


found = {
    "str": find("str", lambda i: "k" + str(i)),
    "bytes": find("bytes", lambda i: b"b" + str(i).encode()),
    "float": find("float", lambda i: (i + 2) ** 0.5 + 0.1),
    "tuple": find("tuple", lambda i: (i, "t")),
}
uh, n_uh = UserHashed("u"), 1
assert index_of(hash(uh)) == index_of(hash(n_uh))

PAIRS = [p for p in found.values() if p is not None] + [(uh, n_uh)]
# The keyword, module and class checks below use the str pair; without one
# they run on a non-colliding pair (the skip above was reported).
st, n_st = found["str"] if found["str"] is not None else ("k_none", 12345)


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
