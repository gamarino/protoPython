gen = ((v, k) for k, v in {'a': 1}.items())
code = gen.gi_code
print("co_code:", list(code.co_code))
