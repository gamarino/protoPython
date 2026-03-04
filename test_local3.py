class _localimpl:
    def __init__(self):
        self.dicts = {}
    def create_dict(self):
        pass

class local:
    __slots__ = '_local__impl', '__dict__'

    def __new__(cls, /, *args, **kw):
        print("1")
        if (args or kw) and (cls.__init__ is object.__init__):
            raise TypeError("Initialization arguments are not supported")
        print("2")
        self = object.__new__(cls)
        print("3")
        impl = _localimpl()
        print("4")
        impl.localargs = (args, kw)
        print("5", impl)
        object.__setattr__(self, '_local__impl', impl)
        print("6")
        impl.create_dict()
        print("7")
        return self

print("calling local")
l = local()
print("done", l)
