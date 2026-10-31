# A large set keeps every element: ints, strings, tuples, and objects that
# collide in groups, with membership, removal, and set operations checked
# against counts computed independently.

N = 100000

ints = set(range(N))
assert len(ints) == N
assert sum(1 for i in range(0, 2 * N, 7) if i in ints) == len(range(0, N, 7))

strs = {str(i) for i in range(N)}
assert len(strs) == N and "99999" in strs and "100000" not in strs

tups = {(i, i % 3) for i in range(N)}
assert len(tups) == N

evens = {i for i in range(0, N, 2)}
assert len(ints - evens) == N // 2
assert len(ints & evens) == N // 2
assert len(ints ^ evens) == N // 2
assert len(evens | {N, N + 1}) == N // 2 + 2
assert evens <= ints and not ints <= evens

for i in range(0, N, 3):
    ints.discard(i)
assert len(ints) == N - len(range(0, N, 3))


class G:
    """Hash collides in groups of 16."""
    def __init__(self, v):
        self.v = v
    def __hash__(self):
        return self.v // 16
    def __eq__(self, other):
        return isinstance(other, G) and self.v == other.v


M = 20000
g = {G(i) for i in range(M)}
assert len(g) == M, len(g)
assert sum(1 for i in range(M) if G(i) in g) == M
assert G(M) not in g
for i in range(0, M, 2):
    g.remove(G(i))
assert len(g) == M // 2
assert sorted(e.v for e in g) == list(range(1, M, 2))

print("set_large: ok")
