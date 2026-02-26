class Base:
    def method(self): return "base"
class Meta(type):
    def method(cls): return "meta"
class C(Base, metaclass=Meta):
    pass
print("C.method:", C.method(C))
