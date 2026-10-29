# vars() and __dict__ of a dict-subclass instance are the instance's own
# namespace, as in CPython; dict itself has no own '__dict__' entry.
class D(dict):
    pass


d = D(a=1)
d.x = 5
v = vars(d)
assert v is not None
assert dict(v) == {"x": 5}, dict(v)
assert list(v.items()) == [("x", 5)]
assert d.__dict__ == {"x": 5}
assert repr(v) == "{'x': 5}", repr(v)
v["y"] = 6
assert d.y == 6
assert dict(d) == {"a": 1}
assert "__dict__" not in dict.__dict__
assert dict.__dict__["keys"] is not None


class L(list):
    pass


lst = L()
lst.y = 2
assert vars(lst) == {"y": 2}
print("vars_dict_subclass OK")
