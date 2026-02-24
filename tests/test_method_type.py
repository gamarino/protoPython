class Foo:
    def send(self, value):
        raise StopIteration

print("Foo.send =", Foo.__dict__['send'])
