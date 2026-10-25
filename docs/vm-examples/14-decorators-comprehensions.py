def trace(f):
    return f


@trace
def double(x):
    return 2 * x


evens = [x for x in range(10) if x % 2 == 0]
squares = {x: x * x for x in range(3)}
