# Review round 13 of 1.2.2 (tasks/p8-round12-review-report.md):
#  * HIGH1: the descriptor protocol follows CPython's: a data descriptor is
#    one whose TYPE defines __set__ or __delete__; with __get__ it answers
#    reads before the instance __dict__, without __get__ the instance value
#    answers when there is one (else the descriptor itself). A __set__-only
#    "validate on write" descriptor reads back the stored value.
#  * Q20 (contained, pre-existing): a __get__ + __delete__ descriptor is a
#    data descriptor (it wins over the instance __dict__ and refuses writes);
#    a data descriptor added to a class (or a base) after an instance owns
#    that name answers for the instance.
#  * MEDIUM1: __class__ assignment follows object_set_class: TypeError for an
#    immutable (built-in) old or new class unless both are ModuleType
#    subclasses, and for incompatible layouts, with CPython's messages.
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix.

SECTIONS = {}

SECTIONS['HIGH1_set_only_descriptor'] = r"""
class Positive:
    def __set_name__(self, owner, name): self.name = name
    def __set__(self, inst, value):
        if value <= 0: raise ValueError(self.name)
        inst.__dict__[self.name] = value
class Order:
    qty = Positive()
    def __init__(self, q): self.qty = q
o = Order(3)
assert o.qty == 3, o.qty
o.qty = 7
assert o.qty == 7 and vars(o) == {'qty': 7}, (o.qty, vars(o))
try:
    o.qty = -1
except ValueError as e:
    assert str(e) == 'qty'
else:
    raise AssertionError('validation did not run')
assert o.qty == 7 and getattr(o, 'qty') == 7 and o.__dict__.get('qty') == 7
for i in range(1, 50):
    o.qty = i
    assert o.qty == i, (i, o.qty)
# The setter writes through vars() or __dict__.update().
class ViaVars(Positive):
    def __set__(self, inst, value): vars(inst)[self.name] = value
class ViaUpdate(Positive):
    def __set__(self, inst, value): inst.__dict__.update({self.name: value})
class O2:
    q = ViaVars()
    r = ViaUpdate()
o2 = O2(); o2.q = 4; o2.r = 5
assert (o2.q, o2.r) == (4, 5), (o2.q, o2.r)
# No instance value: the descriptor itself; del refuses (no __delete__).
o3 = O2()
assert o3.q is O2.__dict__['q'], o3.q
try:
    del o2.q
except AttributeError as e:
    assert str(e) == '__delete__', str(e)
else:
    raise AssertionError('del of a __set__-only descriptor succeeded')
# A __set__ stored on the descriptor INSTANCE does not make it a data
# descriptor: the instance __dict__ wins, and the write goes to the instance.
class G:
    def __get__(self, i, o=None): return 'DESCR'
g = G(); g.__set__ = lambda *a: None
class E:
    p = g
e = E(); e.__dict__['p'] = 'inst'
assert e.p == 'inst', e.p
e.p = 'w'
assert e.p == 'w' and vars(e) == {'p': 'w'}, (e.p, vars(e))
# The common __get__ + __set__ descriptor storing under its own name.
class Typed:
    def __set_name__(self, owner, name): self.name = name
    def __get__(self, inst, owner=None):
        return self if inst is None else inst.__dict__[self.name] * 10
    def __set__(self, inst, value): inst.__dict__[self.name] = value
class TT:
    t = Typed()
tt = TT(); tt.t = 2
assert tt.t == 20 and vars(tt) == {'t': 2}, (tt.t, vars(tt))
"""

SECTIONS['Q20_get_delete_descriptor'] = r"""
log = []
class GD:
    def __get__(self, i, o=None): return 'DESCR'
    def __delete__(self, i): log.append('deleted')
class C:
    p = GD()
c = C(); c.__dict__['p'] = 'shadow'
assert c.p == 'DESCR', c.p
assert c.__dict__['p'] == 'shadow'
try:
    c.p = 1
except AttributeError as e:
    assert str(e) == '__set__', str(e)
else:
    raise AssertionError('write to a __get__+__delete__ descriptor succeeded')
del c.p
assert log == ['deleted'], log
"""

