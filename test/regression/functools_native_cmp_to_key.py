# _functools.cmp_to_key is a builtin, as in CPython: stored on a class it
# stays a plain callable instead of becoming a method.  _functools had no
# cmp_to_key, so test_functools' TestCmpToKeyC stored functools.py's Python
# function and every call got the test instance as an extra argument.
import _functools
import functools


class Holder:
    cmp_to_key = _functools.cmp_to_key


def cmp(x, y):
    return (x > y) - (x < y)


key = Holder().cmp_to_key(cmp)
assert key(3) == key(3) and key(3) > key(1) and key(1) <= key(2)
assert key(obj=50).obj == 50
assert functools.cmp_to_key is _functools.cmp_to_key
assert _functools.cmp_to_key(mycmp=cmp)(2) < _functools.cmp_to_key(mycmp=cmp)(3)
assert sorted(range(5), key=_functools.cmp_to_key(lambda x, y: y - x)) == [4, 3, 2, 1, 0]


def raises(exc, f, *a, **k):
    try:
        f(*a, **k)
    except exc:
        return
    raise AssertionError(f"{exc.__name__} not raised")


raises(TypeError, _functools.cmp_to_key)
raises(TypeError, _functools.cmp_to_key, cmp, None)
raises(TypeError, key)
raises(TypeError, key, None, None)
raises(TypeError, hash, key(10))
raises((TypeError, AttributeError), lambda: key(3) > 1)
print("functools_native_cmp_to_key: ok")
