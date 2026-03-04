class MockEnumDict(dict):
    pass

class MockMeta(type):
    @classmethod
    def __prepare__(metacls, name, bases, **kwds):
        return MockEnumDict()
    
    def __new__(metacls, name, bases, classdict, **kwds):
        print("classdict is:", classdict)
        print("classdict.__class__ is:", getattr(classdict, "__class__", None))
        try:
            classdict.setdefault('x', 1)
            print("setdefault worked!")
        except Exception as e:
            print("Failed setdefault:", type(e), e)
        return super().__new__(metacls, name, bases, classdict)

class MyEnum(metaclass=MockMeta):
    pass
