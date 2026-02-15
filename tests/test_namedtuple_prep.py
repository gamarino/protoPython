names = 'a b'.split()
print(f"split names: {names}")
print(f"type(names[0]): {type(names[0])}")

mapped = list(map(str, names))
print(f"mapped names: {mapped}")
print(f"type(mapped[0]): {type(mapped[0])}")
print(f"Match: {type(mapped[0]) is str}")

combined = ['TokenInfo'] + mapped
print(f"combined: {combined}")
for name in combined:
    print(f"name: {name}, type: {type(name)}, is str: {type(name) is str}")
