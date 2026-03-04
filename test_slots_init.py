class MyClassWithoutSlots:
    pass

class MyClassWithSlots:
    __slots__ = 'a', 'b'

print("Without slots:")
print("hasattr __init__:", hasattr(MyClassWithoutSlots, "__init__"))

print("With slots:")
print("hasattr __init__:", hasattr(MyClassWithSlots, "__init__"))
try:
    print(MyClassWithSlots.__init__)
except AttributeError as e:
    print("AttributeError on MyClassWithSlots.__init__:", e)
