# functools.cmp_to_key builds a key class whose instances compare through
# the cmp function, as CPython's does; unittest's loader sorts with it.
import functools


def cmp(x, y):
    return (x > y) - (x < y)


def reverse(x, y):
    return cmp(y, x)


assert sorted([3, 1, 2], key=functools.cmp_to_key(cmp)) == [1, 2, 3]
assert sorted([3, 1, 2], key=functools.cmp_to_key(reverse)) == [3, 2, 1]
k = functools.cmp_to_key(cmp)
assert k(1) < k(2) and k(2) > k(1) and k(2) == k(2) and k(1) <= k(1) and k(2) >= k(1)


class Holder:
    by = staticmethod(cmp)

    def order(self, names):
        names.sort(key=functools.cmp_to_key(self.by))
        return names


assert Holder().order(["b", "c", "a"]) == ["a", "b", "c"]

import unittest


class Sample(unittest.TestCase):
    def test_b(self):
        pass

    def test_a(self):
        pass


names = unittest.TestLoader().getTestCaseNames(Sample)
assert list(names) == ["test_a", "test_b"], names
print("functools_cmp_to_key OK")
