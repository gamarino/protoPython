def countdown(n):
    while n > 0:
        yield n
        n -= 1


def chain(a, b):
    yield from a
    yield from b


doubled = (x * 2 for x in range(3))
