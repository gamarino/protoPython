"""sorted() accepts any iterable: builtin subclasses and Python-level iterators."""


class L(list):
    pass


class Countdown:
    def __init__(self):
        self.i = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.i += 1
        if self.i > 3:
            raise StopIteration
        return 4 - self.i


assert sorted(L([2, 1])) == [1, 2]
assert sorted(Countdown()) == [1, 2, 3]
assert sorted({"b": 1, "a": 2}) == ["a", "b"]
assert sorted(x for x in (3, 1)) == [1, 3]
assert sorted(frozenset([2, 1]), reverse=True) == [2, 1]
assert sorted([3, 1, 2], key=lambda v: -v) == [3, 2, 1]

print("sorted iterables OK")
