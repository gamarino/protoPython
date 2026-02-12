def count():
    yield 1
    yield 2
    yield 3

c = count()
print(next(c))
print(next(c))
print(next(c))
