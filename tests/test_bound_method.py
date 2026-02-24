from abc import ABCMeta
class A(metaclass=ABCMeta):
    pass
print("ABOUT TO CALL A.register")
A.register(None)
