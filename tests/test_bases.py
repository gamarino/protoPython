class M(type):
    pass

print(f"M bases: {M.__bases__}")
print(f"M mro: {M.__mro__}")

class A:
    pass
print(f"A bases: {A.__bases__}")
