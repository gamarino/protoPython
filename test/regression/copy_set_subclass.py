# copy and pickle rebuild set and frozenset subclass instances with their
# elements and instance attributes, through set.__reduce__ and
# frozenset.__reduce__.
import copy
import pickle


class FS(frozenset):
    pass


class S(set):
    pass


fs = FS([1, 2, 3])
c = copy.copy(fs)
assert type(c) is FS and c == frozenset([1, 2, 3]), (type(c), c)
d = copy.deepcopy(fs)
assert type(d) is FS and d == frozenset([1, 2, 3])

s = S(["a", "b"])
s.tag = "label"
c = copy.copy(s)
assert type(c) is S and c == {"a", "b"}
assert c.tag == "label"
assert c is not s
c.add("c")
assert "c" not in s

d = copy.deepcopy(S([(1, 2)]))
assert type(d) is S and d == {(1, 2)}

# The reduce value has CPython's shape.
cls, args, state = FS([5]).__reduce__()
assert cls is FS and args == ([5],) and state is None, (cls, args, state)
cls, args, state = s.__reduce__()
assert cls is S and sorted(args[0]) == ["a", "b"] and state == {"tag": "label"}, state
assert frozenset([7]).__reduce__() == (frozenset, ([7],), None)
assert set().__reduce__() == (set, ([],), None)

# Plain sets and frozensets still copy.
assert copy.copy({1, 2}) == {1, 2}
assert copy.deepcopy(frozenset({1})) == frozenset({1})

# pickle round trip.
for proto in range(pickle.HIGHEST_PROTOCOL + 1):
    r = pickle.loads(pickle.dumps(FS([4, 5]), proto))
    assert type(r) is FS and r == frozenset([4, 5]), (proto, type(r), r)

print("copy_set_subclass: ok")
