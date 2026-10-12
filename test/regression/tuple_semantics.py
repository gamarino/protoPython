# Tuple semantics against CPython (tuple_semantics.expected is its output):
# construction, type and isinstance, hashing and equality, ordering, tuples as
# dict keys and set members, slicing, concatenation and repetition, unpacking,
# nesting, *args / **kwargs, repr, methods, namedtuple, pickling, identity of
# the empty tuple.  Exact hash values are protoPython's own (see
# tuple_hash_values.py); only their consistency is compared here.
import collections
import pickle


def show(label, value):
    print(label, repr(value))


a = (1, "two", 3.0, None, (4, 5))
show("literal", a)
show("type", (type(a).__name__, type(()) is tuple, isinstance(a, tuple), isinstance([], tuple)))
show("constructor", (tuple(), tuple([1, 2]), tuple("ab"), tuple(range(3)), tuple(x * 2 for x in range(3)),
                     tuple({"k": 1}), tuple((1, 2))))
EMPTY = ()
show("empty identity", (EMPTY is (), tuple() is EMPTY, tuple([]) is EMPTY, (1, 2)[2:] is EMPTY))
show("len/bool", (len(a), bool(()), bool((0,)), len(())))
show("index", (a[0], a[-1], a[4][1], a[1:3], a[::-1], a[::2], a[10:], a[-2:]))
show("eq", ((1, 2) == (1, 2), (1, 2) == [1, 2], (1, 2) != (1, 3), () == (), (1, (2, 3)) == (1, (2, 3)),
            (1, 2.0) == (1, 2), (float("nan"),) == (float("nan"),)))
show("order", ((1, 2) < (1, 3), (1, 2) < (1, 2, 0), (2,) > (1, 9), (1, 2) <= (1, 2), sorted([(2, "b"), (1, "z"), (2, "a")])))
show("hash eq", (hash((1, 2)) == hash((1, 2)), hash((1, "a", (2,))) == hash((1, "a", (2,))),
                 hash((1, 2.0)) == hash((1, 2)), hash(()) == hash(tuple())))
try:
    hash((1, [2]))
except TypeError as e:
    print("unhashable", e)
d = {(1, 2): "a", (1, (2, 3)): "b", (): "empty"}
d[(1, 2)] = "A"
show("dict keys", (d[(1, 2)], d[tuple([1, (2, 3)])], d[()], (1, 2) in d, (2, 1) in d, len(d)))
s = {(1, 2), (1, 2), (2, 1), tuple([1, 2])}
show("set", (len(s), (1, 2) in s, sorted(s)))
show("concat/repeat", ((1,) + (2, 3), (1, 2) * 2, 2 * (0,), () * 5, (1,) * 0))
x, y, *rest = (1, 2, 3, 4)
(p, (q, r)) = (1, (2, 3))
show("unpack", (x, y, rest, p, q, r))
for i, (k, v) in enumerate([("a", 1), ("b", 2)]):
    print("loop unpack", i, k, v)
show("methods", ((1, 2, 1).count(1), (1, 2, 1).index(2), (3, 1, 2).index(2, 1)))
show("contains", (1 in (1, 2), 3 in (1, 2), (1,) in ((1,), 2)))
show("iter", (list(iter((1, 2))), list(reversed((1, 2, 3))), [e for e in (4, 5)], max((3, 9, 2)), sum((1, 2, 3))))
show("nested", ((1, (2, (3, (4,)))), ((1, 2), [3, 4]), tuple([(1, 2), (3, 4)])))


def star(*args, **kwargs):
    return type(args).__name__, args, sorted(kwargs.items())


show("*args", star(1, 2, k=3))
show("*args spread", star(*[1, 2], *(3,), **{"z": 1}))
show("*args empty", (star(), star()[1] is EMPTY))
show("zip/enumerate/items", (list(zip("ab", (1, 2))), list(enumerate("xy")), sorted({"a": 1}.items()),
                             divmod(7, 2), "a-b".partition("-"), type(divmod(7, 2)).__name__))
show("repr", (repr((1,)), repr(()), str((1, "a")), repr(((),)), "%s" % ((1, 2),)))
P = collections.namedtuple("P", "x y")
pt = P(1, 2)
show("namedtuple", (pt, pt.x, pt[1], isinstance(pt, tuple), pt == (1, 2), hash(pt) == hash((1, 2)), pt._replace(x=5), tuple(pt)))
show("pickle", (pickle.loads(pickle.dumps((1, ("a", None), [2]))), pickle.loads(pickle.dumps(()))))
show("conversions", (list((1, 2)), set((1, 1, 2)), dict(((1, 2), (3, 4))), "-".join(("a", "b"))))


class Sub(tuple):
    pass


st = Sub((1, 2))
show("subclass", (type(st).__name__, st == (1, 2), isinstance(st, tuple), st + (3,), type(st[0:1]).__name__, hash(st) == hash((1, 2))))
