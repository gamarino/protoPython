# bytearray is its own mutable type, not a bytes object.
ba = bytearray(b'ab')
assert type(ba) is bytearray
assert isinstance(ba, bytearray)
assert not isinstance(ba, bytes)
assert not isinstance(b'ab', bytearray)
assert repr(ba) == "bytearray(b'ab')"
assert str(bytearray(b'x')) == "bytearray(b'x')"
assert repr(bytearray()) == "bytearray(b'')"
try:
    hash(ba)
except TypeError:
    pass
else:
    raise AssertionError("bytearray is hashable")
try:
    {ba: 1}
except TypeError:
    pass
else:
    raise AssertionError("bytearray accepted as a dict key")
assert bytearray.__hash__ is None
assert bytearray.__mro__ == (bytearray, object)
assert issubclass(bytearray, object) and not issubclass(bytearray, bytes)

ba[0] = 0x41
assert ba == bytearray(b'Ab') and ba == b'Ab'
ba += b'cd'
assert ba == b'Abcd' and type(ba) is bytearray
ba.extend(b'e')
ba.append(0x66)
assert bytes(ba) == b'Abcdef'
assert ba.pop() == 0x66 and ba == b'Abcde'
del ba[0]
assert ba == b'bcde'
ba[1:3] = b'XY'
assert ba == b'bXYe'
assert len(ba) == 4 and ba[0] == 0x62 and list(ba) == [0x62, 0x58, 0x59, 0x65]
assert ba.decode() == 'bXYe'
assert ba.upper() == b'BXYE'
assert 0x58 in ba and b'XY' in ba
assert ba.find(b'Y') == 2
assert bytes(ba) == b'bXYe' and type(bytes(ba)) is bytes
assert b'q' + bytes(ba) == b'qbXYe'
ba.clear()
assert ba == bytearray()
assert bytearray(3) == b'\x00\x00\x00'
assert bytearray([1, 2]) == b'\x01\x02'
assert bytearray(x for x in (65, 66)) == b'AB'

class Sub(bytearray):
    pass
s = Sub(b'q')
assert type(s) is Sub and isinstance(s, bytearray) and not isinstance(s, bytes)
assert s == b'q'

# results and constructor forms
b = bytearray(x for x in [1, 2, 3])
assert b == bytearray(b'\x01\x02\x03'), b
assert b == b'\x01\x02\x03' and b'\x01\x02\x03' == b
assert type(b + b'x') is bytearray, type(b + b'x')
assert type(b'x' + b) is bytes, type(b'x' + b)
assert bytes(b) == b'\x01\x02\x03'
class S(bytearray): pass
s = S(b'ab')
assert type(s) is S and isinstance(s, bytearray) and s == b'ab'
assert repr(s) == "S(b'ab')", repr(s)
try:
    bytearray(b'a', foo=1)
except TypeError:
    pass
else:
    raise AssertionError("unknown keyword accepted")
assert b.decode() == '\x01\x02\x03'
assert bytearray(3) == b'\0\0\0'
assert b'ab' in [bytearray(b'ab')]
assert bytearray(b'abc').upper() == b'ABC'
assert len(bytearray(b'abc')) == 3 and bytearray(b'abc')[1] == 98
assert bytearray(b'abc')[1:] == b'bc'
m = memoryview(bytearray(b'xy'))
assert bytes(m) == b'xy'
assert type(bytearray(b'ab').upper()) is bytearray
assert [type(x) for x in bytearray(b'a b').split()] == [bytearray, bytearray]
assert type(bytearray(b'abc')[1:]) is bytearray
assert type(bytearray(b'ab') * 2) is bytearray

# bytes-like uses of a bytearray
import io
assert int(bytearray(b'1f'), 16) == 31 and int(bytearray(b'12')) == 12
assert float(bytearray(b'1.5')) == 1.5
assert ord(bytearray(b'a')) == 97
assert list(bytearray(b'ab')) == [97, 98] and tuple(bytearray(b'ab')) == (97, 98)
assert repr(bytearray) == "<class 'bytearray'>", repr(bytearray)
f = bytearray.fromhex('6162')
assert f == b'ab' and type(f) is bytearray, type(f)
try:
    io.StringIO().write(bytearray(b'x'))
except TypeError as e:
    assert "bytearray" in str(e), str(e)
else:
    raise AssertionError("StringIO.write accepted a bytearray")
class S(bytearray):
    pass
s = S(b'x')
s.tag = 1
assert s.tag == 1
import weakref
try:
    weakref.ref(bytearray())
except TypeError:
    pass
else:
    raise AssertionError("weak reference to a bytearray")
print("bytearray_type: ok")
