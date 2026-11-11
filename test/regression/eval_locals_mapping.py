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

# The mapping itself is read and written, not a copy (review round 6, M4):
# a dict subclass's __missing__ and any mapping's __getitem__ answer, a name
# the mapping lacks (KeyError) falls back to the globals and the builtins,
# and a large mapping costs nothing per call.
class Missing(dict):
    def __missing__(self, key):
        return 'missing:' + key
assert eval("q", {}, Missing()) == 'missing:q'
class Mapping:
    def __init__(self):
        self.seen = []
    def __getitem__(self, key):
        self.seen.append(key)
        if key == 'y':
            return 10
        raise KeyError(key)
mp = Mapping()
assert eval("y + len('ab')", {}, mp) == 12
assert mp.seen == ['y', 'len'], mp.seen
class Failing:
    def __getitem__(self, key):
        raise RuntimeError("boom")
try:
    eval("anything", {}, Failing())
except RuntimeError:
    pass
else:
    raise AssertionError("an error other than KeyError was swallowed")
try:
    eval("nope", {}, {})
except NameError:
    pass
else:
    raise AssertionError("unbound name did not raise NameError")

# A comprehension at the eval / exec level reads the locals mapping, as
# CPython's inlined comprehensions do; a lambda does not (it is a function).
assert eval("[x for _ in range(2)]", {}, {'x': 1}) == [1, 1]
assert eval("{k: x for k in 'ab'}", {}, {'x': 2}) == {'a': 2, 'b': 2}
try:
    eval("(lambda: x)()", {}, {'x': 1})
except NameError:
    pass
else:
    raise AssertionError("a lambda read the eval locals")

# exec writes and deletes through the mapping, in place.
class Recording(dict):
    def __setitem__(self, key, value):
        super().__setitem__(key, value * 2)
rec = Recording()
exec("v = 3", {}, rec)
assert rec == {'v': 6}, rec
d = {'gone': 1}
exec("del gone", {}, d)
assert d == {}
try:
    exec("del gone", {}, {})
except NameError:
    pass
else:
    raise AssertionError("del of an unbound name did not raise")
loc = {}
exec("import os\ndef fn(): return 1", {}, loc)
assert 'os' in loc and loc['fn']() == 1
star = {}
exec("from math import *", {}, star)
assert 'pi' in star and 'sqrt' in star, sorted(star)[:5]
assert eval("locals()", {}, d) is d

# Many locals: one eval does not walk them.
import time
big = {('n%d' % i): i for i in range(20000)}
t0 = time.perf_counter()
for _ in range(200):
    assert eval("n5", {}, big) == 5
elapsed = time.perf_counter() - t0
assert elapsed < 2.0, elapsed
print("eval_locals_mapping: ok")
