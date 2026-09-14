# Regression: @dataclass(slots=True) recreates the class with __slots__.
# dataclasses._create_slots builds the slot tuple with
# itertools.filterfalse(inherited_slots.__contains__, ...), and
# itertools.filterfalse was registered as a plain, non-callable object,
# so the slot tuple came back None ("'NoneType' object is not iterable").
import dataclasses
import itertools

# itertools.filterfalse with the predicate shapes the stdlib passes.
s = {'b'}
assert list(itertools.filterfalse(s.__contains__, ['a', 'b', 'c'])) == ['a', 'c']
assert list(itertools.filterfalse(lambda x: x % 2, range(6))) == [0, 2, 4]
assert list(itertools.filterfalse(None, [0, 1, '', 'x', None, []])) == [0, '', None, []]
assert list(itertools.filterfalse(bool, iter([1, 0, 2]))) == [0]
assert list(itertools.filterfalse(s.__contains__, itertools.chain(('a', 'b'), ('d',)))) == ['a', 'd']


@dataclasses.dataclass(slots=True)
class Point:
    x: int
    y: int = 2


assert Point.__slots__ == ('x', 'y')
p = Point(1)
assert p.x == 1 and p.y == 2
assert repr(p) == 'Point(x=1, y=2)'
assert p == Point(1, 2)
assert p != Point(1, 3)
assert dataclasses.fields(Point)[0].name == 'x'
try:
    p.z = 3
except AttributeError:
    pass
else:
    raise AssertionError("slots=True instance accepted a new attribute")


@dataclasses.dataclass(slots=True)
class Child(Point):
    z: int = 0


assert Child.__slots__ == ('z',)
c = Child(1, 2, 3)
assert (c.x, c.y, c.z) == (1, 2, 3)
assert repr(c) == 'Child(x=1, y=2, z=3)'

print("dataclass_slots OK")
