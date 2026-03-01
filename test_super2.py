class MetaB(type):
    def __new__(mcls, name, bases, ns):
        return super().__new__(mcls, name, bases, ns)

print(MetaB.__new__.__code__.co_freevars)
