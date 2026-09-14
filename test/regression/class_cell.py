# Regression test: zero-argument `__class__` inside methods.
#
# CPython gives every function nested in a class body an implicit __class__
# cell holding the class the statement creates.  protoPython compiled a free
# `__class__` in a method as LOAD_GLOBAL, which found the module object's own
# __class__, so `class A: def f(self): return __class__` returned `object`.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


class A:
    def f(self):
        return __class__

    def via_lambda(self):
        return (lambda: __class__)()

    def via_inner(self):
        def inner():
            return __class__
        return inner()

    def via_comprehension(self):
        return [__class__ for _ in range(1)][0]

    @classmethod
    def cm(cls):
        return __class__

    @staticmethod
    def sm():
        return __class__

    get = lambda self: __class__


class B(A):
    def g(self):
        return __class__


check("method", A().f() is A, True)
check("inherited method keeps the defining class", B().f() is A, True)
check("subclass method", B().g() is B, True)
check("lambda in a method", A().via_lambda() is A, True)
check("def nested in a method", A().via_inner() is A, True)
check("comprehension in a method", A().via_comprehension() is A, True)
check("classmethod", A.cm() is A, True)
check("staticmethod", A.sm() is A, True)
check("lambda in the class body", A().get() is A, True)


def factory():
    class Local:
        def m(self):
            return __class__
    return Local


L1 = factory()
L2 = factory()
check("class defined in a function", L1().m() is L1, True)
check("each class statement has its own cell", (L2().m() is L2, L1 is L2), (True, False))


class Outer:
    class Inner:
        def m(self):
            return __class__

    def m(self):
        return __class__


check("method of a nested class", Outer.Inner().m() is Outer.Inner, True)
check("method of the outer class", Outer().m() is Outer, True)


class Host:
    def make(self):
        class Guest:
            seen = __class__

            def m(self):
                return __class__
        return Guest


G = Host().make()
check("class body in a method sees the method's __class__", G.seen is Host, True)
check("method of a class defined in a method", G().m() is G, True)


class Renamed:
    def m(self):
        return __class__


Original = Renamed
Renamed = None
check("cell does not depend on the class name", Original().m() is Original, True)


class WithSuper(A):
    def f(self):
        return (super().f(), __class__)


check("super() and __class__ together", WithSuper().f(), (A, WithSuper))


class Shadow:
    def m(self):
        __class__ = "local"
        return __class__


check("assigned __class__ is a plain local", Shadow().m(), "local")

print("class cell OK")
