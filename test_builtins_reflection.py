print("Checking globals()...")
g = globals()
print("Globals type:", type(g))
print("Globals contains 'g':", 'g' in g)

print("\nChecking locals()...")
def f():
    x = 10
    l = locals()
    print("Locals type:", type(l))
    print("Locals contains 'x':", 'x' in l)
    return l

f()

print("\nChecking vars()...")
class C:
    def __init__(self):
        self.a = 1
c = C()
v = vars(c)
print("Vars(c) type:", type(v))
print("Vars(c) contains 'a':", 'a' in v)
