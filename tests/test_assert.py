print("Testing len...")
print(len({'a': 1, 'b': 2}))

print("Testing dict generator...")
d = dict((v, k) for k, v in {'a': 1, 'b': 2}.items())
print("Dict length:", len(d))

print("Testing assert...")
try:
    assert len(d) == 5
except Exception as e:
    print("Caught:", type(e))

print("Done")
