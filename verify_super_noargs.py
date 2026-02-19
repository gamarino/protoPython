
class Base:
    def foo(self): return "base"

class Derived(Base):
    def foo(self):
        print(f"Self: {self}")
        print(f"Locals: {locals()}")
        return super().foo()

try:
    d = Derived()
    res = d.foo()
    print(f"Result: {res}")
    assert res == "base"
    print("SUCCESS")
except Exception as e:
    print(f"FAILED: {e}")

try:
    print("Testing explicit super(Derived, self)...")
    d = Derived()
    res = super(Derived, d).foo()
    print(f"Explicit Result: {res}")
    assert res == "base"
    print("Explicit SUCCESS")
except Exception as e:
    print(f"Explicit FAILED: {e}")