SECTIONS['Q20_late_data_descriptor'] = r"""
def mk():
    class O: pass
    return O
for route in ('plain', 'type', 'setattr'):
    for warm in (0, 5):
        O = mk(); o = O(); o.a = 0
        for i in range(warm):
            o.a = i
            assert o.a == i
        p = property(lambda s: 'PROP', lambda s, v: None)
        if route == 'plain': O.a = p
        elif route == 'type': type.__setattr__(O, 'a', p)
        else: setattr(O, 'a', p)
        assert o.a == 'PROP' and getattr(o, 'a') == 'PROP', (route, warm, o.a)
        o.a = 5
        assert o.a == 'PROP', (route, warm, o.a)
        assert o.__dict__ == {'a': max(warm - 1, 0)}, (route, warm, o.__dict__)
        # Removing the property gives the instance value back.
        del O.a
        assert o.a == max(warm - 1, 0), (route, warm, o.a)
# A property added later to a base class; a hot loop read before.
class Base0: pass
class Der(Base0): pass
x = Der(); x.__dict__['q'] = 'inst'
for _ in range(20): assert x.q == 'inst'
Base0.q = property(lambda s: 'PROP')
assert x.q == 'PROP', x.q
y = Der(); y.q2 = 1
class Late:
    q2 = property(lambda s: 'LATE')
Der.__bases__ = (Late,)
assert y.q2 == 'LATE', y.q2
# A non-data descriptor added later does not win over the instance.
class N: pass
n = N(); n.f = 'inst'
N.f = lambda self: 'method'
assert n.f == 'inst', n.f
# Instances of a class with a property defined in its body are unaffected.
class P:
    a = property(lambda s: 'PROP')
pp = P(); pp.__dict__['a'] = 1
assert pp.a == 'PROP', pp.a
"""

SECTIONS['MEDIUM1_class_assignment'] = r"""
import sys, types
class L:
    def __init__(self): self.v = 1
class LL(list): pass
class LL2(list): pass
class D(dict): pass
class M(types.ModuleType): pass
class M2(types.ModuleType): pass
class S1: __slots__ = ('a',)
class S2: __slots__ = ('b', 'c')
class E(Exception): pass
class E2(Exception): pass
class EV(ValueError): pass
class I(int): pass
class I2(int): pass
IMM = '__class__ assignment only supported for mutable types or ModuleType subclasses'
def check(obj, cls, msg):
    try:
        obj.__class__ = cls
    except TypeError as e:
        assert msg is not None and str(e) == msg, (cls, str(e))
    else:
        assert msg is None, (cls, 'accepted')
        assert type(obj) is cls
for tgt in (int, float, str, bytes, tuple, frozenset, bool, complex, range, type(None),
            type(len), type(sys), list, dict, set, object, type, Exception, property):
    l = L()
    check(l, tgt, IMM)
    assert type(l) is L and l.v == 1
check(LL(), LL2, None)
check(LL(), D, "__class__ assignment: 'D' object layout differs from 'LL'")
check(LL(), L, "__class__ assignment: 'L' object layout differs from 'LL'")
check(L(), LL, "__class__ assignment: 'LL' object layout differs from 'L'")
check(LL(), list, IMM)
check([], LL, IMM)
check(M('m'), M2, None)
check(types.ModuleType('x'), M, None)
check(M('m'), type(sys), None)
check(S1(), S2, "__class__ assignment: 'S2' object layout differs from 'S1'")
check(L(), S1, "__class__ assignment: 'S1' object layout differs from 'L'")
check(1, L, IMM)
check(object(), L, IMM)
check(L(), 3, "__class__ must be set to a class, not 'int' object")
check(E(), E2, None)
check(E(), EV, "__class__ assignment: 'EV' object layout differs from 'E'")
check(E(), L, "__class__ assignment: 'L' object layout differs from 'E'")
check(I(3), I2, None)
# A user class swap keeps working (state pattern).
class A:
    def who(self): return 'A'
class B:
    def who(self): return 'B'
a = A(); a.__class__ = B
assert a.who() == 'B' and isinstance(a, B)
a.__class__ = A
assert a.who() == 'A'
"""

import sys

failed = []
for name, src in SECTIONS.items():
    try:
        exec(compile(src, f'<review_round13 {name}>', 'exec'), {'__name__': f'r13_{name}'})
    except BaseException as e:
        failed.append(name)
        print(f'FAIL {name}: {type(e).__name__}: {e}'[:600])
if failed:
    print('review_round13: FAILED', failed)
    sys.exit(1)
print('review_round13: ok')
