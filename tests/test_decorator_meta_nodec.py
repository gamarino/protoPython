class Meta(type):
    def __new__(mcls, name, bases, ns, **kw):
        print("Meta __new__ seeing send in", name, ":", ns.get('send'))
        return super().__new__(mcls, name, bases, ns, **kw)

class Base(metaclass=Meta):
    pass

class Sub(Base):
    def send(self):
        pass

print("Sub.send =", Sub.__dict__.get('send'))
