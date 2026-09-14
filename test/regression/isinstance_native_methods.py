# Regression test: native callables are instances of their own types.
#
# getType() classifies native method cells through the native-method side
# table, so type(len) is builtin_function_or_method, but isinstance() only
# walked parent chains, which such cells don't have:
# isinstance(len, types.BuiltinFunctionType) was False.
import types

assert type(len) is types.BuiltinFunctionType
assert isinstance(len, types.BuiltinFunctionType)
assert isinstance(len, types.BuiltinMethodType)
assert isinstance([].append, types.BuiltinMethodType)
assert isinstance(object().__str__, types.MethodWrapperType)
assert isinstance(str.join, types.MethodDescriptorType)
assert isinstance(len, (int, types.BuiltinFunctionType))
assert isinstance(len, object)
assert callable(len)

assert not isinstance(len, types.FunctionType)
assert isinstance(lambda: 0, types.FunctionType)
assert not isinstance(lambda: 0, types.BuiltinFunctionType)
assert not isinstance(1, types.BuiltinFunctionType)
assert not isinstance(len, int)

print("isinstance native methods OK")
