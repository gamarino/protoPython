# Regression test: shifts of ints of any size, in-place forms included.
#
# INPLACE_LSHIFT and INPLACE_RSHIFT computed on 64-bit integers:
# `x = 1; x <<= 70` gave 0 and `y = -8; y >>= 70` gave 0 (CPython:
# 1180591620717411303424 and -1); a bignum operand overflowed asLong.  The
# binary forms truncated a shift count above 2**31 to an int.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


def raises(label, exc_type, fn):
    try:
        fn()
    except exc_type:
        return
    raise AssertionError("%s: %s not raised" % (label, exc_type.__name__))


x = 1
x <<= 70
check("1 <<= 70", x, 1180591620717411303424)
y = -8
y >>= 70
check("-8 >>= 70", y, -1)
z = 8
z >>= 70
check("8 >>= 70", z, 0)
big = 3 ** 50
big <<= 3
check("bignum <<= 3", big, (3 ** 50) * 8)
big >>= 3
check("bignum >>= 3", big, 3 ** 50)
nb = -(3 ** 50)
nb >>= 100
check("negative bignum >>= 100", nb, -1)
t = True
t <<= 3
check("True <<= 3", t, 8)
s = 5
s <<= False
check("5 <<= False", s, 5)


def f(a, b):
    a <<= b
    return a


check("in a function", f(1, 64), 18446744073709551616)

check("1 << 70", 1 << 70, 1180591620717411303424)
check("-8 >> 70", -8 >> 70, -1)
check("1 >> 2**40", 1 >> 2 ** 40, 0)
check("-1 >> 2**40", -1 >> 2 ** 40, -1)
check("1 >> 2**100", 1 >> 2 ** 100, 0)
check("0 << 2**40", 0 << 2 ** 40, 0)
check("2**100 >> 2**33", (2 ** 100) >> 2 ** 33, 0)
raises("1 << 2**100", OverflowError, lambda: 1 << 2 ** 100)
raises("1 << -1", ValueError, lambda: 1 << -1)
raises("1 >> -1", ValueError, lambda: 1 >> -1)


def neg_inplace():
    v = 1
    v <<= -1


raises("v <<= -1", ValueError, neg_inplace)


class Shifty:
    def __ilshift__(self, other):
        return ("ilshift", other)

    def __rshift__(self, other):
        return ("rshift", other)


o = Shifty()
o <<= 2
check("__ilshift__", o, ("ilshift", 2))
o2 = Shifty()
o2 >>= 3
check(">>= falls back to __rshift__", o2, ("rshift", 3))
print("int_shift_big: ok")
