# Keyword arguments reach every callee under their names, whatever path the
# call took: literal keywords, `**mapping` with computed (non-literal) names,
# keywords forwarded through **kwargs, and built-ins that read a keyword or
# enumerate them (dict(), dict.update, complex(), str(), print(), split,
# __init_subclass__). protoCore keys a keyword by its interned name's
# pointer; protoPython keyed them by content hash, and several built-ins
# could only enumerate keywords through the interpreter's keyword-name stack.
import io


def kw(**kwargs):
    return kwargs


def named(a, b=0, *, c=0):
    return (a, b, c)


computed = "".join(["al", "pha"])
assert kw(**{computed: 1}) == {"alpha": 1}
assert named(**{"".join(["a"]): 1, "c": 3}) == (1, 0, 3)
assert named(1, **{"b" + "": 2}) == (1, 2, 0)


def forward(**kwargs):
    return named(**kwargs)


assert forward(a=5, c=6) == (5, 0, 6)

assert dict(**{computed: 1}, beta=2) == {"alpha": 1, "beta": 2}
d = {}
d.update(**{computed: 1}, gamma=3)
assert d == {"alpha": 1, "gamma": 3}
assert list(dict(z=1, a=2, m=3)) == ["z", "a", "m"]

assert complex(**{"re" + "al": 1, "imag": 2}) == 1 + 2j
try:
    complex(**{"bogus": 1})
except TypeError:
    pass
else:
    raise AssertionError("complex() accepted an unknown keyword")
assert str(**{"obj" + "ect": 5}) == "5"
try:
    str(**{"wrong": 1})
except TypeError:
    pass
else:
    raise AssertionError("str() accepted an unknown keyword")

buf = io.StringIO()
print("a", "b", **{"se" + "p": "-", "end": "!", "file": buf})
assert buf.getvalue() == "a-b!", buf.getvalue()
assert "a b c".split(**{"max" + "split": 1}) == ["a", "b c"]
assert sorted([3, 1, 2], **{"rev" + "erse": True}) == [3, 2, 1]


seen = []


class Base:
    def __init_subclass__(cls, **kwargs):
        seen.append(sorted(kwargs.items()))


class Sub(Base, **{"fla" + "g": 1}, other=2):
    pass


assert seen == [[("flag", 1), ("other", 2)]], seen


# A call with keywords inside a call that received none (the keyword-name
# stack of the outer call must not leak into the inner one).
def outer(**kwargs):
    return dict([(1, 2)]), kwargs


assert outer(x=1) == ({1: 2}, {"x": 1})

print("kwargs_name_pointer: ok")
