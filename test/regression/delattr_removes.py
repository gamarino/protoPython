"""delattr(obj, name) deletes like `del obj.name`."""
import json
import sys


class C:
    pass


c = C()
c.a = 1
delattr(c, "a")
assert not hasattr(c, "a") and "a" not in vars(c)
try:
    delattr(c, "a")
except AttributeError:
    pass
else:
    raise AssertionError("deleting a missing attribute must raise AttributeError")

json.protopy_tmp = 1
delattr(json, "protopy_tmp")
assert not hasattr(json, "protopy_tmp")


class P:
    deleted = []

    @property
    def x(self):
        return 1

    @x.deleter
    def x(self):
        P.deleted.append(True)


p = P()
delattr(p, "x")
del p.x
assert P.deleted == [True, True]


class Logged:
    log = []

    def __delattr__(self, name):
        Logged.log.append(name)
        object.__delattr__(self, name)


o = Logged()
o.y = 2
delattr(o, "y")
assert Logged.log == ["y"] and not hasattr(o, "y")
o.z = 3
del o.z
assert Logged.log == ["y", "z"] and not hasattr(o, "z")
try:
    object.__delattr__(o, "missing")
except AttributeError:
    pass
else:
    raise AssertionError("object.__delattr__ on a missing name must raise")


class K:
    attr = 1


delattr(K, "attr")
assert not hasattr(K, "attr")

x_global = 1
delattr(sys.modules[__name__], "x_global")
try:
    x_global
except NameError:
    pass
else:
    raise AssertionError("delattr on the module unbinds the global")
print("delattr removes OK")
