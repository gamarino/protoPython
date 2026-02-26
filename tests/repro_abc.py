from _py_abc import ABCMeta

print("Imported ABCMeta")

class Test(metaclass=ABCMeta):
    pass

print("Created Test class")
