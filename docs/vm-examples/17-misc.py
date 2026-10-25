def read(path):
    with open(path) as f:
        return f.read()


def largest(*args):
    return max(*args)


def label(name, n):
    return f"{name}: {n!r}"
