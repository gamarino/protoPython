def g():
    yield 1
gen = g()
gen_type = type(gen)
print("gen_type:", gen_type)
print("type(gen_type):", type(gen_type))
print("isinstance(gen_type, type):", isinstance(gen_type, type))
print("issubclass(type(gen_type), type):", issubclass(type(gen_type), type))
