# Review round 10 of 1.2.2 (tasks/p8-round9-review-report.md, ruling Q20):
# f-string fields without a spec go through format(value, '') (H1), so a
# float is its repr and a subclass's __format__ is called (H1-b); the
# f-string field scanner of PEP 701 (M1); '%' formatting with __index__,
# __float__, Decimal and Fraction (M3); and the pre-existing data-corrupting
# bugs the round fixed: function __dict__, print() and write() of a NUL,
# math and arithmetic on ints past the float range, float overflow,
# __qualname__ and __module__ of type()-made classes (pickling), round(),
# and attribute keys made from Python strings (the escaping boundary).
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix.

SECTIONS = {}

SECTIONS['H1_spec_less_fields'] = r'''
vs = [0.1 + 0.2, 1e-7, 1e16, 1e22, 5e-324, -0.0, 2.5, 1 / 3, 1.7976931348623157e308]
for v in vs:
    assert f'{v}' == repr(v), (f'{v}', repr(v))
    assert f'{v!s}' == str(v) and f'{v!r}' == repr(v)
    assert f'{v:}' == format(v, '') == repr(v)
    assert '{}'.format(v) == repr(v) and object.__str__(v) == repr(v)
assert f'{1e22=}' == '1e22=1e+22', f'{1e22=}'
assert f'{float("inf")}' == 'inf' and f'{-float("nan")}' == 'nan'
assert f'{2 ** 100}' == str(2 ** 100) and f'{True}' == 'True' and f'{None}' == 'None'
'''

SECTIONS['H1b_subclass_format'] = r'''
class F(float):
    def __format__(self, spec): return 'F<' + spec + '>'
class I(int):
    def __format__(self, spec): return 'I<' + spec + '>'
class S(str):
    def __format__(self, spec): return 'S<' + spec + '>'
class B(int):
    def __format__(self, spec): return 'B'
for v, want in [(F(1.5), 'F<>'), (I(3), 'I<>'), (S('x'), 'S<>')]:
    assert f'{v}' == want, (f'{v}', want)
    assert format(v) == want and '{}'.format(v) == want
    assert f'{v:>3}' == want[:2] + '>3>'
assert f'{F(1.5)!r}' == '1.5' and f'{I(3)!s}' == '3'
class Bad:
    def __format__(self, spec): return 5
try:
    f'{Bad()}'
except TypeError as e:
    assert '__format__ must return a str, not int' in str(e), e
else:
    raise AssertionError('no TypeError')
# Shadowed names do not change what a field calls.
format = str = repr = None
assert f'{1.5}|{2!s}|{"a"!r}|{3:>2}' == "1.5|2|'a'| 3"
'''

SECTIONS['M1_fstring_scanner'] = r'''
x, y, s = 3, 4, 'ab'
assert f'{x != y}' == 'True' and f'{x!=y}' == 'True'
assert f'{x=}' == 'x=3' and f'{x = }' == 'x = 3' and f'{s=}' == "s='ab'"
assert f'{s=!s}' == 's=ab' and f'{x=:>4}' == 'x=   3' and f'{s=:>4}' == 's=  ab'
assert f'{x if x > y else y}' == '4' and f'{ x + y }' == '7'
assert f'{"a:b"}' == 'a:b' and f'{"{"}' == '{' and f"{'}'}" == '}' and f'{"a!r"}' == 'a!r'
assert f'{s:\x41>5}' == 'AAAab' and f'{s:\N{BULLET}>4}' == '\u2022\u2022ab'
assert f'{x:{"0"}{4}}' == '0003' and f'{3.14159:.{x}f}' == '3.142'
assert f'{ {"k": 1}["k"] }' == '1' and f'{[1, 2][x - 2]}' == '2'
assert f"""{
    x
    +
    y
}""" == '7'
assert rf'\{x}' == '\\3' and f'{{x}}' == '{x}'
for bad in ["f'{}'", "f'{x!z}'", "f'{x!}'", "f'{x'", "f'{x:{y:{x:{y}}}}}'"]:
    try:
        eval(bad)
    except SyntaxError:
        pass
    else:
        raise AssertionError(bad)
'''

SECTIONS['str_format_fields'] = r'''
class G:
    def __getitem__(self, k): return 'got-' + str(k)
assert '{0[a]}'.format(G()) == 'got-a' and '{0[1]}'.format(G()) == 'got-1'
assert '{!a}'.format('\xe9') == "'\\xe9'"
for spec, exc in [('{', ValueError), ('}', ValueError), ('{0!q}', ValueError), ('{0', ValueError)]:
    try:
        spec.format(1)
    except exc:
        pass
    else:
        raise AssertionError(spec)
try:
    format(1, 5)
except TypeError:
    pass
else:
    raise AssertionError('format(1, 5)')
'''

