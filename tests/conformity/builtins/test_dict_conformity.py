# Phase 1.1.4: dict isolation — get/set/delete, iteration.
# Immutability: map updates return new root propagated to owner (frame/global).

d = {}
d["a"] = 1
d["b"] = 2
assert d["a"] == 1
assert d.get("b") == 2
assert d.get("c") is None

# Iteration
keys = list(d.keys())
assert set(keys) == {"a", "b"}
assert list(d.values()) == [1, 2]
assert list(d.items()) == [("a", 1), ("b", 2)]

# Delete
del d["a"]
assert "a" not in d
assert d.get("a") is None
assert d == {"b": 2}

print("OK dict conformity")
