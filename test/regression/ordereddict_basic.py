# Regression test: collections.OrderedDict is a working, order-aware dict
# subclass.
#
# `from _collections import OrderedDict` replaced the Python class with a
# native stub that returned a plain object, not a dict, without move_to_end,
# so item assignment was lost. CPython's pure-Python OrderedDict cannot run
# here either: weakref.proxy does not forward attributes, and the native
# _collections_abc views and MutableMapping.update are stubs. OrderedDict now
# keeps its order in the inherited dict, which preserves insertion order.
import copy
from collections import OrderedDict


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


d = OrderedDict()
d["b"] = 1
d["a"] = 2
d["c"] = 3
check("insertion order", (list(d), list(d.items())), (["b", "a", "c"], [("b", 1), ("a", 2), ("c", 3)]))
d.move_to_end("b")
check("move_to_end", list(d), ["a", "c", "b"])
d.move_to_end("b", last=False)
check("move_to_end(last=False)", list(d), ["b", "a", "c"])
check("keys and values follow the order", (list(d.keys()), list(d.values())), (["b", "a", "c"], [1, 2, 3]))
check("popitem", d.popitem(), ("c", 3))
check("popitem(last=False)", d.popitem(last=False), ("b", 1))
check("after popitem", list(d), ["a"])

e = OrderedDict([("x", 1), ("y", 2)])
f = OrderedDict([("y", 2), ("x", 1)])
check("OrderedDict == is order-sensitive", (e == f, e != f), (False, True))
check("== with a dict ignores order", (e == dict(f), dict(e) == f), (True, True))
check("== compares the items", (OrderedDict(a=1) == OrderedDict(a=2), OrderedDict(a=1) == {"a": 2},
                                {"a": 1, "b": 2} == OrderedDict(a=1)), (False, False, False))
check("repr", repr(e), "OrderedDict({'x': 1, 'y': 2})")
check("empty repr", repr(OrderedDict()), "OrderedDict()")
del e["x"]
e["x"] = 5
check("re-inserted key goes last", list(e.items()), [("y", 2), ("x", 5)])
check("reversed", list(reversed(OrderedDict(a=1, b=2, c=3))), ["c", "b", "a"])

g = OrderedDict.fromkeys("abc", 0)
check("fromkeys", (type(g), list(g.items())), (OrderedDict, [("a", 0), ("b", 0), ("c", 0)]))
check("copy", (g.copy() == g, type(g.copy())), (True, OrderedDict))
check("copy.copy", (copy.copy(g) == g, type(copy.copy(g))), (True, OrderedDict))
check("setdefault", (g.setdefault("z", 9), list(g)), (9, ["a", "b", "c", "z"]))
check("pop", (g.pop("a"), list(g)), (0, ["b", "c", "z"]))
check("isinstance dict", isinstance(g, dict), True)

h = OrderedDict(a=1)
h.update([("b", 2)], c=3)
h |= {"d": 4}
check("update and |=", list(h.items()), [("a", 1), ("b", 2), ("c", 3), ("d", 4)])
union = OrderedDict(a=1) | {"b": 2}
check("|", (type(union), list(union.items())), (OrderedDict, [("a", 1), ("b", 2)]))
check("reflected |", list(({"z": 0} | OrderedDict(a=1)).items()), [("z", 0), ("a", 1)])


class O(OrderedDict):
    pass


o = O(q=1)
o["r"] = 2
check("subclass", (list(o), type(o).__name__), (["q", "r"], "O"))

for label, make in (
    ("popitem on an empty OrderedDict", lambda: OrderedDict().popitem()),
    ("move_to_end of a missing key", lambda: OrderedDict().move_to_end("k")),
):
    try:
        make()
    except KeyError:
        pass
    else:
        raise AssertionError(label + " did not raise KeyError")

print("OrderedDict OK")
