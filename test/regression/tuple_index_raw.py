# Regression test: tuple.index and tuple.count work on raw tuples.
#
# Both read the receiver's __data__ attribute. A raw ProtoTuple, such as the
# tuple bound to *args, has none, so index() returned None instead of the
# position and count() returned 0. typing's _GenericAlias.__mro_entries__
# calls bases.index(self) on such a tuple.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


def args_tuple(*args):
    return args


t = args_tuple(1, 2, 3, 2)
check("index on *args", t.index(2), 1)
check("index with start", t.index(2, 2), 3)
check("count on *args", t.count(2), 2)
check("index on a literal tuple", (5, 6, 7).index(7), 2)
check("count on a literal tuple", (5, 5).count(5), 2)
try:
    t.index(99)
except ValueError:
    pass
else:
    raise AssertionError("index of a missing element did not raise ValueError")

print("tuple index raw OK")
