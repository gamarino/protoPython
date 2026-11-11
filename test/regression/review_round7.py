# Review round 7 of 1.2.2 (tasks/p8-round6-review-report.md): internal state
# Python code cannot reach (H1), consistent buffer snapshots (H2), bytes-like
# arguments (M1), codecs and failing imports (M2), memoryview exports (M3),
# dir() and f_locals over a mapping (M5), and the open items: alternate
# constructors return the subclass, dir() of built-in types, native module
# attribute assignment.
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix.

SECTIONS = {}

SECTIONS['H1_internal_names_unreachable'] = r'''
# Names the runtime once used for its own state, and the same names with the
# U+0001 prefix internal keys now carry: setting them from Python neither
# crashes nor redirects anything, and they behave as ordinary names.
names = ['__data__', '__keys__', '__bytearray__', '__mv_data__', '__locals_mapping__',
         '__is_python_class__', '__pyflags__',
         '\x01__data__', '\x01__keys__', '\x01__bytearray__', '\x01__mv_data__',
         '\x01__is_python_class__', '\x01__executed__', '\x01__locals_mapping__']
class C:
    pass
for n in names:
    l = [1, 2]; d = {1: 2}; ba = bytearray(b'ab'); c = C()
    for obj in (l, d, ba):
        try:
            setattr(obj, n, 'zz')
        except AttributeError:
            pass
        else:
            raise AssertionError('%r accepted %r' % (type(obj).__name__, n))
    setattr(c, n, 'zz')
    assert getattr(c, n) == 'zz' and n in dir(c) and vars(c)[n] == 'zz', n
    c2 = C(); c2.__dict__[n] = 5
    assert getattr(c2, n) == 5 and n in vars(c2), n
    t = type('T', (), {n: 7})
    assert getattr(t, n) == 7 and n in dir(t) and t.__dict__[n] == 7, n
    g = {n: 9}
    exec('x = 1', g)
    assert g['x'] == 1 and g[n] == 9, n
    assert l == [1, 2] and len(l) == 2 and d == {1: 2} and list(d) == [1], n
    assert ba == bytearray(b'ab') and bytes(ba) == b'ab' and len(ba) == 2, n
    delattr(c, n)
    assert not hasattr(c, n), n

# Spoofed bytes-like objects: an attribute never makes an object a bytearray
# or a memoryview.
class X:
    pass
x = X(); x.__bytearray__ = [1, 2, 3]; x.__mv_data__ = b'abc'
for f in (lambda: bytearray(x), lambda: bytes(x), lambda: str(x, 'ascii'),
          lambda: bytearray.append(x, 1), lambda: b''.join([x])):
    try:
        f()
    except TypeError:
        pass
    else:
        raise AssertionError('spoofed bytes-like object accepted')
assert (x == bytearray()) is False

# A module-level name once used for the exec mapping frame is an ordinary global.
g = {}
exec('__locals_mapping__ = {}\na = 1', g)
assert g['a'] == 1 and g['__locals_mapping__'] == {}

# Python hooks receive the name as Python code spelled it.
seen = []
class G:
    def __getattr__(self, n):
        return ('ga', n)
    def __setattr__(self, n, v):
        seen.append(n); object.__setattr__(self, n, v)
gg = G()
assert getattr(gg, '\x01__data__') == ('ga', '\x01__data__')
setattr(gg, '\x01__data__', 3)
assert seen == ['\x01__data__'] and getattr(gg, '\x01__data__') == 3
assert vars(gg) == {'\x01__data__': 3}
class H:
    def __getattribute__(self, n):
        return ('gab', n)
assert getattr(H(), '\x01k') == ('gab', '\x01k')

# Module namespaces: an attribute, globals() and vars() agree.
import types
m = types.ModuleType('m')
setattr(m, '\x01__keys__', 5); m.a = 1
assert getattr(m, '\x01__keys__') == 5 and '\x01__keys__' in dir(m) and vars(m)['\x01__keys__'] == 5
gl = {}
exec("g = globals(); g['\x01__data__'] = 7; v = g['\x01__data__']; del g['\x01__data__']; w = '\x01__data__' in g", gl)
assert gl['v'] == 7 and gl['w'] is False

# A module's dict copy lists the module's own names, not the module type's.
c = m.__dict__.copy()
assert 'a' in c and '__new__' not in c and 'keys' not in c, sorted(c)
'''

