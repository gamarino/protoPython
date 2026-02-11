class X:
    pass
x = X()
x.a = 1
print("a:", x.a)
del x.a
print("Deleted a")
try:
    print(x.a)
except Exception as e:
    print("Caught expected error")
print("Done")
