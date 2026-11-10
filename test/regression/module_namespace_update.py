# Writes to a module namespace keep the names written before them.
# `m.a = 1; vars(m).update({"b": 2})` left only "b" in vars(m).keys() and
# items() (the first tracked write started a key list holding its own name),
# and update() skipped dunder names and rejected keyword arguments.
import types


def names(ns):
    return [k for k in ns if not k.startswith("__")]


m = types.ModuleType("m")
m.a = 1
assert names(vars(m)) == ["a"], names(vars(m))
vars(m).update({"b": 2})
assert names(vars(m)) == ["a", "b"], names(vars(m))
assert sorted(k for k, _ in vars(m).items() if not k.startswith("__")) == ["a", "b"]
assert m.a == 1 and m.b == 2

# Dunder keys are written, as CPython writes them.
vars(m).update({"__x__": 1})
assert "__x__" in vars(m) and m.__x__ == 1

# Keyword arguments and iterables of pairs, as dict.update takes them.
vars(m).update(c=3)
vars(m).update([("e", 5)])
vars(m)["d"] = 4
assert sorted(names(vars(m))) == ["a", "b", "c", "d", "e"], names(vars(m))
assert (m.c, m.d, m.e) == (3, 4, 5)

# Another module's namespace, __name__ included.
n = types.ModuleType("n")
n.z = 9
vars(m).update(vars(n))
assert m.z == 9 and m.__name__ == "n", (m.z, m.__name__)

# setattr and attribute writes after the first one.
p = types.ModuleType("p")
p.a = 1
setattr(p, "b", 2)
p.c = 3
assert names(vars(p)) == ["a", "b", "c"], names(vars(p))

# A module namespace takes str keys only in protoPython (a documented
# deviation, docs/PYTHON_COMPATIBILITY.md); CPython accepts any key.
import sys
try:
    vars(m).update({1: "int key"})
except TypeError:
    assert sys.implementation.name != "cpython"
else:
    assert sys.implementation.name == "cpython", "a non-str namespace key was accepted"

print("module_namespace_update: ok")
