class A:
    pass
a = A()
a.x = 42
print("a.x before del:", a.x)
del a.x
try:
    print("a.x after del:", a.x)
except:
    print("a.x is gone")
