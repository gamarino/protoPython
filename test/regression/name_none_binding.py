# Module and class-body names bound to None are found as bindings of the
# namespace (LOAD_NAME), shadowing builtins, and rebinding keeps one key.
len = None
assert len is None
del len
assert len([1]) == 1


class K:
    print = None
    seen = print
    a = 1
    a = 2
    b = a


assert K.seen is None
assert K.print is None
assert K.b == 2
assert sorted(k for k in K.__dict__ if k in ("print", "seen", "a", "b")) == ["a", "b", "print", "seen"]
ns = {}
exec("x = None\ny = x\nx = 3\nz = x", ns)
assert ns["y"] is None and ns["z"] == 3
print("name_none_binding OK")
