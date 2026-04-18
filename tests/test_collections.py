"""Conformance test for the collections module."""
import collections

# Module has expected public names
for name in ['namedtuple', 'OrderedDict', 'defaultdict', 'deque',
             'Counter', 'ChainMap', 'UserDict', 'UserList', 'UserString']:
    assert hasattr(collections, name), f"missing: {name}"

# namedtuple — creation, index access, _fields, iteration, _make
Point = collections.namedtuple('Point', 'x y')
p = Point(10, 20)
assert p[0] == 10
assert p[1] == 20
assert p._fields == ('x', 'y')
assert len(p) == 2
assert list(p) == [10, 20]
p2 = Point._make([30, 40])
assert p2[0] == 30
assert p2[1] == 40

# Three-field namedtuple
Color = collections.namedtuple('Color', ['r', 'g', 'b'])
c = Color(255, 128, 0)
assert c[0] == 255
assert c[1] == 128
assert c[2] == 0
assert c._fields == ('r', 'g', 'b')

# ChainMap — multi-map key lookup and new_child
cm = collections.ChainMap({'a': 1, 'b': 2}, {'c': 3, 'd': 4})
assert cm['a'] == 1
assert cm['b'] == 2
assert cm['c'] == 3
assert cm['d'] == 4
child = cm.new_child({'e': 5})
assert child['e'] == 5
assert child['a'] == 1

print("test_collections passed")
