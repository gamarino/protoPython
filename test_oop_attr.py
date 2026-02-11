class B:
    def __init__(self, x):
        print("B init with", x)
        self.x = x
    def show(self):
        print("Value of x:", self.x)

print("Instantiating B")
b = B(42)
b.show()
