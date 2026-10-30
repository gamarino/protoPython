# The native functools.reduce follows CPython 3.14's signature and errors.
import sys
def add(a, b):
    return a + b


if len(sys.argv) > 1 and sys.argv[1] == "py":
    from test.support.import_helper import import_fresh_module
    reduce = import_fresh_module("functools", blocked=["_functools"]).reduce
else:
    from _functools import reduce


def raises(*args, **kw):
    try:
        reduce(*args, **kw)
    except TypeError:
        return True
    return False


assert reduce(add, [1, 2, 3]) == 6
assert reduce(add, [1, 2, 3], 10) == 16
assert reduce(add, [1, 2], initial=10) == 13
assert reduce(42, "", initial="1") == "1"
assert reduce(add, [], 5) == 5
assert raises()
assert raises(add)
assert raises(42, 42)
assert raises(42, 42, 42)
assert raises(add, [])
assert raises(add, [0, 1], initial="")
assert raises(add, [1], 1, 2)
assert raises(add, [1], 1, initial=2)
assert raises(add, [1], other=2)
print("functools_reduce_args: ok")
