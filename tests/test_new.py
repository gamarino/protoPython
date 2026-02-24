class C: pass
print("C.__new__(C) =", C.__new__(C))
print("object.__new__(C) =", object.__new__(C))
print("type.__new__(C) =", type.__new__(C))
