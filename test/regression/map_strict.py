# Regression test: map(func, *iterables, strict=True), new in Python 3.14.
#
# protoPython ignored the keyword, so iterables of different lengths were
# silently truncated instead of raising ValueError like zip(strict=True).


def mismatch(fn, message):
    try:
        fn()
    except ValueError as exc:
        assert str(exc) == message, str(exc)
    else:
        raise AssertionError("expected ValueError: " + message)


add = lambda a, b: a + b
first = lambda a, b, c: a
assert list(map(add, [1, 2], [3, 4], strict=True)) == [4, 6]
assert list(map(str, [1, 2], strict=True)) == ['1', '2']
assert list(map(add, [], [], strict=True)) == []
assert list(map(add, [1, 2, 3], [1], strict=False)) == [2]
assert list(map(add, [1, 2, 3], [1])) == [2]
mismatch(lambda: list(map(add, [1], [2, 3], strict=True)),
         "map() argument 2 is longer than argument 1")
mismatch(lambda: list(map(add, [1, 2], [3], strict=True)),
         "map() argument 2 is shorter than argument 1")
mismatch(lambda: list(map(first, [1, 2], [1, 2], [1], strict=True)),
         "map() argument 3 is shorter than arguments 1-2")
mismatch(lambda: list(map(first, [1], [1], [1, 2], strict=True)),
         "map() argument 3 is longer than arguments 1-2")

# Items before the mismatch are still produced.
it = map(add, [1, 2], [10], strict=True)
assert next(it) == 11
mismatch(lambda: next(it), "map() argument 2 is shorter than argument 1")


def boom():
    yield 1
    raise KeyError("boom")


try:
    list(map(add, boom(), [1, 2], strict=True))
except KeyError:
    pass
else:
    raise AssertionError("an iterator's own exception must propagate")

print("map strict OK")
