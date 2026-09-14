"""A class that defines __eq__ without __hash__ gets __hash__ = None, like CPython."""


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


class A:
    def __eq__(self, other):
        return self is other


class B(A):
    pass


class C:
    def __eq__(self, other):
        return self is other

    def __hash__(self):
        return 1


class D(A):
    __hash__ = object.__hash__


class E:
    pass


assert A.__hash__ is None
assert raises(TypeError, lambda: hash(A()))
assert raises(TypeError, lambda: {A()})
assert raises(TypeError, lambda: {A(): 1})
assert B.__hash__ is None and raises(TypeError, lambda: hash(B()))
assert hash(C()) == 1 and len({C(), C()}) == 1
d = D()
assert hash(d) == hash(d) and {d: 1}[d] == 1
e = E()
assert {e: 1}[e] == 1

X = type("X", (), {"__eq__": lambda self, other: self is other})
assert X.__hash__ is None

print("class eq unhashable OK")
