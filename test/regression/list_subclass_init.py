# Regression test: list subclasses take their own __init__ arguments, and
# list.__init__ fills the list.
#
# list.__new__ (py_list_call) rejected keyword arguments before checking
# whether the subclass overrides __init__, so `L(it, tag="t")` raised
# "list() takes no keyword arguments". list.__init__ was a no-op, so
# super().__init__(it), list.__init__(self, it) and x.__init__(it) left the
# list unchanged. CPython's list.__new__ ignores its arguments; list.__init__
# clears the list and extends it from the iterable.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


class L(list):
    def __init__(self, it, tag=None):
        super().__init__(it)
        self.tag = tag


l = L([1, 2, 3], tag="t")
check("subclass keyword argument", (list(l), len(l), l.tag), ([1, 2, 3], 3, "t"))
check("subclass positional argument", list(L((4, 5), "u")), [4, 5])


class Plain(list):
    pass


check("subclass without __init__", (Plain((4, 5)), len(Plain("ab"))), ([4, 5], 2))


class Unbound(list):
    def __init__(self, *args):
        list.__init__(self, *args)


check("list.__init__(self, it)", Unbound(range(3)), [0, 1, 2])
check("list.__init__ from a generator", Unbound(i for i in range(3)), [0, 1, 2])


class Ignores(list):
    def __init__(self, it):
        pass


check("__new__ ignores the arguments", Ignores([1, 2]), [])


class OwnNew(list):
    def __new__(cls, *args, **kwargs):
        return super().__new__(cls)

    def __init__(self, it, tag=None):
        super().__init__(it)


check("__new__ override then super().__init__", OwnNew([5], tag=1), [5])

x = list([9])
x.__init__([7, 8])
check("re-init replaces the contents", x, [7, 8])
x.__init__()
check("re-init without arguments clears", x, [])

check("list(generator) consumes it once", list(i * i for i in range(4)), [0, 1, 4, 9])
check("list(str)", list("ab"), ["a", "b"])
check("list(dict)", list({"a": 1, "b": 2}), ["a", "b"])
check("list()", list(), [])

for label, make in (
    ("list(x=1)", lambda: list(x=1)),
    ("list([1], [2])", lambda: list([1], [2])),
    ("Plain([1], x=1)", lambda: Plain([1], x=1)),
    ("list.__init__(self, a, b)", lambda: Unbound([1], [2])),
    ("[].__init__(x=1)", lambda: [].__init__(x=1)),
):
    try:
        make()
    except TypeError:
        pass
    else:
        raise AssertionError(label + " did not raise TypeError")

print("list subclass init OK")
