# struct.error is an Exception subclass of its own, not ValueError.
import struct

assert issubclass(struct.error, Exception)
assert not issubclass(struct.error, ValueError)
assert struct.error.__name__ == "error" and struct.error.__module__ == "struct"

for call in (lambda: struct.pack("Q", -1),
             lambda: struct.unpack("i", b"\x00"),
             lambda: struct.calcsize("y")):
    try:
        call()
    except struct.error as e:
        assert type(e) is struct.error, type(e)
    else:
        raise AssertionError("no struct.error")

# A ValueError is not a struct.error.
try:
    raise ValueError("x")
except struct.error:
    raise AssertionError("ValueError caught as struct.error")
except ValueError:
    pass

print("struct_error_class: ok")
