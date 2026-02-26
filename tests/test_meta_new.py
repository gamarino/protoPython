class Meta(type):
    def __new__(mcls, name, bases, namespace):
        print("Meta.__new__ called for", name)
        cls = super().__new__(mcls, name, bases, namespace)
        cls._my_registry = set()
        return cls

class MyClass(metaclass=Meta):
    pass

print(hasattr(MyClass, '_my_registry'))
