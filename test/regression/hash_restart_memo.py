# A key's __eq__ that inserts a new key with the same hash into the container
# being searched. CPython's open addressing puts the new key in another slot,
# so the entry being compared is untouched and the lookup does not restart:
# two comparisons, one result. A lookup that restarted after every write ran
# __eq__ without end (review round 6, M1: the restart memo remembers the
# outcome per stored key).
calls = [0]
target = [None]

class K:
    def __init__(self, v):
        self.v = v
    def __hash__(self):
        return 7
    def __eq__(self, other):
        calls[0] += 1
        if calls[0] > 2000:
            raise AssertionError("lookup restarted without end")
        if not isinstance(other, K) or other.v >= 0:
            n = K(-calls[0])
            t = target[0]
            if isinstance(t, dict):
                t[n] = 1
            else:
                t.add(n)
        return isinstance(other, K) and self.v == other.v

def run(make, op):
    calls[0] = 0
    target[0] = make()
    result = op(target[0])
    return result, calls[0], len(target[0])

cases = {
    "dict_get": (lambda: {K(1): 'a'}, lambda d: d.get(K(1)), ('a', 2, 2)),
    "dict_in": (lambda: {K(1): 'a'}, lambda d: K(1) in d, (True, 2, 2)),
    "dict_set": (lambda: {K(1): 'a'}, lambda d: d.__setitem__(K(1), 'b'), (None, 2, 2)),
    "dict_pop": (lambda: {K(1): 'a'}, lambda d: d.pop(K(1)), ('a', 2, 1)),
    "set_in": (lambda: {K(1)}, lambda s: K(1) in s, (True, 2, 2)),
    "set_add": (lambda: {K(1)}, lambda s: s.add(K(1)), (None, 2, 2)),
    "set_remove": (lambda: {K(1)}, lambda s: s.remove(K(1)), (None, 2, 1)),
}
for name, (make, op, expected) in cases.items():
    got = run(make, op)
    assert got == expected, (name, got, expected)
print("hash_restart_memo: ok")
