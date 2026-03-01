import argparse

print(f"Namespace.__name__: {argparse.Namespace.__name__}")
n = argparse.Namespace()

print(f"n.__class__.__name__: {n.__class__.__name__}")

class MyClass:
    pass

m = MyClass()
print(f"m.__class__.__name__: {m.__class__.__name__}")
