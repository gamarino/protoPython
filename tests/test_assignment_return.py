def my_decorator(func):
    func.attr = True
    return func

@my_decorator
def foo():
    pass

print("foo = ", foo)
