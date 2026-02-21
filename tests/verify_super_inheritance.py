class A:
    def foo(self):
        return "A"

class B(A):
    def foo(self):
        return super().foo()

class C(B):
    def foo(self):
        return super().foo()

try:
    c = C()
    print(c.foo())
except RecursionError:
    print("RecursionError caught")
except Exception as e:
    print(f"Caught {type(e).__name__}: {e}")
