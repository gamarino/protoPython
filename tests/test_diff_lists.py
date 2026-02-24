l = []
print("type(l):", type(l))
print("l.__class__:", l.__class__)
print("iter(l):", iter(l))

k = {}.keys()
print("type(k):", type(k))
print("k.__class__:", k.__class__)
try:
    print("iter(k):", iter(k))
except Exception as e:
    print("iter(k) Error:", repr(e))
