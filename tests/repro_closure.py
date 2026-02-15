def outer(x):
    def inner():
        return x
    return inner

fn = outer(42)
print(fn())
