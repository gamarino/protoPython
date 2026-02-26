r = range(5)
print("dir(r):", dir(r))
print("hasattr __iter__:", hasattr(r, "__iter__"))
try:
    print("r.__iter__:", r.__iter__)
except Exception as e:
    print("r.__iter__ error:", e)