SECTIONS['M3_percent_formatting'] = r'''
import decimal, fractions
class Ix:
    def __index__(self): return 42
class Fl:
    def __float__(self): return 2.5
nan, inf = float('nan'), float('inf')
cases = [
    ('%d', Ix(), '42'), ('%x', Ix(), '2a'), ('%o', Ix(), '52'), ('%c', Ix(), '*'),
    ('%.2f', Fl(), '2.50'), ('%e', Fl(), '2.500000e+00'), ('%g', Ix(), '42'),
    ('%.2f', decimal.Decimal('2.25'), '2.25'), ('%d', decimal.Decimal(7), '7'),
    ('%.2f', fractions.Fraction(1, 3), '0.33'), ('%05f', nan, '00nan'), ('%05e', -inf, '-0inf'),
    ('%a', '\xe9', "'\\xe9'"), ('%c', 0x10FFFF, '\U0010ffff'), ('%5c', 'z', '    z'), ('%i', True, '1'),
]
for fmt, v, want in cases:
    got = fmt % v
    assert got == want, (fmt, got, want)
for fmt, v, exc in [('%c', 0x110000, OverflowError), ('%c', 'ab', TypeError), ('%x', 1.5, TypeError),
                    ('%d', 'a', TypeError), ('%q', 1, ValueError), ('%.2f', 'a', TypeError)]:
    try:
        fmt % v
    except exc:
        pass
    else:
        raise AssertionError((fmt, v))
'''

SECTIONS['round_ndigits'] = r'''
assert round(2.675, 2) == 2.67 and round(2.5, 0) == 2.0 and round(0.5) == 0 and round(1.5) == 2
assert round(1e300) == int(1e300) and round(-2.5) == -2 and round(1234.5678, -2) == 1200.0
assert round(5e-324, 400) == 5e-324 and round(1e300, -300) == 1e300
for v, exc in [(float('nan'), ValueError), (float('inf'), OverflowError)]:
    for conv in (int, lambda x: '%d' % x):
        try:
            conv(v)
        except exc:
            pass
        else:
            raise AssertionError((conv, v))
    try:
        round(v)
    except exc:
        pass
    else:
        raise AssertionError(v)
'''

SECTIONS['function_dict'] = r'''
import functools
def f(): pass
f.__dict__['a'] = 5
assert f.a == 5 and sorted(vars(f)) == ['a']
f.q = 1
assert sorted(f.__dict__) == ['a', 'q']
def g(): pass
g.marker = 'M'
@functools.wraps(g)
def w(): pass
assert w.marker == 'M' and w.__wrapped__ is g
f.__dict__ = {'n': 1}
assert f.n == 1 and not hasattr(f, 'a') and sorted(vars(f)) == ['n']
lam = lambda: 0
lam.k = 2
assert lam.__dict__ == {'k': 2}
'''

SECTIONS['write_nul'] = r'''
import sys, io
assert sys.stdout.write('') == 0
buf = io.StringIO()
print('a\0b', 'c', sep='\0', end='\0', file=buf)
assert buf.getvalue() == 'a\0b\0c\0'
import os, tempfile
fd, path = tempfile.mkstemp()
os.close(fd)
saved = os.dup(1)
try:
    out = os.open(path, os.O_WRONLY)
    sys.stdout.flush()
    os.dup2(out, 1)
    n = sys.stdout.write('x\0y')
    print('p\0q')
    sys.stdout.flush()
finally:
    os.dup2(saved, 1)
    os.close(out)
    os.close(saved)
with open(path, 'rb') as fh:
    data = fh.read()
os.unlink(path)
assert n == 3 and data == b'x\0yp\0q\n', (n, data)
'''

SECTIONS['math_big_ints'] = r'''
import math
B = 10 ** 400
assert abs(math.log(B) - 921.0340371976183) < 1e-9 and abs(math.log10(B) - 400.0) < 1e-12
assert abs(math.log2(2 ** 2000) - 2000.0) < 1e-9 and abs(math.log(B, 10) - 400.0) < 1e-9
assert math.floor(1e300) == int(1e300) and math.ceil(-1e300) == int(-1e300) and math.trunc(B) == B
assert math.floor(B) == B and math.comb(B, 2) == B * (B - 1) // 2 and math.perm(B, 1) == B
assert math.prod([B, B]) == B * B and math.prod(range(1, 6), start=2) == 240
assert math.sumprod([B, 1], [B, 1]) == B * B + 1
assert math.pow(2, 10) == 1024.0 and math.dist((B // B, 0), (0, 0)) == 1.0
for call, exc in [(lambda: math.sqrt(B), OverflowError), (lambda: math.exp(B), OverflowError),
                  (lambda: math.log(0), ValueError), (lambda: math.log(-B), ValueError),
                  (lambda: math.log(5, 1), ZeroDivisionError), (lambda: math.gamma(0), ValueError),
                  (lambda: math.gamma(200.5), OverflowError), (lambda: math.pow(-8, 1 / 3), ValueError),
                  (lambda: math.pow(10.0, 400), OverflowError), (lambda: math.pow(0.0, -1), ValueError),
                  (lambda: math.comb(-1, 2), ValueError), (lambda: math.factorial(-1), ValueError)]:
    try:
        call()
    except exc:
        pass
    else:
        raise AssertionError(call)
'''

