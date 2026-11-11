# bytes() and bytearray() encoding and errors parameters (CPython 3.14).
def raises(fn, exc=TypeError):
    try:
        fn()
    except exc:
        return True
    return False

assert bytes('\xe9', 'latin-1') == b'\xe9'
assert bytes('\xe9', encoding='latin-1') == b'\xe9'
assert bytes('\xe9', 'utf-8') == b'\xc3\xa9'
assert bytes(source='ab', encoding='ascii') == b'ab'
assert bytes('a\xe9', 'ascii', 'ignore') == b'a'
assert bytes('a\xe9', 'ascii', errors='replace') == b'a?'
assert raises(lambda: bytes("a\xe9", "ascii"), ValueError)  # UnicodeEncodeError in CPython
assert raises(lambda: bytes('abc'))
assert raises(lambda: bytes('abc', errors='strict'))
assert raises(lambda: bytes(b'abc', 'utf-8'))
assert raises(lambda: bytes(3, encoding='utf-8'))
assert raises(lambda: bytes([1], errors='strict'))
assert raises(lambda: bytes(encoding='utf-8'))
assert raises(lambda: bytes(b'a', bogus=1))

assert bytearray('\xe9', 'latin-1') == bytearray(b'\xe9')
assert bytearray('\xe9', encoding='latin-1') == bytearray(b'\xe9')
assert bytearray('a\xe9', 'ascii', 'replace') == bytearray(b'a?')
assert raises(lambda: bytearray('abc'))
assert raises(lambda: bytearray('abc', errors='strict'))
assert raises(lambda: bytearray(b'abc', 'utf-8'))
assert raises(lambda: bytearray(b'a', bogus=1))
assert raises(lambda: bytearray(source=b'a', bogus=1))
assert bytearray(b'ab') == bytearray(b'ab')
ba = bytearray()
bytearray.__init__(ba, '\xe9', encoding='latin-1')
assert ba == bytearray(b'\xe9')
assert raises(lambda: bytearray.__init__(bytearray(), b'a', bogus=1))

class Sub(bytearray):
    def __init__(self, src, tag=None):
        super().__init__(src)
        self.tag = tag
s = Sub(b'xy', tag=3)
assert s.tag == 3 and bytes(s) == b'xy'
print("bytes_constructor_encoding: ok")
