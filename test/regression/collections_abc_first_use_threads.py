# Threads calling methods of the collections.abc ABCs for the first time at
# once.  The methods are native trampolines until the first call loads
# lib/python3.14/_collections_abc_mixins.py; every thread must find them
# installed, none a half-imported module.  The .expected file is CPython's
# output.
import threading
import collections.abc as abc

class M(abc.Mapping):
    def __init__(self): self.d = {"a": 1, "b": 2}
    def __getitem__(self, k): return self.d[k]
    def __iter__(self): return iter(self.d)
    def __len__(self): return len(self.d)

class S(abc.MutableSet):
    def __init__(self, it=()): self.s = set(it)
    def __contains__(self, x): return x in self.s
    def __iter__(self): return iter(self.s)
    def __len__(self): return len(self.s)
    def add(self, x): self.s.add(x)
    def discard(self, x): self.s.discard(x)

N = 8
barrier = threading.Barrier(N)
results = [None] * N
def work(i):
    barrier.wait()
    m = M()
    s = S([1, 2])
    r = (m.get("a"), "b" in m, sorted(m.keys()), m == {"a": 1, "b": 2},
         sorted(s | {3}), s.isdisjoint([9]), len(s))
    for _ in range(200):
        m.get("z", 0); s |= [i]
    results[i] = r
ts = [threading.Thread(target=work, args=(i,)) for i in range(N)]
for t in ts: t.start()
for t in ts: t.join()
print(all(r == (1, True, ['a', 'b'], True, [1, 2, 3], True, 2) for r in results), results[0])
