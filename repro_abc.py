import abc
from types import GenericAlias
print("GenericAlias:", GenericAlias)
class Foo(abc.ABC):
    __class_getitem__ = classmethod(GenericAlias)
    @abc.abstractmethod
    def bar(self): pass
print("Defined Foo with GenericAlias")
