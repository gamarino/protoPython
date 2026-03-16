class ABCMeta(type):
    def __new__(mcls, name, bases, namespace, **kwargs):
        print("ABCMeta.__new__")
        return super().__new__(mcls, name, bases, namespace, **kwargs)

class MyMeta(ABCMeta):
    def __new__(mcls, name, bases, namespace):
        print("MyMeta.__new__")
        # Let's inspect super() manually
        s = super()
        print(s)
        # What is s.__new__?
        print(s.__new__)
        return s.__new__(mcls, name, bases, namespace)

class MyClass(metaclass=MyMeta):
    pass
