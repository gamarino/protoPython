def abstractmethod(func):
    func.__isabstractmethod__ = True
    return func

class Coroutine:
    @abstractmethod
    def send(self, value):
        pass

print("getting getattr on send")
v = Coroutine.__dict__['send']
res = getattr(v, "__isabstractmethod__", False)
print(res)
