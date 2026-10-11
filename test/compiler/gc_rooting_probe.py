# Compiled-module workloads that allocate in loops long enough for collections
# to run under a heap limit (protoPython issue #2).  Each function returns a
# value the driver checks, so a freed cell shows up as a wrong answer or a
# crash, not as silence.


def build(n):
    # The reproduction from issue #2: a while loop that allocates.
    lst = []
    i = 0
    while i < n:
        lst.append([i, i])
        i += 1
    return len(lst)


def for_sum(n):
    # A for loop over a list it allocates, building and dropping objects.
    total = 0
    pairs = []
    for i in range(n):
        pairs.append((i, i + 1))
    for p in pairs:
        q = [p, p]
        total += len(q)
    return total


def closure_calls(n):
    # A nested function that captures an enclosing variable, called per
    # iteration.
    base = [7]

    def add(x):
        return x + base[0]

    acc = 0
    i = 0
    while i < n:
        acc = add(acc) - 6
        i += 1
    return acc


def comprehensions(n):
    # List, dict and set comprehensions and a generator expression, rebuilt
    # every iteration.
    total = 0
    for i in range(n):
        sq = [x * x for x in range(8)]
        d = {x: [x] for x in range(4)}
        s = {x % 3 for x in range(9)}
        g = sum(x for x in range(5))
        total += len(sq) + len(d) + len(s) + g
    return total


def raises(n):
    # An exception raised and caught on every iteration.
    caught = 0
    i = 0
    while i < n:
        try:
            raise ValueError("x" + str(i))
        except ValueError:
            caught += 1
        i += 1
    return caught


def lambdas(n):
    f = lambda a, b: [a, b]
    total = 0
    for i in range(n):
        total += len(f(i, i))
    return total


class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def norm1(self):
        return self.x + self.y


def objects(n):
    total = 0
    for i in range(n):
        p = Point(i, 1)
        total += p.norm1()
    return total


def strings(n):
    parts = []
    for i in range(n):
        parts.append("v" + str(i))
    return len(parts)


# A module-level loop runs in proto_module_init, on the importer's thread.
MODULE_ROWS = []
for _i in range(3000):
    MODULE_ROWS.append([_i, str(_i)])
