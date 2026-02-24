ba = bytearray()
print("type(ba):", type(ba))
print("ba.__class__:", ba.__class__)
try:
    print("iter(ba):", iter(ba))
except Exception as e:
    print("iter(ba) Error:", repr(e))
