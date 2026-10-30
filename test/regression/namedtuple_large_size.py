# Creating a class with a large namespace is linear: py_type tested each
# name against the class's __keys__ list with a linear scan, and the
# compiler scanned a lambda's parameter list once per name it captures.
# namedtuple with 150,000 fields (test_collections' test_large_size) never
# finished.  The CTest TIMEOUT is the bound: at 30,000 fields the quadratic
# version took minutes.
import random
import string
from collections import namedtuple

n = 30_000
names = list(set(''.join([random.choice(string.ascii_letters) for _ in range(10)])
                 for _ in range(n)))
n = len(names)
Big = namedtuple('Big', names)
b = Big(*range(n))
assert b == tuple(range(n))
assert Big._make(range(n)) == tuple(range(n))
for pos, name in enumerate(names[:1000]):
    assert getattr(b, name) == pos
assert b._asdict() == dict(zip(names, range(n)))
b2 = b._replace(**{names[1]: 999, names[-5]: 42})
assert b2[1] == 999 and b2[-5] == 42
assert b._fields == tuple(names)

# A plain class with a large namespace, and a lambda with many parameters.
ns = {"a%d" % i: i for i in range(n)}
C = type("C", (), ns)
assert C.a0 == 0 and getattr(C, "a%d" % (n - 1)) == n - 1
f = eval("lambda %s: (%s)" % (", ".join(ns), ", ".join(ns)))
assert f(*range(n))[-1] == n - 1

# The generator matches CPython's stream for a seed.
r = random.Random(42)
assert r.random() == 0.6394267984578837
assert r.getrandbits(5) == 0
assert r.getrandbits(100) == 257086819302815227845767741691
assert r.choice("abcdef") == "b"
s = r.getstate()
x = [r.random() for _ in range(700)]
r.setstate(s)
assert [r.random() for _ in range(700)] == x
print("namedtuple_large_size: ok")
