# Comparisons and operators that answered differently from CPython:
# ordering a dict view against a list returned a value instead of raising
# TypeError; set.__eq__ of a non-set returned None instead of NotImplemented;
# vars(module).keys() was a list, not a set-like view; bool & | ^ bool gave
# an int; a dict subclass | dict gave the subclass.
import types


def raises(fn):
    try:
        fn()
    except TypeError:
        return True
    return False


d = {1: 2, 3: 4}
assert raises(lambda: d.keys() < [1])
assert raises(lambda: d.items() <= [(1, 2)])
assert raises(lambda: [1] > d.keys())
assert raises(lambda: d.keys() < 5)
assert (d.keys() == [1, 3]) is False and (d.keys() != [1, 3]) is True
assert d.keys() == {1, 3} and d.keys() < {1, 3, 5} and d.keys() >= frozenset({1})

s = {1, 2}
assert s.__eq__([1, 2]) is NotImplemented
assert frozenset(s).__eq__((1, 2)) is NotImplemented
assert s.__ne__(3) is NotImplemented
assert (s == [1, 2]) is False and (s != [1, 2]) is True
assert raises(lambda: s < [1])
assert s == frozenset({1, 2})

m = types.ModuleType("m")
m.z = 1
m.a = 2
ks = vars(m).keys()
assert type(ks).__name__ == "dict_keys", type(ks)
assert sorted(ks & {"a", "z", "q"}) == ["a", "z"]
assert "a" not in (ks - {"a"}) and "z" in (ks - {"a"})
assert ks <= set(vars(m))
assert ("z", 1) in vars(m).items()
assert type(vars(m).values()).__name__ == "dict_values"

for a in (True, False):
    for b in (True, False):
        assert (a & b) is (a and b)
        assert (a | b) is (a or b)
        assert (a ^ b) is (a != b)
        x = a
        x &= b
        assert x is (a and b)
assert (True & 1) == 1 and type(True & 1) is int
assert (True | 2) == 3 and type(False ^ 0) is int


class D(dict):
    pass


assert type(D(a=1) | {"b": 2}) is dict
assert type({"b": 2} | D(a=1)) is dict
assert D(a=1) | {"b": 2} == {"a": 1, "b": 2}

print("operator_view_conformance: ok")
