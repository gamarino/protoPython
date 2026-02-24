b = b''
print("type(b):", type(b))
print("b.__class__:", b.__class__)
try:
    print("iter(b):", iter(b))
except Exception as e:
    print("iter(b) Error:", repr(e))
