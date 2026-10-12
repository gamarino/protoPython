# Regression test: tuples of fresh objects are collected.
#
# Python tuples were protoCore ProtoTuples, which all go through protoCore's
# tuple interner: its table is keyed by the element pointers, is a GC root and
# never drops an entry, so every tuple ever built stayed alive with everything
# it held.  A call taking *args, a tuple display of new objects, dict.items(),
# zip() or enumerate() left about ten cells per operation behind, and a loop of
# them ran out of any fixed heap.  Tuples are now immutable ProtoList-backed
# objects, collected like anything else.  CTest runs this under a fixed heap
# (PROTOCORE_HEAP_LIMIT_CELLS, the adaptive controller off); before the change
# it ran out of memory.
import sys


class Item:
    def __init__(self, v):
        self.v = v


def star(*args):
    return len(args) + args[0].v


def star_kw(*args, **kwargs):
    return len(args) + len(kwargs)


def displays(i):
    t = (Item(i), Item(i + 1), str(i))
    u = t + (Item(i),)
    a, b, c = t
    return len(u) + a.v + b.v - 2 * i + len(t[1:])


def builtins(i):
    total = 0
    for k, v in {"k" + str(i): i, "x": 1}.items():
        total += v
    for j, x in enumerate([Item(i), Item(i)]):
        total += j
    for x, y in zip([Item(1)], [Item(2)]):
        total += y.v - x.v
    q, r = divmod(i + 7, 3)
    return total + q * 3 + r - (i + 7) - i


def main(n):
    total = 0
    for i in range(n):
        args = (Item(i), Item(i + 1))
        total += star(*args) - i
        total += star_kw(Item(i), k=Item(i))
        total += displays(i)
        total += builtins(i) - 1
        total += len(tuple([Item(i), Item(i)]))
    return total


N = int(sys.argv[1]) if len(sys.argv) > 1 else 100000
# per iteration: star 2, star_kw 2, displays 7, builtins 2, tuple() 2: 15
assert main(N) == 15 * N, main(N)
print("tuple bounded memory OK")
