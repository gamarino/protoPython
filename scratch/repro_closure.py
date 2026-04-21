def outer(*args, **kwargs):
    def inner():
        return args, kwargs
    return inner

f = outer(1, 2, a=3)
print(f())
