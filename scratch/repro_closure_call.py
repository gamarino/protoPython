def outer():
    def inner(*args, **kwargs):
        print(f"inner: kwargs id={id(kwargs)}, type(kwargs) id={id(type(kwargs))}")
        def callback():
            pass
        return callback
    return inner

f = outer()
cb = f(1, 2)
cb()

print(f"main: dict id={id(dict)}")
print(f"main: dictPrototype id={id(type({}))}")
