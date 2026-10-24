import sys
from contextlib import suppress


def risky(n):
    if n > 2:
        raise ValueError("too big: %d" % n)
    return n


def handled():
    try:
        risky(5)
    except (KeyError, ValueError) as e:
        print("caught", e)


def native():
    try:
        return 1 / 0
    except ZeroDivisionError:
        return None


class Countdown:
    def __init__(self):
        self.i = 3

    def __iter__(self):
        return self

    def __next__(self):
        self.i -= 1
        if self.i < 0:
            raise StopIteration
        return self.i


def gen():
    yield 1
    yield 2


def internal_flow():
    # Iteration ends, a generator closed, a default of a lookup, a
    # suppressed error: none of these is a stop.
    total = 0
    for x in iter([1, 2, 3]):
        total += x
    for y in Countdown():
        total += y
    for z in gen():
        total += z
    g = gen()
    next(g)
    g.close()
    total += next(iter([]), 10)
    total += getattr(total, "missing", 1)
    with suppress(KeyError):
        {}["absent"]
    print("internal", total)


def wrong_handler():
    try:
        risky(7)
    except KeyError:
        print("not reached")


internal_flow()
handled()
native()
if "crash" in sys.argv:
    wrong_handler()
print("finished")
