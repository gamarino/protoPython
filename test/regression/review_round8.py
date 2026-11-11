# Review round 8 of 1.2.2 (tasks/p8-round7-review-report.md): internal keys
# reached through __dict__.update, star import, super and __set_name__ (H1,
# M1); int formatting at any size (H2); struct and memoryview unsigned 64-bit
# items (H3); built-in methods called through their type (H4); failing
# imports (M2); memoryview exports, item reads and formats (M3); the native
# UTF-16 / UTF-32 / UTF-7 / charmap codecs (M4); bytes.hex with a bytes sep.
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix.

SECTIONS = {}

SECTIONS['H1_dict_update_escapes'] = r'''
import copy
K = '\x01__data__'
class X: pass
class L(list): pass
class D(dict): pass
class BA(bytearray): pass
l = L([1, 2, 3]); l.__dict__.update({K: 12345})
assert list(l) == [1, 2, 3] and getattr(l, K) == 12345
d = D(a=1); d.__dict__.update({K: 'zz'})
assert dict(d) == {'a': 1} and len(d) == 1
b = BA(b'abc'); b.__dict__.update({'\x01__bytearray__': [1, 2, 3]})
assert bytes(b) == b'abc'
x = X(); x.__dict__.update({'\x01__keys__': 5}); x.a = 1
assert vars(x) == {'\x01__keys__': 5, 'a': 1}, vars(x)
x = X(); x.__dict__.update({K: 1, '\x01': 2, 'a': 3})
assert sorted(vars(x).items()) == [('\x01', 2), (K, 1), ('a', 3)]
# copy and deepcopy go through __dict__.update (copy._reconstruct).
x = X(); setattr(x, '\x01q', 7); x.b = 2
for c in (copy.copy(x), copy.deepcopy(x)):
    assert getattr(c, '\x01q') == 7 and c.b == 2
# delattr leaves no ghost in vars().
x = X(); setattr(x, '\x01q', 1); delattr(x, '\x01q')
assert vars(x) == {}, vars(x)
# A module is not a dict: dict.update(module, ...) is a TypeError.
import types
m = types.ModuleType('m')
try:
    dict.update(m, {K: 1})
except TypeError:
    pass
else:
    raise AssertionError('dict.update(module)')
'''

SECTIONS['H1_star_import_escapes'] = r'''
import sys, os, tempfile
d = tempfile.mkdtemp()
with open(os.path.join(d, 'r8_starmod.py'), 'w') as f:
    f.write("for k in ('\\x01__data__', '\\x01__keys__', '\\x01__executed__', '\\x01x'):\n"
            "    globals()[k] = 'FORGED'\n"
            "plain = 1\n")
with open(os.path.join(d, 'r8_importer.py'), 'w') as f:
    f.write("mine = 'importer-only'\n"
            "from r8_starmod import *\n"
            "names = sorted(k for k in globals() if not k.startswith('__'))\n")
sys.path.insert(0, d)
import r8_importer as imp
assert imp.mine == 'importer-only' and imp.plain == 1
assert getattr(imp, '\x01x') == 'FORGED' and getattr(imp, '\x01__keys__') == 'FORGED'
# (globals() of a module also lists the module type's dict methods: a
# pre-existing defect outside this section.)
assert {'\x01__data__', '\x01__executed__', '\x01__keys__', '\x01x', 'mine', 'plain'} <= set(imp.names), imp.names
'''

SECTIONS['H1_super_and_set_name'] = r'''
K = '\x01__data__'
class S1: pass
class S2(S1):
    def m(self, n):
        return getattr(super(), n, 'none')
for n in (K, '\x01__keys__', '\x01__is_python_class__', '\x01__pyflags__', '\x01'):
    assert S2().m(n) == 'none', (n, S2().m(n))
class L(list):
    def m(self): return getattr(super(), K, 'none')
assert L([1]).m() == 'none'
try:
    super(S2, S2()).missing
except AttributeError as e:
    assert 'missing' in str(e)
else:
    raise AssertionError('super missing')
seen = []
class SN:
    def __set_name__(self, owner, name): seen.append(name)
type('Q', (), {'n': SN(), K: SN()})
assert seen == ['n', K], seen
class X: pass
for f in (lambda: delattr(X(), '\x01q'), lambda: getattr(X(), '\x01q'), lambda: getattr(X, '\x01q')):
    try:
        f()
    except AttributeError as e:
        assert '\x01\x01' not in str(e), repr(str(e))
    else:
        raise AssertionError('no AttributeError')
try:
    class Bad:
        __slots__ = ('\x01__data__',)
except TypeError:
    pass
else:
    raise AssertionError('__slots__ accepted a non-identifier')
'''

