# dict_keys and dict_items are set-like (comparisons, set operators,
# isdisjoint, unhashable); dict_values compares by identity only. The views
# had none of these: `d.keys() == {"a", "b"}` was False and `d.keys() & x`
# raised. The expected list is CPython 3.14's output for the same probes.

d = {"a": 1, "b": 2}
e = {"b": 0, "a": 0}
k, it, v = d.keys(), d.items(), d.values()
r = []
r += [k == {"a", "b"}, {"a", "b"} == k, k == e.keys(), k != {"a"}, k == ["a", "b"], k != ["a", "b"]]
r += [it == {("a", 1), ("b", 2)}, {("a", 1), ("b", 2)} == it, it == {"a": 1, "b": 2}.items(), it == e.items()]
r += [k <= {"a", "b", "c"}, k < {"a", "b"}, k >= {"a"}, k > {"a"}, {"a"} < k, frozenset("ab") == k]
r += [k.isdisjoint({"c"}), k.isdisjoint(["a"]), it.isdisjoint([("a", 1)]), it.isdisjoint([("a", 2)])]
r += [sorted(k & e.keys()), sorted(k | ["c"]), sorted(k - {"a"}), sorted(k ^ {"a", "z"}), sorted(["a", "q"] & k), sorted({"q"} | k), sorted(["a", "q"] - k), sorted(["a"] ^ k)]
r += [sorted(it & {("a", 1)}), sorted(it - e.items())]
r += ["a" in k, "z" in k, ("a", 1) in it, ("a", 2) in it, "x" in it, 1 in v]
try: [] in k; r.append("no error")
except TypeError: r.append("TypeError")
try: k < 3; r.append("no error")
except TypeError: r.append("TypeError")
try: hash(k); r.append("hashable")
except TypeError: r.append("unhashable")
r += [v == v, d.values() == d.values()]
x = {"l": [1]}.items()
r += [x == {"l": [1]}.items()]
try: x & x; r.append("no error")
except TypeError: r.append("TypeError")
r += [type(k & k).__name__, type(k | k).__name__]
s2 = k | k; s2.add(9); r += [9 in s2]
expected = [True, True, True, True, False, True, True, True, True, False, True, False, True, True, True, True, True, False, False, True, ['a', 'b'], ['a', 'b', 'c'], ['b'], ['b', 'z'], ['a'], ['a', 'b', 'q'], ['q'], ['b'], [('a', 1)], [('a', 1), ('b', 2)], True, False, True, False, False, True, 'TypeError', 'TypeError', 'unhashable', True, False, True, 'TypeError', 'set', 'set', True]
assert r == expected, [(i, a, b) for i, (a, b) in enumerate(zip(r, expected)) if a != b]
print("dict_view_set_ops: ok")
