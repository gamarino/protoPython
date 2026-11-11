# Review round 12 of 1.2.2 (tasks/p8-round11-review-report.md):
#  * HIGH1: a class's cached flags (PYFLAG_HAS_CUSTOM_STR and the others) are
#    dropped on every route that changes a class attribute or its MRO, not only
#    on a plain `Cls.name = v`: type.__setattr__, a metaclass __setattr__ that
#    delegates to it, __bases__ assignment and delattr.
#  * MEDIUM1: mutators on a held instance __dict__ cost time independent of
#    (pop, setdefault, del) or linear in (clear, popitem) the instance's size,
#    not quadratic; a write through a fresh `o.__dict__` is linear, not
#    quadratic.
#  * Q20: `a.__class__ = B` switches the class used for method lookup; a value
#    written into an instance __dict__ under a name that a data descriptor of
#    the type owns (property, __slots__ member) no longer shadows it; a
#    __str__/__repr__ that returns a non-str raises TypeError.
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix.

SECTIONS = {}

SECTIONS['HIGH1_flag_routes'] = r"""
def routes(v):
    return [f'{v}', format(v), '{}'.format(v), '%s' % (v,), str(v)]
# type.__setattr__ on a class whose flags were already computed.
class T2(float): pass
t2 = T2(1.25); f'{t2}'
type.__setattr__(T2, '__str__', lambda s: 'T2')
assert routes(t2) == ['T2'] * 5, routes(t2)
# A metaclass __setattr__ that delegates, through type.__setattr__ or super().
class Meta(type):
    def __setattr__(cls, n, v): type.__setattr__(cls, n, v)
class MM(int, metaclass=Meta): pass
mm = MM(2); f'{mm}'
MM.__str__ = lambda s: 'MM'
assert routes(mm) == ['MM'] * 5, routes(mm)
class Meta2(type):
    def __setattr__(cls, n, v): super().__setattr__(n, v)
class M3(int, metaclass=Meta2): pass
m3 = M3(4); f'{m3}'
M3.__repr__ = lambda s: 'M3r'
assert routes(m3) == ['M3r'] * 5, routes(m3)
# __bases__ assignment, plain and through type.__setattr__.
class M2(int): pass
class M1(int):
    def __str__(self): return 'M1'
class K(M2): pass
k = K(5); f'{k}'
K.__bases__ = (M1,)
assert routes(k) == ['M1'] * 5, routes(k)
class K2(M2): pass
k2 = K2(6); f'{k2}'
type.__setattr__(K2, '__bases__', (M1,))
assert K2.__mro__ == (K2, M1, int, object), K2.__mro__
assert routes(k2) == ['M1'] * 5, routes(k2)
# A subclass of a class patched through type.__setattr__.
class Base(int): pass
class Leaf(Base): pass
lf = Leaf(7); f'{lf}'
type.__setattr__(Base, '__str__', lambda s: 'Base')
assert routes(lf) == ['Base'] * 5, routes(lf)
# delattr on a class, and type.__delattr__-free deletion through del.
class D(int):
    def __str__(self): return 'D'
dv = D(8); assert f'{dv}' == 'D'
delattr(D, '__str__')
assert routes(dv) == ['8'] * 5, routes(dv)
"""

SECTIONS['MEDIUM1_held_dict_cost'] = r"""
import time
class C: pass
def bounded(label, n, steps, limit, step):
    t = time.perf_counter()
    for i in range(steps):
        step(i)
        if time.perf_counter() - t > limit:
            raise AssertionError(f'{label}: over {limit} s after {i + 1} of {steps} steps')
o = C()
for k in range(1000): setattr(o, 'a%d' % k, k)
d = o.__dict__
def sd_pop(i):
    d.setdefault('z', 1); d.pop('z')
    d['y'] = i; del d['y']
bounded('held setdefault/pop/del', 1000, 2000, 30.0, sd_pop)
assert not hasattr(o, 'z') and not hasattr(o, 'y') and o.a999 == 999
fresh = C()
bounded('fresh __dict__ writes', 2000, 2000, 40.0,
        lambda i: fresh.__dict__.__setitem__('a%d' % i, i))
assert len(vars(fresh)) == 2000 and fresh.a1999 == 1999
# The per-key refresh keeps the mutators acting on the live namespace.
p = C(); p.x = 1; p.y = 2
h = p.__dict__
p.x = 10; del p.y; p.w = 5
assert h.pop('x') == 10 and not hasattr(p, 'x')
assert h.pop('y', 'gone') == 'gone'
try:
    del h['y']
except KeyError:
    pass
else:
    raise AssertionError('del of a deleted name')
assert h.setdefault('w', 0) == 5 and p.w == 5
del h['w']
assert not hasattr(p, 'w')
h['v'] = 3; assert p.v == 3 and h.popitem() == ('v', 3) and not hasattr(p, 'v')
"""

