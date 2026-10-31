# divmod() of objects other than numbers calls __divmod__ / __rdivmod__.
from datetime import timedelta

assert divmod(timedelta(hours=5, minutes=30), timedelta(hours=1)) == (5, timedelta(minutes=30))

class L:
    def __divmod__(self, other):
        return ("L", other)
class R:
    def __rdivmod__(self, other):
        return ("R", other)
class N:
    def __divmod__(self, other):
        return NotImplemented

assert divmod(L(), 3) == ("L", 3)
assert divmod(3, R()) == ("R", 3)
r = divmod(N(), R())
assert r[0] == "R" and isinstance(r[1], N)
try:
    divmod(N(), 3)
except TypeError as e:
    assert "divmod" in str(e), e
else:
    raise AssertionError("TypeError expected")
assert divmod(7, 2) == (3, 1) and divmod(-7.5, 2) == (-4.0, 0.5)
print("divmod_dunders: ok")
