class EnumCheck: pass

enum_class = type(EnumCheck.__name__, (str, ), {})
try:
    print("Testing str.__new__ on type")
    obj = str.__new__(enum_class, "hello")
    print("obj:", obj)
    print("type(obj):", type(obj))
    obj._value_ = "hello"
except Exception as e:
    print("Exception on obj:", type(e), e)
