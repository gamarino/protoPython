# An exception raised by __eq__ while a set compares colliding elements
# propagates out of the set operation, as in CPython; it is never swallowed
# into a silent "not found" or "already present".


class Boom(Exception):
    pass


class E:
    armed = False

    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return 5

    def __eq__(self, other):
        if E.armed:
            raise Boom(self.v)
        return isinstance(other, E) and self.v == other.v


def raises_boom(fn):
    E.armed = True
    try:
        fn()
    except Boom:
        return True
    finally:
        E.armed = False
    return False


s = {E(1), E(2)}
assert len(s) == 2, len(s)
probe = E(3)
assert raises_boom(lambda: probe in s), "in"
assert raises_boom(lambda: s.add(probe)), "add"
assert raises_boom(lambda: s.discard(probe)), "discard"
assert raises_boom(lambda: s.remove(probe)), "remove"
assert raises_boom(lambda: {E(1), E(2)}), "literal"
assert raises_boom(lambda: set([E(1), E(2)])), "set()"
assert raises_boom(lambda: frozenset([E(1), E(2)])), "frozenset()"
assert raises_boom(lambda: s == {E(1), E(9)}), "=="
assert raises_boom(lambda: s & {E(7)}), "&"
assert len(s) == 2

# The exception leaves the set unchanged and usable.
assert all(isinstance(e, E) for e in s)
assert sorted(e.v for e in s) == [1, 2]

print("set_eq_raises: ok")
