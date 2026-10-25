import functools


def total(xs):
    return functools.reduce(lambda a, b: a + b, xs, 0)


def sum_squares(xs):
    return sum(map(lambda x: x * x, xs))
