class Foo:
    __slots__ = ()
    def send(self):
        pass

print("Foo.__dict__['send'] =", Foo.__dict__['send'])
