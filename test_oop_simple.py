class A:
    def f(self):
        print("A.f called")

print("Instantiating A")
a = A()
print("Calling a.f()")
a.f()
print("Done")
