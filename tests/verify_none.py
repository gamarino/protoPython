# verify_none.py
print("bool(None) =", bool(None))
if None:
    print("None is truthy! FAIL")
else:
    print("None is falsy. SUCCESS")

print("type(None) =", type(None))
print("repr(None) =", repr(None))
