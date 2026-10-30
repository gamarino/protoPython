# Attribute names computed at run time -- class namespace keys, setattr and
# hasattr arguments, module and instance __dict__ writes -- are used as
# their interned symbols, which protoCore compares by pointer.  Interning
# them must not change what Python code sees: the names stay ordinary str
# keys of __dict__, equal to (and hashing like) the heap strings that
# produced them.
name = "".join(["long", "_attribute", "_name"])
X = type("X", (), {name: 1, "other_long_name": 2})
assert X.long_attribute_name == 1 and getattr(X, name) == 1
assert name in X.__dict__ and X.__dict__[name] == 1
assert hasattr(X, name) and hasattr(X(), "".join(["other_", "long_name"]))
class C:
    some_long_method_name = 3
assert "".join(["some_long", "_method_name"]) in C.__dict__
import types
m = types.ModuleType("m"); m.__dict__["".join(["dyn", "amic_name_x"])] = 5
assert m.dynamic_name_x == 5
o = C(); o.__dict__["".join(["inst", "ance_attr_name"])] = 7
assert o.instance_attr_name == 7 and vars(o) == {"instance_attr_name": 7}
d = {name: 1}
assert d["long_attribute_name"] == 1 and list(X.__dict__).count(name) == 1
print("interned_attribute_names: ok")
