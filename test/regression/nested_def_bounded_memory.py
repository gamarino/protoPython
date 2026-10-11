# Regression test: defining a named nested function in a call leaves nothing
# behind once the call returns.
#
# protoPython closed over the defining frame: the new function referenced the
# frame (a mutable object) and the frame held the function as a local, so
# every call of `outer` left one cycle between mutable objects, which protoCore
# never collects (protoCore docs/MemoryModel.md, section 7).  A loop calling
# such a function exhausted any fixed heap.  Closures now hold cells, never
# frames, and a recursive nested def binds its own name to itself on entry
# instead of closing over the cell that holds it.  CTest runs this under a
# fixed 2.5 M-cell heap; before the change it ran out of memory.
import sys


def plain(k):
    def get():
        return k
    return get()


def recursive(k):
    def rec(n):
        return k if n == 0 else rec(n - 1)
    return rec(3)


def recursive_generator(k):
    def walk(n):
        if n:
            yield n
            yield from walk(n - 1)
    return sum(walk(3)) + k


def recursion_through_comprehension(k):
    def depth(t):
        return 1 + max([depth(c) for c in t], default=0)
    return depth([[[]]]) + k


def nonlocal_counter(k):
    total = 0
    def add(v):
        nonlocal total
        total += v
    add(k)
    add(1)
    return total


def class_with_super(k):
    class Base:
        def value(self):
            return k
    class Derived(Base):
        def value(self):
            return super().value() + 1
    return Derived().value()


def returned_closure(k):
    x = [k]
    return lambda: x[0]


def main(n):
    total = 0
    for i in range(n):
        total += plain(i) + recursive(i) + recursion_through_comprehension(i)
        total += nonlocal_counter(i) + returned_closure(i)()
    return total


N = int(sys.argv[1]) if len(sys.argv) > 1 else 40000
expected = 0
for i in range(N):
    expected += i + i + (3 + i) + (i + 1) + i
assert main(N) == expected
# Recursive generators are slow in this runtime (yield from); a few suffice.
assert sum(recursive_generator(i) for i in range(200)) == sum(6 + i for i in range(200))
# Classes defined in a call are one cycle each by nature (a class's __mro__
# holds the class); a few of them do not grow the live set.
assert sum(class_with_super(i) for i in range(50)) == sum(i + 1 for i in range(50))
print("nested def bounded memory OK")
