# dict.update / OrderedDict / dict subclasses fed by every kind of source.
# The expected output (ordered_dict_iterables.expected) is CPython's.
import types
from collections import OrderedDict

def show(label, d):
    print(label, type(d).__name__, list(d.items()))

show("od gen", OrderedDict((k, k * 2) for k in range(5)))
show("od iter", OrderedDict(iter([("a", 1), ("b", 2)])))
show("od zip", OrderedDict(zip("abc", range(3))))
show("od map", OrderedDict({"b": 1, "a": 2}))
show("od list+kw", OrderedDict([("x", 1)], z=3))
show("od enumerate", OrderedDict(enumerate("xy")))

class Sub(dict):
    pass

d = Sub()
d.update((k, -k) for k in range(3))
show("subclass update gen", d)

d = {}
d.update(iter([(1, 2), (3, 4)]))
show("dict update iter", d)

class Mapping:
    def keys(self):
        return ["k1", "k2"]
    def __getitem__(self, key):
        return key.upper()

d = {}
d.update(Mapping())
show("dict update mapping", d)

d = {}
d.update(types.MappingProxyType({"x": 1, "y": 2}))
show("dict update proxy", d)

class Cls:
    a = 1
    b = 2

d = {}
d.update(Cls.__dict__)
print("dict update class dict", sorted(k for k in d if not k.startswith("__")))

for bad in (5, None):
    try:
        {}.update(bad)
        print("no error for", type(bad).__name__)
    except TypeError:
        print("TypeError for", type(bad).__name__)

od = OrderedDict.fromkeys("abc")
od.update((k, 0) for k in "bd")
show("od update gen", od)
