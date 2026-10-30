# The array module stores typed values: the stub it replaces kept raw bytes,
# so array('i', ...) read back bytes, and fromfile/tofile/append/byteswap
# were missing (test_datetime's ZoneInfo reader calls fromfile and errored
# in some 1,700 tests).
import io
import sys

if len(sys.argv) > 1:   # run against another implementation (CPython check)
    import importlib.util
    spec = importlib.util.spec_from_file_location("array", sys.argv[1])
    array_mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(array_mod)
    array = array_mod.array
else:
    from array import array

a = array('i', [1, -2, 3])
assert a.tolist() == [1, -2, 3] and a.itemsize == 4 and a.typecode == 'i'
assert len(a.tobytes()) == 12
b = array('i')
b.frombytes(a.tobytes())
assert b == a and b[1] == -2 and b[-1] == 3
a.append(4)
a.extend([5, 6])
assert a.tolist() == [1, -2, 3, 4, 5, 6]
assert a[1:3] == array('i', [-2, 3])
a[0] = 10
del a[1]
assert a.tolist() == [10, 3, 4, 5, 6]
assert a.pop() == 6 and a.index(4) == 2 and a.count(3) == 1
a.reverse()
assert a.tolist() == [5, 4, 3, 10]
assert repr(array('d')) == "array('d')"
assert repr(array('h', [1, 2])) == "array('h', [1, 2])"

big = array('i', [1, 256])
big.byteswap()
assert big.tolist() == [16777216, 65536], big.tolist()

f = io.BytesIO(array('H', [7, 8, 9]).tobytes())
c = array('H')
c.fromfile(f, 2)
assert c.tolist() == [7, 8]
try:
    c.fromfile(f, 5)
except EOFError:
    assert c.tolist() == [7, 8, 9]
else:
    raise AssertionError("short fromfile did not raise EOFError")
out = io.BytesIO()
c.tofile(out)
assert out.getvalue() == c.tobytes()

for code, bad in (('b', 128), ('B', -1), ('h', 1 << 15)):
    try:
        array(code, [bad])
    except OverflowError:
        pass
    else:
        raise AssertionError("no OverflowError for %s %d" % (code, bad))
try:
    array('i', [1.5])
except TypeError:
    pass
else:
    raise AssertionError("float accepted for 'i'")
assert array('f', [1, 2.5]).tolist() == [1.0, 2.5]
assert array('u', 'abc').tounicode() == 'abc'
assert array('B', b'\x01\x02').tolist() == [1, 2]
assert array('i', [1, 2]) + array('i', [3]) == array('i', [1, 2, 3])
assert array('b', [1]) * 3 == array('b', [1, 1, 1])
# Bytes-like consumers read an array through its buffer.
if len(sys.argv) == 1:
    import base64, binascii, struct
    raw = array('B', b'abc')
    assert bytes(raw) == b'abc'
    assert memoryview(raw).tobytes() == b'abc'
    assert memoryview(array('i', [1])).format == 'i'
    assert base64.b64encode(raw) == b'YWJj'
    assert base64.b64decode(array('B', b'YWJj')) == b'abc'
    assert binascii.hexlify(raw) == b'616263'
    assert struct.unpack('=i', array('i', [7])) == (7,)
# bytes.join drives its iterable with the iterator protocol (byteswap joins
# a generator) and takes only bytes-like items.
assert b'-'.join(x for x in [b'a', bytearray(b'b'), memoryview(b'c')]) == b'a-b-c'
for bad in ([1], ['a'], [None]):
    try:
        b''.join(bad)
    except TypeError:
        pass
    else:
        raise AssertionError("bytes.join accepted %r" % (bad,))
print("array_module: ok")
