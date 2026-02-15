print("RuntimeError:", RuntimeError)
print("type(RuntimeError):", type(RuntimeError))
try:
    raise RuntimeError("test")
except RuntimeError as e:
    print("Caught:", e)
