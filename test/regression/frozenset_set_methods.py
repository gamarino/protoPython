# frozenset carries the non-mutating set methods.
#
# frozenset had only the operator dunders (| & - ^), so issubset, union,
# isdisjoint, intersection, difference, symmetric_difference, issuperset and
# copy raised AttributeError.

f = frozenset([1, 2, 3])
g = frozenset([3, 4])
s = {3, 4}

# Present and callable.
for name in ("copy", "union", "intersection", "difference",
             "symmetric_difference", "issubset", "issuperset", "isdisjoint"):
    assert hasattr(f, name), "frozenset has no %s" % name

# Results match the operator forms and stay frozensets.
assert f.union(g) == f | g == frozenset([1, 2, 3, 4])
assert f.intersection(g) == f & g == frozenset([3])
assert f.difference(g) == f - g == frozenset([1, 2])
assert f.symmetric_difference(g) == f ^ g == frozenset([1, 2, 4])

assert type(f.union(g)) is frozenset
assert type(f.intersection(g)) is frozenset
assert type(f.difference(g)) is frozenset
assert type(f.symmetric_difference(g)) is frozenset
assert type(f.copy()) is frozenset

assert f.copy() == f

# Predicates.
assert frozenset([1, 2]).issubset(f) is True
assert f.issubset(frozenset([1, 2])) is False
assert f.issuperset(frozenset([1, 2])) is True
assert f.issuperset(frozenset([9])) is False
assert f.isdisjoint(frozenset([9, 10])) is True
assert f.isdisjoint(g) is False

# The arguments may be plain sets or any iterable, as in CPython.
assert f.union(s) == frozenset([1, 2, 3, 4])
assert f.union([7]) == frozenset([1, 2, 3, 7])
assert f.issubset([1, 2, 3, 4]) is True
assert f.isdisjoint([9]) is True

# union/intersection/difference accept several arguments.
assert f.union(frozenset([4]), frozenset([5])) == frozenset([1, 2, 3, 4, 5])

# The methods are bound to the instance.
m = f.issubset
assert m([1, 2, 3]) is True

# frozenset stays immutable: the mutating half is still absent.
for name in ("add", "remove", "discard", "pop", "clear", "update"):
    assert not hasattr(f, name), "frozenset should not have %s" % name

# The originals are untouched.
assert f == frozenset([1, 2, 3])
assert g == frozenset([3, 4])

# set keeps every method it already had.
t = {1, 2, 3}
assert t.union({4}) == {1, 2, 3, 4}
assert type(t.union({4})) is set
assert t.issubset({1, 2, 3, 4}) is True
assert t.isdisjoint({9}) is True
assert type(t.copy()) is set

print("frozenset_set_methods: ok")
