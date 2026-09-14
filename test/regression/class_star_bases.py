# Regression test: class statements accept *bases and **keywords.
#
# The parser only accepted plain expressions and name=value pairs between the
# parentheses of a class statement, so `class C(*bases)` was a SyntaxError
# ("expected expression, but got '*'") and `class D(A, **kw)` never parsed.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


class A:
    pass


class B:
    pass


bases = (A, B)


class C(*bases):
    pass


check("starred bases", C.__bases__, (A, B))
check("starred bases mro", C.__mro__, (C, A, B, object))


class E(*[A], *(B,)):
    pass


check("two starred bases", E.__bases__, (A, B))


class F(A, *(B,)):
    pass


check("plain then starred", F.__bases__, (A, B))


class Meta(type):
    seen = None

    def __new__(mcls, name, bases, ns, **kwargs):
        Meta.seen = kwargs
        return super().__new__(mcls, name, bases, ns)

    def __init__(cls, name, bases, ns, **kwargs):
        super().__init__(name, bases, ns)


kw = {"metaclass": type}


class D(A, **kw):
    pass


check("double-starred metaclass", type(D), type)
check("double-starred bases", D.__bases__, (A,))


class G(B, metaclass=Meta, **{"flag": 1}):
    pass


check("keyword plus double-starred", type(G), Meta)
check("double-starred extra kwargs reach metaclass", Meta.seen, {"flag": 1})


class H(**{"metaclass": Meta, "other": 2}):
    pass


check("metaclass only via **", type(H), Meta)
check("remaining kwargs", Meta.seen, {"other": 2})

empty = ()


class I(*empty):
    pass


check("empty starred bases", I.__bases__, (object,))

print("class star bases OK")
