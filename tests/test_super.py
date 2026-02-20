class Base:
    def __init__(self):
        print("Base init", self)
class Child(Base):
    def __init__(self, color=True):
        super().__init__()
        try:
            print("hasattr self method2:", hasattr(self, "method2"))
        except Exception as e:
            print("Error:", e)
    def method2(self): return "found!"

Child(color=True)
