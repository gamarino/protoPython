# Attribute reads and writes resolved by one protoCore lookup (OP_LOAD_ATTR /
# OP_STORE_ATTR common paths) against CPython: what the class annotates (a
# property, a classmethod, a descriptor, a function to bind, an overriding
# __getattribute__ or __setattr__) is honoured, whatever the depth.
class O: v = 'O'
class L(O): pass
class R(O): v = 'R'
class X(L, R): pass
x = X(); print("diamond", x.v)
class B:
    def __getattribute__(s, n): return 42
class C(B): pass
c = C(); object.__setattr__(c, 'y', 1); print("base getattribute", c.y)
class D: pass
d = D(); d.y = 1; print("before", d.y)
D.__getattribute__ = lambda s, n: 7
print("assigned later", d.y)
class P:
    @property
    def p(self): return 'prop'
    def m(self): return 'meth'
    cv = 5
p = P(); print("property", p.p, "method", p.m(), "classvar", p.cv, "bound", p.m.__self__ is p)
def free(): return 'free'
p.f = free; print("own function", p.f())
class Q(P): pass
print("class access", Q.m is P.m, Q.cv)
class S2(P):
    def m(self): return super().m() + '+'
print("super", S2().m())
import functools, collections, dataclasses, enum, math
class K:
    cv = 1
    @classmethod
    def cm(cls): return ('cm', cls.__name__)
    @staticmethod
    def sm(x): return ('sm', x)
    def m(self): return 'm'
k = K()
print(k.cm(), K.cm(), k.sm(3), K.sm(4), K.m(k), k.m())
f = k.cm; print(f())
class P:
    def __init__(self): self._x = 0
    @property
    def x(self): return self._x
    @x.setter
    def x(self, v): self._x = v * 2
p = P(); p.x = 5; print("prop", p.x)
class Sl:
    __slots__ = ('a', 'b')
    def __init__(self): self.a = 1; self.b = None
s = Sl(); print("slots", s.a, s.b)
class Call:
    def __call__(self): return 'called'
class H: pass
h = H(); h.c = Call(); h.g = len; print("own callable", h.c(), h.g([1, 2]))
class Meta(type):
    mv = 'meta'
    @property
    def mp(cls): return 'metaprop ' + cls.__name__
class WM(metaclass=Meta): pass
print("meta", WM.mv, WM.mp)
class NonData:
    def __get__(self, o, t=None): return 'nondata'
class ND:
    d = NonData()
nd = ND(); print("nondata", nd.d); nd.__dict__['d'] = 'shadow'; print("shadowed", nd.d)
class GA:
    real = 1
    def __getattr__(self, n): return 'fallback ' + n
ga = GA(); print("getattr", ga.real, ga.missing)
class CP:
    @functools.cached_property
    def v(self): print("compute"); return 42
cp = CP(); print("cached", cp.v, cp.v)
Pt = collections.namedtuple('Pt', 'x y'); pt = Pt(1, 2); print("namedtuple", pt.x, pt.y)
@dataclasses.dataclass
class DC:
    a: int
    b: str = 'z'
dc = DC(3); print("dataclass", dc.a, dc.b)
class Color(enum.Enum):
    RED = 1
print("enum", Color.RED.value, Color.RED.name, Color(1) is Color.RED)
print("module", math.pi > 3, math.sqrt(4))
class Fl:
    t = True
    f = 1.5
    st = 'abc'
    n = None
fl = Fl(); print("kinds", fl.t, fl.f, fl.st, fl.n)
class Outer:
    class Inner: pass
print("nested", Outer().Inner.__name__)
class Ctr:
    count = 0
    def inc(self): self.count += 1; return self.count
c = Ctr(); c.inc(); c.inc(); print("counter", c.count, Ctr.count)
class DS:
    def __init__(self): self.v = 1
ds = DS(); ds.v = 2
DS.__setattr__ = lambda s, n, v: object.__setattr__(s, n, v * 10)
ds.v = 3; print("setattr later", ds.v)
class Sh:
    def m(self): return 'class'
sh = Sh(); sh.m = lambda: 'instance'; print("shadow", sh.m(), Sh.m(sh))
class CW: k = 1
cw = CW(); CW.k = 5; print("class write", cw.k); cw.k = 6; print("instance write", cw.k, CW.k)
class SW:
    __slots__ = ('a',)
sw = SW(); sw.a = 3; print("slot write", sw.a)
try:
    sw.b = 1
except AttributeError:
    print("slot reject")
class GB:
    def __init__(self): self.z = 'zz'
class GC(GB):
    def get(self): return super().__getattribute__('z')
print("super getattribute", GC().get())
class Deep0:
    def __init__(self): self.f = 0
    def bump(self): self.f += 1; return self.f
class Deep1(Deep0): pass
class Deep2(Deep1): pass
class Deep3(Deep2): pass
dp = Deep3(); [dp.bump() for _ in range(3)]; print("deep", dp.f, dp.bump())
