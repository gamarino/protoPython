class A: pass

print("Hash of type:", hash(type))
print("Hash of A:", hash(A))
print("type(A).__hash__:", type(A).__hash__)
