def f(a, b):
    return a + b

print(f(2, 3))       # Positional: 5
print(f(a=1, b=4))   # Keyword: 5
print(f(b=10, a=5))  # Swapped keyword: 15
print(f(7, b=3))     # Positional + Keyword: 10
