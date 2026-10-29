# vars(x) raises TypeError when x has no __dict__ (built-in instances,
# __slots__ classes, None); AttributeError from a __dict__ property means the
# same, and any other exception propagates.
class S:
    __slots__ = ("x",)


class P:
    @property
    def __dict__(self):
        raise ValueError("boom")


class Q:
    @property
    def __dict__(self):
        raise AttributeError("nope")


MSG = "vars() argument must have __dict__ attribute"
for v in (5, 2.5, True, "s", b"b", (1,), [1], {1: 2}, {1}, frozenset(), None, object(), S(), Q()):
    try:
        vars(v)
    except TypeError as e:
        assert str(e) == MSG, (type(v).__name__, str(e))
    else:
        raise AssertionError("vars(%s) did not raise TypeError" % type(v).__name__)

try:
    vars(P())
    raise AssertionError("vars(P()) did not propagate ValueError")
except ValueError as e:
    assert str(e) == "boom"


class C:
    pass


c = C()
c.k = 1
assert vars(c) == {"k": 1}
assert isinstance(vars(C), type(vars(type)))   # a class's namespace (mappingproxy)
import types
m = types.ModuleType("m")
m.q = 2
assert vars(m)["q"] == 2
for v in ([1], 5, None):
    try:
        v.__dict__
    except AttributeError:
        pass
    else:
        raise AssertionError("%r.__dict__ did not raise AttributeError" % (v,))
print("vars_without_dict ok")