SECTIONS['H2_bytes_snapshot'] = r'''
import threading, hashlib, time
# bytes(bytearray) copies one version of the content, at once.
bb = bytearray(b'A' * 700)
stop = False
def writer():
    i = 0
    while not stop:
        bb[:] = (b'A' if i % 2 else b'B') * 700
        i += 1
torn = []
def reader():
    for _ in range(300):
        s = bytes(bb)
        if s.count(s[:1]) != len(s):
            torn.append(s)
w = threading.Thread(target=writer); w.start()
rs = [threading.Thread(target=reader) for _ in range(3)]
for r in rs: r.start()
for r in rs: r.join()
stop = True; w.join()
assert not torn, len(torn)
# Linear: 1 MB in well under a second (it took 1.23 s at 1 us per byte).
big = bytearray(b'x' * (1 << 20))
t0 = time.perf_counter()
assert bytes(big) == b'x' * (1 << 20)
assert hashlib.sha256(big).hexdigest() == hashlib.sha256(b'x' * (1 << 20)).hexdigest()
assert bytes(memoryview(big)[:3]) == b'xxx'
assert time.perf_counter() - t0 < 1.0
'''

SECTIONS['M1_bytes_like_arguments'] = r'''
table = bytes.maketrans(b'a', b'z')
for t in (table, bytearray(table), memoryview(table)):
    assert b'ab'.translate(t) == b'zb'
    assert bytearray(b'ab').translate(t) == bytearray(b'zb')
for fill in (b'*', bytearray(b'*')):
    assert b'abc'.center(7, fill) == b'**abc**'
    assert b'abc'.ljust(5, fill) == b'abc**'
    assert b'abc'.rjust(5, fill) == b'**abc'
    assert bytearray(b'abc').center(7, fill) == bytearray(b'**abc**')
'''

SECTIONS['M2_codecs'] = r'''
import codecs
assert b'\xed\xa0\x80'.decode('utf-8', 'surrogatepass') == '\ud800', ascii(b'\xed\xa0\x80'.decode('utf-8', 'surrogatepass'))
assert '\ud800'.encode('utf-8', 'surrogatepass') == b'\xed\xa0\x80', ascii('\ud800'.encode('utf-8', 'surrogatepass'))
assert 'h\xe9\U0001f600'.encode('utf-16') == b'\xff\xfeh\x00\xe9\x00=\xd8\x00\xde', ascii('h\xe9\U0001f600'.encode('utf-16'))
assert b'\xff\xfeh\x00\xe9\x00=\xd8\x00\xde'.decode('utf-16') == 'h\xe9\U0001f600', ascii(b'\xff\xfeh\x00\xe9\x00=\xd8\x00\xde'.decode('utf-16'))
assert 'ab'.encode('utf-16-be') == b'\x00a\x00b' and 'ab'.encode('utf-16-le') == b'a\x00b\x00', "'ab'.encode('utf-16-be') == b'\\x00a\\x00b"
assert 'a'.encode('utf-32') == b'\xff\xfe\x00\x00a\x00\x00\x00', ascii('a'.encode('utf-32'))
assert b'\x00\x00\x00a'.decode('utf-32-be') == 'a', ascii(b'\x00\x00\x00a'.decode('utf-32-be'))
assert 'A+\u20ac'.encode('utf-7') == b'A+-+IKw-' and b'A+-+IKw-'.decode('utf-7') == 'A+\u20ac', "'A+\u20ac'.encode('utf-7') == b'A+-+IKw-' and"
try:
    b'\x00\xd8a\x00'.decode('utf-16-le')
except UnicodeDecodeError as e:
    assert (e.start, e.end, e.reason) == (0, 2, 'illegal UTF-16 surrogate'), (e.start, e.end, e.reason)
else:
    raise AssertionError('lone surrogate decoded')
assert b'\x00\xd8a\x00'.decode('utf-16-le', 'replace') == '\ufffda', ascii(b'\x00\xd8a\x00'.decode('utf-16-le', 'replace'))
assert b'\x00\xd8a\x00'.decode('utf-16-le', 'surrogatepass') == '\ud800a', ascii(b'\x00\xd8a\x00'.decode('utf-16-le', 'surrogatepass'))
import json
assert json.loads('"x"'.encode('utf-16')) == 'x' and json.loads('[1]'.encode('utf-32-le')) == [1], 'json.loads(\'"x"\'.encode(\'utf-16\')) == \'x'
'''

