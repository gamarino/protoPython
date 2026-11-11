# Review round 9 of 1.2.2 (tasks/p8-round8-review-report.md): one formatter
# for int, float and str presentation types (H1); formatted results are not
# interned (M1); the escaping boundary (M2); PEP 688 exports released through
# the exporter (M3); built-in methods known per type (LOW); a module's own
# ModuleNotFoundError; exact int true division, complex power and str
# precision (ruling Q18); nested replacement fields in a format spec; dict
# charmaps encoded natively.
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix.

SECTIONS = {}

SECTIONS['H1_float_presentation'] = r'''
cases = [
    (1234, ',.2f', '1,234.00'), (1234, ',g', '1,234'), (1234, ',.0%', '123,400%'),
    (1, '_.3f', '1.000'), (1, '#.0f', '1.'), (1, 'z.2f', '1.00'),
    (1234.5, ',.2f', '1,234.50'), (-0.0, 'z.1f', '0.0'), (-1e-9, 'z.3f', '0.000'),
    (1234567.891, '_.2f', '1_234_567.89'), (1234567.891, ',.3e', '1.235e+06'),
    (2 ** 70, ',.3g', '1.18e+21'), (-(2 ** 64), ',', '-18,446,744,073,709,551,616'),
    (10 ** 30, '.2e', '1.00e+30'), (255, '#_b', '0b1111_1111'), (255, '#010x', '0x000000ff'),
    (1234.5678, '.3f', '1234.568'), (1e16, ',', '1e+16'), (12345.678, 'n', '12345.7'),
    (0.1, '.17g', '0.10000000000000001'), (1.5, '=+10.2f', '+     1.50'), (1.5, '*^9.1f', '***1.5***'),
    (float('inf'), ',.2f', 'inf'), (float('nan'), '+08.2f', '+0000nan'), (-5, '010,', '-0,000,005'),
    (1234.5, ',.2%', '123,450.00%'), (1234.5, '.2f', '1234.50'), (True, '.1f', '1.0'), (True, '', 'True'),
    (5e-324, '.3g', '4.94e-324'), (1.7976931348623157e308, ',.0f', format(1.7976931348623157e308, ',.0f')),
    (1234.5, '.3,f', '1234.500'), (1234.56789, '.6_f', '1234.567_890'),
]
bad = []
for v, spec, want in cases:
    for got in (format(v, spec), ('{:' + spec + '}').format(v), eval("f'{v:" + spec + "}'"), type(v).__format__(v, spec)):
        if got != want:
            bad.append((v, spec, got, want))
assert not bad, bad
for v, spec, exc in [(1.5, '.2fq', ValueError), (1, '.2d', ValueError), (1, 'z d', ValueError),
                     (2 ** 63, 'c', OverflowError), (-1, 'c', OverflowError), (1.5, ',_f', ValueError),
                     (10 ** 400, 'f', OverflowError)]:
    try:
        format(v, spec)
    except exc:
        pass
    else:
        raise AssertionError((v, spec))
try:
    format(-2 ** 63, 'c')
except OverflowError as e:
    assert str(e) == '%c arg not in range(0x110000)', e
# The % operator's float conversions: any precision, no truncation.
assert '%.3f|%10.2e|%-8.3g|%+.0f' % (1.0005, 12345.678, 0.0001234, 2.5) == '1.000|  1.23e+04|0.000123|+2'
assert len('%.200f' % 1.0) == 202 and '%#.0f' % 3.0 == '3.' and '%.0e' % 15.0 == '2e+01'
assert repr(1.1) == '1.1' and repr(1e16) == '1e+16' and repr(-0.0) == '-0.0' and str(2.5e-5) == '2.5e-05'
'''

SECTIONS['M1_results_not_interned'] = r'''
# Each call builds a new str, as CPython does; an interned result would be
# kept for the life of the process.
x = 12345678.5
i = 1234567890123
calls = {
    'format(float)': lambda: format(x, '.4f'),
    'float.__format__': lambda: x.__format__('.4f'),
    'format(int, f)': lambda: format(i, '.4f'),
    'f-string': lambda: f'{x:.4f}',
    'str.format': lambda: '{:.4f}'.format(x),
    'percent': lambda: '%.4f' % x,
    'float.hex': lambda: x.hex(),
    'repr': lambda: repr(x * 1.000001),
    'format(int, x)': lambda: format(i, '020x'),
    'format(str)': lambda: format('abcdefgh', '>20'),
}
same = [name for name, f in calls.items() if f() is f()]
assert not same, same
'''

