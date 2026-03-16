l = []
app = l.append
print("LIST BEFORE:", l)
app(1)
print("LIST AFTER app(1):", l)
if l == [1]:
    print("BOUND APPEND OK")
else:
    print("BOUND APPEND FAILED")

def my_sum(a, b):
    return a + b

class A:
    def __init__(self, val):
        self.val = val
    def add(self, x):
        return self.val + x

obj = A(10)
meth = obj.add
print("BOUND METH CALL:", meth(5))
if meth(5) == 15:
    print("BOUND CUSTOM OK")
else:
    print("BOUND CUSTOM FAILED")
