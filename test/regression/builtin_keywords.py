# The builtins take their keyword arguments as CPython 3.14's signatures
# declare them: a parameter that may come by keyword is honoured
# (round(x, ndigits=2) used to round to an int, pow(2, 5, mod=3) ignored the
# modulus), and an unknown keyword, or any keyword for a builtin that takes
# none, raises TypeError instead of being ignored.
import io


def raises(fn, exc=TypeError):
    try:
        fn()
    except exc:
        return True
    return False


# Parameters given by keyword.
assert round(2.567, ndigits=2) == 2.57
assert round(number=2.567) == 3
assert pow(2, 5, mod=3) == 2
assert pow(base=2, exp=5) == 32
assert int("ff", base=16) == 255
assert sum([1, 2], start=10) == 13
assert list(enumerate("ab", start=1)) == [(1, "a"), (2, "b")]
assert list(enumerate(iterable="ab")) == [(0, "a"), (1, "b")]
assert eval("x", globals={"x": 3}) == 3
g = {}
exec("y = 1", globals=g)
assert g["y"] == 1
exec("z = 1", {}, closure=None)
assert type(compile(source="1", filename="f", mode="eval")).__name__ == "code"
assert bytes("a", encoding="ascii") == b"a"
assert bytes(source=b"ab") == b"ab"
assert len(bytearray(source=b"abc")) == 3
assert memoryview(object=b"a").tobytes() == b"a"
assert __import__(name="os", fromlist=["path"]).__name__ == "os"
assert list(zip([1], [2], strict=True)) == [(1, 2)]
assert raises(lambda: list(zip([1], [2, 3], strict=True)), ValueError)
assert sorted([3, 1], key=lambda v: -v) == [3, 1]
assert max([], default=7) == 7
print("x", file=io.StringIO(), flush=True)

# A parameter given twice.
assert raises(lambda: round(1.5, number=1))

# Unknown keywords.
for call in (
    lambda: round(1.5, bogus=1),
    lambda: pow(2, 3, bogus=1),
    lambda: sum([], bogus=1),
    lambda: sorted([], bogus=1),
    lambda: max([1], bogus=1),
    lambda: print("x", bogus=1, file=io.StringIO()),
    lambda: int("1", bogus=1),
    lambda: enumerate([], bogus=1),
    lambda: map(len, [], bogus=1),
    lambda: zip([], bogus=1),
    lambda: exec("pass", {}, closure=(1,)),
):
    assert raises(call), call

# Builtins that take no keywords.
for call in (
    lambda: next(iter([1]), default=0),
    lambda: dir(o=1),
    lambda: vars(object=1),
    lambda: len(obj=[1]),
    lambda: divmod(a=1, b=2),
    lambda: frozenset(iterable=[1]),
    lambda: float(x="1"),
    lambda: bool(x=1),
    lambda: slice(stop=1),
    lambda: reversed(sequence=[1]),
    lambda: filter(function=None, iterable=[1]),
    lambda: type(object=1),
    lambda: isinstance(obj=1, class_or_tuple=int),
    lambda: hash(obj=1),
):
    assert raises(call), call

# A subclass's __init__ still takes its own keywords.
class F(frozenset):
    def __init__(self, items, tag=None):
        self.tag = tag


assert F([1], tag="t").tag == "t"


class B(bytearray):
    def __init__(self, source, extra=None):
        super().__init__(source)
        self.extra = extra


assert B(b"ab", extra=1).extra == 1

print("builtin_keywords: ok")
