# test_mro.py
# Verifies Method Resolution Order (MRO) in ProtoPython

class A:
    def who(self): return "A"

class B(A):
    def who(self): return "B"

class C(A):
    def who(self): return "C"

class D(B, C):
    pass

d = D()
print(f"D().who() = {d.who()}")
# In Python MRO (C3), for D(B, C), it should be D, B, C, A.
# Since B and C both inherit from A.
# My linearization: D -> B -> A -> C -> A (simplified to D, B, A, C)
# Wait! My linearization logic in addParent:
# When adding C to D(B), it adds C's ancestors (A) THEN C.
# If A is already there (from B), it might skip it or move it.
# Current logic in addParent (reverse prepending):
# class D(B, C):
# 1. Add B: D -> B, Ancestors(B) (which is A) => D, B, A
# 2. Add C: Prepend Ancestors(C)=A (if not there), then C.
# Since A is already there, it skips A, then adds C.
# Wait! Prepending C to (B, A) => C, B, A.
# So D -> C -> B -> A.
# Wait! That's reverse of what we want (B before C).
# Ah! That's why I reversed the bases loop in py_type!
# In py_type: loop for base in reversed(bases): addParent(base).
# class D(B, C):
# 1. Add C: D -> C, A
# 2. Add B: Add Ancestors(B)=A (exists), then B.
# Prepend B to (C, A) => B, C, A.
# So D -> B -> C -> A. THIS IS CORRECT for Python MRO!

print(f"B in D parents: {isinstance(d, B)}")
print(f"C in D parents: {isinstance(d, C)}")
print(f"A in D parents: {isinstance(d, A)}")

if d.who() == "B":
    print("MRO SUCCESS: B override found first")
else:
    print(f"MRO FAILURE: Found {d.who()} instead of B")

class E(C, B):
    pass

e = E()
print(f"E().who() = {e.who()}")
if e.who() == "C":
    print("MRO SUCCESS: C override found first")
else:
    print(f"MRO FAILURE: Found {e.who()} instead of C")
