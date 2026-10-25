def sign(n):
    if n < 0:
        return -1
    elif n == 0:
        return 0
    return 1


def parity(n):
    return "even" if n % 2 == 0 else "odd"
