def dec(f):
    return f

class Meta(type):
    def __new__(mcls, name, bases, ns, **kw):
        print("Meta __new__ seeing send:", ns.get('send'))
        return super().__new__(mcls, name, bases, ns, **kw)

class Base(metaclass=Meta):
    pass

class Sub(Base):
    @dec
    def send(self):
        pass

print("Sub.send =", Sub.__dict__.get('send'))
