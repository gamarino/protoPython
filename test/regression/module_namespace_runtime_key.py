# A module namespace found a name only when the key was the interned
# attribute name: a str built at run time (longer than an inline string)
# was not found by globals()[k], vars(m).get(k) or `k in vars(m)`.
import os
import types

long_global_name = 1
k = "long_global_" + "name"
assert globals()[k] == 1
assert globals().get(k) == 1
assert k in globals()

nm = "long_name_%d" % 7
m = types.ModuleType("mm")
setattr(m, nm, 1)
ns = vars(m)
assert ns[nm] == 1 and ns.get(nm) == 1 and nm in ns and getattr(m, nm) == 1
ns.update({nm: 5})
assert getattr(m, nm) == 5 and ns.get(nm) == 5
assert [key for key in ns if key == nm] == [nm]
assert ns.get("long_name_7") == 5 and ns["long_name_7"] == 5
ns["another_" + "long_name"] = 6
assert m.another_long_name == 6
del ns["another_" + "long_name"]
assert not hasattr(m, "another_long_name")

k3 = "get_" + "exec_path"
assert k3 in vars(os) and vars(os).get(k3) is getattr(os, k3)

print("module_namespace_runtime_key: ok")
