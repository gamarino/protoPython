# Regression test: dict subclasses take their own __init__ arguments, and
# dict.__init__ applies positional and keyword arguments.
#
# dict.__new__ rejected more than one positional argument before checking
# whether the subclass overrides __init__, so `D(factory, {...})` raised
# "dict expected at most 1 argument"; CPython's dict.__new__ ignores its
# arguments and only dict.__init__ validates them. dict.__init__ also ignored
# keyword arguments.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


class D(dict):
    def __init__(self, factory=None, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.factory = factory


d = D(list, {"a": 1}, b=2)
check("subclass __init__ arguments", (dict(d), d.factory), ({"a": 1, "b": 2}, list))


class K(dict):
    def __init__(self, *args):
        super().__init__()
        self.args = args


check("subclass takes any positional arguments", K(1, 2, 3).args, (1, 2, 3))

check("dict keyword arguments", dict(a=1, b=2), {"a": 1, "b": 2})
check("dict mapping and keywords", dict({"a": 1}, b=2), {"a": 1, "b": 2})
check("dict pairs", dict([("a", 1), ("b", 2)]), {"a": 1, "b": 2})
check("dict(1.0 key) looks up by 1", dict([(1.0, "x")])[1], "x")


class NoInit(dict):
    pass


check("subclass without __init__", dict(NoInit({"a": 1}, b=2)), {"a": 1, "b": 2})

for label, make in (
    ("dict(1, 2)", lambda: dict(1, 2)),
    ("NoInit(1, 2)", lambda: NoInit(1, 2)),
    ("super().__init__(a, b)", lambda: D(None, {}, {})),
):
    try:
        make()
    except TypeError:
        pass
    else:
        raise AssertionError(label + " did not raise TypeError")

print("dict subclass init OK")
