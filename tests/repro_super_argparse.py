class A:
    def __init__(self, x):
        print("A.__init__", x)

class B(object):
    pass

class C(B, A):
    def __init__(self, x):
        print("C.__init__", x)
        super().__init__(x)

print("id(object) in script:", hex(id(object)))
print("MRO of A:", [(cls.__name__, hex(id(cls))) for cls in A.__mro__])
print("MRO of B:", [(cls.__name__, hex(id(cls))) for cls in B.__mro__])
print("MRO of C:", [(cls.__name__, hex(id(cls))) for cls in C.__mro__])
print("Starting C(10)")
c = C(10)
print("Finished C(10)")
