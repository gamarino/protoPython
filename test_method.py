class A:
    def greet(self):
        return "hello"
a = A()
try:
    print(f"a.greet() returns: {repr(a.greet())}")
except Exception as e:
    print(f"Exception: {e}")
