# Compiled by protopyc, generators and classes behave as the interpreter runs
# the same source; protopyc_generators_classes.sh compares the two outputs.

LOG = []


def count_up(start, stop, step=1):
    i = start
    while i < stop:
        yield i
        i = i + step


def running_totals(n):
    total = 0
    for k in range(n):
        total = total + k
        yield total
    return total * 10


def echo(first):
    got = first
    while True:
        got = yield got
        if got is None:
            break
        got = got * 2
    return "echo done"


def catcher():
    try:
        yield 1
        yield 2
    except ValueError as e:
        yield "caught " + str(e)
    yield "after"


def guarded(tag):
    try:
        yield tag + "-a"
        yield tag + "-b"
    finally:
        LOG.append(tag + " finally")


def inner(n):
    for i in range(n):
        yield i * i
    return "inner returned %d" % n


def outer(n):
    result = yield from inner(n)
    yield result
    yield from [100, 200]


def branches(n):
    for i in range(n):
        if i % 3 == 0:
            yield "fizz %d" % i
        elif i % 3 == 1:
            continue
        else:
            yield i
    else:
        yield "for-else"
    j = 0
    while j < 2:
        yield "while %d" % j
        j += 1
    else:
        yield "while-else"


def make_counter(base):
    offset = base * 100

    def gen(n):
        for i in range(n):
            yield offset + i
    return gen


def rows(n):
    # Allocates per item: the generator's state must survive collections
    # that run while it is suspended.
    for i in range(n):
        row = [i, str(i), {"k": i}]
        yield row


def nones(n):
    for i in range(n):
        yield None


SCALE = 3


class Point:
    origin_name = "origin"

    def __init__(self, x, y):
        self.x = x
        self.y = y

    def moved(self, dx):
        return Point(self.x + dx, self.y)

    def scaled(self):
        return Point(self.x * SCALE, self.y * SCALE)

    def describe(self):
        return describe_point(self)


def describe_point(p):
    return "Point(%d, %d)" % (p.x, p.y)


def make_point():
    return Point(1, 2)


def later_caller():
    return defined_later() + 1


def defined_later():
    return 41


def nested_def(n):
    def helper(v):
        return v + n
    return helper(10)


def lambda_global():
    f = lambda v: v * SCALE
    return f


def lambda_closure(n):
    return lambda v: v + n


class Bag:
    def __init__(self, items):
        self.items = items

    def __iter__(self):
        for x in self.items:
            yield x

    def evens(self):
        for x in self.items:
            if x % 2 == 0:
                yield Bag([x])

    def total(self):
        return sum_bag(self)


def sum_bag(bag):
    t = 0
    for x in bag:
        t = t + x
    return t
