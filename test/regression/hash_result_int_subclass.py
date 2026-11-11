# hash() and dict/set indexing read a __hash__ result by one rule: a bool is
# the int it equals, an int or int subclass that fits a word is itself, a
# larger one is reduced, anything else raises TypeError.
class J(int):
    pass

class T:
    def __init__(self, h):
        self.h = h
    def __hash__(self):
        return self.h
    def __eq__(self, other):
        return other == 5

for h in (5, J(5), True and 5, 2**64 + 5, J(2**64 + 5)):
    t = T(h)
    assert hash(t) == hash(h) if not isinstance(h, J) else hash(t) == hash(int(h)), h
    if hash(t) == hash(5):
        assert 5 in {t}, h
        assert {t: 'x'}.get(5) == 'x', h

t = T(J(5))
assert hash(t) == 5
assert 5 in {t}
assert {t: 'x'}.get(5) == 'x'
assert {5: 'y'}.get(t) == 'y'

class B:
    def __hash__(self):
        return True
    def __eq__(self, other):
        return other == 1
assert hash(B()) == 1
assert 1 in {B()}

for bad in (1.5, None, "5"):
    t = T(bad)
    for op in (lambda: hash(t), lambda: {t}, lambda: {t: 1}, lambda: t in {1: 2}):
        try:
            op()
        except TypeError:
            pass
        else:
            raise AssertionError("no TypeError for %r" % (bad,))
print("hash_result_int_subclass: ok")
