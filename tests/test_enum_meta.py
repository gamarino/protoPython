class EnumType(type):
    @classmethod
    def __prepare__(metacls, cls, bases):
        print("mcls is", metacls)
        print("dir(metacls) is", dir(metacls))
        print("dir(EnumType) is", dir(EnumType))
        return {}
    
    @classmethod
    def _check_for_existing_members_(cls, class_dict):
        print("_check_for_existing_members_ called!")

class Enum(metaclass=EnumType):
    pass
