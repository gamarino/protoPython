# math.isclose follows CPython: NaN is close to nothing, infinities only to
# themselves, and rel_tol and abs_tol are keyword-only, non-negative tolerances.
import math

nan = float("nan")
inf = math.inf

assert math.isclose(nan, nan) is False
assert math.isclose(1.0, nan) is False
assert math.isclose(nan, 1.0) is False
assert math.isclose(nan, nan, abs_tol=inf) is False
assert math.isclose(inf, inf) is True
assert math.isclose(-inf, -inf) is True
assert math.isclose(inf, -inf) is False
assert math.isclose(inf, 1e308) is False
assert math.isclose(inf, inf, rel_tol=0.5) is True

assert math.isclose(1.0, 1.0 + 1e-10) is True
assert math.isclose(1.0, 1.1) is False
assert math.isclose(5, 5.0) is True
assert math.isclose(0.0, 1e-10) is False


class F(float):
    pass


assert math.isclose(F("nan"), F("nan")) is False
assert math.isclose(F(1.0), 1.0) is True

# The tolerances are honoured when given by keyword.
assert math.isclose(0.0, 1e-10, abs_tol=1e-9) is True
assert math.isclose(1.0, 1.05, rel_tol=0.1) is True
assert math.isclose(1.0, 1.05, rel_tol=0.01) is False

for kwargs in ({"rel_tol": -1.0}, {"abs_tol": -1.0}):
    try:
        math.isclose(1.0, 1.0, **kwargs)
    except ValueError:
        pass
    else:
        raise AssertionError("negative tolerance accepted: %r" % (kwargs,))

try:
    math.isclose(1.0, 1.0, 0.1)
except TypeError:
    pass
else:
    raise AssertionError("rel_tol accepted as a positional argument")

try:
    math.isclose(1.0)
except TypeError:
    pass
else:
    raise AssertionError("isclose() accepted a single argument")

print("math_isclose: ok")
