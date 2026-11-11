# An __eq__ that writes an unrelated key into the container it is compared
# in (a cache, a log). CPython's lookup restarts only when the table was
# replaced or the compared entry changed, so each operation below calls
# __eq__ exactly once; a restart on every write looped forever.
import sys

calls = [0]
LIMIT = 1000

class K:
    def __init__(self, v):
        self.v = v
    def __hash__(self):
        return 7
    def __eq__(self, other):
        calls[0] += 1
        if calls[0] > LIMIT:
            raise RuntimeError("__eq__ called more than %d times" % LIMIT)
        if isinstance(target, dict):
            target["side%d" % calls[0]] = 1
        else:
            target.add("side%d" % calls[0])
        return isinstance(other, K) and self.v == other.v

def run(name, make, op, expected_result):
    global target
    target = make()
    calls[0] = 0
    result = op(target)
    assert calls[0] == 1, (name, calls[0])
    assert result == expected_result, (name, result)

run("dict in", lambda: {K(1): 'a'}, lambda t: K(1) in t, True)
run("dict get", lambda: {K(1): 'a'}, lambda t: t.get(K(1)), 'a')
run("dict getitem", lambda: {K(1): 'a'}, lambda t: t[K(1)], 'a')
def setitem(t):
    t[K(1)] = 'b'
    return len([k for k in t if isinstance(k, K)])
run("dict setitem", lambda: {K(1): 'a'}, setitem, 1)
run("dict pop", lambda: {K(1): 'a'}, lambda t: t.pop(K(1)), 'a')
run("dict setdefault", lambda: {K(1): 'a'}, lambda t: t.setdefault(K(1), 'z'), 'a')
def delitem(t):
    del t[K(1)]
    return any(isinstance(k, K) for k in t)
run("dict delitem", lambda: {K(1): 'a'}, delitem, False)
run("set in", lambda: {K(1)}, lambda t: K(1) in t, True)
def add(t):
    t.add(K(1))
    return sum(isinstance(k, K) for k in t)
run("set add", lambda: {K(1)}, add, 1)
def remove(t):
    t.remove(K(1))
    return any(isinstance(k, K) for k in t)
run("set remove", lambda: {K(1)}, remove, False)
def discard(t):
    t.discard(K(1))
    return any(isinstance(k, K) for k in t)
run("set discard", lambda: {K(1)}, discard, False)
print("dict_set_eq_side_write: ok")
