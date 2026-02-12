f = lambda x, y: x + y
print("1 + 2 =", f(1, 2))

g = lambda: 42
print("g() =", g())

h = lambda x: lambda y: x * y
mul2 = h(2)
print("2 * 3 =", mul2(3))

# Lambda in call
print("Sum [1,2,3]:", (lambda l: l[0]+l[1]+l[2])([1,2,3]))
