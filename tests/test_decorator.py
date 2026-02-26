def dec(f):
    print("In dec, f:", f)
    return f

@dec
def foo():
    pass

print("foo is:", foo)
