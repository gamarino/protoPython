"""list.__init__ clears the list before extending it, as CPython."""
x = [1, 2]
x.__init__(x)
assert x == [], x
y = [3]
y.__init__([4, 5])
assert y == [4, 5]
y.__init__()
assert y == []

print("list init self OK")
