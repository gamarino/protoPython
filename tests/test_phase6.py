# Test Slicing
l = [0, 1, 2, 3, 4, 5]
print("List:", l)
s1 = l[1:4]
print("l[1:4]:", s1)
s2 = l[:3]
print("l[:3]:", s2)
s3 = l[2:]
print("l[2:]:", s3)
s4 = l[::2]
print("l[::2]:", s4)

# Test Delete
x = 10
print("x before del:", x)
del x
try:
    print(x)
except NameError:
    print("x deleted successfully")

d = {"a": 1, "b": 2}
print("d before del:", d)
del d["a"]
print("d after del d['a']:", d)

class A:
    pass
obj = A()
obj.attr = 42
print("obj.attr before del:", obj.attr)
del obj.attr
try:
    print(obj.attr)
except AttributeError:
    print("obj.attr deleted successfully")

# Test Assert
print("Testing assert True")
assert True
print("Assert True passed")

print("Testing assert False")
try:
    assert False, "Verification failed"
except AssertionError as e:
    print("Assert False caught:", e)

print("Phase 6 tests completed")
