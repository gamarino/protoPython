def my_decorator(f):
    return f

@my_decorator
def my_func():
    print("hello")

my_func()
