class A:
    x = 1

class B(A):
    pass

b = B()
print(f"getattr(b, 'x'): {getattr(b, 'x')}")
print(f"getattr(b, 'x', 99): {getattr(b, 'x', 99)}")

class C:
    y = None

c = C()
print(f"getattr(c, 'y'): {getattr(c, 'y')}")
print(f"getattr(c, 'y', 99): {getattr(c, 'y', 99)}")
