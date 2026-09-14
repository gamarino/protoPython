"""str() of a dict subclass uses the subclass's __repr__, as dict has no __str__ of its own."""
from collections import OrderedDict


class D(dict):
    def __repr__(self):
        return "D!"


assert str(D()) == "D!", str(D())
assert "%s" % (D(),) == "D!"
assert str(OrderedDict(a=1)) == "OrderedDict({'a': 1})", str(OrderedDict(a=1))
assert str({1: 2}) == "{1: 2}"
assert str({}) == "{}"


class E(dict):
    pass


assert str(E(a=1)) == "{'a': 1}"

print("dict subclass str OK")