SECTIONS['M2_failing_import'] = r'''
import os, sys, tempfile
# A failing import propagates the module's own exception, prints nothing,
# leaves the module out of sys.modules and runs it again on the next import.
d = tempfile.mkdtemp()
with open(os.path.join(d, 'r7_badmod.py'), 'w') as f:
    f.write("COUNT = 1\nraise ValueError('boom in module')\n")
sys.path.insert(0, d)
try:
    for attempt in range(2):
        try:
            import r7_badmod
        except ValueError as e:
            assert str(e) == 'boom in module', e
        else:
            raise AssertionError('import succeeded')
        assert 'r7_badmod' not in sys.modules
finally:
    sys.path.remove(d)
'''

SECTIONS['M3_memoryview'] = r'''
ba = bytearray(b'abcdef')
with memoryview(ba) as m:
    assert m.obj is ba and m.tolist() == [97, 98, 99, 100, 101, 102] and m.hex() == '616263646566'
    try:
        ba.append(1)
    except BufferError as e:
        assert str(e) == 'Existing exports of data: object cannot be re-sized'
    else:
        raise AssertionError('resize with an export held')
assert repr(m).startswith('<released memory')
try:
    m[0]
except ValueError:
    pass
else:
    raise AssertionError('released view readable')
ba.append(1)
assert ba == bytearray(b'abcdef\x01')
for op in ('ba += ba', 'ba += memoryview(ba)'):
    ba = bytearray(b'ab')
    try:
        exec(op)
    except BufferError:
        pass
    else:
        raise AssertionError(op)
    assert ba == bytearray(b'ab')
ba = bytearray(b'ab'); ba.extend(ba); assert ba == bytearray(b'abab')
ba = bytearray(b'ab'); ba[0:0] = ba; assert ba == bytearray(b'abab')
# A view used after the bytearray it views was resized raises, never
# writes at shifted offsets.
# (CPython raises at the resize, protoPython at the use: see
# docs/CPYTHON_CONFORMANCE.md.)
ba = bytearray(b'abcdef'); v = memoryview(ba)[2:5]
try:
    del ba[0:3]
except BufferError:
    pass
else:
    try:
        v[0] = 120
    except BufferError:
        pass
    else:
        raise AssertionError('stale view written')
    assert ba == bytearray(b'def')
v = None
# Step slices share the buffer; hashing follows CPython.
ba = bytearray(b'abcd'); memoryview(ba)[::2][1] = 90
assert ba == bytearray(b'abZd')
assert hash(memoryview(b'ab')) == hash(b'ab')
try:
    hash(memoryview(bytearray(b'ab')))
except ValueError:
    pass
else:
    raise AssertionError('writable view hashed')
m = memoryview(b'abcd')
assert m.cast('B').tolist() == [97, 98, 99, 100] and m.toreadonly().readonly
assert m.shape == (4,) and m.strides == (1,) and m.nbytes == 4 and m.format == 'B'
'''

