from abc import ABCMeta, abstractmethod

class Awaitable(metaclass=ABCMeta):
    __slots__ = ()

    @abstractmethod
    def __await__(self):
        yield

    @classmethod
    def __subclasshook__(cls, C):
        if cls is Awaitable:
            return _check_methods(C, "__await__")
        return NotImplemented

class Coroutine(Awaitable):
    __slots__ = ()

    @abstractmethod
    def send(self, value):
        ...

    @abstractmethod
    def throw(self, typ, val=None, tb=None):
        ...

    def close(self):
        ...

print("START GETATTR TEST")
v = Coroutine.__dict__['send']
print("Got send:", type(v))
res = getattr(v, "__isabstractmethod__", False)
print("Finished getattr:", res)
