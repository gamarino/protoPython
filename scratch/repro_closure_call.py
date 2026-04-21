def callback(*args, **kwargs):
    print(f"callback: {args}, {kwargs}")

def outer(cb, *args, **kwargs):
    def inner(x, y, z):
        cb(*args, **kwargs)
    return inner

f = outer(callback, 1, 2, a=3)
f(None, None, None)