SECTIONS['H2_int_format_any_size'] = r'''
vals = [2**63 - 1, 2**63, 2**64 - 1, -2**63, -2**63 - 1, 10**30, -10**30, 2**200]
for v in vals:
    for base, spec, pre in ((16, 'x', '0x'), (8, 'o', '0o'), (2, 'b', '0b')):
        digits = (hex if base == 16 else oct if base == 8 else bin)(abs(v))[2:]
        sign = '-' if v < 0 else ''
        assert format(v, spec) == sign + digits, (v, spec, format(v, spec))
        assert format(v, '#' + spec) == sign + pre + digits
        assert ('%' + spec) % v == sign + digits if spec != 'b' else True
    assert format(v) == str(v) == format(v, 'd') == format(v, 'n') == '%d' % v == '%i' % v == '%u' % v
    assert format(v, '>60d') == str(v).rjust(60)
    assert format(v, '+d') == ('+' if v >= 0 else '') + str(v)
    assert f'{v:x}' == '{:x}'.format(v) == format(v, 'x')
    assert '%X' % v == format(v, 'X') == ('-' if v < 0 else '') + hex(abs(v))[2:].upper()
    assert '%#x' % v == format(v, '#x') and '%#o' % v == format(v, '#o')
    assert '%.40d' % abs(v) == str(abs(v)).zfill(40)
    assert format(v, ',') == '{:,}'.format(v)
assert format(2**64 - 1, ',') == '18,446,744,073,709,551,615'
assert format(2**64 - 1, '_x') == 'ffff_ffff_ffff_ffff'
assert format(2**64 - 1, '030,d') == '00,018,446,744,073,709,551,615'
assert format(10**30, 'e') == '1.000000e+30' and '%e' % 10**30 == '1.000000e+30'
assert f'{2**80:.1f}' == '1208925819614629174706176.0' == '%.1f' % 2**80
assert format(2**63, '.2%') == '922337203685477580800.00%'
assert format(True) == 'True' and format(True, 'd') == '1' and f'{False:>6}' == '     0' and f'{True!s:>6}' == '  True'
for bad in ('.3d', ',x', 'z', 's'):
    try:
        format(2**64, bad)
    except ValueError:
        pass
    else:
        raise AssertionError(bad)
try:
    format(10**400, 'e')
except OverflowError:
    pass
else:
    raise AssertionError('int too large to convert to float')
try:
    float(10**400)
except OverflowError:
    pass
else:
    raise AssertionError('float(big int)')
'''

SECTIONS['H3_unsigned_64_bit_items'] = r'''
import struct
for order in '<>=!':
    for v in (2**63, 2**64 - 1, 2**63 - 1, 0):
        assert struct.unpack(order + 'Q', struct.pack(order + 'Q', v)) == (v,)
assert struct.unpack('<Q', b'\xff' * 8) == (2**64 - 1,)
assert struct.unpack('>Q', b'\x80' + b'\x00' * 7) == (2**63,)
assert list(struct.iter_unpack('<Q', b'\xff' * 16)) == [(2**64 - 1,), (2**64 - 1,)]
assert struct.Struct('<2Q').unpack(b'\xff' * 16) == (2**64 - 1, 2**64 - 1)
assert struct.unpack_from('<Q', b'\x00' + b'\xff' * 8, 1) == (2**64 - 1,)
m = memoryview(bytearray(16)).cast('Q')
for v in (2**64 - 1, 2**63, 5):
    m[0] = v
    assert m[0] == v
assert bytes(m.obj[:8]) == (5).to_bytes(8, 'little')
for v in (2**64, -1):
    try:
        m[1] = v
    except ValueError:
        pass
    else:
        raise AssertionError(v)
'''

