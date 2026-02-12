data = [
    ("int", 1),
    ("float", 1.5),
    ("str", "hi"),
    ("list", [1]),
    ("dict", {"a": 1}),
    ("tuple", (1,)),
    ("set", {1}),
    ("bytes", b"a"),
    ("None", None)
]

for name, obj in data:
    print("Checking", name, "type:", type(obj))
