class A(type):
    def __new__(mcls, name, bases, namespace):
        print("A.__new__")
        return super().__new__(mcls, name, bases, namespace)
class B(A):
    def __new__(mcls, name, bases, namespace):
        print("B.__new__")
        return super().__new__(mcls, name, bases, namespace)
class C(metaclass=B):
    pass
