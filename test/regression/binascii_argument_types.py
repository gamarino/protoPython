# binascii takes bytes-like data, and an a2b_* function also an ASCII str.
# Anything else was converted to empty data: hexlify([]) gave b"",
# crc32(3) gave 0 and base64.b16encode("abc") encoded the str.
import array
import base64
import binascii


def raises(exc, f, *args):
    try:
        f(*args)
    except exc:
        return
    raise AssertionError(f"{f.__name__}{args!r} did not raise {exc.__name__}")


for f in (binascii.hexlify, binascii.b2a_hex, binascii.b2a_base64, binascii.crc32):
    for bad in ("abc", [], 3, None, True, 1.5):
        raises(TypeError, f, bad)
for f in (binascii.unhexlify, binascii.a2b_hex, binascii.a2b_base64):
    for bad in ([], 3, None, 1.5):
        raises(TypeError, f, bad)
    raises(ValueError, f, "\xe9\xe9")
for f in (base64.b64encode, base64.b16encode, base64.b32encode, base64.encodebytes):
    raises(TypeError, f, "str")
    raises(TypeError, f, [])

assert binascii.hexlify(b"\x01\xff") == b"01ff"
assert binascii.hexlify(bytearray(b"ab")) == b"6162"
assert binascii.hexlify(memoryview(b"ab")) == b"6162"
assert binascii.hexlify(array.array("B", b"ab")) == b"6162"
assert binascii.unhexlify("6162") == b"ab" and binascii.unhexlify(b"6162") == b"ab"
assert binascii.a2b_base64("YWI=") == b"ab"
assert binascii.b2a_base64(b"ab") == b"YWI=\n"
assert binascii.crc32(b"abc") == 891568578
print("binascii_argument_types: ok")
