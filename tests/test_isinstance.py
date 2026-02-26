class Custom: pass
print("isinstance(Custom, type):", isinstance(Custom, type))
print("issubclass(Custom, type):", issubclass(Custom, type))
class CustomMeta(type): pass
print("isinstance(CustomMeta, type):", isinstance(CustomMeta, type))
def g(): yield 1
G = type(g())
print("isinstance(G, type):", isinstance(G, type))
