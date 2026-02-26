from abc import abstractmethod

class Awaitable: pass

class Coroutine(Awaitable):
    __slots__ = ()

    @abstractmethod
    def send(self, value):
        ...

print("START GETATTR TEST")
v = Coroutine.__dict__['send']
print("Got send:", type(v))
res = getattr(v, "__isabstractmethod__", False)
print("Finished getattr:", res)
