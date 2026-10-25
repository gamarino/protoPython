def make_adder(n):
    def add(x):
        return x + n
    return add


def counter():
    count = 0
    def bump():
        nonlocal count
        count += 1
        return count
    return bump


square = lambda x: x * x
