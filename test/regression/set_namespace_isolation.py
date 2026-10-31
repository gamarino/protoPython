# A set's element table is never used as an attribute or item store: an
# instance attribute of a set subclass stays out of the elements, deleting
# it leaves the elements alone, and item assignment on a set is a TypeError.


class S(set):
    pass


t = S(["a", "tag"])
t.tag = "label"
assert sorted(t) == ["a", "tag"], sorted(t)
assert t.tag == "label" and vars(t) == {"tag": "label"}
del t.tag
assert sorted(t) == ["a", "tag"], sorted(t)
assert vars(t) == {}

f = type("F", (frozenset,), {})(["x"])
f.note = 1
assert sorted(f) == ["x"] and f.note == 1

s = {"add", "x", 0, 1}
try:
    s[0] = 5
except TypeError:
    pass
else:
    raise AssertionError("item assignment on a set must raise TypeError")
assert len(s) == 4 and 5 not in s

print("set_namespace_isolation: ok")
