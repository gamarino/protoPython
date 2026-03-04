print("hasattr(object, '__init__'):", hasattr(object, "__init__"))
try:
    print("object.__init__:", object.__init__)
except AttributeError as e:
    print("AttributeError on object.__init__:", e)
class MyClass:
    pass
print("hasattr(MyClass, '__init__'):", hasattr(MyClass, "__init__"))
try:
    print("MyClass.__init__:", MyClass.__init__)
except AttributeError as e:
    print("AttributeError on MyClass.__init__:", e)
