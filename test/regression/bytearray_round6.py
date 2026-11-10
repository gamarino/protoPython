# bytearray as CPython's own mutable type (1.2.2, review round 6): writes
# from several threads are not lost, every bytes-like consumer accepts it,
# copy / deepcopy / pickle round-trip it, unbound methods check their
# receiver, and the method set is CPython's.
#
# Each section is compiled and run on its own, so each fails alone on a build
# without its fix.

SECTIONS = {}
SECTIONS['N1_concurrent_appends'] = r'''
import threading
# N1: concurrent appends publish through a CAS on the storage; none is lost.
ba = bytearray()
N = 5000
def writer():
    for _ in range(N):
        ba.append(1)
threads = [threading.Thread(target=writer) for _ in range(4)]
for t in threads:
    t.start()
for t in threads:
    t.join()
assert len(ba) == 4 * N, len(ba)
assert sum(ba) == 4 * N
'''

SECTIONS['N2_bytes_like'] = r'''
import io
import struct
# N2: bytes-like wherever CPython takes one.
assert str(bytearray(b'hi'), 'ascii') == 'hi'
assert str(memoryview(b'hi'), 'utf-8') == 'hi'
try:
    str(5, 'ascii')
except TypeError as e:
    assert "decoding to str: need a bytes-like object, int found" in str(e), e
else:
    raise AssertionError("str(str, enc) did not raise")
assert bytearray(b'a') + memoryview(b'b') == b'ab'
assert b'a' + bytearray(b'b') == b'ab' and type(b'a' + bytearray(b'b')) is bytes
assert int(bytearray(b'42')) == 42 and float(bytearray(b'1.5')) == 1.5
assert ord(bytearray(b'a')) == 97
bio = io.BytesIO(b'hello')
buf = bytearray(3)
assert bio.readinto(buf) == 3 and buf == b'hel'
sb = bytearray(4)
struct.pack_into('>H', sb, 1, 258)
assert sb == b'\x00\x01\x02\x00'
try:
    struct.pack_into('>I', sb, 2, 1)
except struct.error:
    pass
else:
    raise AssertionError("pack_into past the end did not raise")
'''

SECTIONS['memoryview_write_through'] = r'''
# memoryview over a bytearray writes through; over bytes it is read-only.
x = bytearray(b'abc')
m = memoryview(x)
m[0] = 0x41
assert x == b'Abc' and m[1] == 0x62 and type(m.tobytes()) is bytes
m[1:3] = b'YZ'
assert x == b'AYZ'
try:
    memoryview(b'ab')[0] = 1
except TypeError as e:
    assert "read-only" in str(e), e
else:
    raise AssertionError("write through a bytes memoryview")
'''

SECTIONS['codecs'] = r'''
# decode / encode with error handlers.
assert b'\xffa'.decode('utf-8', 'replace') == '�a'
assert b'\xffa'.decode('utf-8', 'ignore') == 'a'
assert b'\xffa'.decode('utf-8', 'backslashreplace') == '\\xffa'
assert b'\xffa'.decode('utf-8', 'surrogateescape') == '\udcffa'
try:
    b'\xffa'.decode('utf-8')
except UnicodeDecodeError as e:
    assert e.start == 0 and e.end == 1 and e.encoding == 'utf-8', (e.start, e.end, e.encoding)
    assert e.reason == 'invalid start byte', e.reason
else:
    raise AssertionError("invalid utf-8 decoded")
assert 'a\xe9'.encode('ascii', 'xmlcharrefreplace') == b'a&#233;'
assert 'a\udcff'.encode('utf-8', 'surrogateescape') == b'a\xff'
try:
    'x\xe9'.encode('ascii')
except UnicodeEncodeError as e:
    assert e.start == 1 and e.end == 2, (e.start, e.end)
else:
    raise AssertionError("non-ascii encoded as ascii")
e = UnicodeDecodeError('utf-8', b'xy', 0, 1, 'bad')
assert str(e) == "'utf-8' codec can't decode byte 0x78 in position 0: bad", str(e)
'''

SECTIONS['N3_copy_pickle'] = r'''
import copy
import pickle
# N3: copy, deepcopy and pickle.
y = bytearray(b'\xffab')
assert copy.copy(y) == y and copy.copy(y) is not y and type(copy.copy(y)) is bytearray
assert copy.deepcopy(y) == y and type(copy.deepcopy(y)) is bytearray
for proto in range(pickle.HIGHEST_PROTOCOL + 1):
    z = pickle.loads(pickle.dumps(y, proto))
    assert z == y and type(z) is bytearray, (proto, z)
assert y.__reduce_ex__(2) == (bytearray, ('\xffab', 'latin-1'), None)
assert y.__reduce_ex__(3) == (bytearray, (b'\xffab',), None)
class Sub(bytearray):
    pass
s = Sub(b'q')
s.tag = 5
s2 = pickle.loads(pickle.dumps(s, 2))
assert type(s2) is Sub and s2 == b'q' and s2.tag == 5
s3 = copy.deepcopy(s)
assert type(s3) is Sub and s3.tag == 5
'''

SECTIONS['M2_receiver_check'] = r'''
# M2: an unbound method checks its receiver.
try:
    bytearray.append(b'x', 1)
except TypeError as e:
    assert "descriptor 'append' for 'bytearray' objects doesn't apply to a 'bytes' object" in str(e), e
else:
    raise AssertionError("bytearray.append accepted bytes")
'''

SECTIONS['M5_method_set'] = r'''
# M5: the rest of the method set.
c = bytearray(b'ab')
c *= 3
assert c == b'ababab' and type(c) is bytearray
assert c.rsplit(b'b') == [bytearray(b'a'), bytearray(b'a'), bytearray(b'a'), bytearray(b'')]
assert bytearray(b'a\tb').expandtabs() == b'a       b'
assert bytearray(b'%d') % 5 == b'5'
assert type(bytearray.maketrans(b'a', b'b')) is bytes
z = bytearray(5)
z.resize(2)
assert z == b'\x00\x00' and z.__alloc__() >= 2
w = bytearray(b'abc')
w.insert(0, 0x7a)
w.remove(0x62)
w.reverse()
assert w == b'caz'
assert bytearray(b"it's") == b"it's" and repr(bytearray(b"it's")) == "bytearray(b\"it\\'s\")"
class Sub(bytearray):
    pass
assert type(Sub.fromhex('61')) is Sub
'''

failed = []
for name, code in SECTIONS.items():
    try:
        # In this module's namespace, so pickle finds the classes a section defines.
        exec(compile(code, "<%s>" % name, "exec"), globals())
    except BaseException as e:
        failed.append(name)
        print("section %s: FAIL %s: %s" % (name, type(e).__name__, e))
if failed:
    print("bytearray_round6: failed", failed)
else:
    print("bytearray_round6: ok")
