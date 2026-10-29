# vars(o) / o.__dict__ is o's namespace: removing a name through it removes
# the attribute (it used to be rebound to None), and the dict a caller holds
# reflects its own writes.
class O:
    pass


class WithClassAttr:
    a = "class-a"


def fresh(cls=O):
    o = cls()
    o.a = 1
    o.b = 2
    o.c = 3
    return o


def gone(o, name):
    try:
        getattr(o, name)
    except AttributeError:
        return name not in vars(o) and not hasattr(o, name)
    return False


o = fresh()
del vars(o)["a"]
assert gone(o, "a"), vars(o)
assert sorted(vars(o)) == ["b", "c"]

o = fresh()
v = vars(o)
del v["a"]
assert "a" not in v and sorted(v) == ["b", "c"]
v["z"] = 9
assert v["z"] == 9 and o.z == 9

o = fresh()
assert o.__dict__.pop("a") == 1 and gone(o, "a")
assert o.__dict__.pop("missing", 7) == 7
try:
    o.__dict__.pop("missing")
    raise AssertionError("pop of a missing key did not raise")
except KeyError:
    pass

o = fresh()
k, val = o.__dict__.popitem()
assert gone(o, k) and len(vars(o)) == 2 and val == {"a": 1, "b": 2, "c": 3}[k]

o = fresh()
o.__dict__.clear()
assert vars(o) == {} and gone(o, "a") and gone(o, "b") and gone(o, "c")

o = fresh()
try:
    del vars(o)["nope"]
    raise AssertionError("del of a missing key did not raise")
except KeyError:
    pass

o = fresh()
assert o.__dict__.setdefault("y", 6) == 6 and o.y == 6
assert o.__dict__.setdefault("y", 8) == 6 and o.y == 6

# Removing the instance attribute uncovers the class attribute, as in CPython.
w = fresh(WithClassAttr)
del w.__dict__["a"]
assert w.a == "class-a" and "a" not in vars(w)

# Built-in subclass instance: same namespace semantics.
class L(list):
    pass


l = L()
l.a = 1
del vars(l)["a"]
assert not hasattr(l, "a") and vars(l) == {}
print("vars_namespace_writes ok")
