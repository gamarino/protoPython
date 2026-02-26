class Foo:
    def send(self, value):
        """docstring"""
        raise StopIteration
print("Foo.send =", Foo.__dict__['send'])
