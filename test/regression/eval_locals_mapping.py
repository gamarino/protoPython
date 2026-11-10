# eval() with a dict for locals reads (and exec() writes) the mapping.
assert eval("x", None, {'x': 4}) == 4
assert eval("x", {}, {'x': 4}) == 4
assert eval("x", {}, locals={'x': 5}) == 5
assert eval("x + y", {'y': 1}, {'x': 4}) == 5
assert eval("x", {'x': 2}) == 2
assert eval("[v for v in (1, 2)] + [x]", {}, {'x': 3}) == [1, 2, 3]
loc = {'a': 1}
exec("b = a + 1", {}, loc)
assert loc == {'a': 1, 'b': 2}
def f():
    z = 7
    return eval("z")
assert f() == 7
print("eval_locals_mapping: ok")
