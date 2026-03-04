class MockEnumDict(dict):
    pass
m = MockEnumDict()
print("m is:", m)
print("m.__class__ is:", m.__class__)
print("type(m) is:", type(m))
try:
    m.setdefault('a', 1)
    print("setdefault ok")
except Exception as e:
    print("Error:", e)
