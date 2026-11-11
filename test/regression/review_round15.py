# Review round 15 of 1.2.2 (tasks/p8-round13-review-report.md, "Round 14
# re-review"):
#  * R14_2: a plain store on a class whose first protoCore parent is not a
#    base it registered with (a metaclass mro() puts another class first)
#    takes the class store protocol, not the instance fast store.
#  * R14_1: the same store is decided without any cached class flag.  The
#    race the review reproduced (a base's flags computed before a subclass
#    existed and published after it registered) left exactly the state the
#    R14_2 section builds deterministically: a class receiver whose first
#    parent's cached flags do not know it.  The threaded section replays the
#    review's t3b.py; its assertion does not depend on timing (every trial
#    must call the hook), only its power to catch a regression does.
#  * Q22: an unannotated class's __annotations__ is an empty dict (it held
#    dict's attribute names), so @dataclass works on it.
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix. Every class store below is a plain `Cls.name = value`.

SECTIONS = {}

SECTIONS['R14_2_custom_mro_first_parent'] = r"""
class P: pass
p = P(); p.a = 1; p.a          # P's flags are cached
class X: pass
class MM(type):
    def mro(cls):
        return [cls, X, P, object] if cls.__name__ == 'R' else type.mro(cls)
R = MM('R', (P,), {})
assert R.__mro__ == (R, X, P, object), R.__mro__
r = R(); r.k = 'own'; r.z = 0
for _ in range(5): assert r.k == 'own'
log = []
R.__setattr__ = lambda s, n, v: log.append((n, v))
r.y = 5
assert log == [('y', 5)] and 'y' not in vars(r), (log, vars(r))
del R.__setattr__
R.k = property(lambda s: 'prop')
assert r.k == 'prop' and getattr(r, 'k') == 'prop', r.k
R.__getattribute__ = lambda s, n: 'hooked' if n == 'z' else object.__getattribute__(s, n)
assert r.z == 'hooked', r.z
del R.__getattribute__
assert r.z == 0
# The same through X, a class with subclasses of its own.
class X2: pass
class X2a(X2): pass
class MM2(type):
    def mro(cls):
        return [cls, X2, object] if cls.__name__ == 'S' else type.mro(cls)
S = MM2('S', (), {})
s = S(); s.v = 1
for _ in range(5): assert s.v == 1
S.v = property(lambda self: 'Sprop')
assert s.v == 'Sprop', s.v
"""

SECTIONS['R14_1_threaded_subclass_creation'] = r"""
import threading
bad = []
for trial in range(300):
    P = type('P%d' % trial, (object,), {})
    p = P()
    go = threading.Event()
    def a():
        go.wait()
        for i in range(30): p.a = i
    t = threading.Thread(target=a); t.start()
    go.set()
    R = type('R%d' % trial, (P,), {})
    t.join()
    log = []
    r = R(); r.y0 = 0
    R.__setattr__ = lambda s, n, v: log.append(n)
    r.y = 1
    if log != ['y'] or 'y' in vars(r):
        bad.append((trial, log, vars(r)))
assert not bad, bad[:3]
"""

SECTIONS['Q22_unannotated_class_annotations'] = r"""
import dataclasses, types, typing, annotationlib
class E: pass
class F:
    x: int
class G(F): pass
for c in (E, G, type('N', (), {}), types.new_class('M')):
    a = c.__annotations__
    assert type(a) is dict and a == {} and len(a) == 0 and list(a) == [], (c, a)
    assert dict(a) == {} and 'pop' not in a and a.get('pop') is None, (c, a)
    assert annotationlib.get_annotations(c) == {}, c
assert F.__annotations__ == {'x': int}
E.__annotations__['q'] = 1
assert E.__annotations__ == {'q': 1}, E.__annotations__
assert typing.get_type_hints(G) == {'x': int}, typing.get_type_hints(G)
@dataclasses.dataclass
class D: pass
assert repr(D()) == 'D()' and dataclasses.fields(D) == () and D() == D()
@dataclasses.dataclass
class D2(D):
    y: int = 2
assert repr(D2()) == 'D2(y=2)', repr(D2())
@dataclasses.dataclass(frozen=True)
class D3:
    def m(self): return 1
assert D3().m() == 1 and hash(D3()) == hash(D3())
"""

import sys

failed = []
for name, src in SECTIONS.items():
    try:
        exec(compile(src, f'<review_round15 {name}>', 'exec'), {'__name__': f'r15_{name}'})
    except BaseException as e:
        failed.append(name)
        print(f'FAIL {name}: {type(e).__name__}: {e}'[:600])
if failed:
    print('review_round15: FAILED', failed)
    sys.exit(1)
print('review_round15: ok')
