class Meta(type):
    def __new__(mcls, name, bases, ns):
        print("Meta.__new__ args:", mcls, name, bases, ns)
        return super().__new__(mcls, name, bases, ns)

class MyClass(metaclass=Meta):
    pass
