import dis
d = {'a': 1}
gen = ((v, k) for k, v in d.items())
print("Generator bytecode:")
dis.dis(gen.gi_code)
