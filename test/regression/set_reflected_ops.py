"""Set operators with a dict view on the left reach the set's reflected operators."""

d = {1: "a", 2: "b"}
assert d.keys() & {1.0} == {1}
assert d.keys() | {3} == {1, 2, 3}
assert d.keys() - {1} == {2}
assert d.keys() ^ {2, 3} == {1, 3}
assert d.items() & {(1, "a")} == {(1, "a")}
assert type(d.keys() & {1}) is set
assert {1} & d.keys() == {1}
assert {3} - d.keys() == {3}
assert frozenset([1, 2]) - d.keys() == frozenset()

try:
    [1] & {1}
except TypeError:
    pass
else:
    raise AssertionError("list & set must raise TypeError")

print("set reflected ops OK")
