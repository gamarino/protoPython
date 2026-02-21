d = {'a': 1, 'b': 2}
g = ((v, k) for k, v in d.items())
print("Generator:", list(g))
for x in g:
    print(x)
