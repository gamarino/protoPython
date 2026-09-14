"""A keyword given twice in a call raises TypeError naming the callee."""


def f(**kw):
    return kw


class C:
    def m(self, **kw):
        return kw


def message(fn):
    try:
        fn()
    except TypeError as exc:
        return str(exc)
    raise AssertionError("no TypeError")


def check(fn, suffix):
    msg = message(fn)
    assert msg.endswith(suffix), msg


check(lambda: f(a=1, **{'a': 2}), "f() got multiple values for keyword argument 'a'")
check(lambda: f(**{'a': 1}, **{'a': 2}), "f() got multiple values for keyword argument 'a'")
check(lambda: f(**{'b': 1}, b=2), "f() got multiple values for keyword argument 'b'")
check(lambda: C().m(x=1, **{'x': 2}), "C.m() got multiple values for keyword argument 'x'")
check(lambda: dict(a=1, **{'a': 2}), "dict() got multiple values for keyword argument 'a'")

# Distinct keywords still merge in order.
assert f(a=1, **{'b': 2}, c=3) == {'a': 1, 'b': 2, 'c': 3}
assert list(f(z=1, **{'y': 2}, x=3)) == ['z', 'y', 'x']
assert C().m(**{'p': 1}, q=2) == {'p': 1, 'q': 2}
assert f(*(), **{}) == {}
print("call duplicate keywords OK")
