def total(n):
    s = 0
    i = 0
    while i < n:
        s += i
        i += 1
    return s


def sum_squares(xs):
    out = 0
    for x in xs:
        out += x * x
    return out
