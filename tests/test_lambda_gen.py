def f():
    yield
print("type(f())", type(f()))
l = lambda: (yield)
print("type(l())", type(l()))
