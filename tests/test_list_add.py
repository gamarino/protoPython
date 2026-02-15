l1 = ['a']
l2 = ['b', 'c']
l3 = l1 + l2
print(f"l1: {l1}")
print(f"l2: {l2}")
print(f"l3: {l3}")
for x in l3:
    print(f"item: {x}, type: {type(x)}")
