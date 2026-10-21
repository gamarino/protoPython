# Instantiation on the one-lookup path of runUserClassCall (__new__ and
# __init__ found on the class's chain) against CPython: the checks
# object.__new__ makes, __init__ kinds, containers, exceptions, and the
# instance __dict__ created on demand.
import dataclasses
class Plain:
    def __init__(self, a, b=2): self.a = a; self.b = b
p = Plain(1, b=3); print("plain", p.a, p.b, sorted(vars(p).items()))
class NoInit: pass
n = NoInit(); print("no init", type(n).__name__, vars(n))
try:
    NoInit(1)
except TypeError as e:
    print("takes no arguments", "takes no arguments" in str(e))
class BadInit:
    def __init__(self): return 5
try:
    BadInit()
except TypeError:
    print("init returned non-None")
class Base:
    def __init__(self, v): self.v = v
class Derived(Base): pass
print("inherited init", Derived(7).v)
class NewOverride:
    def __new__(cls, *a):
        o = super().__new__(cls); o.from_new = a; return o
    def __init__(self, *a): self.from_init = a
no = NewOverride(1, 2); print("new override", no.from_new, no.from_init)
class Singleton:
    _inst = None
    def __new__(cls):
        if cls._inst is None: cls._inst = super().__new__(cls)
        return cls._inst
print("singleton", Singleton() is Singleton())
class MyList(list):
    def __init__(self, *a): super().__init__(*a); self.tag = 'ml'
ml = MyList([1, 2]); print("list subclass", ml, ml.tag, len(ml))
class MyDict(dict): pass
md = MyDict(a=1); print("dict subclass", md, type(md).__name__)
e = ValueError("bad", 2); print("exception args", e.args, str(e), e.__traceback__, e.__cause__)
class MyErr(Exception):
    def __init__(self, code): super().__init__("code %d" % code); self.code = code
try:
    raise MyErr(3)
except MyErr as err:
    print("custom exception", err.code, err.args)
@dataclasses.dataclass
class DC:
    x: int
    y: int = 0
print("dataclass", DC(1), DC(1, 2) == DC(1, 2))
class Sl:
    __slots__ = ('a',)
    def __init__(self): self.a = 1
print("slots", Sl().a)
class Late: pass
lt = Late(); lt.k = 1; lt.__dict__['j'] = 2; print("dict on demand", sorted(lt.__dict__.items()), lt.j)
class KwOnly:
    def __init__(self, *, k): self.k = k
print("kw only", KwOnly(k=9).k)
try:
    KwOnly(1)
except TypeError:
    print("kw only rejects positional")
