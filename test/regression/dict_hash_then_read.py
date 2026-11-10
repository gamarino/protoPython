# A dict or set lookup hashes the key first and then reads the table, so a
# __hash__ that changes the container is seen by the lookup (CPython).
class K:
    def __init__(self, v, action=None):
        self.v = v
        self.action = action
    def __hash__(self):
        if self.action:
            act, self.action = self.action, None
            act()
        return hash(self.v)
    def __eq__(self, other):
        return isinstance(other, K) and self.v == other.v

d = {}
k = K(1, lambda: d.__setitem__(K(1), 'late'))
assert (k in d) is True
d = {}
k = K(1, lambda: d.__setitem__(K(1), 'late'))
assert d.get(k) == 'late'
d = {}
k = K(1, lambda: d.__setitem__(K(1), 'late'))
assert d[k] == 'late'
d = {K(1): 'gone'}
k = K(1, lambda: d.clear())
assert d.get(k, 'missing') == 'missing'
s = set()
k = K(1, lambda: s.add(K(1)))
assert (k in s) is True
s = {K(1)}
k = K(1, lambda: s.clear())
assert (k in s) is False
print("dict_hash_then_read: ok")