SECTIONS['H4_unbound_descriptor_calls'] = r'''
SKIP = {'__init__', '__new__', '__getattribute__', '__setattr__', '__delattr__', '__reduce__', '__reduce_ex__',
        '__sizeof__', '__dir__', '__getstate__', '__hash__', '__getnewargs__', '__release_buffer__',
        '__buffer__', 'release', '__exit__', '__enter__', '__alloc__', '__iter__', '__reversed__',
        'clear', 'pop', 'popitem', 'remove', 'discard', 'append', 'extend',
        'insert', 'sort', 'reverse', 'update', 'setdefault', 'add', '__setitem__', '__delitem__',
        '__iadd__', '__imul__', '__ior__', '__iand__', '__ixor__', '__isub__',
        'intersection_update', 'difference_update', 'symmetric_difference_update', 'resize', 'copy',
        '__repr__' if False else ''}
samples = [
    (int, lambda: 255), (float, lambda: 1.5), (complex, lambda: 1+2j), (str, lambda: 'aBc'),
    (bytes, lambda: b'aBc'), (bytearray, lambda: bytearray(b'aBc')), (tuple, lambda: (1, 2, 1)),
    (list, lambda: [3, 1, 2]), (dict, lambda: {1: 'a'}), (set, lambda: {1, 2}),
    (frozenset, lambda: frozenset({1, 2})), (memoryview, lambda: memoryview(b'ab')),
]
argsets = [(), (1,), (2,), ('a',), (b'a',), ('.1f',), ('x',), (1, 2), ({1},), ((1,),), ([1],), (b'aBc',), ('aBc',), (0, 1)]
def outcome(f):
    try:
        r = f()
    except Exception as e:
        return 'EXC ' + type(e).__name__
    if isinstance(r, memoryview):
        return 'mv'
    if type(r).__name__ in ('dict_keys', 'dict_values', 'dict_items', 'list_iterator', 'tuple_iterator'):
        return type(r).__name__
    try:
        return '%s:%r' % (type(r).__name__, r)
    except Exception as e:
        return 'REPR ' + type(e).__name__

ROWS = [
    ('int', '__abs__', (), 'int:255'),
    ('int', '__add__', (1,), 'int:256'),
    ('int', '__and__', (1,), 'int:1'),
    ('int', '__bool__', (), 'bool:True'),
    ('int', '__ceil__', (), 'int:255'),
    ('int', '__eq__', (1,), 'bool:False'),
    ('int', '__float__', (), 'float:255.0'),
    ('int', '__floor__', (), 'int:255'),
    ('int', '__floordiv__', (1,), 'int:255'),
    ('int', '__format__', ('.1f',), "str:'255.0'"),
    ('int', '__ge__', (1,), 'bool:True'),
    ('int', '__gt__', (1,), 'bool:True'),
    ('int', '__index__', (), 'int:255'),
    ('int', '__int__', (), 'int:255'),
    ('int', '__invert__', (), 'int:-256'),
    ('int', '__le__', (1,), 'bool:False'),
    ('int', '__lshift__', (1,), 'int:510'),
    ('int', '__lt__', (1,), 'bool:False'),
    ('int', '__mod__', (1,), 'int:0'),
    ('int', '__mul__', (1,), 'int:255'),
    ('int', '__ne__', (1,), 'bool:True'),
    ('int', '__neg__', (), 'int:-255'),
    ('int', '__or__', (1,), 'int:255'),
    ('int', '__pos__', (), 'int:255'),
    ('int', '__pow__', (1,), 'int:255'),
    ('int', '__radd__', (1,), 'int:256'),
    ('int', '__repr__', (), "str:'255'"),
    ('int', '__rmul__', (1,), 'int:255'),
    ('int', '__round__', (), 'int:255'),
    ('int', '__rpow__', (1,), 'int:1'),
    ('int', '__rshift__', (1,), 'int:127'),
    ('int', '__sub__', (1,), 'int:254'),
    ('int', '__truediv__', (1,), 'float:255.0'),
    ('int', '__trunc__', (), 'int:255'),
    ('int', '__xor__', (1,), 'int:254'),
    ('int', 'as_integer_ratio', (), 'tuple:(255, 1)'),
    ('int', 'bit_count', (), 'int:8'),
    ('int', 'bit_length', (), 'int:8'),
    ('int', 'conjugate', (), 'int:255'),
    ('int', 'is_integer', (), 'bool:True'),
    ('int', 'to_bytes', (), "bytes:b'\\xff'"),
    ('float', '__abs__', (), 'float:1.5'),
    ('float', '__add__', (1,), 'float:2.5'),
    ('float', '__bool__', (), 'bool:True'),
    ('float', '__ceil__', (), 'int:2'),
    ('float', '__eq__', (1,), 'bool:False'),
    ('float', '__float__', (), 'float:1.5'),
    ('float', '__floor__', (), 'int:1'),
    ('float', '__floordiv__', (1,), 'float:1.0'),
    ('float', '__format__', ('.1f',), "str:'1.5'"),
    ('float', '__ge__', (1,), 'bool:True'),
    ('float', '__gt__', (1,), 'bool:True'),
    ('float', '__int__', (), 'int:1'),
    ('float', '__le__', (1,), 'bool:False'),
    ('float', '__lt__', (1,), 'bool:False'),
    ('float', '__mod__', (1,), 'float:0.5'),
    ('float', '__mul__', (1,), 'float:1.5'),
    ('float', '__ne__', (1,), 'bool:True'),
    ('float', '__neg__', (), 'float:-1.5'),
    ('float', '__pos__', (), 'float:1.5'),
    ('float', '__pow__', (1,), 'float:1.5'),
    ('float', '__radd__', (1,), 'float:2.5'),
    ('float', '__repr__', (), "str:'1.5'"),
    ('float', '__rmul__', (1,), 'float:1.5'),
    ('float', '__round__', (), 'int:2'),
    ('float', '__rsub__', (1,), 'float:-0.5'),
    ('float', '__rtruediv__', (1,), 'float:0.6666666666666666'),
    ('float', '__sub__', (1,), 'float:0.5'),
    ('float', '__truediv__', (1,), 'float:1.5'),
    ('float', '__trunc__', (), 'int:1'),
    ('float', 'as_integer_ratio', (), 'tuple:(3, 2)'),
    ('float', 'conjugate', (), 'float:1.5'),
    ('float', 'hex', (), "str:'0x1.8000000000000p+0'"),
    ('float', 'is_integer', (), 'bool:False'),
    ('complex', '__abs__', (), 'float:2.23606797749979'),
    ('complex', '__add__', (1,), 'complex:(2+2j)'),
    ('complex', '__bool__', (), 'bool:True'),
    ('complex', '__complex__', (), 'complex:(1+2j)'),
    ('complex', '__eq__', (1,), 'bool:False'),
    ('complex', '__mul__', (1,), 'complex:(1+2j)'),
    ('complex', '__ne__', (1,), 'bool:True'),
    ('complex', '__neg__', (), 'complex:(-1-2j)'),
    ('complex', '__pos__', (), 'complex:(1+2j)'),
    ('complex', '__radd__', (1,), 'complex:(2+2j)'),
    ('complex', '__repr__', (), "str:'(1+2j)'"),
    ('complex', '__rmul__', (1,), 'complex:(1+2j)'),
    ('complex', '__rpow__', (1,), 'complex:(1+0j)'),
    ('complex', '__rsub__', (1,), 'complex:-2j'),
    ('complex', '__rtruediv__', (1,), 'complex:(0.2-0.4j)'),
    ('complex', '__sub__', (1,), 'complex:2j'),
    ('complex', '__truediv__', (1,), 'complex:(1+2j)'),
    ('complex', 'conjugate', (), 'complex:(1-2j)'),
    ('str', '__add__', ('a',), "str:'aBca'"),
    ('str', '__contains__', ('a',), 'bool:True'),
    ('str', '__eq__', (1,), 'NotImplementedType:NotImplemented'),
    ('str', '__ge__', (1,), 'NotImplementedType:NotImplemented'),
    ('str', '__getitem__', (1,), "str:'B'"),
    ('str', '__gt__', (1,), 'NotImplementedType:NotImplemented'),
    ('str', '__le__', (1,), 'NotImplementedType:NotImplemented'),
    ('str', '__len__', (), 'int:3'),
    ('str', '__lt__', (1,), 'NotImplementedType:NotImplemented'),
    ('str', '__mod__', (b'a',), "str:'aBc'"),
    ('str', '__mul__', (1,), "str:'aBc'"),
    ('str', '__ne__', (1,), 'NotImplementedType:NotImplemented'),
    ('str', '__repr__', (), 'str:"\'aBc\'"'),
    ('str', '__rmul__', (1,), "str:'aBc'"),
    ('str', '__str__', (), "str:'aBc'"),
    ('str', 'capitalize', (), "str:'Abc'"),
    ('str', 'casefold', (), "str:'abc'"),
    ('str', 'center', (1,), "str:'aBc'"),
    ('str', 'count', ('a',), 'int:1'),
    ('str', 'encode', (), "bytes:b'aBc'"),
    ('str', 'endswith', ('a',), 'bool:False'),
    ('str', 'expandtabs', (), "str:'aBc'"),
    ('str', 'find', ('a',), 'int:0'),
    ('str', 'format', (), "str:'aBc'"),
    ('str', 'format_map', (1,), "str:'aBc'"),
    ('str', 'index', ('a',), 'int:0'),
    ('str', 'isalnum', (), 'bool:True'),
    ('str', 'isalpha', (), 'bool:True'),
    ('str', 'isascii', (), 'bool:True'),
    ('str', 'isdecimal', (), 'bool:False'),
    ('str', 'isdigit', (), 'bool:False'),
    ('str', 'isidentifier', (), 'bool:True'),
    ('str', 'islower', (), 'bool:False'),
    ('str', 'isnumeric', (), 'bool:False'),
    ('str', 'isprintable', (), 'bool:True'),
    ('str', 'isspace', (), 'bool:False'),
    ('str', 'istitle', (), 'bool:False'),
    ('str', 'isupper', (), 'bool:False'),
    ('str', 'join', ('a',), "str:'a'"),
    ('str', 'ljust', (1,), "str:'aBc'"),
    ('str', 'lower', (), "str:'abc'"),
    ('str', 'lstrip', (), "str:'aBc'"),
    ('str', 'partition', ('a',), "tuple:('', 'a', 'Bc')"),
    ('str', 'removeprefix', ('a',), "str:'Bc'"),
    ('str', 'removesuffix', ('a',), "str:'aBc'"),
    ('str', 'rfind', ('a',), 'int:0'),
    ('str', 'rindex', ('a',), 'int:0'),
    ('str', 'rjust', (1,), "str:'aBc'"),
    ('str', 'rpartition', ('a',), "tuple:('', 'a', 'Bc')"),
    ('str', 'rsplit', (), "list:['aBc']"),
    ('str', 'rstrip', (), "str:'aBc'"),
    ('str', 'split', (), "list:['aBc']"),
    ('str', 'splitlines', (), "list:['aBc']"),
    ('str', 'startswith', ('a',), 'bool:True'),
    ('str', 'strip', (), "str:'aBc'"),
    ('str', 'swapcase', (), "str:'AbC'"),
    ('str', 'title', (), "str:'Abc'"),
    ('str', 'translate', ('a',), "str:'aBc'"),
    ('str', 'upper', (), "str:'ABC'"),
    ('str', 'zfill', (1,), "str:'aBc'"),
    ('bytes', '__add__', (b'a',), "bytes:b'aBca'"),
    ('bytes', '__bytes__', (), "bytes:b'aBc'"),
    ('bytes', '__contains__', (1,), 'bool:False'),
    ('bytes', '__eq__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytes', '__ge__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytes', '__getitem__', (1,), 'int:66'),
    ('bytes', '__gt__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytes', '__le__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytes', '__len__', (), 'int:3'),
    ('bytes', '__lt__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytes', '__mod__', ([1],), "bytes:b'aBc'"),
    ('bytes', '__mul__', (1,), "bytes:b'aBc'"),
    ('bytes', '__repr__', (), 'str:"b\'aBc\'"'),
    ('bytes', '__rmod__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytes', '__rmul__', (1,), "bytes:b'aBc'"),
    ('bytes', '__str__', (), 'str:"b\'aBc\'"'),
    ('bytes', 'capitalize', (), "bytes:b'Abc'"),
    ('bytes', 'center', (1,), "bytes:b'aBc'"),
    ('bytes', 'count', (1,), 'int:0'),
    ('bytes', 'decode', (), "str:'aBc'"),
    ('bytes', 'endswith', (b'a',), 'bool:False'),
    ('bytes', 'expandtabs', (), "bytes:b'aBc'"),
    ('bytes', 'find', (1,), 'int:-1'),
    ('bytes', 'hex', (), "str:'614263'"),
    ('bytes', 'index', (b'a',), 'int:0'),
    ('bytes', 'isalnum', (), 'bool:True'),
    ('bytes', 'isalpha', (), 'bool:True'),
    ('bytes', 'isascii', (), 'bool:True'),
    ('bytes', 'isdigit', (), 'bool:False'),
    ('bytes', 'islower', (), 'bool:False'),
    ('bytes', 'isspace', (), 'bool:False'),
    ('bytes', 'istitle', (), 'bool:False'),
    ('bytes', 'isupper', (), 'bool:False'),
    ('bytes', 'ljust', (1,), "bytes:b'aBc'"),
    ('bytes', 'lower', (), "bytes:b'abc'"),
    ('bytes', 'lstrip', (), "bytes:b'aBc'"),
    ('bytes', 'partition', (b'a',), "tuple:(b'', b'a', b'Bc')"),
    ('bytes', 'removeprefix', (b'a',), "bytes:b'Bc'"),
    ('bytes', 'removesuffix', (b'a',), "bytes:b'aBc'"),
    ('bytes', 'rfind', (1,), 'int:-1'),
    ('bytes', 'rindex', (b'a',), 'int:0'),
    ('bytes', 'rjust', (1,), "bytes:b'aBc'"),
    ('bytes', 'rpartition', (b'a',), "tuple:(b'', b'a', b'Bc')"),
    ('bytes', 'rsplit', (), "list:[b'aBc']"),
    ('bytes', 'rstrip', (), "bytes:b'aBc'"),
    ('bytes', 'split', (), "list:[b'aBc']"),
    ('bytes', 'splitlines', (), "list:[b'aBc']"),
    ('bytes', 'startswith', (b'a',), 'bool:True'),
    ('bytes', 'strip', (), "bytes:b'aBc'"),
    ('bytes', 'swapcase', (), "bytes:b'AbC'"),
    ('bytes', 'title', (), "bytes:b'Abc'"),
    ('bytes', 'upper', (), "bytes:b'ABC'"),
    ('bytes', 'zfill', (1,), "bytes:b'aBc'"),
    ('bytearray', '__add__', (b'a',), "bytearray:bytearray(b'aBca')"),
    ('bytearray', '__contains__', (1,), 'bool:False'),
    ('bytearray', '__eq__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytearray', '__ge__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytearray', '__getitem__', (1,), 'int:66'),
    ('bytearray', '__gt__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytearray', '__le__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytearray', '__len__', (), 'int:3'),
    ('bytearray', '__lt__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytearray', '__mod__', ([1],), "bytearray:bytearray(b'aBc')"),
    ('bytearray', '__mul__', (1,), "bytearray:bytearray(b'aBc')"),
    ('bytearray', '__repr__', (), 'str:"bytearray(b\'aBc\')"'),
    ('bytearray', '__rmod__', (1,), 'NotImplementedType:NotImplemented'),
    ('bytearray', '__rmul__', (1,), "bytearray:bytearray(b'aBc')"),
    ('bytearray', '__str__', (), 'str:"bytearray(b\'aBc\')"'),
    ('bytearray', 'capitalize', (), "bytearray:bytearray(b'Abc')"),
    ('bytearray', 'center', (1,), "bytearray:bytearray(b'aBc')"),
    ('bytearray', 'count', (1,), 'int:0'),
    ('bytearray', 'decode', (), "str:'aBc'"),
    ('bytearray', 'endswith', (b'a',), 'bool:False'),
    ('bytearray', 'expandtabs', (), "bytearray:bytearray(b'aBc')"),
    ('bytearray', 'find', (1,), 'int:-1'),
    ('bytearray', 'hex', (), "str:'614263'"),
    ('bytearray', 'index', (b'a',), 'int:0'),
    ('bytearray', 'isalnum', (), 'bool:True'),
    ('bytearray', 'isalpha', (), 'bool:True'),
    ('bytearray', 'isascii', (), 'bool:True'),
    ('bytearray', 'isdigit', (), 'bool:False'),
    ('bytearray', 'islower', (), 'bool:False'),
    ('bytearray', 'isspace', (), 'bool:False'),
    ('bytearray', 'istitle', (), 'bool:False'),
    ('bytearray', 'isupper', (), 'bool:False'),
    ('bytearray', 'ljust', (1,), "bytearray:bytearray(b'aBc')"),
    ('bytearray', 'lower', (), "bytearray:bytearray(b'abc')"),
    ('bytearray', 'lstrip', (), "bytearray:bytearray(b'aBc')"),
    ('bytearray', 'partition', (b'a',), "tuple:(bytearray(b''), bytearray(b'a'), bytearray(b'Bc'))"),
    ('bytearray', 'removeprefix', (b'a',), "bytearray:bytearray(b'Bc')"),
    ('bytearray', 'removesuffix', (b'a',), "bytearray:bytearray(b'aBc')"),
    ('bytearray', 'rfind', (1,), 'int:-1'),
    ('bytearray', 'rindex', (b'a',), 'int:0'),
    ('bytearray', 'rjust', (1,), "bytearray:bytearray(b'aBc')"),
    ('bytearray', 'rpartition', (b'a',), "tuple:(bytearray(b''), bytearray(b'a'), bytearray(b'Bc'))"),
    ('bytearray', 'rsplit', (), "list:[bytearray(b'aBc')]"),
    ('bytearray', 'rstrip', (), "bytearray:bytearray(b'aBc')"),
    ('bytearray', 'split', (), "list:[bytearray(b'aBc')]"),
    ('bytearray', 'splitlines', (), "list:[bytearray(b'aBc')]"),
    ('bytearray', 'startswith', (b'a',), 'bool:True'),
    ('bytearray', 'strip', (), "bytearray:bytearray(b'aBc')"),
    ('bytearray', 'swapcase', (), "bytearray:bytearray(b'AbC')"),
    ('bytearray', 'title', (), "bytearray:bytearray(b'Abc')"),
    ('bytearray', 'upper', (), "bytearray:bytearray(b'ABC')"),
    ('bytearray', 'zfill', (1,), "bytearray:bytearray(b'aBc')"),
    ('tuple', '__add__', ((1,),), 'tuple:(1, 2, 1, 1)'),
    ('tuple', '__contains__', (1,), 'bool:True'),
    ('tuple', '__eq__', (1,), 'NotImplementedType:NotImplemented'),
    ('tuple', '__ge__', (1,), 'NotImplementedType:NotImplemented'),
    ('tuple', '__getitem__', (1,), 'int:2'),
    ('tuple', '__gt__', (1,), 'NotImplementedType:NotImplemented'),
    ('tuple', '__le__', (1,), 'NotImplementedType:NotImplemented'),
    ('tuple', '__len__', (), 'int:3'),
    ('tuple', '__lt__', (1,), 'NotImplementedType:NotImplemented'),
    ('tuple', '__mul__', (1,), 'tuple:(1, 2, 1)'),
    ('tuple', '__repr__', (), "str:'(1, 2, 1)'"),
    ('tuple', '__rmul__', (1,), 'tuple:(1, 2, 1)'),
    ('tuple', 'count', (1,), 'int:2'),
    ('tuple', 'index', (1,), 'int:0'),
    ('list', '__add__', ([1],), 'list:[3, 1, 2, 1]'),
    ('list', '__contains__', (1,), 'bool:True'),
    ('list', '__eq__', (1,), 'NotImplementedType:NotImplemented'),
    ('list', '__ge__', (1,), 'NotImplementedType:NotImplemented'),
    ('list', '__getitem__', (1,), 'int:1'),
    ('list', '__gt__', (1,), 'NotImplementedType:NotImplemented'),
    ('list', '__le__', (1,), 'NotImplementedType:NotImplemented'),
    ('list', '__len__', (), 'int:3'),
    ('list', '__lt__', (1,), 'NotImplementedType:NotImplemented'),
    ('list', '__mul__', (1,), 'list:[3, 1, 2]'),
    ('list', '__repr__', (), "str:'[3, 1, 2]'"),
    ('list', '__rmul__', (1,), 'list:[3, 1, 2]'),
    ('list', 'count', (1,), 'int:1'),
    ('list', 'index', (1,), 'int:1'),
    ('dict', '__contains__', (1,), 'bool:True'),
    ('dict', '__eq__', (1,), 'NotImplementedType:NotImplemented'),
    ('dict', '__getitem__', (1,), "str:'a'"),
    ('dict', '__len__', (), 'int:1'),
    ('dict', '__or__', (1,), 'NotImplementedType:NotImplemented'),
    ('dict', '__repr__', (), 'str:"{1: \'a\'}"'),
    ('dict', 'get', (1,), "str:'a'"),
    ('dict', 'items', (), 'dict_items'),
    ('dict', 'keys', (), 'dict_keys'),
    ('dict', 'values', (), 'dict_values'),
    ('set', '__and__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__contains__', (1,), 'bool:True'),
    ('set', '__eq__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__ge__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__gt__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__le__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__len__', (), 'int:2'),
    ('set', '__lt__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__ne__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__or__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__rand__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__repr__', (), "str:'{1, 2}'"),
    ('set', '__ror__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__rsub__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__rxor__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__sub__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', '__xor__', (1,), 'NotImplementedType:NotImplemented'),
    ('set', 'difference', (), 'set:{1, 2}'),
    ('set', 'intersection', (), 'set:{1, 2}'),
    ('set', 'isdisjoint', ('a',), 'bool:True'),
    ('set', 'issubset', ('a',), 'bool:False'),
    ('set', 'issuperset', ('a',), 'bool:False'),
    ('set', 'union', (), 'set:{1, 2}'),
    ('frozenset', '__and__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__contains__', (1,), 'bool:True'),
    ('frozenset', '__eq__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__ge__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__gt__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__le__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__len__', (), 'int:2'),
    ('frozenset', '__lt__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__ne__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__or__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__rand__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__repr__', (), "str:'frozenset({1, 2})'"),
    ('frozenset', '__ror__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__rsub__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__rxor__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__sub__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', '__xor__', (1,), 'NotImplementedType:NotImplemented'),
    ('frozenset', 'difference', (), 'frozenset:frozenset({1, 2})'),
    ('frozenset', 'intersection', (), 'frozenset:frozenset({1, 2})'),
    ('frozenset', 'isdisjoint', ('a',), 'bool:True'),
    ('frozenset', 'issubset', ('a',), 'bool:False'),
    ('frozenset', 'issuperset', ('a',), 'bool:False'),
    ('frozenset', 'union', (), 'frozenset:frozenset({1, 2})'),
    ('memoryview', '__eq__', (1,), 'NotImplementedType:NotImplemented'),
    ('memoryview', '__getitem__', (1,), 'int:98'),
    ('memoryview', '__len__', (), 'int:2'),
    ('memoryview', '__ne__', (1,), 'NotImplementedType:NotImplemented'),
    ('memoryview', 'count', (1,), 'int:0'),
    ('memoryview', 'hex', (), "str:'6162'"),
    ('memoryview', 'tobytes', (), "bytes:b'ab'"),
    ('memoryview', 'tolist', (), 'list:[97, 98]'),
    ('memoryview', 'toreadonly', (), 'mv'),
]

mk = dict((T.__name__, m) for T, m in samples)
types_ = dict((T.__name__, T) for T, m in samples)
bad = []
for tn, name, a, exp in ROWS:
    got = outcome(lambda: getattr(types_[tn], name)(mk[tn](), *a))
    if got != exp:
        bad.append((tn, name, a, got, exp))
assert not bad, bad[:3]
# The receiver is checked, with CPython's messages.
for f, msg in ((lambda: str.upper(5), "descriptor 'upper' for 'str' objects doesn't apply to a 'int' object"),
               (lambda: str.upper(), "unbound method str.upper() needs an argument"),
               (lambda: list.__len__(5), "descriptor '__len__' requires a 'list' object but received a 'int'"),
               (lambda: dict.keys([]), "descriptor 'keys' for 'dict' objects doesn't apply to a 'list' object")):
    try:
        f()
    except TypeError as e:
        assert str(e) == msg, str(e)
    else:
        raise AssertionError(msg)
class S(str): pass
assert type(str.__str__(S('x'))) is str
assert float.hex(1.25) == '0x1.4000000000000p+0' and (0.1).hex() == '0x1.999999999999ap-4'
assert float.hex(5e-324) == '0x0.0000000000001p-1022' and float.hex(-0.0) == '-0x0.0p+0'
assert (1.5).__format__('.1f') == '1.5' and float.__format__(2.25, '>7.2f') == '   2.25'
assert int.to_bytes(5) == b'\x05' and int.to_bytes(5, 2, 'big') == b'\x00\x05'
assert complex.conjugate(1 + 2j) == 1 - 2j and float.__float__(1.5) == 1.5
assert int.is_integer(3) is True
'''

