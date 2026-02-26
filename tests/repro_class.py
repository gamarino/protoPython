
def make_class():
    class Foo:
        pass

code = make_class.__code__
print(code.co_code)
names = code.co_names
print(names)
