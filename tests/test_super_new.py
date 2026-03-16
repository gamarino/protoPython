class Meta(type):
    def __new__(mcls, name, bases, ns):
        print("mcls is:", mcls)
        cls = super().__new__(mcls, name, bases, ns)
        print("cls is:", cls)
        return cls

class A(metaclass=Meta):
    pass

print("A is:", A)