SECTIONS['M2_escape_boundary'] = r'''
KD = '\x01__data__'; KK = '\x01__keys__'; KF = '\x01__pyflags__'
class L(list): pass
class D(dict): pass
l = L([1, 2])
for fmt, arg in (('{0.' + KD + '}', l), ('{0.' + KF + '}', 5), ('{0.' + KD + '}', 'abc'),
                 ('{0.__class__.' + KD + '}', l)):
    try:
        fmt.format(arg)
    except AttributeError:
        pass
    else:
        raise AssertionError(fmt)
try:
    ('{a.' + KK + '}').format_map({'a': l})
except AttributeError:
    pass
else:
    raise AssertionError('format_map')
assert (L.__dict__.get(KF), L.__dict__.get(KD), dict.__dict__.get(KD, 'NO')) == (None, None, 'NO')
assert KF not in L.__dict__ and not [k for k in L.__dict__ if k.startswith('\x01')]
# An attribute of a dict subclass instance is not an item.
d = D(k=1); setattr(d, '\x01q', 5); setattr(d, 'zz', 5)
assert dict(d) == {'k': 1} and len(d) == 1 and list(d) == ['k'] and '\x01q' not in d, dict(d)
assert getattr(d, '\x01q') == 5 and d.zz == 5
d3 = D(k=1); d3.__dict__['\x01z'] = 5
assert dict(d3) == {'k': 1}
# __prepare__'s items, internal-looking names included, become the class's.
class M(type):
    @classmethod
    def __prepare__(m, n, b): return {KK: 5, '\x01z': 6}
class C(metaclass=M):
    z = 1
    def meth(self): return 'm'
assert sorted(k for k in vars(C) if not k.startswith('__')) == [KK, '\x01z', 'meth', 'z']
assert (C().z, C().meth(), getattr(C, KK)) == (1, 'm', 5)
class M2(type):
    @classmethod
    def __prepare__(m, n, b): return {KD: 5}
class C2(metaclass=M2):
    def meth(self): return 'm'
assert C2().meth() == 'm' and C2.__name__ == 'C2' and getattr(C2, KD) == 5
'''

SECTIONS['M3_buffer_release'] = r'''
log = []
class Buf:
    def __init__(self): self.ba = bytearray(b'hello')
    def __buffer__(self, flags):
        log.append('get'); return self.ba.__buffer__(flags)
    def __release_buffer__(self, view):
        log.append('rel'); self.ba.__release_buffer__(view)
def resizable(ba):
    try:
        ba.append(1); ba.pop(); return True
    except BufferError:
        return False
b = Buf()
m = memoryview(b)
assert bytes(m) == b'hello' and not resizable(b.ba) and log == ['get']
m.release()
assert resizable(b.ba) and log == ['get', 'rel'], log
m.release()
assert log == ['get', 'rel'], log
with memoryview(b) as m2:
    assert not resizable(b.ba)
assert resizable(b.ba) and log == ['get', 'rel', 'get', 'rel'], log
# __release_buffer__ checks its argument.
ba = bytearray(b'x')
for arg, exc in ((b'x', TypeError), (memoryview(bytearray(b'y')), ValueError)):
    try:
        ba.__release_buffer__(arg)
    except exc:
        pass
    else:
        raise AssertionError(arg)
v = ba.__buffer__(0); v.release()
try:
    ba.__release_buffer__(v)
except ValueError:
    pass
else:
    raise AssertionError('released view accepted')
'''

