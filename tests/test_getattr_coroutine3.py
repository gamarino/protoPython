from abc import ABCMeta

def abstractmethod(func):
    return func

class Awaitable(metaclass=ABCMeta):
    __slots__ = ()
    @abstractmethod
    def __await__(self):
        yield

class Coroutine(Awaitable):
    __slots__ = ()

    @abstractmethod
    def send(self, value):
        raise StopIteration

    @abstractmethod
    def throw(self, typ, val=None, tb=None):
        pass

    def close(self):
        pass

print("START GETATTR TEST")
print("Coroutine.__dict__ type:", type(Coroutine.__dict__))
print("getattr(Coroutine, 'send') =", getattr(Coroutine, 'send', 'NOT_FOUND'))
try:
    print("Coroutine.__dict__['send'] =", Coroutine.__dict__['send'])
except Exception as e:
    print("Coroutine.__dict__ error:", e)
