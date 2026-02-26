import abc
class MyABC(metaclass=abc.ABCMeta):
    pass
print("MyABC has _abc_registry:", hasattr(MyABC, '_abc_registry'))
