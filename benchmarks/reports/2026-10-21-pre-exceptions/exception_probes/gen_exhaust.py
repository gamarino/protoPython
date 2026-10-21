def g():
    yield 1
    yield 2
def run(n):
    c = 0; i = 0
    while i < n:
        for v in g(): c += v
        i += 1
    return c
print("CHECK", run(200000))
