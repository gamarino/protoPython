import argparse
n = argparse.Namespace()
print(f"n is argparse.Namespace: {n is argparse.Namespace}")
print(f"type(n): {type(n)}")

class MyClass:
    pass

m = MyClass()
print(f"m is MyClass: {m is MyClass}")
print(f"type(m): {type(m)}")