SECTIONS['LOW_descriptor_identity'] = r'''
def type_error(f):
    try:
        f()
    except TypeError as e:
        return str(e)
    raise AssertionError('no TypeError')
# A function several built-in types share answers as the type it was taken from.
assert type_error(lambda: float.__eq__(5, 5)) == "descriptor '__eq__' requires a 'float' object but received a 'int'"
type_error(lambda: float.__lt__(1, 2)); type_error(lambda: int.__eq__(5.0, 5.0))
assert 'frozenset' in type_error(lambda: frozenset.union({1}, {2}))
type_error(lambda: set.union(frozenset({1}), {2}))
assert "'str'" in type_error(lambda: str.__iter__([1, 2]))
type_error(lambda: list.__iter__('ab'))
assert '__ceil__' in type_error(lambda: int.__ceil__(object()))
assert '__repr__' in type_error(lambda: list.__repr__(object()))
assert 'conjugate' in type_error(lambda: float.conjugate(1))
assert repr(float.__eq__) == "<slot wrapper '__eq__' of 'float' objects>"
assert repr(frozenset.union) == "<method 'union' of 'frozenset' objects>"
# The inherited ones check their receiver too.
for T in (bytes, bytearray, tuple, list, dict):
    assert type_error(lambda: T.__ne__(object(), 1)) == \
        f"descriptor '__ne__' requires a '{T.__name__}' object but received a 'object'"
    assert repr(T.__ne__) == f"<slot wrapper '__ne__' of '{T.__name__}' objects>"
for T in (bytes, bytearray, tuple, list, dict, set, frozenset, memoryview):
    assert type_error(lambda: T.__format__(object())) == 'object.__format__() takes exactly one argument (0 given)'
assert type_error(lambda: list.__getitem__(object(), 0)) == \
    "descriptor '__getitem__' for 'list' objects doesn't apply to a 'object' object"
assert type_error(lambda: float.__getformat__(object())) == '__getformat__() argument must be str, not object'
assert float.__getformat__('double') == 'IEEE, little-endian'
# Ordinary use is unchanged.
assert 1.0 == 1 and 2.5 != 2 and frozenset({1}).union({2}) == {1, 2} and set.union({1}, {2}) == {1, 2}
assert list(str.__iter__('ab')) == ['a', 'b'] and b'a' != b'b' and not ((1,) != (1,)) and [1] != [2]
assert format(b'a', '') == "b'a'" and float.__eq__(1.5, 1.5) and frozenset.union(frozenset({1}), {2}) == {1, 2}
class F(frozenset): pass
assert frozenset.union(F({1}), {2}) == {1, 2} and bytes.__ne__(b'a', b'b') is True
'''

SECTIONS['LOW_module_own_mnfe'] = r'''
import os, sys, tempfile, importlib
d = tempfile.mkdtemp()
with open(os.path.join(d, 'r9_mnfe.py'), 'w') as f:
    f.write('raise ModuleNotFoundError("custom", name="r9_mnfe")\n')
sys.path.insert(0, d)
for imp in (lambda: importlib.import_module('r9_mnfe'), lambda: __import__('r9_mnfe')):
    try:
        imp()
    except ModuleNotFoundError as e:
        assert e.args == ('custom',) and e.name == 'r9_mnfe', (e.args, e.name)
    else:
        raise AssertionError('imported')
try:
    import r9_no_such_module
except ModuleNotFoundError as e:
    assert e.args == ("No module named 'r9_no_such_module'",) and e.name == 'r9_no_such_module'
'''

SECTIONS['Q18_numbers'] = r'''
assert True.__hash__() == 1 and hash(True) == 1 and int.__repr__(True) == '1' and int.__str__(True) == 'True'
assert int.bit_length(True) == 1 and int.to_bytes(True, 1, 'big') == b'\x01'
# int / int is correctly rounded at any size (CPython's long_true_divide).
assert (2 ** 1100) / (2 ** 1000) == 1.2676506002282294e+30
assert (10 ** 400) / (10 ** 399) == 10.0
assert (2 ** 53 + 1) / 1 == 9007199254740992.0
assert -(10 ** 30) / 7 == -1.4285714285714285e+29
assert (3 ** 200) / (7 ** 150) == 4.566094987608383e-32
for f, msg in ((lambda: (10 ** 400) / 3, 'integer division result too large for a float'),
               (lambda: (10 ** 400) / 2.0, 'int too large to convert to float')):
    try:
        f()
    except OverflowError as e:
        assert str(e) == msg, e
    else:
        raise AssertionError(msg)
# Complex power as CPython computes it.
assert (1+2j) ** 2 == (-3+4j) and (1+2j) ** -2 == (-0.12-0.16j)
assert (1+2j) ** 0.5 == (1.272019649514069+0.7861513777574233j)
assert (1+2j) ** (1+1j) == (-0.24720004426291722+0.6964504870825432j)
assert 2 ** 1j == (0.7692389013639721+0.6389612763136348j) and 0j ** 0 == (1+0j)
assert complex.__rsub__(1+2j, 1j) == (-1-1j)
for f, exc, msg in ((lambda: (1e200+1j) ** 2, OverflowError, 'complex exponentiation'),
                    (lambda: 0j ** -1, ZeroDivisionError, 'zero to a negative or complex power'),
                    (lambda: pow(1+2j, 2, 3), ValueError, 'complex modulo')):
    try:
        f()
    except exc as e:
        assert str(e) == msg, e
    else:
        raise AssertionError(msg)
# str precision keeps that many characters, in format() and in %.
s = 'abcd'
assert (format(s, '.3'), '{:.3}'.format(s), f'{s:>8.2}', format('h\xe9llo', '.2')) == ('abc', 'abc', '      ab', 'h\xe9')
assert ('%.3s' % 'abcdef', '%.3r' % s, '%5.2s|' % 'xyz', '%-6.3r|' % s) == ('abc', "'ab", '   xy|', "'ab   |")
assert ('%4s|%-4s|' % ('\xe9', '\xf1'), '%.0s|' % 'x', '%*.*s|' % (6, 2, 'hello')) == ('   \xe9|\xf1   |', '|', '    he|')
'''

