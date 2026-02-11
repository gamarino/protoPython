class C:
    def greet(self):
        print("Hello from C")

class D(C):
    pass

print("Testing simple inheritance")
d = D()
d.greet()

class E:
    def greet(self):
        print("Hello from E")

class F(E):
    def greet(self):
        print("Hello from F")
        E.greet(self)

print("Testing method override and parent call")
f = F()
f.greet()
