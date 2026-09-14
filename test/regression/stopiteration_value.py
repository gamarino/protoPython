"""StopIteration.args / value hold the arguments, not the instance itself."""


def gen():
    x = yield 1
    return x * 2


g = gen()
assert g.send(None) == 1
try:
    g.send(21)
except StopIteration as e:
    assert e.value == 42 and e.args == (42,), (e.value, e.args)
    assert repr(e) == "StopIteration(42)", repr(e)
    print("returned value", e.value)
else:
    raise AssertionError("send() past the end must raise StopIteration")


def returns_seven():
    return 7
    yield


try:
    next(returns_seven())
except StopIteration as e:
    assert e.value == 7 and e.args == (7,)

s = StopIteration(5)
assert s.value == 5 and s.args == (5,)
empty = StopIteration()
assert empty.value is None and empty.args == ()
two = StopIteration(1, 2)
assert two.args == (1, 2) and two.value == 1
assert all(a is not s for a in s.args)

print("stopiteration value OK")
