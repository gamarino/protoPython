def dec(f):
    return f

class Foo:
    @dec
    def send(self):
        pass

print("Foo.send =", Foo.__dict__['send'])
