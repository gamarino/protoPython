# Methods read from a frozenset instance are bound to it, and a frozenset is
# not taken for a class.
f = frozenset([7, 8])

m = f.__contains__
assert m.__self__ is f
assert m(7) is True and m(9) is False
assert f.__contains__(8) is True
assert f.__len__() == 2
assert sorted(f.__iter__()) == [7, 8]
assert getattr(f, "__contains__")(7) is True
assert f.__hash__() == hash(f)
assert type(f) is frozenset and isinstance(f, frozenset)
assert not isinstance(f, type)

# A frozenset built by a set operation behaves the same way.
u = f | frozenset([1])
assert type(u) is frozenset
assert u.__contains__(1) is True and u.__len__() == 3
assert not isinstance(u, type)


class FS(frozenset):
    def extra(self):
        return len(self)


g = FS([1, 2, 3])
assert type(g) is FS and isinstance(g, frozenset)
assert g.__contains__(2) is True
assert g.extra() == 3
assert g.__len__() == 3
assert not isinstance(g, type)

print("frozenset_methods_bound: ok")
