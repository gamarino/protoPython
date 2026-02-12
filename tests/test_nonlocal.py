def outer():
    x = 10
    def inner():
        nonlocal x
        x = 20
        print(f"inner x: {x}")
    print(f"outer x before: {x}")
    inner()
    print(f"outer x after: {x}")
    return x

res = outer()
assert res == 20
print("Nonlocal test passed!")

def counter():
    c = 0
    def inc():
        nonlocal c
        c = c + 1
        return c
    return inc

f = counter()
assert f() == 1
assert f() == 2
assert f() == 3
print("Closure counter test passed!")
