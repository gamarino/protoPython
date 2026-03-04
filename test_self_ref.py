class Meta(type):
    @classmethod
    def __prepare__(metacls, cls, bases, **kwds):
        print("Bases", bases)
        if not bases:
            return MyClass
        return {}

class MyClass(metaclass=Meta):
    pass
