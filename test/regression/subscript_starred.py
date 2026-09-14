# Regression test: starred expressions in subscripts (PEP 646).
#
# parseSubscript only accepted expressions and slices, so `x[*a]`,
# `x[*a, b]` and `Generic[T, *Ts]` failed with "expected expression, but got
# '*'".  A starred element makes the index a tuple: `x[*a, b]` is
# `x[(*a, b)]`, and `x[*a]` is `x[(*a,)]`.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


class Probe:
    def __getitem__(self, key):
        return key


p = Probe()
a = (1, 2)

check("x[*a]", p[*a], (1, 2))
check("x[*a, b]", p[*a, 3], (1, 2, 3))
check("x[b, *a]", p[0, *a], (0, 1, 2))
check("x[*a, *a]", p[*a, *a], (1, 2, 1, 2))
check("x[*()]", p[*()], ())
check("single starred is a tuple", p[*[5]], (5,))
check("same as the parenthesized tuple", p[*a, 3], p[(*a, 3)])
check("trailing comma", p[*a,], (1, 2))
check("slice next to a starred element", p[1:2, *a], (slice(1, 2, None), 1, 2))
check("starred generator", p[*(i for i in range(3))], (0, 1, 2))
check("plain index unchanged", p[1], 1)
check("plain tuple unchanged", p[1, 2], (1, 2))

d = {}
d[*a] = "set"
check("store", d, {(1, 2): "set"})
d[*a] += "!"
check("augmented store", d, {(1, 2): "set!"})
del d[*a]
check("delete", d, {})

from typing import Generic, TypeVar, TypeVarTuple

T = TypeVar("T")
Ts = TypeVarTuple("Ts")


class G(Generic[T, *Ts]):
    pass


(base,) = G.__orig_bases__
check("Generic[T, *Ts] origin", base.__origin__ is Generic, True)
check("Generic[T, *Ts] arity", len(base.__args__), 2)
check("Generic[T, *Ts] first argument", base.__args__[0] is T, True)

print("subscript starred OK")
