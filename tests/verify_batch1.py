class Base:
    def __init__(self):
        self._val = 0
    def set_val(self, v):
        self._val = v
    def get_val(self):
        return self._val
    x = property(get_val, set_val)

class Derived(Base):
    def get_val(self):
        return super(Derived, self).get_val() + 10

print("Testing property and super...")
d = Derived()
d.x = 42
print("Value via property:", d.x)
# print("Value via super call in get_val:", d.get_val()) # super proxy might be tricky

print("\nTesting isinstance with tuples...")
print("isinstance(1, (int, str)):", isinstance(1, (int, str)))

print("\nTesting recursive dir()...")
print("'set_val' in dir(Derived):", 'set_val' in dir(Derived))

print("\nTesting abs() and round()...")
print("abs(-5.5):", abs(-5.5))
print("round(1234.56, -1):", round(1234.56, -1))
print("round(1234.56, 1):", round(1234.56, 1))
