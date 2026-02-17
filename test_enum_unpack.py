from enum import Enum, auto

class Color(Enum):
    RED = auto()
    GREEN = auto()
    BLUE = auto()

print(f"Members: {list(Color)}")
r, g, b = Color
print(f"R={r}, G={g}, B={b}")
