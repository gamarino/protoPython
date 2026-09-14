# Regression test: class creation honours PEP 560 __mro_entries__.
#
# A base that is not a class but defines __mro_entries__ must be replaced by
# the tuple that method returns, and the bases as written recorded as
# __orig_bases__. protoPython ignored it, so `class B(Generic[T])` used the
# alias object itself as a base (or its type as the metaclass).


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


class Real:
    pass


class Mixin:
    pass


class Stand:
    def __init__(self, *targets):
        self.targets = targets
        self.seen = None

    def __mro_entries__(self, bases):
        self.seen = bases
        return self.targets


stand = Stand(Real, Mixin)


class Built(stand):
    pass


check("bases replaced", Built.__bases__, (Real, Mixin))
check("mro", Built.__mro__, (Built, Real, Mixin, object))
check("__orig_bases__", Built.__orig_bases__, (stand,))
check("__mro_entries__ receives the original bases", stand.seen, (stand,))
check("instance of resolved base", isinstance(Built(), Real), True)


class Empty:
    def __mro_entries__(self, bases):
        return ()


class OnlyObject(Empty(), Real):
    pass


check("entry removed", OnlyObject.__bases__, (Real,))


class NoEntries(Real):
    pass


check("no __orig_bases__ when nothing was replaced", "__orig_bases__" in NoEntries.__dict__, False)


class Bad:
    def __mro_entries__(self, bases):
        return [Real]


try:
    class Broken(Bad()):
        pass
except TypeError:
    pass
else:
    raise AssertionError("__mro_entries__ returning a list did not raise TypeError")

print("mro entries OK")
