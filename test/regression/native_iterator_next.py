# Calling __next__ on an exhausted built-in iterator raises StopIteration.
# heapq.merge calls the bound __next__ methods of its inputs and relies on the
# exception to drop an exhausted input.
import heapq


def raises_when_exhausted(it, limit=100):
    for _ in range(limit):
        try:
            it.__next__()
        except StopIteration:
            return True
    return False


cases = {
    "list": lambda: iter([1, 2]),
    "tuple": lambda: iter((1, 2)),
    "str": lambda: iter("ab"),
    "bytes": lambda: iter(b"ab"),
    "dict": lambda: iter({1: 2}),
    "dict.values": lambda: iter({1: 2}.values()),
    "dict.items": lambda: iter({1: 2}.items()),
    "set": lambda: iter({1, 2}),
    "range": lambda: iter(range(2)),
    "zip": lambda: zip([1, 2], [3, 4]),
    "map": lambda: map(abs, [1, 2]),
    "filter": lambda: filter(None, [1, 2]),
    "enumerate": lambda: enumerate([1, 2]),
    "reversed": lambda: reversed([1, 2]),
}
for name, make in cases.items():
    assert raises_when_exhausted(make()), name + ".__next__() did not raise StopIteration"

# The values before exhaustion are unchanged.
it = iter([1, 2])
nxt = it.__next__
assert nxt() == 1 and nxt() == 2
assert not isinstance(getattr(it, "__next__"), type(None))

# Loops and next() keep working on the same iterators.
assert sum(x for x in [1, 2, 3]) == 6
assert list(zip("ab", range(5))) == [("a", 0), ("b", 1)]
assert next(iter([]), "default") == "default"

# heapq.merge.
assert list(heapq.merge([1, 3], [2, 4])) == [1, 2, 3, 4]
assert list(heapq.merge([1, 3, 5, 7], [0, 2, 4, 8], [5, 10, 15, 20], [], [25])) == [
    0, 1, 2, 3, 4, 5, 5, 7, 8, 10, 15, 20, 25]
assert list(heapq.merge(["dog", "horse"], ["cat", "fish", "kangaroo"], key=len)) == [
    "dog", "cat", "fish", "horse", "kangaroo"]
assert list(heapq.merge([7, 3], [8, 2], reverse=True)) == [8, 7, 3, 2]
assert list(heapq.merge([1, 2])) == [1, 2]
assert list(heapq.merge()) == []

print("native_iterator_next: ok")
