class EnumType(type):
    @classmethod
    def __prepare__(metacls, cls, bases, **kwds):
        pass
    @classmethod
    def _check_for_existing_members_(mcls, class_name, bases):
        return True

print("dir EnumType:", dir(EnumType))
print("hasattr EnumType._check_for_existing_members_:", hasattr(EnumType, "_check_for_existing_members_"))
