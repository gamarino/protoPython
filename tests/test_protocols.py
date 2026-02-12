def check_protocols(name, obj, protocols):
    print(f"--- Checking {name} ({type(obj).__name__}) ---")
    for proto in protocols:
        has = hasattr(obj, proto)
        print(f"  {proto}: {has}")
        if has and proto == "__len__":
            try:
                print(f"    len: {len(obj)}")
            except Exception as e:
                print(f"    len error: {e}")
        if has and proto == "__bool__":
            print(f"    bool: {bool(obj)}")

types_to_check = [
    ("int", 1, ["__bool__", "__eq__", "__hash__", "__add__", "__str__", "__repr__"]),
    ("float", 1.5, ["__bool__", "__eq__", "__hash__", "__add__", "__str__", "__repr__"]),
    ("str", "hello", ["__iter__", "__len__", "__getitem__", "__contains__", "__bool__", "__str__", "__repr__"]),
    ("list", [1, 2], ["__iter__", "__len__", "__getitem__", "__setitem__", "__delitem__", "__contains__", "__bool__", "__str__", "__repr__"]),
    ("dict", {"a": 1}, ["__iter__", "__len__", "__getitem__", "__setitem__", "__delitem__", "__contains__", "__bool__", "__str__", "__repr__"]),
    ("tuple", (1, 2), ["__iter__", "__len__", "__getitem__", "__contains__", "__bool__", "__str__", "__repr__"]),
    ("set", {1, 2}, ["__iter__", "__len__", "__contains__", "__bool__", "__str__", "__repr__"]),
    ("bytes", b"abc", ["__iter__", "__len__", "__getitem__", "__contains__", "__bool__", "__str__", "__repr__"]),
    ("None", None, ["__bool__", "__str__", "__repr__"]),
]

for name, obj, protos in types_to_check:
    check_protocols(name, obj, protos)

print("\n--- Testing iteration ---")
for name, obj, _ in types_to_check:
    if hasattr(obj, "__iter__"):
        print(f"Iterating over {name}: ", end="")
        try:
            items = []
            for x in obj:
                items.append(repr(x))
                if len(items) > 5: break
            print(", ".join(items))
        except Exception as e:
            print(f"Error: {e}")

print("\n--- Testing boolean truthiness ---")
for name, obj, _ in types_to_check:
    print(f"{name} ({obj!r}): {bool(obj)}")

# Specialized checks
print("\n--- Specialized checks ---")
l = [1, 2, 3]
print(f"list before del: {l}")
del l[1]
print(f"list after del l[1]: {l}")
l[0] = 10
print(f"list after l[0]=10: {l}")

d = {"a": 1, "b": 2}
print(f"dict before del: {d}")
del d["a"]
print(f"dict after del d['a']: {d}")
d["c"] = 3
print(f"dict after d['c']=3: {d}")
