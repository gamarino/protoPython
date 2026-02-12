def gen():
    yield "hello"
    yield "world"

g = gen()
print(next(g))
print(next(g))
