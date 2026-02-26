import abc
class MyABC(metaclass=abc.ABCMeta):
    pass
print("hasattr:", hasattr(MyABC, '_abc_registry'))
try:
    print("getattr:", getattr(MyABC, '_abc_registry'))
    print("direct:", MyABC._abc_registry)
except Exception as e:
    print("Exception:", type(e), e)
