"""str(e) is str(e.args[0]) for one argument and str(e.args) otherwise."""


class E(Exception):
    pass


class P:
    def __str__(self):
        return "p-str"


assert str(ValueError(5)) == "5", repr(str(ValueError(5)))
assert str(RuntimeError(3.5)) == "3.5"
assert str(StopIteration(42)) == "42"
assert str(ValueError("msg")) == "msg"
assert str(ValueError()) == ""
assert str(ValueError(1, 2)) == "(1, 2)"
assert str(E([1, 2])) == "[1, 2]"
assert str(ValueError(P())) == "p-str"
assert f"{ValueError(7)}" == "7"

try:
    raise ValueError(9)
except ValueError as e:
    assert str(e) == "9" and isinstance(str(e), str)

print("exception str args OK")
