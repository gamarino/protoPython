def g_func():
    yield 1

g = g_func()

print("--- diag_type.py ---")
print(f"type object: {type(type)}")
print(f"type(1): {type(1)}")
print(f"type(None): {type(None)}")
print(f"type(list): {type(list)}")
print(f"type(g): {type(g)}")
print(f"repr(g): {repr(g)}")
