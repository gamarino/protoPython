d = {}
k = d.keys()
print("type(k):", type(k))
print("k.__class__:", k.__class__)
try:
    print("iter(k):", iter(k))
except Exception as e:
    print("iter(k) Error:", repr(e))
