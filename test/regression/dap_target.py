import sys


def add(a, b):
    total = a + b
    return total

# the line above is blank on purpose, this one is a comment
def outer(n):
    items = [n, n * 2]
    r = add(n, 1)
    return r + len(items)


print("before")
data = {"k": [1, 2, 3]}
x = outer(5)
print("after", x)
sys.exit(3 if x != 8 else 0)
