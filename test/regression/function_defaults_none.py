# A function without defaults answers None for __defaults__ and
# __kwdefaults__ (CPython); with defaults, the tuple and the dict; both can
# be assigned and the call uses the new values.
def none():
    pass


def pos(a, b=2, c=3):
    return a, b, c


def kw(a, *, k=5, j):
    return a, k, j


assert none.__defaults__ is None and none.__kwdefaults__ is None
assert pos.__defaults__ == (2, 3) and pos.__kwdefaults__ is None
assert kw.__defaults__ is None and kw.__kwdefaults__ == {"k": 5}
assert (lambda: 0).__defaults__ is None
assert (lambda x=1: x).__defaults__ == (1,)
assert "__defaults__" not in vars(none)

pos.__defaults__ = (20, 30)
assert pos(1) == (1, 20, 30)
none.__defaults__ = None
assert none.__defaults__ is None


class C:
    def m(self, x=4):
        return x


assert C.m.__defaults__ == (4,) and C().m.__defaults__ == (4,)
assert C().m.__kwdefaults__ is None
# A bound method reads what its type does not define from the function.
bm = C().m
assert bm.__name__ == "m" and bm.__qualname__ == "C.m", (bm.__name__, bm.__qualname__)
C.m.tag = "t"
assert bm.tag == "t"
try:
    bm.no_such_attribute
except AttributeError as e:
    assert "'function' object has no attribute 'no_such_attribute'" in str(e), str(e)
else:
    raise AssertionError("missing attribute of a bound method")
import inspect
assert str(inspect.signature(pos)) == "(a, b=20, c=30)"
print("function_defaults_none: ok")
