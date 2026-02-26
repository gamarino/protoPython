class EnumDict(dict):
    pass

class EnumType(type):
    @classmethod
    def __prepare__(metacls, cls, bases, **kwds):
        print("meta.__prepare__ called with metacls =", metacls)
        metacls._check_for_existing_members_(cls, bases)
        return EnumDict()

    def __new__(metacls, cls, bases, classdict, **kwds):
        return super().__new__(metacls, cls, bases, classdict, **kwds)

    @classmethod
    def _check_for_existing_members_(mcls, class_name, bases):
        print("_check_for_existing_members_ called")
        return True

try:
    class Enum(metaclass=EnumType):
        pass
    print("Enum created successfully")
except Exception as e:
    print("Error:", type(e), e)
