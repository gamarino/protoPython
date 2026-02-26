import _collections_abc

class DummyCoro:
    pass

print("Testing issubclass")
try:
    print(issubclass(DummyCoro, _collections_abc.Coroutine))
except Exception as e:
    print("Exception", e)
print("Finished")
