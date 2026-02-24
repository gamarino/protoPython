class Foo:
    __slots__ = ()
    def bar(self):
        pass

print("Foo.bar =", Foo.bar)
