# Regression test: ints and floats that are equal behave as one value beyond
# the long long range too.
#
# - Comparing an int beyond long long with a float raised
#   "RuntimeError: LargeInteger value exceeds long long range", and ints
#   were compared with floats through a double, which rounds above 2**53.
# - A LargeInteger hashed only its lowest 64-bit digit, so 2**64, 2**65 and
#   2**70 had the same hash and a dict holding them kept a single entry.
# - Integral floats beyond long long were not mapped to the int they equal,
#   so {2**70: x}[2.0**70] raised KeyError and hash(2.0**70) != hash(2**70).


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


big = 2 ** 70
fbig = 2.0 ** 70

check("int == float beyond long long", big == fbig, True)
check("int + 1 != float", big + 1 == fbig, False)
check("int + 1 > float", big + 1 > fbig, True)
check("int < larger float", big < 2.0 ** 71, True)
check("2**53 + 1 != 2.0**53", 2 ** 53 + 1 == 2.0 ** 53, False)
check("negative beyond long long", -(2 ** 64) == -(2.0 ** 64), True)
check("int vs fractional float", 3 < 3.5, True)
check("int vs inf", 10 ** 400 < float("inf"), True)
check("true division of bigints", big / 2 ** 69, 2.0)
check("floor division with float", big // 2.0, 2.0 ** 69)

check("distinct multiples of 2**64 as keys", len({2 ** 64: "a", 2 ** 65: "b", 2 ** 70: "c"}), 3)
check("lookup among multiples", {2 ** 64: "a", 2 ** 65: "b"}[2 ** 65], "b")
check("low digit equal, high digit differs", len({2 ** 64 + 1: 1, 2 ** 128 + 1: 2}), 2)
check("hash of multiples differs", hash(2 ** 64) == hash(2 ** 65), False)

check("dict int key, float lookup", {big: "i"}[fbig], "i")
check("dict float key, int lookup", {fbig: "f"}[big], "f")
check("float in int-keyed dict", fbig in {big: 1}, True)
check("int and equal float share one key", len({big: 1, fbig: 2}), 1)
check("negative big float key", {-(2 ** 64): "n"}[-(2.0 ** 64)], "n")
check("huge float key", {int(1e300): "h"}[1e300], "h")
check("2**63 boundary", {2 ** 63: "e"}[2.0 ** 63], "e")
check("hash(int) == hash(equal float)", hash(big) == hash(fbig), True)
check("hash(2**63) == hash(2.0**63)", hash(2 ** 63) == hash(2.0 ** 63), True)
check("fractional float key", {2.5: "x"}[2.5], "x")

print("numeric keys OK")
