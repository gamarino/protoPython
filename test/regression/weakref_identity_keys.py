"""weakref.ref hashes and compares like its referent, so WeakKeyDictionary
and functools.singledispatch's dispatch cache find their entries."""
import functools
import gc
import weakref


class C:
    pass


a = C()
b = C()

# id() and hash() of an instance do not change when its attributes change
# (a mutable object's identity is the reference, not its current snapshot).
ida, ha = id(a), hash(a)
a.x = 1
a.y = [1, 2]
del a.x
assert id(a) == ida, "id() changed after attribute mutation"
assert hash(a) == ha, "hash() changed after attribute mutation"
assert a is a and a == a

# weakref.ref equality and hashing.
r1 = weakref.ref(a)
r2 = weakref.ref(a)
assert r1() is a and r2() is a
assert r1 == r2, "two refs to the same live object must compare equal"
assert not (r1 != r2)
assert hash(r1) == hash(r2) == hash(a), "a ref hashes like its referent"
assert weakref.ref(a) != weakref.ref(b)
assert r1 != a and not (r1 == a), "a ref never equals a non-ref"
assert type(r1) is weakref.ref and isinstance(r1, weakref.ref)
assert repr(r1).startswith("<weakref at "), repr(r1)
assert len({r1, r2, weakref.ref(b)}) == 2

# Referents with their own __eq__/__hash__ are compared by value.
class V:
    def __init__(self, v):
        self.v = v

    def __eq__(self, other):
        return isinstance(other, V) and self.v == other.v

    def __hash__(self):
        return hash(self.v)


v1, v2 = V(7), V(7)
assert weakref.ref(v1) == weakref.ref(v2)
assert hash(weakref.ref(v1)) == hash(7)

# WeakKeyDictionary.
d = weakref.WeakKeyDictionary()
d[a] = 1
assert a in d and b not in d
assert d.get(a) == 1 and d[a] == 1 and d.get(b, 'none') == 'none'
assert len(d) == 1
d[a] = 2
assert len(d) == 1 and d[a] == 2, "re-setting a key must replace, not add"
a.z = 3                                   # mutating the key keeps it found
assert d[a] == 2
d[b] = 3
assert len(d) == 2 and sorted(d.values()) == [2, 3]
assert set(d.keys()) == {a, b}
del d[a]
assert a not in d and len(d) == 1
assert d.setdefault(b, 99) == 3
d[C] = 'class key'                        # classes are weak-referenceable
assert d[C] == 'class key'

# WeakValueDictionary.
wv = weakref.WeakValueDictionary()
wv['k'] = a
assert wv['k'] is a and wv.get('k') is a and 'k' in wv and len(wv) == 1
wv['k'] = b
assert wv['k'] is b and len(wv) == 1

# WeakSet.
ws = weakref.WeakSet()
ws.add(a)
ws.add(a)
assert a in ws and b not in ws and len(ws) == 1

# functools.singledispatch hits its dispatch cache after the first call.
misses = []
_orig_find_impl = functools._find_impl


def counting_find_impl(cls, registry):
    misses.append(cls)
    return _orig_find_impl(cls, registry)


functools._find_impl = counting_find_impl
try:
    @functools.singledispatch
    def f(x):
        return 'object'

    @f.register(int)
    def _(x):
        return 'int'

    for _i in range(5):
        assert f(1) == 'int' and f('s') == 'object' and f(a) == 'object'
finally:
    functools._find_impl = _orig_find_impl
assert int not in misses, misses          # found in the registry directly
assert misses.count(str) == 1, misses
assert misses.count(C) == 1, misses

gc.collect()
assert r1() is a
print("weakref identity keys OK")
