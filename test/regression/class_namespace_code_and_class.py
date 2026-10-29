# A class's own namespace may bind names the runtime uses as markers.
#  * `__code__` (annotationlib.ForwardRef has it in __slots__): calling the
#    class used to run it as a function and answer None.
#  * `__class__` (unittest.mock's NonCallableMock defines it as a property
#    for its instances): the class's type used to be read as that property.
class WithCodeSlot:
    __slots__ = ("__code__", "v")

    def __init__(self, v):
        self.__code__ = None
        self.v = v


w = WithCodeSlot(4)
assert type(w) is WithCodeSlot and w.v == 4


class WithCodeAttr:
    __code__ = "not a code object"

    def __init__(self):
        self.ok = True


assert WithCodeAttr().ok is True


class Base:
    pass


class ClassProperty(Base):
    def _get_class(self):
        return Base

    __class__ = property(_get_class)


assert type(ClassProperty) is type
assert ClassProperty().__class__ is Base           # the property, for instances
assert type(ClassProperty()) is ClassProperty


class LooksLikeInt:
    @property
    def __class__(self):
        return int


assert isinstance(LooksLikeInt(), int)            # isinstance consults obj.__class__
assert not isinstance(LooksLikeInt(), str)
assert type(LooksLikeInt()) is LooksLikeInt


class Other(Base):
    pass


class Both(Other, ClassProperty):                 # no metaclass conflict
    pass


assert type(Both) is type and issubclass(Both, ClassProperty)


class Meta(type):
    pass


class WithMeta(metaclass=Meta):
    pass


class MetaAndProperty(WithMeta, ClassProperty):   # the metaclass is still found
    pass


assert type(MetaAndProperty) is Meta

import annotationlib
ref = annotationlib.ForwardRef("int")
assert type(ref) is annotationlib.ForwardRef and ref.__forward_arg__ == "int"
import test.typinganndata.ann_module as ann_module
assert ann_module.C.__annotations__ is not None
print("class_namespace_code_and_class ok")
