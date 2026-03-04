print("A")
try:
    from _types import *
except ImportError:
    print("Caught ImportError!")
print("B")
