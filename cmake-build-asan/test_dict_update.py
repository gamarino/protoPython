def f(): pass
def g(): pass
f.a = 1

print("before dict update")
try:
    g.__dict__.update(f.__dict__)
    print("after dict update")
    print(g.__dict__)
except Exception as e:
    print(e)
