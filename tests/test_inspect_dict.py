class Meta(type):
    def __new__(mcls, name, bases, namespace, **kwargs):
        cls = super().__new__(mcls, name, bases, namespace, **kwargs)
        print("namespace keys:", list(namespace.keys()))
        print("namespace items:", list(namespace.items()))
        for k, v in namespace.items():
            print("iterating", k)
            getattr(v, "__isabstractmethod__", False)
            print("done with", k)
        return cls

class ABC(metaclass=Meta):
    __slots__ = ()
print("Done")
