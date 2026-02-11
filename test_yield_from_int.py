def subgen():
    yield 1
    yield 2
    return 3

def main_gen():
    res = yield from subgen()
    yield res
    yield 4

g = main_gen()
for i in range(4):
    try:
        val = next(g)
        print("Val:", val)
    except StopIteration:
        print("Fin")
        break