SECTIONS['M2_failing_imports'] = r'''
import os, sys, tempfile, importlib
d = tempfile.mkdtemp(); sys.path.insert(0, d)
def w(name, src):
    p = os.path.join(d, *name.split('/'))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'w') as f: f.write(src)
w('r8top.py', 'raise RuntimeError("top")\n')
w('r8pk/__init__.py', '')
w('r8pk/bad.py', 'Z = 5\nraise RuntimeError("sub")\n')
for f in (lambda: importlib.import_module('r8top'), lambda: importlib.import_module('r8pk.bad'),
          lambda: importlib.import_module('.bad', 'r8pk')):
    try:
        f()
    except RuntimeError:
        pass
    else:
        raise AssertionError('module exception masked')
for i in range(2):
    try:
        import r8pk.bad
    except RuntimeError:
        pass
    assert 'r8pk.bad' not in sys.modules
    assert getattr(sys.modules['r8pk'], 'bad', None) is None
try:
    from r8pk import bad
except RuntimeError:
    pass
else:
    raise AssertionError('half-run submodule returned')
w('r8rl.py', 'N = 1\n')
import r8rl
w('r8rl.py', 'N = 2\nM = 3\n')
assert importlib.reload(r8rl) is r8rl and r8rl.N == 2 and r8rl.M == 3
w('r8rl.py', 'N = 4\nraise ValueError("reload")\n')
try:
    importlib.reload(r8rl)
except ValueError:
    pass
else:
    raise AssertionError('reload error masked')
import math
assert importlib.reload(math) is math
'''

