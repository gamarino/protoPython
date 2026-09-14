# Regression test: collections.defaultdict is a working dict subclass.
#
# _collections.defaultdict was a bare native method whose prototype lived on
# an unreferenced object, so defaultdict(list) returned None. defaultdict is
# now implemented in Python on top of dict.__missing__.
import collections
import copy


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


dd = collections.defaultdict(list)
check("type", type(dd) is collections.defaultdict, True)
check("isinstance dict", isinstance(dd, dict), True)
check("class name", collections.defaultdict.__name__, "defaultdict")
dd["x"].append(1)
dd["x"].append(2)
dd["y"].append(3)
check("appending to defaults", dict(dd), {"x": [1, 2], "y": [3]})
check("default_factory", dd.default_factory is list, True)

counter = collections.defaultdict(int)
for ch in "banana":
    counter[ch] += 1
check("counting with +=", dict(counter), {"b": 1, "a": 3, "n": 2})

check("get and in do not insert", (dd.get("z"), "z" in dd), (None, False))

plain = collections.defaultdict()
try:
    plain["missing"]
except KeyError:
    pass
else:
    raise AssertionError("defaultdict() without a factory did not raise KeyError")

try:
    collections.defaultdict(42)
except TypeError:
    pass
else:
    raise AssertionError("a non-callable factory was accepted")

init = collections.defaultdict(list, {"a": [1]}, b=[2])
check("initial mapping and keywords", dict(init), {"a": [1], "b": [2]})

cp = dd.copy()
check("copy keeps factory and items", (cp.default_factory is list, dict(cp)), (True, dict(dd)))
check("copy.copy", dict(copy.copy(dd)), dict(dd))


class Sub(collections.defaultdict):
    pass


s = Sub(set)
s["k"].add(1)
check("subclass", (type(s) is Sub, dict(s)), (True, {"k": {1}}))

print("defaultdict OK")
