import sys
import _py_abc

print("Loading collections.abc")
import _collections_abc

print("Finished importing collections_abc. Registering dummy.")
class DummyCoro:
    pass

class MyCoro(_collections_abc.Coroutine):
    def send(self): pass
    def throw(self): pass
    def close(self): pass
    def __await__(self): pass

_collections_abc.Coroutine.register(DummyCoro)
print("Finished register.")

