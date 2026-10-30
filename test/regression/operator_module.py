# `operator` is CPython's operator.py: its functions apply the language's
# operators.  A native module registered as `operator` handled only ints,
# floats and strs (operator.add('', 0) returned None) and its methodcaller
# ignored keyword arguments.
import operator
import pickle


class V:
    def __init__(self, x):
        self.x = x

    def __sub__(self, other):
        return V(self.x - other.x)

    def __rmul__(self, k):
        return V(self.x * k)


try:
    operator.add("", 0)
except TypeError:
    pass
else:
    raise AssertionError("operator.add('', 0) did not raise TypeError")
assert operator.add([1], [2]) == [1, 2]
assert operator.sub(10 ** 20, 1) == 10 ** 20 - 1
assert operator.sub({1, 2}, {1}) == {2}
assert operator.sub(V(5), V(2)).x == 3
assert operator.mul(3, V(2)).x == 6
assert operator.mul([0], 3) == [0, 0, 0]
assert operator.truediv(7, 2) == 3.5 and operator.floordiv(-7, 2) == -4
assert operator.mod("%s!", "hi") == "hi!"
assert operator.pow(2, 100) == 2 ** 100
assert operator.lshift(1, 70) == 1 << 70 and operator.and_({1, 2}, {2}) == {2}
assert operator.neg(3) == -3 and operator.not_([]) is True and operator.index(True) == 1
assert operator.methodcaller("split", sep=",")("x,y") == ["x", "y"]
assert repr(operator.itemgetter(1)) == "operator.itemgetter(1)"
assert repr(operator.methodcaller("m", 1, k=2)) == "operator.methodcaller('m', 1, k=2)"
g = pickle.loads(pickle.dumps(operator.attrgetter("real")))
assert g(5) == 5
assert operator.lt(1, 2) and operator.contains([1], 1)
assert operator.__lt__ is operator.lt
import hmac
assert hmac.compare_digest(b"ab", b"ab") and not hmac.compare_digest("a", "b")
print("operator_module: ok")
