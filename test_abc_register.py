class _AbstractBase:
    pass

def _check_methods(C, *methods):
    mro = C.__mro__
    for method in methods:
        for B in mro:
            if method in B.__dict__:
                break
        else:
            return NotImplemented
    return True

class Coroutine(_AbstractBase):
    @classmethod
    def __subclasshook__(cls, C):
        if cls is Coroutine:
            return _check_methods(C, '__await__', 'send', 'throw', 'close')
        return NotImplemented

class MyCoroutine:
    def __await__(self): pass
    def send(self): pass
    def throw(self): pass
    def close(self): pass

print("Checking subclasshook", Coroutine.__subclasshook__(MyCoroutine))

