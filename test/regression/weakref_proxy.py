# weakref.proxy forwards attribute access and operators to its referent and
# raises ReferenceError once the referent is gone.
import gc
import weakref

assert issubclass(ReferenceError, Exception) and ReferenceError.__module__ == "builtins"

class O:
    def __init__(self):
        self.v = 1
    def f(self):
        return 5
    def __add__(self, other):
        return ("add", other)
    def __len__(self):
        return 3
    def __getitem__(self, k):
        return k * 2

o = O()
p = weakref.proxy(o)
assert type(p) is weakref.ProxyType, type(p)
assert isinstance(p, weakref.ProxyTypes)
assert p.f() == 5 and p.v == 1
p.z = 3
assert o.z == 3
del p.z
assert not hasattr(o, "z")
assert p + 1 == ("add", 1)
assert len(p) == 3 and p[4] == 8
assert p.__class__ is O
try:
    hash(p)
except TypeError:
    pass
else:
    raise AssertionError("a proxy is unhashable")
assert repr(p).startswith("<weakproxy at ")

n = weakref.proxy(o)
lst = [1, 2, 3]
class L(list):
    pass
seq = L(lst)
q = weakref.proxy(seq)
assert len(q) == 3 and 2 in q and list(q) == [1, 2, 3] and q == [1, 2, 3]

def g(x):
    return x + 1
c = weakref.proxy(g)
assert type(c) is weakref.CallableProxyType and c(1) == 2

# A dead referent raises ReferenceError.  Collection is not deterministic
# here (gc.collect() does not force it), so the check runs when the
# referent is gone; on protoPython the dead path is also driven directly.
class D:
    pass
d = D()
alive = weakref.ref(d)
pd = weakref.proxy(d)
del d
gc.collect()
if alive() is None:
    try:
        pd.anything
    except ReferenceError:
        pass
    else:
        raise AssertionError("ReferenceError expected after the referent died")
try:
    import _weakrefproxy
except ImportError:
    pass
else:
    dead = object.__new__(_weakrefproxy.ProxyType)
    object.__setattr__(dead, "_wr", lambda: None)
    for op in (lambda: dead.x, lambda: len(dead), lambda: dead + 1, lambda: str(dead)):
        try:
            op()
        except ReferenceError:
            pass
        else:
            raise AssertionError("ReferenceError expected for a dead proxy")
    assert "dead" in repr(dead)
try:
    weakref.ProxyType(o)
except TypeError:
    pass
else:
    raise AssertionError("ProxyType cannot be instantiated")
# A user __getattribute__ answers __class__ and __dict__ too, as in CPython.
class G:
    def __getattribute__(self, name):
        return ("ga", name)
gobj = G()
assert gobj.__class__ == ("ga", "__class__") and getattr(gobj, "__dict__") == ("ga", "__dict__")
assert type(gobj) is G
# object.__getattribute__ bypasses a user __getattribute__ even when called
# outside the override.
class S:
    __slots__ = ("a",)
    def __getattribute__(self, n):
        return "user"
s = object.__new__(S)
object.__setattr__(s, "a", 7)
assert object.__getattribute__(s, "a") == 7
assert s.a == "user"

print("weakref_proxy: ok")
