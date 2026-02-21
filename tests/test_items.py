d = {'a': 1, 'b': 2}
print("Items:", list(d.items()))

def gen():
    yield 1
    yield 2

print("Gen list:", list(gen()))
