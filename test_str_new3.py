class EnumCheck: pass
enum_class = type(EnumCheck.__name__, (str, ), {})
obj = str.__new__(enum_class, "hello")
print(obj.__class__)