SECTIONS['M5_dir_and_f_locals_over_mapping'] = r'''
import sys
m = {}
exec('a1 = 1\nr = dir()', {}, m)
assert m['r'] == ['a1'], m['r']
assert eval('dir()', {}, {'q': 1}) == ['q']
ns = {'sys': sys}
exec('k = 2\nr = sorted(sys._getframe().f_locals)', {}, ns)
assert ns['r'] == ['k', 'sys'], ns['r']
'''

SECTIONS['open_alternate_constructors'] = r'''
class D(dict): pass
class B(bytes): pass
class I(int): pass
class F(float): pass
assert type(D.fromkeys('ab')) is D and D.fromkeys('ab') == {'a': None, 'b': None}
assert type(B.fromhex('61')) is B and B.fromhex('61') == b'a'
assert type(I.from_bytes(b'\x01', 'big')) is I and I.from_bytes(b'\x01', 'big') == 1
assert type(F.fromhex('0x1p1')) is F and F.fromhex('0x1p1') == 2.0
assert type(dict.fromkeys('a')) is dict and type(bytes.fromhex('61')) is bytes
'''

SECTIONS['open_dir_of_builtin_types'] = r'''
for T in (range, enumerate, zip, map, filter, reversed, memoryview, bytearray, list, dict, str):
    names = dir(T)
    assert not [n for n in names if n.startswith('\x01')], T
    for meta in ('__instancecheck__', '__prepare__', 'mro', '__mro__', '__bases__', '__name__', '__qualname__'):
        assert meta not in names, (T, meta)
    assert T.__mro__[0] is T and isinstance(T, type), T
assert 'mro' in dir(type) and '__name__' not in dir(int)
'''

SECTIONS['open_native_module_attributes'] = r'''
import math, sys, time
from unittest import mock
with mock.patch('time.time', lambda: 42.0):
    assert time.time() == 42.0
assert time.time() != 42.0
old = math.sqrt
math.sqrt = lambda x: -1
try:
    assert math.sqrt(4) == -1
finally:
    math.sqrt = old
assert math.sqrt(4) == 2.0 and sys.modules['math'] is math
math.r7_probe = 1
assert math.r7_probe == 1
del math.r7_probe
assert not hasattr(math, 'r7_probe')
'''

SECTIONS['small_items'] = r'''
import os, re
# bytes.partition returns the separator object it was given.
sep = bytearray(b',')
assert b'a,b'.partition(sep)[1] is sep and b'a,b'.rpartition(sep)[1] is sep
# A sequence's concatenation reports CPython's message.
for left, name in (('a', 'str'), ([1], 'list'), ((1,), 'tuple')):
    try:
        left + 1
    except TypeError as e:
        assert str(e) == 'can only concatenate %s (not "int") to %s' % (name, name), e
    else:
        raise AssertionError(name)
# re looks a pattern up in a dict: a bytearray pattern is unhashable.
try:
    re.compile(bytearray(b'a'))
except TypeError:
    pass
else:
    raise AssertionError('re.compile(bytearray)')
# Paths: bytes names a file; a bytearray or None is a TypeError, not None.
assert os.stat(b'/') is not None and os.path.isdir(b'/')
for bad in (bytearray(b'/'), None):
    for f in (os.stat, open):
        try:
            f(bad)
        except TypeError:
            pass
        else:
            raise AssertionError((f, bad))
'''

failed = []
for name, code in SECTIONS.items():
    try:
        exec(compile(code, "<%s>" % name, "exec"), {'__name__': '__r7_' + name})
    except BaseException as e:
        failed.append(name)
        print("section %s: FAIL %s: %s" % (name, type(e).__name__, e))
if failed:
    print("review_round7: failed", failed)
else:
    print("review_round7: ok")
