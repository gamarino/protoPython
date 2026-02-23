class Meta(type):
    def hello(cls):
        return "world"

class MyClass(metaclass=Meta):
    pass

print("MyClass.__mro__:", MyClass.__mro__)
print("MyClass.__class__:", MyClass.__class__)
print("Meta.hello:", Meta.hello)
