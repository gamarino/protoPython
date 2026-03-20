import enum
print(f"Enum.mro() = {[c.__name__ for c in enum.Enum.mro()]}")
print(f"EnumType.mro() = {[c.__name__ for c in enum.EnumType.mro()]}")