SECTIONS['nested_format_spec'] = r'''
# A spec with replacement fields is evaluated, in f-strings and str.format.
w = 8; spec = '>8'; v = 5
assert (f'{v:{w}}', f'{v:>{w}}', f'{v:{spec}}', f'{3.14159:{w}.{2}f}', f'{v!r:{w}}') == \
    ('       5', '       5', '       5', '    3.14', '5       ')
def scoped():
    spec = '>6'
    for v in (1,):
        return f'{v:{spec}}|{v:{"^"}{len(spec) + 1}}|'
assert scoped() == '     1| 1 |'
def outer():
    width = 7
    def inner(x): return f'{x:{width}}'
    return inner(3)
assert outer() == '      3'
assert [f'{i:{n}}' for n in (2, 3) for i in (1,)] == [' 1', '  1']
def gen():
    yield f'{1:{(yield 4)}}'
it = gen(); assert next(it) == 4 and it.send('>3') == '  1'
assert '{:{w}}|{:{}.{}f}'.format(5, 2.5, 6, 1, w=4) == '   5|   2.5'
assert str.format('{:{}}|{}', 'ab', 5, 'z') == 'ab   |z'
assert ('{0:{1}}'.format(3, '>4'), '{a:{b}}'.format_map({'a': 1, 'b': '^5'})) == ('   3', '  1  ')
assert '{x:{y[0]}{z.real}}'.format(x=7, y=['>'], z=5) == '    7'
for f, exc in ((lambda: '{0:{}}'.format(3, 4), ValueError), (lambda: '{:{:{}}}'.format(1, 2, 3), ValueError)):
    try:
        f()
    except exc:
        pass
    else:
        raise AssertionError('no error')
try:
    compile("f'{1:{2:{3:{4}}}}'", 'x', 'eval')
except SyntaxError:
    pass
else:
    raise AssertionError('nesting limit')
'''

SECTIONS['codecs_dict_charmap'] = r'''
import codecs, time
def best(f):
    r = []
    for _ in range(3):
        t = time.perf_counter(); f(); r.append(time.perf_counter() - t)
    return min(r)
def pyloop(src):
    n = 0
    for c in src:
        n += 1
    return n
s = 'h\xe9llo w\xf6rld ─│ ' * 6000
for enc in ('cp437', 'cp850'):
    src = s.encode(enc, 'replace').decode(enc)
    assert src.encode(enc).decode(enc) == src
    # Natively: a fraction of one Python-level pass over the same text
    # (a Python loop per character costs tens of passes).
    ratio = best(lambda: src.encode(enc)) / best(lambda: pyloop(src))
    assert ratio < 3, (enc, ratio)
assert 'a€b'.encode('cp437', 'replace') == b'a?b'
assert 'a€€b'.encode('cp437', 'xmlcharrefreplace') == b'a&#8364;&#8364;b'
try:
    'a€b'.encode('cp850')
except UnicodeEncodeError as e:
    assert (e.start, e.end, e.reason) == (1, 2, 'character maps to <undefined>')
m = codecs.make_encoding_map({0x41: 0x61, 0x42: 0x62})
assert codecs.charmap_encode('ab', 'strict', {ord('a'): 0x41, ord('b'): b'BB'}) == (b'ABB', 2)
'''

import sys

failed = []
for name, src in SECTIONS.items():
    try:
        exec(compile(src, f'<review_round9 {name}>', 'exec'), {'__name__': f'r9_{name}'})
    except BaseException as e:
        failed.append(name)
        print(f'FAIL {name}: {type(e).__name__}: {e}'[:600])
if failed:
    print('review_round9: FAILED', failed)
    sys.exit(1)
print('review_round9: ok')
