X = type('X', (), {'a': 1})
print(f"X = {X}")
print(f"X.a = {getattr(X, 'a', 'MISSING')}")
