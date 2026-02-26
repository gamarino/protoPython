class Meta(type):
    def __new__(mcls, name, bases, namespace):
        print("Meta.__new__ called for", name)
        cls = type.__new__(mcls, name, bases, namespace)
        cls._my_registry = set()
        return cls

class Base(metaclass=Meta):
    pass

class Derived(Base):
    pass

print("type(Derived):", type(Derived))
print("Derived has registry:", hasattr(Derived, '_my_registry'))
