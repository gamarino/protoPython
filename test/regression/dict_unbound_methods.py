"""dict methods called unbound on a subclass instance act on that instance."""


class D(dict):
    pass


d = D(a=1, b=2)
assert dict.popitem(d) == ("b", 2) and d == {"a": 1}
e = D(a=1)
dict.clear(e)
assert e == {} and len(e) == 0
x = {"k": 1}
dict.clear(x)
assert x == {}
assert {"q": 1}.popitem() == ("q", 1)
assert type(dict.copy(D(a=1))) is dict and type(D(a=1).copy()) is dict
assert dict.copy(D(a=1)) == {"a": 1}
assert type({"z": 0}.copy()) is dict

print("dict unbound methods OK")