SECTIONS['M3_memoryview'] = r'''
import pickle
ba = bytearray(b'abc')
m = ba.__buffer__(0)
try:
    ba.append(1)
except BufferError:
    pass
else:
    raise AssertionError('__buffer__ holds no export')
ba.__release_buffer__(m)
ba.append(1); assert len(ba) == 4
ba2 = bytearray(b'xyz'); m2 = memoryview(ba2).__buffer__(0)
try:
    ba2.append(1)
except BufferError:
    pass
else:
    raise AssertionError('memoryview.__buffer__ holds no export')
m2.release(); ba2.append(1)
try:
    b'abc'.center(7, memoryview(b'*'))
except TypeError:
    pass
else:
    raise AssertionError('memoryview fill accepted')
assert b'abc'.center(7, bytearray(b'*')) == b'**abc**'
r = memoryview(bytearray(b'x')); r.release()
assert r == r and not (r == memoryview(b'x')) and not (r != r) and r in [r]
e = memoryview(bytearray(8)).cast('e')
for x, want in ((1.5, 1.5), (-2.0, -2.0), (65504.0, 65504.0), (0.1, 0.0999755859375), (1e-7, 1.1920928955078125e-07)):
    e[0] = x
    assert e[0] == want, (x, e[0])
try:
    e[1] = 1e6
except ValueError:
    pass
else:
    raise AssertionError('e overflow')
class I:
    def __index__(self): return 1
assert memoryview(b'abc')[I()] == 98
mm = memoryview(bytearray(b'abc')); mm[I()] = 120; assert bytes(mm) == b'axc'
for f in (lambda: pickle.dumps(memoryview(b'a')), lambda: hash(memoryview(bytearray(b'a')).toreadonly())):
    try:
        f()
    except TypeError:
        pass
    else:
        raise AssertionError(f)
try:
    class M(memoryview): pass
except TypeError:
    pass
else:
    raise AssertionError('memoryview subclassed')
# An item read is O(itemsize), not a copy of the whole buffer.
import time
big = memoryview(bytearray(1 << 22))
t = time.perf_counter()
for i in range(0, 1 << 22, 1 << 12):
    big[i]
assert time.perf_counter() - t < 0.5, time.perf_counter() - t
'''

