print("object:", object)
print("object.__setattr__:", object.__setattr__)

class _localimpl:
    def __init__(self):
        self.dicts = {}
    def create_dict(self):
        pass

class local:
    __slots__ = '_local__impl', '__dict__'

    def __new__(cls, /, *args, **kw):
        self = object.__new__(cls)
        impl = _localimpl()
        impl.localargs = (args, kw)
        
        try:
            print("Trying object.__setattr__")
            res = object.__setattr__(self, '_local__impl', impl)
            print("Success, res=", res)
        except AttributeError as e:
            print("CAUGHT AttributeError:", e)
        except Exception as e:
            print("CAUGHT Exception:", e)

        impl.create_dict()
        return self

print("calling local")
l = local()
print("done", l)

