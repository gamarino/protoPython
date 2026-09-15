# operator.contains(a, b) is `b in a`.
import operator

assert operator.contains([1, 2, 3], 2) is True
assert operator.contains([1, 2, 3], 5) is False
assert operator.contains("hello", "ell") is True
assert operator.contains({"k": 1}, "k") is True
assert operator.contains({1, 2}, 3) is False
assert operator.contains((1, (2, 3)), (2, 3)) is True
assert operator.contains(range(10), 7) is True


class Box:
    def __contains__(self, item):
        return item == "inside"


class Seq:
    def __getitem__(self, i):
        if i < 3:
            return i * 10
        raise IndexError(i)


assert operator.contains(Box(), "inside") is True
assert operator.contains(Box(), "outside") is False
assert operator.contains(Seq(), 20) is True
assert operator.contains(Seq(), 25) is False

try:
    operator.contains(5, 1)
except TypeError:
    pass
else:
    raise AssertionError("expected TypeError")

assert operator.contains is operator.__contains__

print("operator_contains: ok")
