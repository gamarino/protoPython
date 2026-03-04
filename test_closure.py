def f():
    a=1
    def g(): nonlocal a
    return g.__closure__[0]

print(f())
