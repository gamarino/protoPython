class MetaA(type):
    def __new__(mcls, name, bases, ns):
        print('MetaA.__new__')
        return super().__new__(mcls, name, bases, ns)

class MetaB(MetaA):
    def __new__(mcls, name, bases, ns):
        print('MetaB.__new__')
        return super().__new__(mcls, name, bases, ns)

class C(metaclass=MetaB):
    pass
