# Regression test: augmented assignment calls the class's in-place dunder.
#
# INPLACE_AND, _OR, _XOR, _LSHIFT, _RSHIFT, _MODULO, _FLOOR_DIVIDE and
# _TRUE_DIVIDE called `__iand__` ... only when it was a native method: one
# defined in Python was ignored and the binary operator raised TypeError
# ("unsupported operand type(s) for &").  The division forms also kept a
# NotImplemented result instead of falling back to the binary operator.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


class C:
    def __iand__(self, o): return ("iand", o)
    def __ior__(self, o): return ("ior", o)
    def __ixor__(self, o): return ("ixor", o)
    def __ilshift__(self, o): return ("ilshift", o)
    def __irshift__(self, o): return ("irshift", o)
    def __imod__(self, o): return ("imod", o)
    def __ifloordiv__(self, o): return ("ifloordiv", o)
    def __itruediv__(self, o): return ("itruediv", o)


for op, name in (("&=", "iand"), ("|=", "ior"), ("^=", "ixor"), ("<<=", "ilshift"),
                 (">>=", "irshift"), ("%=", "imod"), ("//=", "ifloordiv"), ("/=", "itruediv")):
    ns = {"C": C}
    exec("x = C()\nx %s 7" % op, ns)
    check(op, ns["x"], (name, 7))


def in_function():
    x = C()
    x &= 1
    y = C()
    y /= 2
    return x, y


check("in a function", in_function(), (("iand", 1), ("itruediv", 2)))


class Fallback:
    # NotImplemented from the in-place method falls back to the binary one.
    def __itruediv__(self, o): return NotImplemented
    def __truediv__(self, o): return ("truediv", o)
    def __imod__(self, o): return NotImplemented
    def __mod__(self, o): return ("mod", o)


f = Fallback()
f /= 3
check("/= NotImplemented", f, ("truediv", 3))
g = Fallback()
g %= 4
check("%= NotImplemented", g, ("mod", 4))

# Built-in types keep working.
s = {1, 2}
s &= {2, 3}
check("set &=", s, {2})
v = 7
v //= 2
v %= 3
check("int //= and %=", v, 0)
t = "a%s"
t %= "b"
check("str %=", t, "ab")
print("inplace_user_dunders: ok")
