class A:
    def __init__(self, x):
        print("A.__init__", x)

class B(object):
    pass

class C(B, A):
    def __init__(self, x):
        print("C.__init__", x)
        super().__init__(x)

print("Starting C(10)")
c = C(10)
print("Finished C(10)")
