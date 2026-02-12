def gen():
    try:
        yield 1
        yield 2
    except GeneratorExit:
        print("Caught GeneratorExit")
    except ValueError:
        print("Caught ValueError")
    yield 3

print("--- Test next/send ---")
g1 = gen()
print(next(g1))
print(g1.send(None))
print(next(g1))

print("--- Test throw ---")
g2 = gen()
print(next(g2))
try:
    g2.throw(ValueError())
except StopIteration:
    print("Generator stopped after throw")

print("--- Test close ---")
g3 = gen()
print(next(g3))
g3.close()
print("Closed")