SECTIONS['M4_codecs'] = r'''
import codecs, time
s = 'h\xe9llo w\u20acrld \U0001f600 ' * 4000
for enc in ('utf-16', 'utf-16-le', 'utf-16-be', 'utf-32', 'utf-32-be', 'utf-7', 'cp1252', 'cp437', 'koi8-r'):
    src = s if enc not in ('cp1252', 'cp437', 'koi8-r') else s.encode(enc, 'replace').decode(enc)
    t = time.perf_counter()
    assert src.encode(enc).decode(enc) == src, enc
    assert time.perf_counter() - t < 2.0, (enc, time.perf_counter() - t)
assert 'a\U0001f600'.encode('utf-16-le') == b'a\x00=\xd8\x00\xde'
assert 'A\u2262\u0391.'.encode('utf-7') == b'A+ImIDkQ.' and b'A+ImIDkQ.'.decode('utf-7') == 'A\u2262\u0391.'
assert b'+2D3'.decode('utf-7', 'replace') == '\ufffd'
# A run of lone surrogates is reported one character at a time.
spans = []
codecs.register_error('r8spans', lambda e: (spans.append((e.start, e.end)) or '', e.end))
'\udcff\udc80x'.encode('utf-16-le', 'r8spans')
assert spans == [(0, 1), (1, 2)], spans
try:
    '\udcff\udc80'.encode('utf-16', 'surrogateescape')
except UnicodeEncodeError:
    pass
else:
    raise AssertionError('utf-16 surrogateescape')
# A charmap run of unmappable characters is one error.
spans.clear()
'a\u4e00\u4e01b'.encode('cp1252', 'r8spans')
assert spans == [(1, 3)], spans
assert 'a\u4e00\u4e01b'.encode('cp1252', 'replace') == b'a??b'
assert b'\x81a'.decode('cp1252', 'replace') == '\ufffda'
assert b'\xff\xfe\x00'.decode('utf-16', 'replace') == '\ufffd'
assert b'\x00\xd8a\x00'.decode('utf-16-le', 'replace') == '\ufffda'
dec = codecs.getincrementaldecoder('utf-16-le')()
assert dec.decode(b'a\x00=') == 'a' and dec.decode(b'\xd8\x00\xde', True) == '\U0001f600'
'''

SECTIONS['LOW_bytes_hex_sep'] = r'''
assert b'abc'.hex(b':') == '61:62:63' and bytearray(b'abc').hex(b'-', 2) == '61-6263'
assert memoryview(b'abc').hex(b'-') == '61-62-63'
for sep, exc in ((bytearray(b':'), TypeError), (b'::', ValueError), (b'\xff', ValueError), (5, TypeError)):
    try:
        b'abc'.hex(sep)
    except exc:
        pass
    else:
        raise AssertionError(sep)
'''

failed = []
for name, code in SECTIONS.items():
    try:
        exec(compile(code, "<%s>" % name, "exec"), {'__name__': '__r8_' + name})
    except BaseException as e:
        failed.append(name)
        print("section %s: FAIL %s: %s" % (name, type(e).__name__, str(e)[:300]))
if failed:
    print("review_round8: failed", failed)
else:
    print("review_round8: ok")
