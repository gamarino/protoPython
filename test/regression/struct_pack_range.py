# struct.pack checks the value against the format code, as CPython does.
# Out-of-range and non-integer values were truncated or packed as 0:
# pack("!I", 2**32) gave b"\0\0\0\0", so base64.b85decode never reported
# an overflow.
import struct


def error(fmt, value):
    try:
        struct.pack(fmt, value)
    except struct.error:
        return True
    return False


assert struct.pack("!I", 2**32 - 1) == b"\xff\xff\xff\xff"
assert struct.pack("!i", -(2**31)) == b"\x80\x00\x00\x00"
assert struct.pack("!Q", 2**64 - 1) == b"\xff" * 8
assert struct.pack("!q", -(2**63)) == b"\x80" + b"\x00" * 7
assert struct.pack("B", True) == b"\x01"
assert struct.pack("!d", 1) == struct.pack("!d", 1.0)
assert struct.pack("!H", 65535) == b"\xff\xff"


class Index:
    def __index__(self):
        return 7


assert struct.pack("B", Index()) == b"\x07"

for fmt, value in [("!I", 2**32), ("!I", -1), ("B", 256), ("b", 128), ("b", -129),
                   ("!H", 70000), ("!i", 2**31), ("!Q", 2**64), ("!q", 2**63),
                   ("!I", 3.0), ("!I", "1"), ("!I", None), ("!d", "x")]:
    assert error(fmt, value), (fmt, value)

try:
    struct.pack("!I", 2**32)
except struct.error as e:
    assert str(e) == "'I' format requires 0 <= number <= 4294967295", str(e)

import base64

for data in (b"|", b"|NsC"):
    try:
        base64.b85decode(data)
    except ValueError:
        pass
    else:
        raise AssertionError(f"b85decode({data!r}) did not overflow")
print("struct_pack_range: ok")
