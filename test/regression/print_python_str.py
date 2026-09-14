"""print() and format() use a __str__ / __format__ written in Python, as str() does."""
import contextlib
import enum
import io


class Plain:
    def __str__(self):
        return "plain-str"

    def __repr__(self):
        return "plain-repr"


class Sub(Plain):
    pass


class Fmt:
    def __format__(self, spec):
        return "fmt[" + spec + "]"


class Color(enum.Enum):
    RED = 1


class Bad:
    def __str__(self):
        raise ValueError("no str")


buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    print(Plain(), Sub(), Color.RED)
assert buf.getvalue() == "plain-str plain-str Color.RED\n", repr(buf.getvalue())

assert format(Plain()) == "plain-str"
assert format(Color.RED) == "Color.RED"
assert format(Fmt(), "x") == "fmt[x]" and f"{Fmt():>3}" == "fmt[>3]"
assert object.__format__(Plain(), "") == "plain-str" and Plain().__format__("") == "plain-str"
assert str.__format__("abc", ">5") == "  abc" and str.__format__("abc", "") == "abc"
assert "{}".format(Plain()) == "plain-str" and "{!s}".format(Plain()) == "plain-str"
assert "{}|{:>4}".format(Color.RED, Fmt()) == "Color.RED|fmt[>4]"
assert "{} {}".format(1, "x") == "1 x" and "{:05.1f}".format(2.25) == "002.2"

try:
    format(Plain(), "x")
except TypeError:
    pass
else:
    raise AssertionError("object.__format__ with a non-empty spec must raise TypeError")

try:
    with contextlib.redirect_stdout(io.StringIO()):
        print(Bad())
except ValueError:
    pass
else:
    raise AssertionError("print() must propagate __str__ exceptions")

print("print python str OK")
