class Meta(type):
    def my_method(cls):
        print("Meta.my_method called with cls:", cls)
        print("cls is MyClass:", cls is MyClass)
        print("cls is Meta:", cls is Meta)

class MyClass(metaclass=Meta):
    pass

MyClass.my_method()
