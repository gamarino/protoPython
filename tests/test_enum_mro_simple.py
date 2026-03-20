import enum
print("Testing Enum.mro()...")
m = enum.Enum.mro()
print(f"MRO: {m}")
for x in m:
    print(f"Item: {x.__name__}")

print("Testing EnumType.mro()...")
mt = enum.EnumType.mro()
print(f"EnumType MRO: {mt}")
for x in mt:
    print(f"Item: {x.__name__}")
