class C: pass
print("C.__new__(C): ", C.__new__(C))
print("C(): ", C())
