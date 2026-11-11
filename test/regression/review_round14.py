# Review round 14 of 1.2.2 (tasks/p8-round13-review-report.md):
#  * HIGH1: a plain store on a class whose base is a user class (`R.x = v`
#    with `class R(P)`) takes the class store protocol, not the instance fast
#    store: cached class flags are dropped (late __getattribute__,
#    __setattr__, __str__) and a late data descriptor is marked.
#  * LOW4: after `o.__class__ = C`, a data descriptor of C answers for a name
#    the instance already owned.
#  * LOW5: a str subclass instance can be swapped to another str subclass; an
#    instance-namespace entry named __class__ is not the object's class.
#  * LOW3: __getattr__ answers when a property raises AttributeError.
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix. Every class store below is a plain `Cls.name = value`.

SECTIONS = {}

SECTIONS['HIGH1_class_store_on_user_base'] = r"""
class P: pass
class R(P): pass
r = R(); r.x = 1
for _ in range(5): assert r.x == 1
R.__getattribute__ = lambda s, n: 'hooked:' + n
assert r.x == 'hooked:x', r.x
class P2: pass
class R2(P2): pass
r2 = R2(); log = []
for i in range(5): r2.z = i
R2.__setattr__ = lambda s, n, v: log.append((n, v))
r2.y = 5
assert log == [('y', 5)] and vars(r2) == {'z': 4}, (log, vars(r2))
class P4: pass
class R4(P4): pass
r4 = R4(); r4.k = 'own'
for _ in range(5): assert r4.k == 'own'
R4.k = property(lambda s: 'R4.k')
assert r4.k == 'R4.k' and getattr(r4, 'k') == 'R4.k', r4.k
# Three levels: the property goes on the middle class.
class A0: pass
class A1(A0): pass
class A2(A1): pass
a2 = A2(); a2.m = 'own'
for _ in range(5): assert a2.m == 'own'
A1.m = property(lambda s: 'A1.m')
assert a2.m == 'A1.m', a2.m
# Diamond: a property on one branch over a plain value on the root.
class D0: pass
class DQ(D0): pass
class DR(D0): pass
class DT(DQ, DR): pass
t = DT(); t.w = 'own'
DR.w = property(lambda s: 'DR.w')
D0.w = 1
assert t.w == 'DR.w' and getattr(t, 'w') == 'DR.w', t.w
# A plain value on the first branch shadows the property (as in CPython).
class X0: pass
class X1: pass
class XZ(X0, X1): pass
z = XZ(); z.k = 'own'
X1.k = property(lambda s: 'X1.k')
X0.k = 1
assert z.k == 'own', z.k
# f-string of a float-subclass instance after a late __str__.
class Base(float): pass
class V(Base): pass
v = V(1.25)
assert f'{v}' == '1.25'
V.__str__ = lambda s: 'custom'
assert (f'{v}', str(v), '%s' % v, format(v)) == ('custom',) * 4, (f'{v}', str(v))
"""

SECTIONS['LOW4_class_swap_descriptor'] = r"""
class C:
    @property
    def p(self): return 'prop'
    @p.setter
    def p(self, v): pass
class Plain: pass
o = Plain(); o.p = 'own'
for _ in range(5): assert o.p == 'own'
o.__class__ = C
for _ in range(3): assert o.p == 'prop', o.p
assert getattr(o, 'p') == 'prop'
assert vars(o) == {'p': 'own'}, vars(o)
o.__class__ = Plain
assert o.p == 'own', o.p
"""

SECTIONS['LOW5_class_swap_sources'] = r"""
class St1(str): pass
class St2(str): pass
s = St1('ab')
s.__class__ = St2
assert type(s) is St2 and s == 'ab', (type(s), s)
try:
    'ab'.__class__ = St2
except TypeError as e:
    assert str(e) == '__class__ assignment only supported for mutable types or ModuleType subclasses', str(e)
else:
    raise AssertionError('str instance re-classed')
class A: pass
class B: pass
a = A(); a.__dict__['__class__'] = list
assert type(a) is A and isinstance(a, A) and a.__dict__['__class__'] is list
a.__class__ = B
assert type(a) is B
"""

SECTIONS['LOW3_getattr_after_property_error'] = r"""
class G:
    @property
    def a(self): raise AttributeError('inner')
    def __getattr__(self, n): return 'fallback:' + n
g = G()
assert g.a == 'fallback:a' and getattr(g, 'a') == 'fallback:a' and hasattr(g, 'a')
for _ in range(5): assert g.a == 'fallback:a'
class H:
    @property
    def a(self): raise ValueError('v')
    def __getattr__(self, n): return 'fb'
try:
    H().a
except ValueError as e:
    assert str(e) == 'v'
else:
    raise AssertionError('ValueError swallowed')
class K:
    @property
    def a(self): raise AttributeError('inner')
try:
    K().a
except AttributeError as e:
    assert str(e) == 'inner', str(e)
else:
    raise AssertionError('no AttributeError')
assert not hasattr(K(), 'a')
class L:
    def __getattr__(self, n): raise AttributeError(n + '!')
    @property
    def a(self): raise AttributeError('inner')
try:
    L().a
except AttributeError as e:
    assert str(e) == 'a!', str(e)
assert not hasattr(L(), 'a')
"""

import sys

failed = []
for name, src in SECTIONS.items():
    try:
        exec(compile(src, f'<review_round14 {name}>', 'exec'), {'__name__': f'r14_{name}'})
    except BaseException as e:
        failed.append(name)
        print(f'FAIL {name}: {type(e).__name__}: {e}'[:600])
if failed:
    print('review_round14: FAILED', failed)
    sys.exit(1)
print('review_round14: ok')
