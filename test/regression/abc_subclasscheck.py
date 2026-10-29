# issubclass(C, A) dispatches to type(A).__subclasscheck__, as in CPython:
# ABCMeta consults the class's __subclasshook__ and its registry.
import abc


class Hooked(abc.ABC):
    @classmethod
    def __subclasshook__(cls, C):
        if cls is Hooked:
            return hasattr(C, "quack")
        return NotImplemented


class Duck:
    def quack(self):
        pass


class Stone:
    pass


assert issubclass(Duck, Hooked)
assert not issubclass(Stone, Hooked)
assert isinstance(Duck(), Hooked)
assert issubclass(Duck, (int, Hooked))


class Child(Hooked):
    pass


# The hook answers NotImplemented for Child: the MRO and registry decide.
assert not issubclass(Duck, Child)


class Registered(abc.ABC):
    pass


Registered.register(int)
assert issubclass(int, Registered)
assert issubclass(bool, Registered)
assert not issubclass(str, Registered)


class Meta(type):
    def __subclasscheck__(cls, sub):
        return sub is str


class Any(metaclass=Meta):
    pass


assert issubclass(str, Any)
assert not issubclass(int, Any)
assert issubclass(int, int)
assert not issubclass(int, str)
print("abc_subclasscheck OK")
