class A:
    pass

class B:
    def __call__(self):
        pass

print("callable(A) =", callable(A))
print("callable(B) =", callable(B))
print("callable(B()) =", callable(B()))