SECTIONS['Q20_class_assignment'] = r"""
class A:
    def who(self): return 'A'
class B:
    def who(self): return 'B'
    def __str__(self): return 'B-str'
a = A()
a.__class__ = B
assert type(a) is B and a.__class__ is B and isinstance(a, B) and not isinstance(a, A)
assert a.who() == 'B' and str(a) == 'B-str' and f'{a}' == 'B-str', (a.who(), str(a))
a.__class__ = A
assert type(a) is A and a.who() == 'A'
b = A()
setattr(b, '__class__', B)
assert type(b) is B and b.who() == 'B'
object.__setattr__(b, '__class__', A)
assert type(b) is A and b.who() == 'A'
# The instance keeps its own attributes.
c = A(); c.x = 1
c.__class__ = B
assert c.x == 1 and vars(c) == {'x': 1} and c.who() == 'B'
# A subclass target resolves through its whole MRO.
class B2(B):
    def extra(self): return 'extra'
c.__class__ = B2
assert c.who() == 'B' and c.extra() == 'extra' and isinstance(c, B)
"""

SECTIONS['Q20_descriptor_not_shadowed'] = r"""
import copy
class PP:
    @property
    def p(self): return 'prop'
    @p.setter
    def p(self, v): self._p = v
def a(): o = PP(); o.__dict__['p'] = 'shadow'; return o.p, o.__dict__['p']
def b(): o = PP(); o.__dict__.update(p='shadow'); return o.p, vars(o)['p']
def c(): o = PP(); vars(o).update({'p': 'shadow'}); return o.p, vars(o)['p']
def d(): o = PP(); o.__dict__.setdefault('p', 'shadow'); return o.p, o.__dict__['p']
def e(): o = PP(); o.__dict__ |= {'p': 'shadow'}; return o.p, o.__dict__['p']
def g(): o = PP(); o.__dict__['p'] = 'shadow'; return copy.copy(o).p, copy.copy(o).__dict__['p']
for fn in (a, b, c, d, e, g):
    assert fn() == ('prop', 'shadow'), (fn.__name__, fn())
o = PP(); o.__dict__['p'] = 'shadow'
del o.__dict__['p']
assert o.p == 'prop' and 'p' not in o.__dict__
h = PP().__dict__
h.update(p='shadow')
assert h.pop('p') == 'shadow'
o.p = 'via-setter'
assert o._p == 'via-setter' and o.p == 'prop'
class S:
    __slots__ = ('v', '__dict__')
s = S(); s.v = 1
s.__dict__['v'] = 2
assert s.v == 1, s.v
"""

SECTIONS['Q20_str_returns_non_str'] = r"""
class S:
    def __str__(self): return 5
class R:
    def __repr__(self): return 6
class N:
    def __str__(self): return None
def raises(f, msg):
    try:
        f()
    except TypeError as e:
        assert msg is None or str(e) == msg, str(e)
    else:
        raise AssertionError(msg)
sm = '__str__ returned non-string (type int)'
rm = '__repr__ returned non-string (type int)'
for f in (lambda: str(S()), lambda: '%s' % S(), lambda: f'{S()}', lambda: f'{S()!s}',
          lambda: format(S()), lambda: '{}'.format(S()), lambda: print(S())):
    raises(f, sm)
raises(lambda: str(N()), '__str__ returned non-string (type NoneType)')
for f in (lambda: repr(R()), lambda: '%r' % R(), lambda: f'{R()!r}', lambda: ascii(R()),
          lambda: repr([R()])):
    raises(f, rm)
raises(lambda: str(R()), None)
class Ok(str): pass
class SS:
    def __str__(self): return Ok('fine')
assert str(SS()) == 'fine' and f'{SS()}' == 'fine'
"""

import sys

failed = []
for name, src in SECTIONS.items():
    try:
        exec(compile(src, f'<review_round12 {name}>', 'exec'), {'__name__': f'r12_{name}'})
    except BaseException as e:
        failed.append(name)
        print(f'FAIL {name}: {type(e).__name__}: {e}'[:600])
if failed:
    print('review_round12: FAILED', failed)
    sys.exit(1)
print('review_round12: ok')
