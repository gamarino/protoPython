from abc import ABCMeta, abstractmethod

class GenericAlias: pass

class Iterable(metaclass=ABCMeta):
    __slots__ = ()
    @abstractmethod
    def __iter__(self):
        while False: yield None
    
    __class_getitem__ = classmethod(GenericAlias)

class Sized(metaclass=ABCMeta):
    __slots__ = ()
    @abstractmethod
    def __len__(self): return 0

class Container(metaclass=ABCMeta):
    __slots__ = ()
    @abstractmethod
    def __contains__(self, x): return False
    
    __class_getitem__ = classmethod(GenericAlias)

print("Defining Collection")
class Collection(Sized, Iterable, Container):    
    __slots__ = ()

print("Success")
