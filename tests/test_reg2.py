import abc
class MyABC(metaclass=abc.ABCMeta):
    pass
def myfunc():
    pass
print("isinstance myfunc type?", isinstance(myfunc, type))
try:
    MyABC.register(myfunc)
except Exception as e:
    print("register myfunc exception:", type(e), e)
