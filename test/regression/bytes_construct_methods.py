# bytes / bytearray built from iterables, and bytes methods hmac and other
# library code use (ljust, translate with a bytes table, ...).
# bytes_construct_methods.expected is CPython's output.
for label, make in [("list", lambda: bytes([1, 2])), ("range", lambda: bytes(range(3))),
                    ("generator", lambda: bytes(x for x in range(3))), ("iter", lambda: bytes(iter([1, 2]))),
                    ("map", lambda: bytes(map(int, "12"))), ("tuple", lambda: bytes((1, 2))),
                    ("table", lambda: bytes((x ^ 0x5C) for x in range(256))[250:])]:
    print(label, make())
for label, make in [("list", lambda: bytearray([1, 2])), ("range", lambda: bytearray(range(3))),
                    ("generator", lambda: bytearray(x for x in range(3))), ("bytes", lambda: bytearray(b"ab"))]:
    print("bytearray", label, list(make()))
for bad in ([256], [-1], ["a"], (x for x in [1, 300])):
    try:
        bytes(bad)
    except (ValueError, TypeError) as e:
        print(type(e).__name__)

t = bytes((x ^ 0x5C) for x in range(256))
k = b"key".ljust(8, b"\0")
print(k, k.translate(t), b"abc".translate(None, b"b"),
      b"hello".translate(bytes.maketrans(b"el", b"ip")))
try:
    b"x".translate(b"short")
except ValueError as e:
    print("ValueError", e)
print(b"ab".ljust(5), b"ab".rjust(5, b"-"), b"ab".center(7, b"*"), b"abc".center(6), b"abc".center(2))
print(b"abc1".isalnum(), b"ab c".isalnum(), b" \t\n".isspace(), b"".isspace(),
      b"abc1".islower(), b"ABC".isupper(), b"1".isupper(), b"Hello World".istitle(), b"hello World".istitle())
print(b"hELLO wORLD".capitalize())
