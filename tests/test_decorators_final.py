def dec(f):
    print("Decorating function")
    def wrap(a):
        return f(a) + 1
    return wrap

@dec
def double(x):
    return x * 2

print("Double(10):", double(10))

def cls_dec(c):
    print("Decorating class")
    c.flag = True
    return c

@cls_dec
class A:
    def __init__(self, val):
        self.val = val

print("A.flag:", A.flag)
obj = A(42)
print("obj.val:", obj.val)
