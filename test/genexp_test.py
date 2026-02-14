l = [1, 2, 3]
g = (x * 2 for x in l)
print(type(g))
for i in g:
    print(i)

# Nested
g2 = (x + y for x in [10, 20] for y in [1, 2])
for i in g2:
    print(i)

# With if
g3 = (x for x in range(10) if x % 2 == 0)
for i in g3:
    print(i)