SECTIONS['int_float_mixed'] = r'''
B = 10 ** 400
for call in [lambda: B * 1.0, lambda: 1.0 + B, lambda: B - 0.5, lambda: B % 1.5, lambda: B // 1.5,
             lambda: B / 1.5, lambda: 2.0 ** 2000, lambda: 10.0 ** 400, lambda: (1.5).__pow__(2000.0),
             lambda: pow(10.0, 400)]:
    try:
        call()
    except OverflowError:
        pass
    else:
        raise AssertionError(call)
assert B // B == 1 and (B * 3) // B == 3 and -B // B == -1 and B * 0 == 0 and (2 ** 1023) * 1.0 == 8.98846567431158e+307 and (2 ** 1000) // 2.0 ** 990 == 1024.0
assert type((-8) ** (1 / 3)) is complex and abs((-8) ** (1 / 3) - (1.0000000000000002 + 1.7320508075688772j)) < 1e-12
assert (2).__pow__(100) == 2 ** 100 and (3).__rpow__(2) == 8 and pow(2, 100, 10 ** 9) == 2 ** 100 % 10 ** 9
assert int.__pow__(2, 70) == 2 ** 70 and (2).__pow__(-1) == 0.5
assert 7.5 // 2 == 3.0 and -7.5 // 2 == -4.0 and (2 ** 100) % 3.5 == 2.0 and -(2 ** 100) % 3.5 == 1.5
try:
    0.0 ** -1
except ZeroDivisionError:
    pass
else:
    raise AssertionError('0.0 ** -1')
'''

SECTIONS['qualname_module_pickle'] = r'''
import pickle, collections, dataclasses, types, sys
mod = types.ModuleType('r10_pickle_mod')
sys.modules['r10_pickle_mod'] = mod
exec("""
C = type('C', (), {})
P = __import__('collections').namedtuple('P', 'x y')
D = __import__('dataclasses').make_dataclass('D', ['q'])
""", mod.__dict__)
C, P, D = mod.C, mod.P, mod.D
assert C.__module__ == 'r10_pickle_mod' and C.__qualname__ == 'C', (C.__module__, C.__qualname__)
assert P.__qualname__ == 'P' and D.__qualname__ == 'D'
c = C(); c.a = 1
d = pickle.loads(pickle.dumps(c))
assert type(d) is C and d.a == 1
assert pickle.loads(pickle.dumps(P(1, 2))) == P(1, 2) and pickle.loads(pickle.dumps(D(3))) == D(3)
E = type('E', (), {'__qualname__': 'Outer.E', '__module__': 'm2'})
assert E.__qualname__ == 'Outer.E' and E.__module__ == 'm2'
'''

SECTIONS['escaped_keys'] = r'''
import collections.abc as cabc, types
class A:
    def __len__(self): return 0
assert cabc._check_methods(A, '\x01__data__') is NotImplemented
assert cabc._check_methods(A, '\x01__keys__') is NotImplemented
class M:
    def m(self): pass
setattr(M.m, '\x01__keys__', 7)
assert getattr(M().m, '\x01__keys__') == 7
m = types.ModuleType('r10_m')
vars(m).update({'\x01__data__': 3})
assert getattr(m, '\x01__data__') == 3 and vars(m)['\x01__data__'] == 3
'''

SECTIONS['exec_dict_namespace'] = r'''
g = {'__name__': 'zz'}
exec("x = __name__\ndef f(): return __name__\ny = f()\nclass C: pass\nm = C.__module__", g)
assert (g['x'], g['y'], g['m']) == ('zz', 'zz', 'zz'), (g['x'], g['y'], g['m'])
assert eval('__name__', {'__name__': 'ev'}) == 'ev' and eval('__qualname__', {'__qualname__': 'Q'}) == 'Q'
try:
    exec('get', {})
except NameError:
    pass
else:
    raise AssertionError('dict.get read as a name')
d = {}
exec('get = 5\nkeys = get + 1', d)
assert d['get'] == 5 and d['keys'] == 6
'''

import sys

failed = []
for name, src in SECTIONS.items():
    try:
        exec(compile(src, f'<review_round10 {name}>', 'exec'), {'__name__': f'r10_{name}'})
    except BaseException as e:
        failed.append(name)
        print(f'FAIL {name}: {type(e).__name__}: {e}'[:600])
if failed:
    print('review_round10: FAILED', failed)
    sys.exit(1)
print('review_round10: ok')
