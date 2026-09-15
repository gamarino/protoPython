# An instance of a float subclass holding NaN follows IEEE 754 like a plain
# float: it is unequal to and unordered with everything, itself included.
import math


class F(float):
    pass


x = F("nan")
w = F("nan")
nan = float("nan")
assert math.isnan(x)

# The same instance.
y = x
assert not (x == y)
assert x != y
assert not (x < y)
assert not (x <= y)
assert not (x > y)
assert not (x >= y)

# Distinct instances, and mixed with a plain float NaN.
assert not (x == w)
assert x != w
assert not (x <= w)
assert not (x >= w)
assert not (x == nan)
assert not (nan == x)
assert x != nan
assert nan != x

# Ordinary values of the subclass still compare by value.
assert F(1.5) == F(1.5)
assert F(1.5) <= F(1.5)
assert F(1.5) < F(2.5)
assert F(1.5) == 1.5
assert not (F(1.5) != F(1.5))

# Containers still find the same object (`x is e or x == e`).
assert x in [x]
assert [x] == [x]


# A subclass that defines __eq__ and __ne__ is answered by them.
class G(float):
    def __eq__(self, other):
        return True

    def __ne__(self, other):
        return False

    __hash__ = float.__hash__


g = G("nan")
assert g == g
assert not (g != g)
assert G("nan") == G("nan")

print("float_subclass_nan: ok")
