import enum
print("Testing EnumType.mro()...")
try:
    m = enum.EnumType.mro()
    print(f"EnumType MRO: {m}")
    names = [c.__name__ for c in m]
    print(f"Names: {names}")
except Exception as e:
    print(f"Failed: {e}")
