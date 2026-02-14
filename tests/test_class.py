class A:
    def __init__(self, x):
        print("A.__init__ called with x =", x)
        self.x = x

print("Calling A(10)...")
a = A(10)
print("a is", a)
print("type(a) is", type(a))
print("a.x is", getattr(a, 'x', 'MISSING'))
