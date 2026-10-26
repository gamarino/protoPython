# len() of a str is O(1) whatever the string's shape.  It used to build two
# O(n) lists of the characters per call (ProtoObject::asList on a string):
# len("ab" * 100000) took about 0.7 s.  str.__len__ was missing as well.
import time


class S(str):
    pass


class Sized(str):
    def __len__(self):
        return 7


def per_call(s, n=200):
    t = time.perf_counter()
    for _ in range(n):
        len(s)
    return (time.perf_counter() - t) / n


small = "ab" * 10
large = "ab" * 100000
assert len(small) == 20 and len(large) == 200000
assert len("x" * 300000) == 300000
assert len("".join(["ab"] * 50000)) == 100000
assert len("é" * 1000) == 1000 and len("€😀" * 10) == 20
assert len("") == 0 and len("abc"[1:]) == 2

assert "abc".__len__() == 3
assert str.__len__("abcd") == 4
assert large.__len__() == 200000
assert len(S("xyz")) == 3 and S("xyz").__len__() == 3
assert len(Sized("abc")) == 7, "a str subclass keeps its own __len__"

# Constant time: a 10 000-times longer string costs about the same per call.
# The bound is loose (timing noise), the old cost was ~10 000x.
t_small = min(per_call(small) for _ in range(3))
t_large = min(per_call(large) for _ in range(3))
assert t_large < max(t_small * 20, 50e-6), (t_small, t_large)
print("ok", "%.2f us" % (t_small * 1e6), "%.2f us" % (t_large * 1e6))
